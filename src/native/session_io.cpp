#include "atomic_file.hpp"
#include "session_io_internal.hpp"
#include <imagejxx/session.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace imagejxx {
namespace {
namespace fs = std::filesystem;
constexpr std::size_t readBlock = 1024u * 1024u;

fs::path sessionPath(const std::string &utf8) {
  if (utf8.empty() || utf8.find('\0') != std::string::npos)
    throw std::invalid_argument("Session path must be nonempty UTF-8 text");
  try {
    // Keep the public path contract consistent even on byte-oriented systems.
    (void)nlohmann::json(utf8).dump();
  } catch (const nlohmann::json::exception &error) {
    throw std::invalid_argument(error.what());
  }
  return fs::absolute(fs::u8path(utf8));
}

std::size_t admittedFileSize(std::uintmax_t size, const SessionLimits &limits) {
  if (size > limits.max_encoded_bytes || size > limits.max_working_bytes ||
      size > std::numeric_limits<std::size_t>::max())
    throw std::invalid_argument(
        "Session file exceeds the encoded or working byte limit");
  return static_cast<std::size_t>(size);
}

} // namespace

void save_session(const std::string &path, const SessionCapture &capture,
                  const SessionLimits &limits,
                  const ExecutionControl &control) {
  if (control.cancellation && control.cancellation->canceled())
    throw OperationCancelled();
  // Freeze the relative destination before encoding can dispatch user hooks.
  // Encoding validates the capture before any filesystem mutation or staging.
  const auto destination = sessionPath(path);
  const auto bytes = encode_session(capture, limits, control);
  detail::file_checkpoint(control);
  detail::atomic_replace_file(destination, bytes.data(), bytes.size(),
                              "session", control);
}

std::vector<unsigned char>
detail::read_session_bytes(const std::string &path, const SessionLimits &limits,
                           const ExecutionControl &control) {
  if (control.cancellation && control.cancellation->canceled())
    throw OperationCancelled();
  const auto source = sessionPath(path);
  detail::file_checkpoint(control);
  const auto announcedSize = fs::file_size(source);
  admittedFileSize(announcedSize, limits);
  std::ifstream stream(source, std::ios::binary | std::ios::ate);
  if (!stream.is_open())
    throw fs::filesystem_error(
        "Could not open session", source,
        errno ? std::error_code(errno, std::generic_category())
              : std::make_error_code(std::errc::io_error));
  stream.exceptions(std::ios::badbit);
  const auto end = stream.tellg();
  if (end == std::ifstream::pos_type(-1) ||
      static_cast<std::streamoff>(end) < 0)
    throw std::ios_base::failure("Could not inspect session input size");
  const auto openedSize =
      static_cast<std::uintmax_t>(static_cast<std::streamoff>(end));
  const auto size = admittedFileSize(openedSize, limits);
  if (openedSize != announcedSize)
    throw std::ios_base::failure("Session file changed before reading");
  stream.seekg(0, std::ios::beg);
  if (stream.fail())
    throw std::ios_base::failure("Could not seek session input");

  detail::file_stage_progress(control, "Session file read", 0, size);
  std::vector<unsigned char> bytes;
  if (size > bytes.max_size())
    throw std::invalid_argument(
        "Session file exceeds the addressable input size");
  bytes.resize(size);
  std::size_t offset = 0;
  while (offset < size) {
    detail::file_checkpoint(control);
    const auto count = std::min(readBlock, size - offset);
    stream.read(reinterpret_cast<char *>(bytes.data() + offset),
                static_cast<std::streamsize>(count));
    if (stream.gcount() != static_cast<std::streamsize>(count) || stream.fail())
      throw std::ios_base::failure(
          "Session input was truncated or could not be read");
    offset += count;
    detail::file_stage_progress(control, "Session file read", offset, size);
  }
  detail::file_checkpoint(control);
  if (stream.peek() != std::ifstream::traits_type::eof())
    throw std::ios_base::failure("Session file grew while reading");
  stream.clear(); // Clear the expected EOF before testing close itself.
  stream.close();
  if (stream.fail())
    throw std::ios_base::failure("Could not close session input");
  detail::file_checkpoint(control);
  return bytes;
}

DecodedSession load_session(const std::string &path,
                            const SessionLimits &limits,
                            const ExecutionControl &control) {
  return decode_session(detail::read_session_bytes(path, limits, control),
                        limits, control);
}

} // namespace imagejxx
