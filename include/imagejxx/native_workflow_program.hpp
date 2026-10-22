#pragma once
#include <imagejxx/native_workflow.hpp>
namespace imagejxx {
struct NativeWorkflowProgramLimits {
  static constexpr std::size_t hard_max_record_bytes = 1024ULL * 1024 * 1024;
  std::size_t max_steps = 64;
  // Logical record and retained typed requests, plus UTF-8 JSON transport.
  // Decoder phase scratch has the separate declared per-step codec limits.
  // Conservative copy admission, not a whole-process allocation quota.
  std::size_t max_record_bytes = 128ULL * 1024 * 1024;
};
namespace detail {
struct NativeWorkflowProgramAccess;
}
class NativeWorkflowProgram {
public:
  const std::vector<NativeWorkflowStep> &steps() const { return *steps_; }
  const NativeWorkflowProgramLimits &limits() const { return limits_; }
  MetadataObject record() const;
  std::string json() const;

private:
  friend struct detail::NativeWorkflowProgramAccess;
  std::shared_ptr<const std::vector<NativeWorkflowStep>> steps_;
  NativeWorkflowProgramLimits limits_;
  NativeWorkflowProgram(std::vector<NativeWorkflowStep> steps,
                        NativeWorkflowProgramLimits limits)
      : steps_(std::make_shared<const std::vector<NativeWorkflowStep>>(
            std::move(steps))),
        limits_(limits) {}
};
// Closed V1/V2/V3/V4 portable intent; V4 adds histogram analysis. Original captured records remain historical
// evidence; typed choices/targets contain no destination document identity.
// No pixel reads, execution, file paths, write consent or filesystem IO.
NativeWorkflowProgram
make_native_workflow_program(const std::vector<NativeWorkflowStep> &,
                             const NativeWorkflowProgramLimits & = {});
NativeWorkflowProgram
native_workflow_program_from_record(const MetadataObject &,
                                    const NativeWorkflowProgramLimits & = {});
NativeWorkflowProgram
native_workflow_program_from_json(const std::string &,
                                  const NativeWorkflowProgramLimits & = {});
} // namespace imagejxx
