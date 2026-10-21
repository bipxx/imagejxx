#pragma once
#include <imagejxx/io.hpp>
#include <memory>
namespace imagejxx::io_detail {
// Private frontend primitive. Preparation never publishes a destination; a
// caller-thread publication may follow after document/dialog identity guards.
// Destruction abandons only the owned sibling. Every publish attempt consumes
// the preparation, including callback/cancellation/filesystem failures.
class StagedImageWrite {
public:
  ~StagedImageWrite();
  StagedImageWrite(const StagedImageWrite &) = delete;
  StagedImageWrite &operator=(const StagedImageWrite &) = delete;
  bool active() const noexcept;
  void publish(const ExecutionControl & = {});

private:
  struct State;
  explicit StagedImageWrite(std::unique_ptr<State>);
  std::unique_ptr<State> state_;
  friend std::unique_ptr<StagedImageWrite>
  stage_image_series(const std::string &, const Dataset &,
                     const MetadataObject &, const ImageWriteRequest &,
                     const ImageFileLimits &, const ExecutionControl &);
};
std::unique_ptr<StagedImageWrite>
stage_image_series(const std::string &, const Dataset &,
                   const MetadataObject & = {}, const ImageWriteRequest & = {},
                   const ImageFileLimits & = {}, const ExecutionControl & = {});
} // namespace imagejxx::io_detail
