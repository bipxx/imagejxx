#pragma once

#include <imagejxx/operations.hpp>
#include <imagejxx/rank.hpp>
#include <imagejxx/targets.hpp>

namespace imagejxx {

struct RankFilterCommandOptions {
  RankFilterMethod method = RankFilterMethod::Median;
  double radius = 2.0;
};

struct RemoveNaNsCommandOptions {
  double radius = 2.0;
};
void validate_remove_nans_command_options(const RemoveNaNsCommandOptions &);

struct TopHatCommandOptions {
  double radius = 2.0;
  bool light_background = false;
  bool dont_subtract = false;
};
void validate_top_hat_command_options(const TopHatCommandOptions &);

struct RemoveOutliersCommandOptions {
  double radius = 2.0;
  double threshold = 50.0;
  std::string which = "bright";
};
void validate_remove_outliers_command_options(const RemoveOutliersCommandOptions &);
// Metadata-only active processor LUT policy. Threshold presentation is ignored.
bool remove_outliers_lut_inverted(const DocumentSnapshot &);

void validate_rank_command_options(const RankFilterCommandOptions &);

// Immutable ImageJ 1.54r pixel-radius intent and a source-bound plane target.
// Resolution, record validation and rebinding inspect metadata only; source
// samples are validated by the finite scalar kernel when execution reads them.
class ResolvedRankFilterCommand {
public:
  const RankFilterCommandOptions &options() const { return options_; }
  const ResolvedTarget &target() const { return target_; }
  // Outliers share this immutable rank-family envelope and target/job lanes.
  // options() describes the underlying median footprint; the optional intent
  // and captured LUT flag distinguish the separate Remove Outliers operation.
  const std::optional<RemoveOutliersCommandOptions> &outlier_options() const { return outliers_; }
  const std::optional<TopHatCommandOptions> &top_hat_options() const { return top_hat_; }
  const std::optional<RemoveNaNsCommandOptions> &remove_nans_options() const { return remove_nans_; }
  bool inverted_lut() const { return inverted_lut_; }
  const char *operation() const;
  OperationParameters parameters() const;
  MetadataObject record() const;

private:
  friend ResolvedRankFilterCommand
  resolve_rank_command(const DocumentSnapshot &, const TargetRequest &,
                       RankFilterCommandOptions);
  friend ResolvedRankFilterCommand resolve_remove_nans_command(
      const DocumentSnapshot &, const TargetRequest &, RemoveNaNsCommandOptions);
  friend ResolvedRankFilterCommand resolve_top_hat_command(
      const DocumentSnapshot &, const TargetRequest &, TopHatCommandOptions);
  friend ResolvedRankFilterCommand resolve_remove_outliers_command(
      const DocumentSnapshot &, const TargetRequest &, RemoveOutliersCommandOptions);
  friend ResolvedRankFilterCommand rebind_rank_command(
      const DocumentSnapshot &, const MetadataObject &, const TargetRequest &);
  friend ResolvedRankFilterCommand
  rank_command_from_record(const DocumentSnapshot &, const MetadataObject &);
  ResolvedRankFilterCommand(RankFilterCommandOptions options,
                            ResolvedTarget target);
  RankFilterCommandOptions options_;
  ResolvedTarget target_;
  std::optional<RemoveOutliersCommandOptions> outliers_;
  std::optional<TopHatCommandOptions> top_hat_;
  std::optional<RemoveNaNsCommandOptions> remove_nans_;
  bool inverted_lut_ = false;
};

// Supports finite-only S1 UInt8, UInt16 and Float32 datasets. Radius is in
// [0,1000], or [0,100] for Median; radius zero retains the five-point cross.
ResolvedRankFilterCommand
resolve_rank_command(const DocumentSnapshot &source,
                     const TargetRequest &request = {},
                     RankFilterCommandOptions options = {});
// Float32 S1 with finite-only, opted quiet-NaN or IEEE storage. No LUT polarity or
// display stretch is involved; radius is finite in [0,100].
ResolvedRankFilterCommand resolve_remove_nans_command(
    const DocumentSnapshot &, const TargetRequest &request = {},
    RemoveNaNsCommandOptions options = {});

// Captures active processor polarity once for every selected plane. The strict
// record freezes this numeric direction across replay and explicit rebinding.
ResolvedRankFilterCommand resolve_remove_outliers_command(
    const DocumentSnapshot &, const TargetRequest &request = {},
    RemoveOutliersCommandOptions options = {});
// Metadata-only source-bound Top Hat intent. Captures one active base-LUT
// polarity for every selected plane. Replay/rebind preserve that frozen flag.
// options() describes the first min/max footprint; top_hat_options() carries
// the compound intent. Final display/preview execution is a separate backend.
ResolvedRankFilterCommand resolve_top_hat_command(
    const DocumentSnapshot &, const TargetRequest &request = {},
    TopHatCommandOptions options = {});
// Fixed Despeckle preset. Uses the existing strict median/radius1 record;
// replay, explicit rebinding, workflows and resource admission stay shared.
ResolvedRankFilterCommand
resolve_despeckle_command(const DocumentSnapshot &source,
                         const TargetRequest &request = {});
ResolvedRankFilterCommand
rank_command_from_record(const DocumentSnapshot &source,
                         const MetadataObject &record);

// Validate every original schema1/profile/options/target claim, including the
// original target's S1 shape and plane execution mode. This does not establish
// that the original document exists or authenticate its identity/revisions.
void validate_rank_command_record(const MetadataObject &record);

// Validate the complete original record first, then resolve its method/radius
// on the destination using the required new request. Recorded identities and
// frozen target fields are never rewritten or borrowed from the destination.
ResolvedRankFilterCommand
rebind_rank_command(const DocumentSnapshot &destination,
                    const MetadataObject &record, const TargetRequest &request);

struct RankFilterCommandRegion {
  std::optional<RankFilterExtent> extent;
  std::optional<Image> mask;
};

// Geometry determines the write extent; mask occupancy determines writes.
// Reuses the qualified area/non-area and positioned-union planner without
// source pixel reads. An unmatched positioned ROI is an explicit empty region.
RankFilterCommandRegion rank_command_region(const ResolvedTarget &target,
                                            std::size_t index);

// Explicit geometry admission for Remove NaNs; existing finite rank admission
// remains unchanged. No source samples are read by either geometry adapter.
RankFilterCommandRegion remove_nans_command_region(const ResolvedTarget &target,
                                                   std::size_t index);

} // namespace imagejxx
