#include "../native/atomic_file.hpp"
#include "internal.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace imagejxx::io_detail {
void require(bool b, const char *message) {
  if (!b)
    throw std::invalid_argument(message);
}
std::size_t add(std::size_t a, std::size_t b) {
  if (b > SIZE_MAX - a)
    throw std::length_error("Image IO sum overflow");
  return a + b;
}
std::size_t mul(std::size_t a, std::size_t b) {
  if (a && b > SIZE_MAX / a)
    throw std::length_error("Image IO product overflow");
  return a * b;
}
void cap(std::size_t value, std::size_t limit, const char *message) {
  if (value > limit)
    throw std::length_error(message);
}
void valid_limits(const ImageFileLimits &l) {
  require(l.max_file_bytes && l.max_ifds && l.max_ifd_entries_per_page &&
              l.max_metadata_bytes && l.max_total_samples &&
              l.max_plane_samples && l.max_codec_bytes && l.max_working_bytes &&
              l.max_output_file_bytes,
          "Image IO limits must be positive");
  cap(l.max_codec_bytes,
      std::size_t(std::numeric_limits<std::ptrdiff_t>::max()),
      "Image codec allocation limit is not addressable");
}
void token(const ExecutionControl &c) {
  if (c.cancellation && c.cancellation->canceled())
    throw OperationCancelled();
}
void checkpoint(const ExecutionControl &c) { detail::file_checkpoint(c); }
void progress(const ExecutionControl &c, std::size_t n) {
  token(c);
  if (c.progress)
    c.progress(n, 1);
  token(c);
}
void rows_progress(const ExecutionControl &c, std::size_t n,
                   std::size_t total, const char *stage) {
  checkpoint(c);
  if (c.stage_progress)
    c.stage_progress(stage, n, total);
  token(c);
}
namespace {
void utf8(const std::string &s) {
  for (std::size_t i = 0; i < s.size();) {
    const auto a = static_cast<unsigned char>(s[i++]);
    require(a != 0, "Image path contains NUL");
    if (a < 128)
      continue;
    unsigned n;
    std::uint32_t value, minimum;
    if (a >= 0xc2 && a <= 0xdf) {
      n = 1;
      value = a & 31;
      minimum = 128;
    } else if (a >= 0xe0 && a <= 0xef) {
      n = 2;
      value = a & 15;
      minimum = 2048;
    } else if (a >= 0xf0 && a <= 0xf4) {
      n = 3;
      value = a & 7;
      minimum = 65536;
    } else
      throw std::invalid_argument("Image path requires UTF-8");
    require(n <= s.size() - i, "Truncated UTF-8 image path");
    while (n--) {
      const auto b = static_cast<unsigned char>(s[i++]);
      require((b & 0xc0) == 0x80, "Image path requires UTF-8");
      value = (value << 6) | (b & 63);
    }
    require(value >= minimum && value <= 0x10ffff &&
                !(value >= 0xd800 && value <= 0xdfff),
            "Image path requires scalar UTF-8");
  }
}
#ifdef _WIN32
using Handle = HANDLE;
const Handle invalid = INVALID_HANDLE_VALUE;
Handle open_read(const std::filesystem::path &p) {
  return CreateFileW(p.c_str(), GENERIC_READ,
                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     nullptr, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr);
}
void close_handle(Handle h) {
  if (h != invalid)
    CloseHandle(h);
}
struct Stamp {
  std::array<std::uint64_t, 7> values{};
  std::uint64_t size = 0;
  bool operator==(const Stamp &s) const {
    return size == s.size && values == s.values;
  }
};
Stamp stamp(Handle h) {
  BY_HANDLE_FILE_INFORMATION i{};
  FILE_BASIC_INFO basic{};
  if (!GetFileInformationByHandle(h, &i) ||
      !GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof(basic)))
    throw std::runtime_error("Could not inspect image file identity");
  require(!(i.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY),
          "Image source must be a regular file");
  Stamp s;
  s.size = (std::uint64_t(i.nFileSizeHigh) << 32) | i.nFileSizeLow;
  s.values = {i.dwVolumeSerialNumber,
              i.nFileIndexHigh,
              i.nFileIndexLow,
              std::uint64_t(basic.CreationTime.QuadPart),
              std::uint64_t(basic.LastWriteTime.QuadPart),
              std::uint64_t(basic.ChangeTime.QuadPart),
              i.dwFileAttributes};
  return s;
}
#else
using Handle = int;
constexpr Handle invalid = -1;
Handle open_read(const std::filesystem::path &p) {
  return ::open(p.c_str(), O_RDONLY);
}
void close_handle(Handle h) {
  if (h != invalid)
    ::close(h);
}
struct Stamp {
  std::array<std::uint64_t, 6> values{};
  std::uint64_t size = 0;
  bool operator==(const Stamp &s) const {
    return size == s.size && values == s.values;
  }
};
Stamp stamp(Handle h) {
  struct stat i{};
  if (::fstat(h, &i) != 0)
    throw std::runtime_error("Could not inspect image file identity");
  require(S_ISREG(i.st_mode) && i.st_size >= 0,
          "Image source must be a regular file");
  Stamp s;
  s.size = std::uint64_t(i.st_size);
#if defined(__APPLE__)
  s.values = {std::uint64_t(i.st_dev),
              std::uint64_t(i.st_ino),
              std::uint64_t(i.st_mtimespec.tv_sec),
              std::uint64_t(i.st_mtimespec.tv_nsec),
              std::uint64_t(i.st_ctimespec.tv_sec),
              std::uint64_t(i.st_ctimespec.tv_nsec)};
#else
  s.values = {
      std::uint64_t(i.st_dev),         std::uint64_t(i.st_ino),
      std::uint64_t(i.st_mtim.tv_sec), std::uint64_t(i.st_mtim.tv_nsec),
      std::uint64_t(i.st_ctim.tv_sec), std::uint64_t(i.st_ctim.tv_nsec)};
#endif
  return s;
}
#endif
} // namespace
void validate_path_length(const std::filesystem::path &p) {
  require(p.has_filename(), "Choose an image filename");
#ifdef _WIN32
  cap(p.native().size(), 32766,
      "Image path exceeds the Windows native path limit");
  cap(p.filename().native().size(), 255,
      "Image filename exceeds the Windows component limit");
  for (auto c : p.filename().native())
    require(c >= 32 &&
                std::wstring(L"<>:\"/\\|?*").find(c) == std::wstring::npos,
            "Invalid Windows image filename");
#else
  auto parent = p.parent_path();
  if (parent.empty())
    parent = ".";
  const auto names = ::pathconf(parent.c_str(), _PC_NAME_MAX),
             paths = ::pathconf(parent.c_str(), _PC_PATH_MAX);
  if (names > 0)
    cap(p.filename().native().size(), std::size_t(names),
        "Image filename exceeds filesystem limit");
  if (paths > 0)
    cap(p.native().size(), std::size_t(paths) - 1,
        "Image path exceeds filesystem limit");
#endif
}
std::filesystem::path native_path(const std::string &s, std::size_t limit) {
  require(!s.empty(), "Choose an image path");
  cap(s.size(), limit, "Image path metadata limit");
  utf8(s);
  auto p =
      std::filesystem::absolute(std::filesystem::u8path(s)).lexically_normal();
#ifdef _WIN32
  auto wide = p.native();
  if (wide.rfind(L"\\\\?\\", 0) != 0) {
    if (wide.rfind(L"\\\\", 0) == 0)
      wide = L"\\\\?\\UNC\\" + wide.substr(2);
    else
      wide = L"\\\\?\\" + wide;
    p = std::filesystem::path(wide);
  }
#endif
  validate_path_length(p);
  return p;
}
struct Source::State {
  std::filesystem::path path;
  Handle handle = invalid;
  Stamp initial;
  ~State() { close_handle(handle); }
};
Source::Source(const std::filesystem::path &p, std::size_t limit,
               const ExecutionControl &c)
    : state_(std::make_unique<State>()) {
  token(c);
  state_->path = p;
  state_->handle = open_read(p);
  if (state_->handle == invalid)
    throw std::runtime_error("Could not open image file for reading");
  state_->initial = stamp(state_->handle);
  if (state_->initial.size > SIZE_MAX)
    throw std::length_error("Image source size is not addressable");
  cap(std::size_t(state_->initial.size), limit, "Image input file-byte limit");
  cap(std::size_t(state_->initial.size),
      std::size_t(std::numeric_limits<std::int64_t>::max()),
      "Image file size is not seekable");
  token(c);
}
Source::~Source() = default;
std::size_t Source::size() const { return std::size_t(state_->initial.size); }
void Source::read(std::size_t offset, unsigned char *out, std::size_t count,
                  const ExecutionControl &c) {
  require(offset <= size() && count <= size() - offset,
          "TIFF range outside input file");
  while (count) {
    token(c);
    const auto n = std::min(count, std::size_t(65536));
#ifdef _WIN32
    LARGE_INTEGER pos{};
    pos.QuadPart = LONGLONG(offset);
    DWORD actual = 0;
    if (!SetFilePointerEx(state_->handle, pos, nullptr, FILE_BEGIN) ||
        !ReadFile(state_->handle, out, DWORD(n), &actual, nullptr) ||
        actual != n)
      throw std::runtime_error("Short image file read");
#else
    const auto actual =
        ::pread(state_->handle, out, n, static_cast<off_t>(offset));
    if (actual < 0 && errno == EINTR)
      continue;
    if (actual != static_cast<ssize_t>(n))
      throw std::runtime_error("Short image file read");
#endif
    out += n;
    offset += n;
    count -= n;
  }
  token(c);
}
void Source::unchanged() const {
  if (!(stamp(state_->handle) == state_->initial))
    throw std::runtime_error("Image source changed during IO");
  auto h = open_read(state_->path);
  if (h == invalid)
    throw std::runtime_error("Image source path changed during IO");
  try {
    const auto current = stamp(h);
    close_handle(h);
    h = invalid;
    if (!(current == state_->initial))
      throw std::runtime_error("Image source path changed during IO");
  } catch (...) {
    close_handle(h);
    throw;
  }
}
} // namespace imagejxx::io_detail
