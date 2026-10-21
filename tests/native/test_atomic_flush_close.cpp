// Private syscall-boundary qualification. Only this test TU redirects calls;
// product builds compile atomic_file.cpp normally and expose no fault controls.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <cstdarg>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
namespace fault_test {
namespace fs = std::filesystem;
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
enum class Fault {
  none,
  flush_error,
  flush_kernel,
  retain_error,
  close_held,
  close_released,
  close_reused,
  close_eintr,
  close_badfd,
  publish_close_held,
  publish_close_reused
};
#ifdef _WIN32
using Resource = HANDLE;
#else
using Resource = int;
#endif
struct State {
  Fault fault = Fault::none;
  bool consumed = false;
  int error = 0;
  int close_calls = 0;
  std::set<Resource> owned;
  std::vector<Resource> witnesses;
  fs::path witness;
};
State *active = nullptr;
#ifdef _WIN32
HANDLE WINAPI create(LPCWSTR path, DWORD access, DWORD sharing,
                     LPSECURITY_ATTRIBUTES security, DWORD disposition,
                     DWORD flags, HANDLE parent) {
  auto h = ::CreateFileW(path, access, sharing, security, disposition, flags,
                         parent);
  if (active && h != INVALID_HANDLE_VALUE)
    active->owned.insert(h);
  return h;
}
HANDLE WINAPI reopen(HANDLE h, DWORD access, DWORD sharing, DWORD flags) {
  if (active && active->fault == Fault::retain_error && !active->consumed) {
    active->consumed = true;
    active->error = ERROR_TOO_MANY_OPEN_FILES;
    SetLastError(active->error);
    return INVALID_HANDLE_VALUE;
  }
  auto value = ::ReOpenFile(h, access, sharing, flags);
  if (active && value != INVALID_HANDLE_VALUE)
    active->owned.insert(value);
  return value;
}
BOOL WINAPI flush(HANDLE h) {
  if (active && !active->consumed &&
      (active->fault == Fault::flush_error ||
       active->fault == Fault::flush_kernel)) {
    active->consumed = true;
    if (active->fault == Fault::flush_kernel) {
      const auto restricted = ::ReOpenFile(
          h, GENERIC_READ,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
      check(restricted != INVALID_HANDLE_VALUE,
            "Create read-only kernel flush control");
      const auto result = ::FlushFileBuffers(restricted);
      const auto error = GetLastError();
      ::CloseHandle(restricted);
      check(!result && error == ERROR_ACCESS_DENIED,
            "Kernel did not refuse read-only flush");
      active->error = error;
    } else
      active->error = ERROR_WRITE_FAULT;
    SetLastError(active->error);
    return FALSE;
  }
  return ::FlushFileBuffers(h);
}
BOOL WINAPI close(HANDLE h) {
  if (active && active->owned.count(h))
    ++active->close_calls;
  if (active && active->owned.count(h) && !active->consumed &&
      (active->fault == Fault::close_held ||
       active->fault == Fault::close_released ||
       active->fault == Fault::close_reused ||
       ((active->fault == Fault::publish_close_held ||
         active->fault == Fault::publish_close_reused) &&
        active->close_calls == 2))) {
    active->consumed = true;
    if (active->fault == Fault::close_held ||
        active->fault == Fault::publish_close_held) {
      check(!IsDebuggerPresent(),
            "Kernel protected-close case requires a non-debugger child");
      check(SetHandleInformation(h, HANDLE_FLAG_PROTECT_FROM_CLOSE,
                                 HANDLE_FLAG_PROTECT_FROM_CLOSE),
            "Protect owned handle");
      const auto result = ::CloseHandle(h);
      const auto error = GetLastError();
      check(SetHandleInformation(h, HANDLE_FLAG_PROTECT_FROM_CLOSE, 0),
            "Restore owned close protection");
      check(!result && error != ERROR_SUCCESS,
            "Kernel did not refuse protected close");
      active->error = error;
    } else {
      check(::CloseHandle(h), "Release faulted original handle");
      active->owned.erase(h);
      if (active->fault == Fault::close_reused ||
          active->fault == Fault::publish_close_reused) {
        bool reused = false;
        // Windows can defer reuse beyond a single 1024-entry handle table
        // segment. Keep the exact-reuse assertion with a bounded larger sweep.
        for (int i = 0; i < 16384 && !reused; ++i) {
          auto witness = ::CreateFileW(active->witness.c_str(), GENERIC_READ,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE |
                                           FILE_SHARE_DELETE,
                                       nullptr, OPEN_EXISTING, 0, nullptr);
          check(witness != INVALID_HANDLE_VALUE,
                "Open unrelated recycling witness");
          active->witnesses.push_back(witness);
          reused = witness == h;
        }
        check(reused,
              "Windows handle slot was not recycled within fixture bound");
      }
      active->error = ERROR_INVALID_HANDLE;
    }
    SetLastError(active->error);
    return FALSE;
  }
  const auto result = ::CloseHandle(h);
  if (result && active)
    active->owned.erase(h);
  return result;
}
#else
int open(const char *path, int flags, mode_t mode) {
  auto fd = int(::syscall(SYS_openat, AT_FDCWD, path, flags, mode));
  if (active && fd >= 0 && (flags & O_CREAT))
    active->owned.insert(fd);
  return fd;
}
int sync(int fd) {
  if (active && !active->consumed &&
      (active->fault == Fault::flush_error ||
       active->fault == Fault::flush_kernel)) {
    active->consumed = true;
    if (active->fault == Fault::flush_kernel) {
      int pipefd[2];
      check(::pipe(pipefd) == 0, "Create kernel flush control pipe");
      const auto result = ::syscall(SYS_fsync, pipefd[1]);
      const auto error = errno;
      ::syscall(SYS_close, pipefd[0]);
      ::syscall(SYS_close, pipefd[1]);
      check(result == -1 && error == EINVAL,
            "Kernel did not refuse pipe fsync");
      active->error = error;
    } else
      active->error = EIO;
    errno = active->error;
    return -1;
  }
  return int(::syscall(SYS_fsync, fd));
}
int close(int fd) {
  if (active && active->owned.count(fd) && !active->consumed &&
      (active->fault == Fault::close_released ||
       active->fault == Fault::close_reused ||
       active->fault == Fault::close_eintr ||
       active->fault == Fault::close_badfd)) {
    active->consumed = true;
    check(::syscall(SYS_close, fd) == 0, "Release faulted Linux descriptor");
    active->owned.erase(fd);
    if (active->fault == Fault::close_reused) {
      int witness = int(::syscall(SYS_openat, AT_FDCWD, active->witness.c_str(),
                                  O_RDONLY, 0));
      check(witness >= 0, "Open recycling witness");
      if (witness != fd) {
        check(::dup2(witness, fd) == fd, "Recycle exact descriptor");
        ::syscall(SYS_close, witness);
      }
      active->witnesses.push_back(fd);
    }
    active->error = active->fault == Fault::close_eintr   ? EINTR
                    : active->fault == Fault::close_badfd ? EBADF
                                                          : EIO;
    errno = active->error;
    return -1;
  }
  const auto result = int(::syscall(SYS_close, fd));
  if (result == 0 && active)
    active->owned.erase(fd);
  return result;
}
#endif
} // namespace fault_test
#ifdef _WIN32
#define CreateFileW fault_test::create
#define ReOpenFile fault_test::reopen
#define FlushFileBuffers fault_test::flush
#define CloseHandle fault_test::close
#else
extern "C" int __wrap_open(const char *p, int f, ...) {
  mode_t m = 0;
  if (f & O_CREAT) {
    va_list a;
    va_start(a, f);
    m = va_arg(a, mode_t);
    va_end(a);
  }
  return fault_test::open(p, f, m);
}
extern "C" int __wrap_fsync(int fd) { return fault_test::sync(fd); }
extern "C" int __wrap_openat(int dirfd, const char *p, int f, ...) {
  mode_t m = 0;
  if (f & O_CREAT) {
    va_list a;
    va_start(a, f);
    m = va_arg(a, mode_t);
    va_end(a);
  }
  const auto fd = int(::syscall(SYS_openat, dirfd, p, f, m));
  if (fault_test::active && fd >= 0 && (f & O_CREAT))
    fault_test::active->owned.insert(fd);
  return fd;
}
extern "C" int __wrap_close(int fd) { return fault_test::close(fd); }
#endif
#include "../../src/native/atomic_file.cpp"
#ifdef _WIN32
#undef CreateFileW
#undef ReOpenFile
#undef FlushFileBuffers
#undef CloseHandle
#else
#endif
namespace fault_test {
std::string bytes(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  check(bool(f), "Read observed file");
  return {std::istreambuf_iterator<char>(f), {}};
}
void put(const fs::path &p, const std::string &value) {
  std::ofstream f(p, std::ios::binary);
  f << value;
  check(bool(f), "Write control bytes");
}
void retire(State &state) {
  active = nullptr;
  for (auto h : state.owned) {
#ifdef _WIN32
    ::CloseHandle(h);
#else
    ::close(h);
#endif
  }
  state.owned.clear();
  for (auto h : state.witnesses) {
#ifdef _WIN32
    ::CloseHandle(h);
#else
    ::close(h);
#endif
  }
  state.witnesses.clear();
}
void run(const fs::path &root, int variant, bool existing, Fault fault,
         int number) {
  const auto work = root / std::to_string(number);
  fs::create_directory(work);
  const auto target = work / "target.bin", witness = work / "unrelated.bin";
  if (existing)
    put(target, "previous target");
  put(witness, "unrelated witness");
  const std::vector<unsigned char> payload(131073, 173), before = payload;
  State state;
  state.fault = fault;
  state.witness = witness;
  active = &state;
  bool failed = false;
  int observed = 0;
  std::string message;
  try {
    if (variant == 0)
      imagejxx::detail::atomic_replace_file(target, payload.data(),
                                            payload.size(), "flush-close test");
    else if (variant == 1) {
      auto value = imagejxx::detail::stage_atomic_file(target, payload.data(),
                                                       payload.size(), {});
      value->publish(true);
    } else {
      imagejxx::detail::StagedAtomicStream value(target);
      value.open({});
      value.append(payload.data(), payload.size(), {});
      value.close({});
      value.publish(true);
    }
  } catch (const std::system_error &error) {
    failed = true;
    observed = error.code().value();
    message = error.what();
  } catch (...) {
    retire(state);
    throw;
  }
  const auto leaks = state.owned.size();
  bool witness_valid = true;
  for (auto h : state.witnesses) {
#ifdef _WIN32
    DWORD flags = 0;
    witness_valid &= GetHandleInformation(h, &flags) != 0;
#else
    witness_valid &= ::fcntl(h, F_GETFD) != -1;
#endif
  }
  retire(state);
  check(witness_valid, "Failure cleanup closed a recycled unrelated resource");
  if (leaks) {
    std::cerr << "variant=" << variant << " existing=" << existing
              << " fault=" << int(fault) << " leaked_owned_resources=" << leaks
              << '\n';
    throw std::runtime_error("Flush/close failure leaked an owned resource");
  }
  if (fault == Fault::none || fault == Fault::publish_close_held ||
      fault == Fault::publish_close_reused) {
    check(!failed && state.consumed == (fault != Fault::none),
          "Published control failed");
    check(bytes(target) == std::string(payload.begin(), payload.end()),
          "Valid control payload mismatch");
  } else {
    check(failed && state.consumed && observed == state.error,
          "Fault code was hidden or boundary not reached");
    check(existing ? bytes(target) == "previous target" : !fs::exists(target),
          "Fault published/changed destination");
    const auto phase =
        (fault == Fault::flush_error || fault == Fault::flush_kernel) ? "flush"
        : fault == Fault::retain_error                                ? "retain"
                                                                      : "close";
    check(message.find(phase) != std::string::npos,
          "Fault phase diagnostic mismatch");
  }
  check(bytes(witness) == "unrelated witness" && payload == before,
        "Fault changed witness or source");
  for (const auto &entry : fs::directory_iterator(work))
    check(entry.path().filename().u8string().find(".tmp.") == std::string::npos,
          "Fault leaked stage pathname");
}
} // namespace fault_test
int main(int argc, char **argv) {
  try {
    fault_test::check(argc == 2 || argc == 3, "Need fresh flush/close root");
    const auto root =
        std::filesystem::absolute(std::filesystem::u8path(argv[1])) /
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    fault_test::check(!std::filesystem::exists(root),
                      "Fault artifacts must be fresh");
    std::filesystem::create_directories(root);
    using F = fault_test::Fault;
#ifdef _WIN32
    std::vector<F> modes{
        F::none,         F::flush_error,        F::flush_kernel,
        F::retain_error, F::close_released,     F::close_reused,
        F::close_held,   F::publish_close_held, F::publish_close_reused};
    if (argc == 3) {
      fault_test::check(std::string(argv[2]) == "publication",
                        "Unknown selector");
      modes = {F::publish_close_held, F::publish_close_reused};
    }
#else
    const std::vector<F> modes{
        F::none,         F::flush_error, F::flush_kernel, F::close_released,
        F::close_reused, F::close_eintr, F::close_badfd};
#endif
    int cases = 0;
    for (int variant = 0; variant < 3; ++variant)
      for (bool existing : {false, true})
        for (auto mode : modes)
          fault_test::run(root, variant, existing, mode, cases++);
    std::cout << "PASS flush/close boundary matrix cases=" << cases
              << "; valid controls; kernel-denied controls; injected API "
                 "faults; recycled resources; zero leaks\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
