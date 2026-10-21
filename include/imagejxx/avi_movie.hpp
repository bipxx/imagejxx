#pragma once
#include <imagejxx/execution.hpp>
#include <functional>
#include <cstdint>
#include <memory>

namespace imagejxx {
// Classic RIFF AVI, one uncompressed video stream. Gray8 has a literal gray
// palette; RGB24 uses bottom-up BGR rows. No codecs, audio or metadata embedding.
struct AviMovieSpec {
  int width = 1, height = 1, frames = 1, channels = 3;
  double fps = 8;
  std::size_t max_file_bytes = 1024ULL * 1024 * 1024;
  std::size_t max_working_bytes = 512ULL * 1024 * 1024;
};
struct AviMovieEstimate {
  std::size_t file_bytes, working_bytes, row_bytes, frame_bytes;
  std::uint32_t rate, scale;
};
// Metadata-only. Working bytes include one returned double-backed image, one
// padded wire row and header/index scratch. Provider caches/scratch are external.
AviMovieEstimate estimate_avi_movie(const AviMovieSpec &);
using AviFrameProvider = std::function<Image(std::size_t)>;
class StagedAviMovie {
public:
  ~StagedAviMovie();
  StagedAviMovie(const StagedAviMovie &) = delete;
  StagedAviMovie &operator=(const StagedAviMovie &) = delete;
  void publish(bool replace_existing = false);
private:
  struct State;
  std::unique_ptr<State> state_;
  explicit StagedAviMovie(std::unique_ptr<State>);
  friend std::unique_ptr<StagedAviMovie> stage_avi_movie(
      const std::string &, const AviMovieSpec &, const AviFrameProvider &,
      const ExecutionControl &);
};
// Provider runs exactly once per frame, in order. Every image must have the
// declared dimensions/channels, UInt8 type and exact integer samples 0..255.
// All writing is to an exclusive sibling; failures/cancellation remove it.
// Publication is separate so a UI consumer can revalidate its frozen source.
std::unique_ptr<StagedAviMovie> stage_avi_movie(
    const std::string &, const AviMovieSpec &, const AviFrameProvider &,
    const ExecutionControl & = {});
void write_avi_movie(const std::string &, const AviMovieSpec &,
                     const AviFrameProvider &, bool replace_existing = false,
                     const ExecutionControl & = {});
// Eager or lazy immutable Z stack adapter. T/C must be singleton, S is 1 or 3,
// and the stored type is UInt8. Source storage is never modified.
void write_avi_dataset(const std::string &, const Dataset &, double fps = 8,
                       bool replace_existing = false,
                       std::size_t max_file_bytes = 1024ULL*1024*1024,
                       std::size_t max_working_bytes = 512ULL*1024*1024,
                       const ExecutionControl & = {});
} // namespace imagejxx
