#pragma once
#include <imagejxx/rgb_contrast_stack_command.hpp>
#include <imagejxx/contrast_apply_job.hpp>
namespace imagejxx {
ContrastApplyJobEstimate estimate_rgb_contrast_stack_job(const ResolvedRgbContrastStackCommand &,
    const ContrastApplyCommandLimits & = {},const JobOptions & = {});
inline ContrastApplyJob submit_rgb_contrast_stack(JobScheduler &scheduler,
    const ResolvedRgbContrastStackCommand &command,const ContrastApplyCommandLimits &limits = {},JobOptions options = {}) {
  return ContrastApplyJob::submit(scheduler,command,limits,std::move(options));
}
} // namespace imagejxx
