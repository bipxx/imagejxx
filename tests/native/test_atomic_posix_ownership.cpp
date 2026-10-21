#include "atomic_file.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/resource.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using namespace imagejxx;
namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
void put(const fs::path &p, const std::string &s) {
  std::ofstream f(p, std::ios::binary);
  f << s;
  check(bool(f), "Write owned control");
}
std::string bytes(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  check(bool(f), "Read observed bytes");
  return {std::istreambuf_iterator<char>(f), {}};
}
std::size_t descriptors() {
  std::size_t count = 0;
  for (int fd = 0; fd < 4096; ++fd)
    if (::fcntl(fd, F_GETFD) != -1)
      ++count;
  return count;
}
void run(const fs::path &root, int variant, bool existing, int phase,
         int attack, int number) {
  const auto work = root / std::to_string(number), live = work / "live",
             moved = work / "moved";
  fs::create_directories(live);
  const auto target = live / "target.bin", witness = work / "witness.bin";
  put(witness, "unrelated witness");
  if (existing)
    put(target, "previous target");
  const std::vector<unsigned char> payload(131073, 173), before = payload;
  fs::path stage, original;
  bool attacked = false;
  auto replace = [&] {
    if (attacked)
      return;
    for (const auto &entry : fs::directory_iterator(live))
      if (entry.path().filename().string().find(".tmp.") != std::string::npos)
        stage = entry.path();
    if (stage.empty())
      return;
    if (attack == 4) {
      fs::rename(live, moved);
      fs::create_directory(live);
      put(target, "replacement directory target");
      put(stage, "replacement entry");
      original = moved / stage.filename();
    } else {
      original = work / "retained-original.bin";
      fs::rename(stage, original);
      if (attack == 0)
        put(stage, "replacement entry");
      else if (attack == 1)
        fs::create_symlink(witness, stage);
      else if (attack == 2) {
        fs::create_directory(stage);
        put(stage / "child.bin", "replacement child");
      }
      // attack 3 leaves the original name absent.
    }
    attacked = true;
  };
  ExecutionControl control;
  if (phase != 1)
    control.checkpoint = [&] {
      replace();
      if (attacked && phase == 2)
        throw OperationCancelled();
    };
  else
    control.stage_progress = [&](std::string_view label, std::size_t,
                                 std::size_t) {
      if (label == "Atomic file replace" ||
          label == "ROI export awaiting publication")
        replace();
    };
  const auto fd_before = descriptors();
  bool refused = false;
  try {
    if (variant == 0)
      detail::atomic_replace_file(target, payload.data(), payload.size(),
                                  "ownership test", control);
    else if (variant == 1) {
      auto value = detail::stage_atomic_file(target, payload.data(),
                                             payload.size(), control);
      value->publish(true);
    } else {
      detail::StagedAtomicStream value(target);
      value.open(control);
      value.append(payload.data(), payload.size(), control);
      value.close(control);
      if (phase == 1)
        replace();
      value.publish(true);
    }
  } catch (const std::exception &) {
    refused = true;
  }
  check(descriptors() == fd_before, "Operation leaked a descriptor");
  check(attacked, "Attack checkpoint not reached");
  if (!refused)
    std::cerr << "variant=" << variant << " phase=" << phase
              << " attack=" << attack << " existing=" << existing << '\n';
  check(refused, "Replaced stage was published");
  check(payload == before && bytes(witness) == "unrelated witness",
        "Caller or witness bytes changed");
  const auto old_target = attack == 4 ? moved / "target.bin" : target;
  check(existing ? bytes(old_target) == "previous target"
                 : !fs::exists(old_target),
        "Original target changed");
  if (attack == 4) {
    check(bytes(target) == "replacement directory target" &&
              bytes(stage) == "replacement entry",
          "Replacement directory contents changed");
    check(!fs::exists(original), "Anchored cleanup leaked its original stage");
  } else {
    check(fs::exists(original),
          "Moved owned original was unexpectedly deleted");
    if (attack == 0)
      check(bytes(stage) == "replacement entry",
            "Replacement file removed/changed");
    else if (attack == 1)
      check(fs::is_symlink(stage) && fs::read_symlink(stage) == witness,
            "Replacement symlink removed/changed");
    else if (attack == 2)
      check(bytes(stage / "child.bin") == "replacement child",
            "Replacement directory removed/changed");
    else
      check(!fs::exists(stage), "Absent stage was recreated");
  }
}
void controls(const fs::path &root) {
  const std::vector<unsigned char> payload{1, 2, 3, 5, 8};
  for (int variant = 0; variant < 3; ++variant)
    for (bool existing : {false, true})
      for (bool replace : {false, true}) {
        const auto work =
            root / ("control-" + std::to_string(variant) +
                    std::to_string(existing) + std::to_string(replace));
        fs::create_directory(work);
        const auto target = work / "target.bin";
        if (existing)
          put(target, "old");
        bool failed = false;
        const auto fd_before = descriptors();
        try {
          if (variant == 0)
            detail::atomic_replace_file(target, payload.data(), payload.size(),
                                        "control");
          else if (variant == 1) {
            auto v = detail::stage_atomic_file(target, payload.data(),
                                               payload.size(), {});
            v->publish(replace);
          } else {
            detail::StagedAtomicStream v(target);
            v.open({});
            v.append(payload.data(), payload.size(), {});
            v.close({});
            v.publish(replace);
          }
        } catch (const std::exception &) {
          failed = true;
        }
        check(descriptors() == fd_before, "Control leaked a descriptor");
        check(failed == (variant != 0 && existing && !replace),
              "Replace/no-replace control result");
        check(
            bytes(target) ==
                (failed ? "old" : std::string(payload.begin(), payload.end())),
            "Control target bytes");
        check(std::distance(fs::directory_iterator(work),
                            fs::directory_iterator{}) == 1,
              "Control leaked stage");
      }
}
void retained_descriptor_limits(const fs::path &root) {
  struct Limit {
    struct rlimit original{};
    Limit() {
      check(::getrlimit(RLIMIT_NOFILE, &original) == 0,
            "Read descriptor limit");
      auto bounded = original;
      bounded.rlim_cur = std::min(rlim_t(64), original.rlim_cur);
      check(::setrlimit(RLIMIT_NOFILE, &bounded) == 0,
            "Bound descriptor limit");
    }
    ~Limit() { ::setrlimit(RLIMIT_NOFILE, &original); }
  };
  for (int variant = 0; variant < 3; ++variant) {
    const auto work = root / ("retain-limit-" + std::to_string(variant));
    fs::create_directory(work);
    const auto target = work / "target.bin";
    put(target, "previous target");
    bool failed = false;
    std::string diagnostic;
    const auto fd_before = descriptors();
    {
      Limit limit;
      std::vector<int> fillers;
      for (;;) {
        const auto fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
          check(errno == EMFILE, "Actual descriptor exhaustion");
          break;
        }
        fillers.push_back(fd);
      }
      check(fillers.size() > 2, "Need room for parent/writer controls");
      for (int i = 0; i < 2; ++i) {
        ::close(fillers.back());
        fillers.pop_back();
      }
      const std::vector<unsigned char> payload(131073, 173);
      try {
        if (variant == 0)
          detail::atomic_replace_file(target, payload.data(), payload.size(),
                                      "limit control");
        else if (variant == 1) {
          auto v = detail::stage_atomic_file(target, payload.data(),
                                             payload.size(), {});
          v->publish(true);
        } else {
          detail::StagedAtomicStream v(target);
          v.open({});
          v.append(payload.data(), payload.size(), {});
          v.close({});
          v.publish(true);
        }
      } catch (const std::system_error &e) {
        failed = e.code().value() == EMFILE;
        diagnostic = e.what();
      }
      for (auto fd : fillers)
        ::close(fd);
    }
    check(failed && diagnostic.find("retain") != std::string::npos,
          "Retained descriptor exhaustion was not safely reported");
    check(descriptors() == fd_before && bytes(target) == "previous target",
          "Descriptor limit changed destination or leaked resources");
    check(std::distance(fs::directory_iterator(work),
                        fs::directory_iterator{}) == 1,
          "Descriptor limit leaked stage");
  }
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 2 || argc == 3,
          "Need output root and optional attack selector");
    const auto root =
        fs::absolute(argv[1]) /
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root);
    int count = 0;
    for (int variant = 0; variant < 3; ++variant)
      for (bool existing : {false, true})
        for (int phase = 0; phase < 3; ++phase)
          for (int attack = 0; attack < 5; ++attack)
            if (argc == 2 || attack == std::stoi(argv[2]))
              run(root, variant, existing, phase, attack, count++);
    controls(root);
    retained_descriptor_limits(root);
    std::cout
        << "PASS POSIX ownership substitutions=" << count
        << " controls=12 retained_descriptor_limits=3 zero descriptor leaks\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
