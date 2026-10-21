#include "atomic_file.hpp"
#include <algorithm>
#ifndef IMAGEJXX_ATOMIC_STANDALONE
#include <imagejxx/session.hpp>
#endif
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>
#endif
#ifdef __linux__
#include <sys/statfs.h>
#include <sys/statvfs.h>
#endif
using namespace imagejxx;
namespace fs = std::filesystem;
namespace {
void check(bool value, const std::string &message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class E = std::exception, class F> void rejects(F f) {
  bool refused = false;
  try {
    f();
  } catch (const E &) {
    refused = true;
  }
  check(refused, "Expected atomic write failure");
}
std::string bytes(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  check(bool(f), "Read observed output");
  return {std::istreambuf_iterator<char>(f), {}};
}
void put(const fs::path &p, const std::string &value) {
  std::ofstream f(p, std::ios::binary);
  f << value;
  check(bool(f), "Write owned control");
}
struct CurrentDirectory {
  const fs::path original = fs::current_path();
  ~CurrentDirectory() {
    std::error_code error;
    fs::current_path(original, error);
  }
};
void clean(const fs::path &directory) {
  for (const auto &entry : fs::directory_iterator(directory))
    check(entry.path().filename().u8string().find(".tmp.") == std::string::npos,
          "Atomic failure leaked stage");
}
void write(int variant, const fs::path &path,
           const std::vector<unsigned char> &data,
           const ExecutionControl &control = {}) {
  if (variant == 0)
    detail::atomic_replace_file(path, data.data(), data.size(), "fault test",
                                control);
  else if (variant == 1) {
    auto staged =
        detail::stage_atomic_file(path, data.data(), data.size(), control);
    staged->publish(true);
  } else {
    detail::StagedAtomicStream staged(path);
    staged.open(control);
    staged.append(data.data(), data.size(), control);
    staged.close(control);
    staged.publish(true);
  }
}
void relative_paths(const fs::path &root) {
  const std::vector<unsigned char> payload(65537, 173);
  for (int variant = 0; variant < 3; ++variant)
    for (bool late : {false, true}) {
      const auto work =
          root / (std::to_string(variant) + (late ? "-late" : "-first"));
      const auto a = work / "original", b = work / "other";
      fs::create_directories(a);
      fs::create_directory(b);
      put(a / "result.bin", "original A");
      put(b / "result.bin", "unrelated B");
      CurrentDirectory restore;
      fs::current_path(a);
      bool changed = false;
      ExecutionControl control;
      if (!late)
        control.checkpoint = [&] {
          if (!changed) {
            fs::current_path(b);
            changed = true;
          }
        };
      else if (variant != 2)
        control.stage_progress = [&](std::string_view stage, auto, auto) {
          if (stage == "Atomic file replace" ||
              stage == "ROI export awaiting publication") {
            fs::current_path(b);
            changed = true;
          }
        };
      if (variant == 2 && late) {
        detail::StagedAtomicStream staged("result.bin");
        staged.open({});
        staged.append(payload.data(), payload.size(), {});
        staged.close({});
        fs::current_path(b);
        changed = true;
        staged.publish(true);
      } else
        write(variant, "result.bin", payload, control);
      fs::current_path(restore.original);
      check(changed, "CWD intervention was not reached");
      check(bytes(a / "result.bin") ==
                std::string(payload.begin(), payload.end()),
            "Relative atomic output was redirected by CWD change");
      check(bytes(b / "result.bin") == "unrelated B",
            "Atomic output replaced unrelated CWD target");
      clean(a);
      clean(b);
    }
  std::cout << "PASS atomic relative paths: three entry points, first/late CWD "
               "changes\n";
}
void invalid_paths(const fs::path &root) {
  const auto target = root / "prefix.bin";
  put(target, "unrelated prefix");
  auto nul = target;
  nul += fs::path::string_type(1, fs::path::value_type(0));
  nul += "tail";
  for (int variant = 0; variant < 3; ++variant)
    for (const auto &p : {fs::path(), nul}) {
      int hooks = 0;
      ExecutionControl control;
      control.checkpoint = [&] { ++hooks; };
      rejects([&] { write(variant, p, {1, 2, 3}, control); });
      check(hooks == 0 && bytes(target) == "unrelated prefix",
            "Invalid destination reached callbacks or changed prefix file");
    }
  clean(root);
  std::cout << "PASS atomic empty/NUL destinations before hooks\n";
}
#ifndef IMAGEJXX_ATOMIC_STANDALONE
SessionCapture capture(const std::string &title) {
  Image pixels(2, 2, 1, PixelType::UInt16);
  pixels.pixels = {1, 255, 32768, 65535};
  DocumentData data{Dataset::from_planes({1, 1, 1, 2, 2, 1}, Stack{pixels})};
  data.metadata.title = title;
  auto document = std::make_shared<Document>(std::move(data));
  SessionCapture result;
  result.documents.push_back(document->snapshot());
  return result;
}
void sessions(const fs::path &root) {
  const auto a = root / "session-original", b = root / "session-other";
  fs::create_directory(a);
  fs::create_directory(b);
  const auto source = capture("expected original");
  put(b / "saved.ijx", "unrelated session target");
  {
    CurrentDirectory restore;
    fs::current_path(a);
    bool changed = false;
    ExecutionControl control;
    control.checkpoint = [&] {
      if (!changed) {
        fs::current_path(b);
        changed = true;
      }
    };
    save_session("saved.ijx", source, {}, control);
    fs::current_path(restore.original);
    check(changed && load_session((a / "saved.ijx").u8string())
                             .documents.at(0)
                             ->snapshot()
                             .data()
                             .metadata.title == "expected original",
          "Session encode callbacks redirected output");
    check(bytes(b / "saved.ijx") == "unrelated session target",
          "Session save overwrote another directory's file");
  }
  save_session((a / "input.ijx").u8string(), source);
  save_session((b / "input.ijx").u8string(), capture("unrelated input"));
  {
    CurrentDirectory restore;
    fs::current_path(a);
    bool changed = false;
    ExecutionControl control;
    control.checkpoint = [&] {
      if (!changed) {
        fs::current_path(b);
        changed = true;
      }
    };
    const auto loaded = load_session("input.ijx", {}, control);
    check(changed && loaded.documents.at(0)->snapshot().data().metadata.title ==
                         "expected original",
          "Session callback rebound relative input");
  }
  for (bool saving : {false, true}) {
    int hooks = 0;
    ExecutionControl control;
    control.checkpoint = [&] { ++hooks; };
    rejects([&] {
      if (saving)
        save_session("", source, {}, control);
      else
        (void)load_session("", {}, control);
    });
    check(hooks == 0, "Invalid session path reached user hooks");
  }
  check(source.documents.at(0).data().dataset.plane({0, 0, 0})->pixels ==
                std::vector<double>({1, 255, 32768, 65535}) &&
            !source.documents.at(0).dirty(),
        "File faults mutated source document");
  clean(a);
  clean(b);
  std::cout << "PASS session destinations and inputs frozen before "
               "encoding/read callbacks\n";
}
#endif
#ifndef _WIN32
void limited_file_size(const fs::path &root) {
  struct Limit {
    rlimit old{};
    decltype(std::signal(SIGXFSZ, SIG_IGN)) signal;
    Limit() : signal(std::signal(SIGXFSZ, SIG_IGN)) {
      check(getrlimit(RLIMIT_FSIZE, &old) == 0,
            "Read original process file limit");
      auto next = old;
      next.rlim_cur = 1024;
      check(setrlimit(RLIMIT_FSIZE, &next) == 0,
            "Set owned process file-size limit");
    }
    ~Limit() {
      setrlimit(RLIMIT_FSIZE, &old);
      std::signal(SIGXFSZ, signal);
    }
  };
  const std::vector<unsigned char> payload(8192, 91), before = payload;
  const auto target = root / "size-limit.bin";
  put(target, "old target");
  {
    Limit scoped;
    for (int variant = 0; variant < 3; ++variant) {
      rejects([&] { write(variant, target, payload); });
      check(bytes(target) == "old target", "EFBIG changed output");
      clean(root);
    }
  }
  check(payload == before, "File-size failure changed caller data");
  std::cout << "PASS real RLIMIT_FSIZE: three writers, old target and source "
               "preserved\n";
}
#endif
#ifdef __linux__
void disk_full(const fs::path &root) {
  struct statfs type{};
  struct statvfs capacity{};
  check(statfs(root.c_str(), &type) == 0 && type.f_type == 0x01021994,
        "Quota case requires a private tmpfs");
  check(statvfs(root.c_str(), &capacity) == 0 &&
            capacity.f_blocks * capacity.f_frsize == 1024 * 1024,
        "Quota case requires exactly 1 MiB; refusing other filesystems");
  const auto target = root / "target.bin";
  put(target, "old target");
  const std::vector<unsigned char> payload(2 * 1024 * 1024, 109),
      before = payload;
  const auto raw = root / "raw-control";
  const auto fd = ::open(raw.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  check(fd >= 0, "Create independent raw control");
  std::size_t completed = 0;
  int failure = 0;
  while (completed < payload.size()) {
    const auto n =
        ::write(fd, payload.data() + completed,
                std::min(std::size_t(65536), payload.size() - completed));
    if (n < 0) {
      failure = errno;
      break;
    }
    check(n > 0, "Raw zero write");
    completed += std::size_t(n);
  }
  check(::close(fd) == 0 && ::unlink(raw.c_str()) == 0,
        "Retire owned raw control");
  check(failure == ENOSPC && completed > 0 && completed < payload.size(),
        "Independent raw control did not reach actual ENOSPC after a partial "
        "write");
  for (int variant = 0; variant < 3; ++variant) {
    rejects([&] { write(variant, target, payload); });
    check(bytes(target) == "old target", "Disk-full changed output");
    clean(root);
  }
  check(payload == before && std::distance(fs::directory_iterator(root),
                                           fs::directory_iterator{}) == 1,
        "Disk-full changed caller bytes or directory membership");
  std::cout << "PASS real tmpfs ENOSPC: raw_errno=" << failure
            << " raw_partial_bytes=" << completed
            << " capacity_bytes=" << capacity.f_blocks * capacity.f_frsize
            << "; three writers preserved target/source/membership\n";
}
#endif
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 2 || argc == 3,
          "Need owned root and optional quota/fsize mode");
    const auto supplied = fs::absolute(fs::u8path(argv[1]));
    const std::string mode = argc == 3 ? argv[2] : "basic";
    if (mode == "quota") {
#ifdef __linux__
      disk_full(supplied);
      return 0;
#else
      throw std::runtime_error("Quota case requires Linux");
#endif
    }
    const auto root =
        supplied /
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root);
    if (mode == "basic") {
      relative_paths(root);
      invalid_paths(root);
#ifndef IMAGEJXX_ATOMIC_STANDALONE
      sessions(root);
#endif
    } else if (mode == "fsize") {
#ifndef _WIN32
      limited_file_size(root);
#else
      throw std::runtime_error("File limit case requires POSIX");
#endif
    } else
      throw std::invalid_argument("Unknown atomic fault mode");
    std::cout << "ARTIFACTS " << root.u8string() << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
