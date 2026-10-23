#pragma once
#include <imagejxx/rgb_contrast_session_command.hpp>
#include <imagejxx/contrast_apply_job.hpp>
namespace imagejxx {
ContrastApplyJobEstimate estimate_rgb_contrast_session_job(const ResolvedRgbContrastSessionCommand &,
    const ContrastApplyCommandLimits & = {},const JobOptions & = {});
inline ContrastApplyJob submit_rgb_contrast_session(JobScheduler &scheduler,
    const ResolvedRgbContrastSessionCommand &command,const ContrastApplyCommandLimits &limits = {},JobOptions options = {}){
  return ContrastApplyJob::submit(scheduler,command,limits,std::move(options));
}
} // namespace imagejxx
