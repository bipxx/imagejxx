#include "atomic_file.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <ios>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/magic.h>
#include <sys/vfs.h>
#endif
#endif

namespace imagejxx::detail {
namespace {
namespace fs = std::filesystem;
constexpr std::size_t writeBlock = 1024u * 1024u;
#ifndef _WIN32
struct Descriptor {
  int value = -1;
  Descriptor() = default;
  Descriptor(const Descriptor &) = delete;
  Descriptor &operator=(const Descriptor &) = delete;
  ~Descriptor() {
    if (value >= 0)
      ::close(value);
  }
};
#endif
fs::path absolute_destination(const fs::path &destination) {
  const auto &text = destination.native();
  if (text.empty() ||
      text.find(fs::path::value_type(0)) != fs::path::string_type::npos)
    throw std::invalid_argument(
        "Atomic destination must be nonempty without NUL");
  // Capture the process working directory before a user checkpoint can change
  // it. All staging, cleanup and publication use the same absolute destination.
  return fs::absolute(destination);
}
fs::path sibling_name(const fs::path &destination) {
  static std::atomic<std::uint64_t> sequence{0};
  auto result = destination;
  result += ".tmp." +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) +
            "." + std::to_string(sequence.fetch_add(1));
  return result;
}

class TemporaryFile {
public:
  TemporaryFile(const fs::path &destination, const char *noun,
                const ExecutionControl &control,
                const fs::path *planned = nullptr)
      : noun_(noun) {
#ifndef _WIN32
    // Anchor every relative operation to the original destination directory.
    // A later rename or reuse of its pathname cannot redirect cleanup.
#ifdef __linux__
    constexpr int directoryAccess = O_PATH;
#else
    constexpr int directoryAccess = O_RDONLY;
#endif
    parent_.value = ::open(destination.parent_path().c_str(),
                           directoryAccess | O_DIRECTORY | O_CLOEXEC);
    if (parent_.value < 0)
      throw fs::filesystem_error(
          message("open", " destination directory"), destination.parent_path(),
          std::error_code(errno, std::generic_category()));
#endif
    for (unsigned int attempt = 0; attempt < (planned ? 1u : 128u); ++attempt) {
      file_checkpoint(control);
      path_ = planned ? *planned : sibling_name(destination);
#ifdef _WIN32
      handle_ = CreateFileW(path_.c_str(), GENERIC_WRITE | DELETE,
                            FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (handle_ != INVALID_HANDLE_VALUE) {
        identity_valid_ =
            GetFileInformationByHandleEx(handle_, FileIdInfo, &identity_,
                                         sizeof(identity_)) != FALSE;
        owned_ = true;
        return;
      }
      const auto error = GetLastError();
      if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
        throw fs::filesystem_error(
            message("create", " temporary"), path_,
            std::error_code(error, std::system_category()));
#else
      name_ = path_.filename().native();
      descriptor_ = ::openat(parent_.value, name_.c_str(),
                             O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
      if (descriptor_ >= 0) {
        owned_ = true;
        return;
      }
      if (errno != EEXIST)
        throw fs::filesystem_error(
            message("create", " temporary"), path_,
            std::error_code(errno, std::generic_category()));
#endif
    }
    throw fs::filesystem_error(message("reserve", " temporary"), path_,
                               std::make_error_code(std::errc::file_exists));
  }
  TemporaryFile(const TemporaryFile &) = delete;
  TemporaryFile &operator=(const TemporaryFile &) = delete;
  ~TemporaryFile() {
#ifdef _WIN32
    // A failed close can leave the writer open, or release its slot for reuse.
    // Retry only when the kernel still identifies the original file object.
    if (sameFile(failed_close_))
      CloseHandle(failed_close_);
    if (handle_ != INVALID_HANDLE_VALUE) {
      // Delete the created file object, even if its name was moved or reused.
      // A failed handle operation must never fall back to deleting by pathname.
      if (owned_) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        SetFileInformationByHandle(handle_, FileDispositionInfo, &disposition,
                                   sizeof(disposition));
      }
      CloseHandle(handle_);
    }
#else
    // Keep the inode pinned until after the ownership check/unlink. Closing
    // first would allow inode reuse to make an unrelated entry look identical.
    if (owned_ && sameTemporary())
      ::unlinkat(parent_.value, name_.c_str(), 0);
    if (descriptor_ >= 0)
      ::close(descriptor_);
#endif
  }
  void writeAndClose(const unsigned char *bytes, std::size_t size,
                     const ExecutionControl &control) {
    std::size_t offset = 0;
    while (offset < size) {
      file_checkpoint(control);
#ifdef _WIN32
      const auto count =
          static_cast<DWORD>(std::min(writeBlock, size - offset));
      DWORD written = 0;
      if (!WriteFile(handle_, bytes + offset, count, &written, nullptr))
        throw fs::filesystem_error(
            message("write", " temporary"), path_,
            std::error_code(GetLastError(), std::system_category()));
      if (!written)
        throw std::ios_base::failure("Zero-length " + noun_ + " write");
#else
      const auto count = std::min(
          {writeBlock, size - offset,
           static_cast<std::size_t>(std::numeric_limits<ssize_t>::max())});
      const auto written = ::write(descriptor_, bytes + offset, count);
      if (written < 0) {
        if (errno == EINTR)
          continue;
        throw fs::filesystem_error(
            message("write", " temporary"), path_,
            std::error_code(errno, std::generic_category()));
      }
      if (!written)
        throw std::ios_base::failure("Zero-length " + noun_ + " write");
#endif
      offset += static_cast<std::size_t>(written);
      file_stage_progress(control, "Atomic file write", offset, size);
    }
    close(control);
  }
  void append(const unsigned char *bytes, std::size_t count,
              const ExecutionControl &control) {
    if (!bytes && count)
      throw std::invalid_argument("Null streaming file bytes");
    while (count) {
      file_checkpoint(control);
      const auto n = std::min(count, std::size_t(65536));
#ifdef _WIN32
      DWORD written = 0;
      if (handle_ == INVALID_HANDLE_VALUE ||
          !WriteFile(handle_, bytes, DWORD(n), &written, nullptr) || !written)
        throw std::runtime_error("Streaming file write failed");
#else
      const auto written = ::write(descriptor_, bytes, n);
      if (written < 0 && errno == EINTR)
        continue;
      if (written <= 0)
        throw std::runtime_error("Streaming file write failed");
#endif
      bytes += written;
      count -= std::size_t(written);
    }
  }
  void patch(std::size_t offset, const unsigned char *bytes, std::size_t count,
             const ExecutionControl &control) {
#ifdef _WIN32
    LARGE_INTEGER saved{}, target{};
    if (!SetFilePointerEx(handle_, target, &saved, FILE_CURRENT))
      throw std::runtime_error("Streaming file position query failed");
    target.QuadPart = static_cast<LONGLONG>(offset);
    file_checkpoint(control);
    if (!SetFilePointerEx(handle_, target, nullptr, FILE_BEGIN))
      throw std::runtime_error("Streaming file patch seek failed");
    append(bytes, count, control);
    if (!SetFilePointerEx(handle_, saved, nullptr, FILE_BEGIN))
      throw std::runtime_error("Streaming file position restore failed");
#else
    while (count) {
      file_checkpoint(control);
      const auto n = std::min(count, std::size_t(65536));
      const auto written = ::pwrite(descriptor_, bytes, n, off_t(offset));
      if (written < 0 && errno == EINTR)
        continue;
      if (written <= 0)
        throw std::runtime_error("Streaming file patch write failed");
      offset += std::size_t(written);
      bytes += written;
      count -= std::size_t(written);
    }
#endif
  }
  void close(const ExecutionControl &control) {
    file_checkpoint(control);
#ifdef _WIN32
    if (!FlushFileBuffers(handle_))
      throw fs::filesystem_error(
          message("flush", " temporary"), path_,
          std::error_code(GetLastError(), std::system_category()));
    // Reopen the same object without write access, so the last writer closes
    // and final write timestamps settle before readers take change tokens.
    // Retain DELETE access for identity-bound publication and abandonment.
    const auto retained =
        ReOpenFile(handle_, DELETE,
                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
    if (retained == INVALID_HANDLE_VALUE)
      throw fs::filesystem_error(
          message("retain", " temporary"), path_,
          std::error_code(GetLastError(), std::system_category()));
    const auto handle = handle_;
    handle_ = retained;
    if (!CloseHandle(handle)) {
      const auto error = GetLastError();
      if (sameFile(handle))
        failed_close_ = handle;
      throw fs::filesystem_error(
          message("close", " temporary"), path_,
          std::error_code(error, std::system_category()));
    }
#else
    if (::fsync(descriptor_) != 0)
      throw fs::filesystem_error(
          message("flush", " temporary"), path_,
          std::error_code(errno, std::generic_category()));
    // Pin the original object after the writer's close, including when close
    // reports an error after releasing/recycling the original descriptor.
    retained_.value = ::fcntl(descriptor_, F_DUPFD_CLOEXEC, 0);
    if (retained_.value < 0)
      throw fs::filesystem_error(
          message("retain", " temporary"), path_,
          std::error_code(errno, std::generic_category()));
    const auto descriptor = descriptor_;
    descriptor_ = -1;
    if (::close(descriptor) != 0)
      throw fs::filesystem_error(
          message("close", " temporary"), path_,
          std::error_code(errno, std::generic_category()));
#endif
  }
  void publish(const fs::path &destination, bool replace = true) {
#ifdef _WIN32
    const auto name = fs::absolute(destination).native();
    if (name.size() >
        (std::numeric_limits<DWORD>::max() - sizeof(FILE_RENAME_INFO)) /
            sizeof(wchar_t))
      throw std::length_error("Atomic destination name is too long");
    const auto bytes = sizeof(FILE_RENAME_INFO) + name.size() * sizeof(wchar_t);
    std::vector<std::max_align_t> storage(
        (bytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
    auto *rename = reinterpret_cast<FILE_RENAME_INFO *>(storage.data());
    rename->ReplaceIfExists = replace ? TRUE : FALSE;
    rename->RootDirectory = nullptr;
    rename->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
    std::copy(name.begin(), name.end(), rename->FileName);
    if (!SetFileInformationByHandle(handle_, FileRenameInfo, rename,
                                    static_cast<DWORD>(bytes)))
      throw fs::filesystem_error(
          message("replace"), path_, destination,
          std::error_code(GetLastError(), std::system_category()));
#else
    if (!sameTemporary() || !sameParent(destination.parent_path()))
      throw fs::filesystem_error(
          message("publish", " changed temporary or parent"), path_,
          destination, std::make_error_code(std::errc::state_not_recoverable));
    const auto targetName = destination.filename().native();
    if (replace) {
      if (::renameat(parent_.value, name_.c_str(), parent_.value,
                     targetName.c_str()) != 0)
        throw fs::filesystem_error(
            message("replace"), path_, destination,
            std::error_code(errno, std::generic_category()));
    } else {
#ifdef __linux__
      struct statfs filesystem{};
      if (::fstatfs(retained_.value, &filesystem) != 0)
        throw fs::filesystem_error(
            message("inspect", " publication filesystem"), path_,
            std::error_code(errno, std::generic_category()));
      // WSL's 9P bridge can resolve both descriptor-link routes through a
      // stale pathname and publish a substituted file. Refuse that observed
      // unsafe provider before any destination link can be created.
      if (filesystem.f_type == V9FS_MAGIC)
        throw fs::filesystem_error(
            message("publish", " no-replace on unsupported 9P filesystem"),
            path_, destination,
            std::make_error_code(std::errc::operation_not_supported));
      // Bind the source to the held inode, not to a name another actor can
      // replace after sameTemporary(). Unprivileged processes can use procfs
      // when AT_EMPTY_PATH requires CAP_DAC_READ_SEARCH. Neither route replaces
      // an existing destination; never fall back to the mutable stage name.
      auto linked = ::linkat(retained_.value, "", parent_.value,
                             targetName.c_str(), AT_EMPTY_PATH);
      if (linked != 0 &&
          (errno == ENOENT || errno == EPERM || errno == EINVAL)) {
        const auto source =
            std::string("/proc/self/fd/") + std::to_string(retained_.value);
        linked = ::linkat(AT_FDCWD, source.c_str(), parent_.value,
                          targetName.c_str(), AT_SYMLINK_FOLLOW);
      }
      if (linked != 0)
#else
      if (::linkat(parent_.value, name_.c_str(), parent_.value,
                   targetName.c_str(), 0) != 0)
#endif
        throw fs::filesystem_error(
            message("publish"), path_, destination,
            std::error_code(errno, std::generic_category()));
      // The destination is now published. Retain owned_ until destruction so
      // cleanup of the sibling cannot turn a successful publication into error.
      return;
    }
#endif
    owned_ = false;
#ifdef _WIN32
    // Release DELETE access immediately, so ordinary readers need not request
    // delete sharing after publication. No throwing work follows the rename.
    if (CloseHandle(handle_) || !sameFile(handle_))
      handle_ = INVALID_HANDLE_VALUE;
    // If closure was refused while the original object remains open, retain
    // it for destruction. owned_ is false: the committed file cannot be
    // deleted.
#endif
  }

private:
  std::string message(const char *verb, const char *suffix = "") const {
    return std::string("Could not ") + verb + " " + noun_ + suffix;
  }
  fs::path path_;
  std::string noun_;
  bool owned_ = false;
#ifdef _WIN32
  bool sameFile(HANDLE handle) const noexcept {
    if (handle == INVALID_HANDLE_VALUE || !identity_valid_)
      return false;
    FILE_ID_INFO observed{};
    if (!GetFileInformationByHandleEx(handle, FileIdInfo, &observed,
                                      sizeof(observed)) ||
        observed.VolumeSerialNumber != identity_.VolumeSerialNumber)
      return false;
    bool nonzero = false;
    for (std::size_t i = 0; i < sizeof(identity_.FileId.Identifier); ++i) {
      if (observed.FileId.Identifier[i] != identity_.FileId.Identifier[i])
        return false;
      nonzero |= identity_.FileId.Identifier[i] != 0;
    }
    return nonzero;
  }
  FILE_ID_INFO identity_{};
  bool identity_valid_ = false;
  HANDLE failed_close_ = INVALID_HANDLE_VALUE;
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
  bool sameTemporary() const noexcept {
    struct stat held{}, named{};
    const auto pinned = descriptor_ >= 0 ? descriptor_ : retained_.value;
    return pinned >= 0 && ::fstat(pinned, &held) == 0 &&
           ::fstatat(parent_.value, name_.c_str(), &named,
                     AT_SYMLINK_NOFOLLOW) == 0 &&
           S_ISREG(named.st_mode) && held.st_dev == named.st_dev &&
           held.st_ino == named.st_ino;
  }
  bool sameParent(const fs::path &parent) const noexcept {
    struct stat held{}, named{};
    return ::fstat(parent_.value, &held) == 0 &&
           ::stat(parent.c_str(), &named) == 0 && held.st_dev == named.st_dev &&
           held.st_ino == named.st_ino;
  }
  Descriptor parent_, retained_;
  std::string name_;
  int descriptor_ = -1;
#endif
};
} // namespace
struct StagedAtomicStream::State {
  fs::path destination, planned;
  std::unique_ptr<TemporaryFile> temporary;
  bool closed = false, published = false, failed = false;
  std::size_t bytes = 0;
  explicit State(const fs::path &p)
      : destination(absolute_destination(p)),
        planned(sibling_name(destination)) {}
};
StagedAtomicStream::StagedAtomicStream(const fs::path &p)
    : state_(std::make_unique<State>(p)) {}
StagedAtomicStream::~StagedAtomicStream() = default;
const fs::path &StagedAtomicStream::temporary_path() const {
  return state_->planned;
}
void StagedAtomicStream::open(const ExecutionControl &c) {
  if (state_->temporary)
    throw std::logic_error("Streaming file already opened");
  state_->temporary = std::make_unique<TemporaryFile>(
      state_->destination, "scientific image export", c, &state_->planned);
}
void StagedAtomicStream::append(const unsigned char *p, std::size_t n,
                                const ExecutionControl &c) {
  if (!state_->temporary || state_->closed || state_->failed)
    throw std::logic_error("Streaming file is not open");
  if ((!p && n) || n > std::size_t(INT64_MAX) - state_->bytes)
    throw std::invalid_argument("Invalid streaming file append extent");
  try {
    state_->temporary->append(p, n, c);
    state_->bytes += n;
  } catch (...) {
    state_->failed = true;
    throw;
  }
}
void StagedAtomicStream::patch(std::size_t offset, const unsigned char *p,
                               std::size_t n, const ExecutionControl &c) {
  if (!state_->temporary || state_->closed || state_->failed)
    throw std::logic_error("Streaming file is not open");
  if ((!p && n) || offset > state_->bytes || n > state_->bytes - offset)
    throw std::invalid_argument("Patch outside appended streaming file extent");
  if (!n)
    return;
#ifndef _WIN32
  if (offset > std::size_t(std::numeric_limits<off_t>::max()) ||
      n > std::size_t(std::numeric_limits<off_t>::max()) - offset)
    throw std::length_error("Streaming file patch offset is not addressable");
#endif
  try {
    state_->temporary->patch(offset, p, n, c);
  } catch (...) {
    state_->failed = true;
    throw;
  }
}
void StagedAtomicStream::close(const ExecutionControl &c) {
  if (!state_->temporary || state_->closed || state_->failed)
    throw std::logic_error("Streaming file is not open");
  try {
    state_->temporary->close(c);
    state_->closed = true;
  } catch (...) {
    state_->failed = true;
    throw;
  }
}
void StagedAtomicStream::publish(bool replace) {
  if (!state_->closed || state_->published || state_->failed)
    throw std::logic_error("Streaming publication is not ready");
  state_->temporary->publish(state_->destination, replace);
  state_->published = true;
}

struct StagedAtomicFile::State {
  fs::path destination;
  TemporaryFile temporary;
  bool published = false;
  State(const fs::path &path, const ExecutionControl &control)
      : destination(absolute_destination(path)),
        temporary(destination, "ROI export", control) {}
};
StagedAtomicFile::StagedAtomicFile(std::unique_ptr<State> state)
    : state_(std::move(state)) {}
StagedAtomicFile::~StagedAtomicFile() = default;
void StagedAtomicFile::publish(bool replace) {
  if (state_->published)
    throw std::logic_error("ROI file was already published");
  state_->temporary.publish(state_->destination, replace);
  state_->published = true;
}
std::unique_ptr<StagedAtomicFile>
stage_atomic_file(const fs::path &destination, const unsigned char *bytes,
                  std::size_t count, const ExecutionControl &control) {
  if (!bytes && count)
    throw std::invalid_argument("Invalid ROI export input");
  if (control.cancellation && control.cancellation->canceled())
    throw OperationCancelled();
  const auto target = absolute_destination(destination);
  file_stage_progress(control, "Stage ROI export", 0, count);
  auto state = std::make_unique<StagedAtomicFile::State>(target, control);
  state->temporary.writeAndClose(bytes, count, control);
  file_stage_progress(control, "ROI export awaiting publication", 1, 1);
  return std::unique_ptr<StagedAtomicFile>(
      new StagedAtomicFile(std::move(state)));
}

void file_checkpoint(const ExecutionControl &control) {
  if (control.cancellation && control.cancellation->canceled())
    throw OperationCancelled();
  if (control.checkpoint)
    control.checkpoint();
  if (control.cancellation && control.cancellation->canceled())
    throw OperationCancelled();
}

void file_stage_progress(const ExecutionControl &control, std::string_view name,
                         std::size_t completed, std::size_t total) {
  file_checkpoint(control);
  if (control.stage_progress)
    control.stage_progress(name, completed, total);
  file_checkpoint(control);
}

void atomic_replace_file(const fs::path &destination,
                         const unsigned char *bytes, std::size_t count,
                         const char *diagnostic_noun,
                         const ExecutionControl &control) {
  if ((!bytes && count) || !diagnostic_noun || !*diagnostic_noun)
    throw std::invalid_argument("Invalid atomic file input");
  if (control.cancellation && control.cancellation->canceled())
    throw OperationCancelled();
  const auto target = absolute_destination(destination);
  file_stage_progress(control, "Atomic file write", 0, count);
  TemporaryFile temporary(target, diagnostic_noun, control);
  temporary.writeAndClose(bytes, count, control);
  file_stage_progress(control, "Atomic file replace", 0, 1);
  // There are no checkpoints/callbacks after the atomic publication boundary.
  temporary.publish(target);
}

} // namespace imagejxx::detail
