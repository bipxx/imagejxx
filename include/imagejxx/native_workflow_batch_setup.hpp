#pragma once
#include <imagejxx/native_workflow_batch.hpp>
namespace imagejxx {
struct NativeWorkflowBatchSetupLimits {
  std::size_t max_inputs = 64, max_steps_per_program = 64,
              max_total_steps = 4096;
  // Combined wire and retained intent admission, not a process allocation
  // quota.
  std::size_t max_record_bytes = 128ULL * 1024 * 1024;
};
namespace detail {
struct NativeWorkflowBatchSetupAccess;
}
class NativeWorkflowBatchSetup {
public:
  const NativeWorkflowProgram &common_program() const { return *common_; }
  const std::vector<NativeWorkflowBatchInput> &inputs() const {
    return *inputs_;
  }
  const NativeWorkflowBatchSelection &selection() const { return selection_; }
  const std::string &output_directory() const { return directory_; }
  const NativeWorkflowBatchSetupLimits &limits() const { return limits_; }
  bool histogram_statistics() const { return histogram_statistics_; }
  // Pure output intent; no file access or publication consent.
  NativeWorkflowBatchSetup with_histogram_statistics(bool,
      const ExecutionControl & = {}) const;
  MetadataObject record(const ExecutionControl & = {}) const;
  std::string json(const ExecutionControl & = {}) const;

private:
  friend struct detail::NativeWorkflowBatchSetupAccess;
  std::shared_ptr<const NativeWorkflowProgram> common_;
  std::shared_ptr<const std::vector<NativeWorkflowBatchInput>> inputs_;
  NativeWorkflowBatchSelection selection_;
  std::string directory_;
  NativeWorkflowBatchSetupLimits limits_;
  bool histogram_statistics_=false;
  NativeWorkflowBatchSetup() = default;
};
// Pure intent: paths are external choices, never opened or resolved here.
// Fingerprints and all publication/review consent are deliberately discarded.
NativeWorkflowBatchSetup make_native_workflow_batch_setup(
    const NativeWorkflowProgram &,
    const std::vector<NativeWorkflowBatchInput> &, const std::string &,
    const NativeWorkflowBatchSelection & = {},
    const NativeWorkflowBatchSetupLimits & = {}, const ExecutionControl & = {});
NativeWorkflowBatchSetup native_workflow_batch_setup_from_record(
    const MetadataObject &, const NativeWorkflowBatchSetupLimits & = {},
    const ExecutionControl & = {});
NativeWorkflowBatchSetup native_workflow_batch_setup_from_json(
    const std::string &, const NativeWorkflowBatchSetupLimits & = {},
    const ExecutionControl & = {});
// Parent must exist; no directory creation, input reads or workflow execution.
// Save stages an exclusive sibling and publishes once; replacement is explicit.
void save_native_workflow_batch_setup(const std::string &,
                                      const NativeWorkflowBatchSetup &,
                                      bool replace_existing = false,
                                      const ExecutionControl & = {});
NativeWorkflowBatchSetup
load_native_workflow_batch_setup(const std::string &,
                                 const NativeWorkflowBatchSetupLimits & = {},
                                 const ExecutionControl & = {});
} // namespace imagejxx
