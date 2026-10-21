// Private syscall fault controls: product builds expose no test switches.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif
namespace probe {
enum class Fault { none, write_error, short_write, zero_write, seek_error, restore_error, interrupted };
Fault fault = Fault::none;
unsigned calls = 0;
#ifdef _WIN32
BOOL write(HANDLE h, LPCVOID p, DWORD n, LPDWORD done, LPOVERLAPPED o) {
  ++calls;
  if (fault == Fault::write_error) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
  if (fault == Fault::zero_write) { *done = 0; return TRUE; }
  return ::WriteFile(h, p, fault == Fault::short_write ? std::min(n, DWORD(7)) : n, done, o);
}
BOOL seek(HANDLE h, LARGE_INTEGER p, PLARGE_INTEGER result, DWORD origin) {
  if ((fault == Fault::seek_error && origin == FILE_BEGIN) ||
      (fault == Fault::restore_error && origin == FILE_BEGIN && calls)) {
    SetLastError(ERROR_SEEK); return FALSE;
  }
  return ::SetFilePointerEx(h, p, result, origin);
}
#else
ssize_t write(int fd, const void *p, size_t n, off_t offset) {
  ++calls;
  if (fault == Fault::write_error) { errno = EIO; return -1; }
  if (fault == Fault::zero_write) return 0;
  if (fault == Fault::interrupted && calls == 1) { errno = EINTR; return -1; }
  return ::pwrite(fd, p, fault == Fault::short_write ? std::min(n, size_t(7)) : n, offset);
}
#endif
}
#ifdef _WIN32
#define WriteFile probe::write
#define SetFilePointerEx probe::seek
#else
#define pwrite probe::write
#endif
#include "../../src/native/atomic_file.cpp"
#ifdef _WIN32
#undef WriteFile
#undef SetFilePointerEx
#else
#undef pwrite
#endif
using imagejxx::detail::StagedAtomicStream;
using imagejxx::ExecutionControl;
namespace fs = std::filesystem;
namespace {
std::size_t checks = 0;
void check(bool ok, const char *message) { ++checks; if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F f) {
  bool caught = false; try { f(); } catch (const std::exception &) { caught = true; }
  check(caught, "Expected stream rejection");
}
std::string read(const fs::path &p) {
#ifdef _WIN32
  // A staged object retains DELETE access until publication/destruction.
  const auto h = CreateFileW(p.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  check(h != INVALID_HANDLE_VALUE, "Read output handle");
  std::string result; char buffer[4096]; DWORD n = 0; BOOL ok;
  while ((ok = ReadFile(h, buffer, sizeof(buffer), &n, nullptr)) && n)
    result.append(buffer, n);
  CloseHandle(h);
  check(ok != FALSE, "Read output bytes");
  return result;
#else
  std::ifstream f(p, std::ios::binary); check(bool(f), "Read output");
  return {std::istreambuf_iterator<char>(f), {}};
#endif
}
void write(const fs::path &p, const std::string &s) {
  std::ofstream f(p, std::ios::binary); f << s; check(bool(f), "Write witness");
}
void append(StagedAtomicStream &s, const std::string &v) {
  s.append(reinterpret_cast<const unsigned char *>(v.data()), v.size(), {});
}
void patch(StagedAtomicStream &s, std::size_t at, const std::string &v,
           const ExecutionControl &c = {}) {
  s.patch(at, reinterpret_cast<const unsigned char *>(v.data()), v.size(), c);
}
}
int main(int argc, char **argv) { try {
  check(argc == 2, "Supply fresh output directory");
  fs::path root = fs::absolute(argv[1]); fs::create_directories(root.parent_path());
  check(fs::create_directory(root), "Fresh output required");
  const auto target = root / "output.bin";
  {
    StagedAtomicStream s(target);
    rejects([&] { patch(s, 0, "x"); }); s.open({});
    append(s, "0000data"); patch(s, 0, "TIFF"); patch(s, 7, "!");
    s.patch(8, nullptr, 0, {});
    rejects([&] { patch(s, 8, "x"); });
    rejects([&] { patch(s, std::numeric_limits<std::size_t>::max(), "x"); });
    rejects([&] { s.patch(1, reinterpret_cast<const unsigned char *>("x"), std::numeric_limits<std::size_t>::max(), {}); });
    rejects([&] { s.patch(0, nullptr, 1, {}); });
    rejects([&] { s.append(nullptr, 1, {}); });
    append(s, "tail"); s.close({});
    rejects([&] { patch(s, 0, "x"); }); s.publish(false);
    check(read(target) == "TIFFdat!tail", "Patch changed length or append position");
    rejects([&] { patch(s, 0, "x"); });
  }
  {
    StagedAtomicStream s(target); s.open({}); append(s, "old");
    patch(s, 0, "new"); s.close({}); rejects([&] { s.publish(false); });
    check(read(target) == "TIFFdat!tail", "No-replace changed destination");
  }
  for (auto fault : {probe::Fault::short_write, probe::Fault::none}) {
    StagedAtomicStream s(target); s.open({});
    std::string expected(150000, 'a'), replacement(140000, 'b'); append(s, expected);
    probe::fault = fault; probe::calls = 0; patch(s, 5, replacement);
    check(probe::calls > 1, "Patch must exercise partial or bounded writes");
    probe::fault = probe::Fault::none; append(s, "tail"); s.close({}); s.publish(true);
    expected.replace(5, replacement.size(), replacement); expected += "tail";
    check(read(target) == expected, "Multi-chunk patch differs");
  }
  const auto before = read(target);
  std::vector<probe::Fault> faults{probe::Fault::write_error, probe::Fault::zero_write};
#ifdef _WIN32
  faults.push_back(probe::Fault::seek_error); faults.push_back(probe::Fault::restore_error);
#else
  {
    StagedAtomicStream s(root / "eintr.bin"); s.open({}); append(s, "1234");
    probe::fault = probe::Fault::interrupted; probe::calls = 0; patch(s, 1, "bc");
    check(probe::calls == 2, "Interrupted pwrite not retried");
    probe::fault = probe::Fault::none; s.close({}); s.publish(false);
    check(read(root / "eintr.bin") == "1bc4", "Interrupted patch differs");
  }
#endif
  for (auto fault : faults) {
    fs::path temporary;
    {
      StagedAtomicStream s(target); s.open({}); append(s, "1234"); temporary = s.temporary_path();
      probe::fault = fault; probe::calls = 0; rejects([&] { patch(s, 0, "FAIL"); });
      probe::fault = probe::Fault::none;
      rejects([&] { append(s, "x"); }); rejects([&] { s.close({}); }); rejects([&] { s.publish(true); });
    }
    check(!fs::exists(temporary) && read(target) == before, "Fault published or leaked stage");
  }
  for (bool cancel : {false, true}) {
    fs::path temporary;
    {
      StagedAtomicStream s(target); s.open({}); append(s, std::string(150000, 'a'));
      temporary = s.temporary_path(); ExecutionControl c; unsigned ticks = 0;
      c.cancellation = std::make_shared<imagejxx::CancellationToken>();
      c.checkpoint = [&] { if (++ticks == 3) { if (cancel) c.cancellation->cancel(); else throw std::runtime_error("callback fault"); } };
      rejects([&] { patch(s, 0, std::string(150000, 'b'), c); });
      rejects([&] { s.close({}); }); rejects([&] { s.publish(true); });
    }
    check(!fs::exists(temporary) && read(target) == before, "Aborted patch leaked or published");
  }
  {
    // Move/reuse the stage name: patch the held object, never the impostor.
    fs::path planned, moved = root / "moved.bin";
    {
      StagedAtomicStream s(target); s.open({}); append(s, "original"); planned = s.temporary_path();
      fs::rename(planned, moved); write(planned, "impostor"); patch(s, 0, "PATCH");
      s.close({}); check(read(moved) == "PATCHnal", "Patch did not follow held object");
      check(read(planned) == "impostor", "Patch modified reused pathname");
    }
    check(read(planned) == "impostor", "Cleanup removed impostor");
#ifdef _WIN32
    check(!fs::exists(moved), "Owned Windows object not cleaned");
#else
    check(read(moved) == "PATCHnal", "POSIX cleanup changed renamed entry");
#endif
  }
  std::cout << checks << " atomic stream patch checks passed\n"; return 0;
} catch (const std::exception &e) { probe::fault = probe::Fault::none; std::cerr << e.what() << '\n'; return 1; } }
