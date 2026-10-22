#pragma once
#include <imagejxx/rgb_contrast_apply_job.hpp>
namespace imagejxx {
// Ordered promoted RGB snapshots followed by the current mapping. All commands
// must capture the same immutable owner/data/revisions/display/geometry. Each
// stage maps the preceding stage, with its own frozen compact/explicit target.
// Source access stays sparse; admission never reads pixels. 1..64 stages.
// The result records an existing portable NativeWorkflowProgram of complete
// historical RGB commands and explicit frozen targets; scalar/RGB wire profiles
// are unchanged. Publication is one guarded paired transaction, not one per
// component. An empty mask remains the native no-change/no-IO extension.
// Estimates conservatively sum per-stage retained planes/metadata/records and
// charge cumulative pixels. Source/provider caches and total RSS are external.
ContrastApplyJobEstimate estimate_rgb_contrast_sequence_job(
    const std::vector<ResolvedRgbContrastApplyCommand> &,
    const ContrastApplyCommandLimits & = {},const JobOptions & = {});
inline ContrastApplyJob submit_rgb_contrast_sequence(JobScheduler &scheduler,
    const std::vector<ResolvedRgbContrastApplyCommand> &commands,
    const ContrastApplyCommandLimits &limits = {},JobOptions options = {}) {
  return ContrastApplyJob::submit_rgb_sequence(scheduler,commands,limits,std::move(options));
}
} // namespace imagejxx
