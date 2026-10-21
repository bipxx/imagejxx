// Linux test-only link boundary scheduling; all filesystem calls reach the
// kernel.
#include "atomic_file.hpp"
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <linux/magic.h>
#include <sys/syscall.h>
#include <sys/vfs.h>
#include <unistd.h>
#include <vector>
namespace fs = std::filesystem;
namespace {
void check(bool v, const char *message) {
  if (!v)
    throw std::runtime_error(message);
}
void put(const fs::path &p, const std::string &s) {
  std::ofstream f(p, std::ios::binary);
  f << s;
  check(bool(f), "Write witness");
}
std::string bytes(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  check(bool(f), "Read witness");
  return {std::istreambuf_iterator<char>(f), {}};
}
struct State {
  fs::path work, stage, moved, witness;
  int mode, route;
  bool consumed = false;
  int calls = 0;
};
State *active = nullptr;
int empty_calls = 0, proc_calls = 0, empty_successes = 0, proc_successes = 0;
std::size_t descriptors() {
  std::size_t count = 0;
  for (int fd = 0; fd < 4096; ++fd)
    if (::fcntl(fd, F_GETFD) != -1)
      ++count;
  return count;
}
} // namespace
extern "C" int __wrap_linkat(int fromfd, const char *from, int tofd,
                             const char *to, int flags) {
  if (active) {
    ++active->calls;
    if (!active->consumed) {
      for (const auto &entry : fs::directory_iterator(active->work))
        if (entry.path().filename().string().find(".tmp.") != std::string::npos)
          active->stage = entry.path();
      check(!active->stage.empty(), "Need real stage at last syscall boundary");
      fs::rename(active->stage, active->moved);
      if (active->mode == 0)
        put(active->stage, "late replacement");
      else if (active->mode == 1)
        fs::create_symlink(active->witness, active->stage);
      else if (active->mode == 2) {
        fs::create_directory(active->stage);
        put(active->stage / "child.bin", "late child");
      } else if (active->mode == 4)
        fs::remove(active->moved);
      active->consumed = true;
    }
  }
  // Private argument controls exercise the fallback with real kernel EINVAL,
  // then its failure with real ENOENT. No return values are fabricated.
  auto kernel_flags = flags;
  std::string missing;
  if (active && active->route && flags == AT_EMPTY_PATH)
    kernel_flags |= 0x40000000;
  if (active && active->route == 2 && flags == AT_SYMLINK_FOLLOW) {
    missing = (active->work / "missing-proc-descriptor").string();
    from = missing.c_str();
  }
  const auto result =
      int(::syscall(SYS_linkat, fromfd, from, tofd, to, kernel_flags));
  if (active && active->route && flags == AT_EMPTY_PATH)
    check(result == -1 && errno == EINVAL,
          "Kernel did not refuse unknown link flags");
  if (flags == AT_EMPTY_PATH) {
    ++empty_calls;
    empty_successes += result == 0;
  } else if (flags == AT_SYMLINK_FOLLOW) {
    ++proc_calls;
    proc_successes += result == 0;
  }
  return result;
}
int main(int argc, char **argv) {
  try {
    check(argc == 2 || argc == 3,
          "Need artifact root and optional filesystem refusal selector");
    const auto root =
        fs::absolute(argv[1]) /
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root);
    if (argc == 3) {
      check(std::string(argv[2]) == "unsupported-filesystem",
            "Unknown selector");
      struct statfs filesystem{};
      check(::statfs(root.c_str(), &filesystem) == 0 &&
                filesystem.f_type == V9FS_MAGIC,
            "Refusal qualification requires real 9P filesystem");
      const std::vector<unsigned char> payload(65537, 173);
      int refused = 0;
      for (int variant = 1; variant <= 2; ++variant)
        for (bool existing : {false, true}) {
          const auto work = root / std::to_string(refused);
          fs::create_directory(work);
          const auto target = work / "target.bin";
          if (existing)
            put(target, "previous target");
          bool failed = false;
          const auto fd_before = descriptors();
          try {
            if (variant == 1) {
              auto v = imagejxx::detail::stage_atomic_file(
                  target, payload.data(), payload.size(), {});
              v->publish(false);
            } else {
              imagejxx::detail::StagedAtomicStream v(target);
              v.open({});
              v.append(payload.data(), payload.size(), {});
              v.close({});
              v.publish(false);
            }
          } catch (const std::system_error &e) {
            failed = e.code() ==
                     std::make_error_code(std::errc::operation_not_supported);
          }
          check(failed && descriptors() == fd_before,
                "Unsafe filesystem did not refuse safely");
          check(existing ? bytes(target) == "previous target"
                         : !fs::exists(target),
                "Filesystem refusal changed destination");
          check(std::distance(fs::directory_iterator(work),
                              fs::directory_iterator{}) == (existing ? 1 : 0),
                "Filesystem refusal leaked stage");
          ++refused;
        }
      check(empty_calls == 0 && proc_calls == 0,
            "Unsafe provider reached link syscall");
      std::cout << "PASS actual 9P filesystem refusal cases=" << refused
                << "; no destination link calls; zero descriptor leaks\n";
      return 0;
    }
    int cases = 0;
    for (int route = 0; route < 3; ++route)
      for (int variant = 1; variant <= 2; ++variant)
        for (bool existing : {false, true})
          for (int mode = 0; mode < 5; ++mode) {
            const auto work = root / std::to_string(cases++);
            fs::create_directory(work);
            const auto target = work / "target.bin";
            if (existing)
              put(target, "previous target");
            const std::vector<unsigned char> payload(131073, 173),
                before = payload;
            State state{
                work, {},   work / "moved-original.bin", work / "witness.bin",
                mode, route};
            put(state.witness, "unrelated witness");
            bool failed = false;
            int error = 0;
            const auto fd_before = descriptors();
            try {
              if (variant == 1) {
                auto v = imagejxx::detail::stage_atomic_file(
                    target, payload.data(), payload.size(), {});
                active = &state;
                v->publish(false);
              } else {
                imagejxx::detail::StagedAtomicStream v(target);
                v.open({});
                v.append(payload.data(), payload.size(), {});
                v.close({});
                active = &state;
                v.publish(false);
              }
            } catch (const std::system_error &e) {
              failed = true;
              error = e.code().value();
            }
            active = nullptr;
            check(descriptors() == fd_before, "Publication leaked descriptors");
            check(state.consumed && state.calls > 0,
                  "Late boundary not reached");
            check(payload == before &&
                      bytes(state.witness) == "unrelated witness",
                  "Source/witness changed");
            if (route == 2)
              check(failed && error == ENOENT &&
                        (existing ? bytes(target) == "previous target"
                                  : !fs::exists(target)),
                    "Unavailable descriptor publication did not fail closed");
            else if (existing)
              check(failed &&
                        (error == EEXIST || (mode == 4 && error == ENOENT)) &&
                        bytes(target) == "previous target",
                    "No-replace existing target changed");
            else if (mode == 4)
              check(failed && !fs::exists(target),
                    "Unlinked original was replaced by another object");
            else {
              if (failed)
                std::cerr << "unexpected errno=" << error << '\n';
              check(!failed && bytes(target) ==
                                   std::string(payload.begin(), payload.end()),
                    "Late substituted stage was published");
            }
            if (mode == 0)
              check(bytes(state.stage) == "late replacement",
                    "Cleanup removed replacement");
            else if (mode == 1)
              check(fs::is_symlink(state.stage) &&
                        fs::read_symlink(state.stage) == state.witness,
                    "Cleanup removed symlink");
            else if (mode == 2)
              check(bytes(state.stage / "child.bin") == "late child",
                    "Cleanup changed directory");
            else
              check(!fs::exists(state.stage), "Missing stage was recreated");
            if (mode != 4)
              check(bytes(state.moved) ==
                        std::string(payload.begin(), payload.end()),
                    "Moved original changed");
          }
    std::cout << "PASS Linux descriptor-bound no-replace late races=" << cases
              << "; actual kernel links; source/replacements preserved; zero "
                 "descriptor leaks\n";
    std::cout << "effective_uid=" << ::geteuid()
              << " empty_calls=" << empty_calls
              << " empty_successes=" << empty_successes
              << " proc_calls=" << proc_calls
              << " proc_successes=" << proc_successes << '\n';
    return 0;
  } catch (const std::exception &e) {
    active = nullptr;
    std::cerr << e.what() << '\n';
    return 1;
  }
}
