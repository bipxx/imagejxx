#include "kernel_control.hpp"
#include "rank_display_internal.hpp"
#include "target_record_internal.hpp"
#include "legacy_rgb_palette.hpp"
#include <imagejxx/channel_display.hpp>
#include <limits>
#include <imagejxx/gaussian_command.hpp>
#include <imagejxx/rank_command.hpp>

#include <cmath>
#include <initializer_list>
#include <set>
#include <stdexcept>
#include <utility>

namespace imagejxx {
namespace {

constexpr const char *operation_id = "rank_filter_imagej";
constexpr const char *profile_id = "imagej_154r";

void validate_source_metadata(const DocumentSnapshot &source) {
  kernel_checkpoint();
  const auto &dataset = source.data().dataset;
  const auto shape = dataset.shape();
  const auto type = dataset.pixel_type();
  if (shape.s != 1 || (type != PixelType::UInt8 && type != PixelType::UInt16 &&
                       type != PixelType::Float32))
    throw std::invalid_argument(
        "ImageJ rank commands support UInt8, UInt16 and Float32 grayscale");
  if (dataset.sample_policy() != FloatingSamplePolicy::FiniteOnly)
    throw std::invalid_argument(
        "ImageJ rank commands require finite-only sources");
}

void validate_remove_nans_source(const DocumentSnapshot &source) {
  kernel_checkpoint();
  const auto &dataset = source.data().dataset;
  if (dataset.shape().s != 1 || dataset.pixel_type() != PixelType::Float32 ||
      (dataset.sample_policy() != FloatingSamplePolicy::FiniteOnly &&
       dataset.sample_policy() != FloatingSamplePolicy::PreserveQuietNaNFloat32 &&
       dataset.sample_policy() != FloatingSamplePolicy::PreserveIEEEFloat32))
    throw std::invalid_argument("Remove NaNs requires admitted Float32 grayscale source storage");
}

void validate_options(const RankFilterCommandOptions &options) {
  // The shared formatter rejects unknown enum values without allocating a
  // kernel footprint or examining source samples.
  (void)rank_filter_method_name(options.method);
  if (!std::isfinite(options.radius) || options.radius < 0 ||
      options.radius > 1000)
    throw std::invalid_argument(
        "ImageJ rank radius must be finite and in [0,1000]");
  if (options.method == RankFilterMethod::Median && options.radius > 100)
    throw std::invalid_argument("ImageJ median radius must be in [0,100]");
}

void exact_fields(const MetadataObject &object,
                  std::initializer_list<const char *> names) {
  std::set<std::string> expected;
  for (const char *name : names) {
    expected.emplace(name);
    if (!object.count(name))
      throw std::invalid_argument(std::string("Missing rank record field: ") +
                                  name);
  }
  for (const auto &[name, value] : object) {
    (void)value;
    if (!expected.count(name))
      throw std::invalid_argument("Unknown rank record field: " + name);
  }
}

const MetadataObject &object_value(const MetadataValue &value) {
  if (const auto node =
          std::get_if<std::shared_ptr<const MetadataTree>>(&value);
      node && *node)
    if (const auto object = std::get_if<MetadataObject>(&(*node)->value))
      return *object;
  throw std::invalid_argument("Rank record requires an object");
}

const std::string &text_value(const MetadataValue &value) {
  if (const auto text = std::get_if<std::string>(&value))
    return *text;
  throw std::invalid_argument("Rank record requires text");
}

double number_value(const MetadataValue &value) {
  double result;
  if (const auto real = std::get_if<double>(&value))
    result = *real;
  else if (const auto integer = std::get_if<std::int64_t>(&value)) {
    result = static_cast<double>(*integer);
    if (result >= std::ldexp(1.0, 63) || result < -std::ldexp(1.0, 63) ||
        static_cast<std::int64_t>(result) != *integer)
      throw std::invalid_argument(
          "Rank record integer loses numeric precision");
  } else if (const auto integer = std::get_if<std::uint64_t>(&value)) {
    result = static_cast<double>(*integer);
    if (result >= std::ldexp(1.0, 64) ||
        static_cast<std::uint64_t>(result) != *integer)
      throw std::invalid_argument(
          "Rank record integer loses numeric precision");
  } else
    throw std::invalid_argument("Rank record requires a number");
  if (!std::isfinite(result))
    throw std::invalid_argument("Rank record numbers must be finite");
  return result;
}

struct RankRecord {
  RankFilterCommandOptions options;
  detail::TargetRecord target;
  std::optional<RemoveOutliersCommandOptions> outliers;
  bool inverted = false;
  std::optional<TopHatCommandOptions> top_hat;
  std::optional<RemoveNaNsCommandOptions> remove_nans;
};

RankRecord decode_rank_record(const MetadataObject &record) {
  validate_metadata(record);
  exact_fields(record,
               {"schema_version", "operation", "profile", "options", "target"});
  const auto &version = record.at("schema_version");
  const auto signed_version = std::get_if<std::int64_t>(&version);
  const auto unsigned_version = std::get_if<std::uint64_t>(&version);
  if (!((signed_version && *signed_version == 1) ||
        (unsigned_version && *unsigned_version == 1)))
    throw std::invalid_argument("Unsupported rank command record version");
  const bool outliers = text_value(record.at("operation")) == "remove_outliers_imagej";
  const bool top_hat = text_value(record.at("operation")) == "top_hat_imagej";
  const bool remove_nans = text_value(record.at("operation")) == "remove_nans_imagej";
  if (!remove_nans && !outliers && !top_hat && text_value(record.at("operation")) != operation_id)
    throw std::invalid_argument("Rank command record names another operation");
  if (text_value(record.at("profile")) != profile_id)
    throw std::invalid_argument("Unsupported rank command profile");
  const auto &option_record = object_value(record.at("options"));
  RankFilterCommandOptions options;
  std::optional<RemoveOutliersCommandOptions> intent;
  bool inverted = false;
  std::optional<TopHatCommandOptions> top_hat_intent;
  std::optional<RemoveNaNsCommandOptions> remove_nans_intent;
  if (remove_nans) {
    exact_fields(option_record, {"radius"});
    remove_nans_intent = RemoveNaNsCommandOptions{number_value(option_record.at("radius"))};
    validate_remove_nans_command_options(*remove_nans_intent);
    options = {RankFilterMethod::Median, remove_nans_intent->radius};
  } else if (top_hat) {
    exact_fields(option_record, {"radius", "light_background", "dont_subtract", "inverted_lut"});
    const auto light = std::get_if<bool>(&option_record.at("light_background"));
    const auto dont = std::get_if<bool>(&option_record.at("dont_subtract"));
    const auto flag = std::get_if<bool>(&option_record.at("inverted_lut"));
    if (!light || !dont || !flag) throw std::invalid_argument("Top Hat flags require Booleans");
    top_hat_intent = TopHatCommandOptions{number_value(option_record.at("radius")), *light, *dont};
    validate_top_hat_command_options(*top_hat_intent);
    inverted = *flag;
    options = {(*light != inverted) ? RankFilterMethod::Maximum : RankFilterMethod::Minimum, top_hat_intent->radius};
  } else if (outliers) {
    exact_fields(option_record, {"radius", "threshold", "which", "inverted_lut"});
    intent = RemoveOutliersCommandOptions{number_value(option_record.at("radius")),
        number_value(option_record.at("threshold")), text_value(option_record.at("which"))};
    validate_remove_outliers_command_options(*intent);
    const auto flag = std::get_if<bool>(&option_record.at("inverted_lut"));
    if (!flag) throw std::invalid_argument("Outlier LUT flag requires a Boolean");
    inverted = *flag;
    options = {RankFilterMethod::Median, intent->radius};
  } else {
    exact_fields(option_record, {"method", "radius"});
    options = {parse_rank_filter_method(text_value(option_record.at("method"))),
               number_value(option_record.at("radius"))};
    validate_options(options);
  }
  auto target = detail::decode_target_record(object_value(record.at("target")));
  if (target.request.mode != ExecutionMode::Planes)
    throw std::invalid_argument("Rank command requires plane execution");
  if (target.source_shape.s != 1)
    throw std::invalid_argument(
        "Rank command record requires grayscale samples");
  return {options, std::move(target), std::move(intent), inverted, top_hat_intent, remove_nans_intent};
}

} // namespace

ResolvedRankFilterCommand::ResolvedRankFilterCommand(
    RankFilterCommandOptions options, ResolvedTarget target)
    : options_(options), target_(std::move(target)) {}

const char *ResolvedRankFilterCommand::operation() const {
  return remove_nans_ ? "remove_nans_imagej" : top_hat_ ? "top_hat_imagej" : outliers_ ? "remove_outliers_imagej" : operation_id;
}

OperationParameters ResolvedRankFilterCommand::parameters() const {
  if (remove_nans_) return {{"radius", remove_nans_->radius}};
  if (top_hat_) return {{"radius", top_hat_->radius}, {"light_background", top_hat_->light_background},
      {"dont_subtract", top_hat_->dont_subtract}, {"inverted_lut", inverted_lut_}};
  if (outliers_) return {{"radius", outliers_->radius}, {"threshold", outliers_->threshold},
      {"which", outliers_->which}, {"inverted_lut", inverted_lut_}};
  return {{"method", rank_filter_method_name(options_.method)},
          {"radius", options_.radius}};
}

MetadataObject ResolvedRankFilterCommand::record() const {
  if (remove_nans_) return {{"schema_version", 1}, {"operation", std::string(operation())},
      {"profile", std::string(profile_id)},
      {"options", metadata_object({{"radius", remove_nans_->radius}})},
      {"target", metadata_object(target_.record())}};
  if (top_hat_) return {{"schema_version", 1}, {"operation", std::string(operation())},
      {"profile", std::string(profile_id)}, {"options", metadata_object({
          {"radius", top_hat_->radius}, {"light_background", top_hat_->light_background},
          {"dont_subtract", top_hat_->dont_subtract}, {"inverted_lut", inverted_lut_}})},
      {"target", metadata_object(target_.record())}};
  if (outliers_) return {{"schema_version", 1}, {"operation", std::string(operation())},
      {"profile", std::string(profile_id)}, {"options", metadata_object({
          {"radius", outliers_->radius}, {"threshold", outliers_->threshold},
          {"which", outliers_->which}, {"inverted_lut", inverted_lut_}})},
      {"target", metadata_object(target_.record())}};
  return {{"schema_version", 1},
          {"operation", std::string(operation_id)},
          {"profile", std::string(profile_id)},
          {"options", metadata_object(
                          {{"method", rank_filter_method_name(options_.method)},
                           {"radius", options_.radius}})},
          {"target", metadata_object(target_.record())}};
}

void validate_remove_nans_command_options(const RemoveNaNsCommandOptions &options) {
  validate_options({RankFilterMethod::Median, options.radius});
}

ResolvedRankFilterCommand resolve_remove_nans_command(
    const DocumentSnapshot &source, const TargetRequest &request,
    RemoveNaNsCommandOptions options) {
  validate_remove_nans_command_options(options);
  validate_remove_nans_source(source);
  if (request.mode != ExecutionMode::Planes)
    throw std::invalid_argument("Remove NaNs requires plane execution");
  ResolvedRankFilterCommand result({RankFilterMethod::Median, options.radius},
      resolve_target(source, request, "remove_nans_imagej"));
  result.remove_nans_ = options;
  return result;
}

void validate_top_hat_command_options(const TopHatCommandOptions &options) {
  validate_options({RankFilterMethod::Minimum, options.radius});
}

void validate_remove_outliers_command_options(const RemoveOutliersCommandOptions &options) {
  validate_options({RankFilterMethod::Median, options.radius});
  if (!std::isfinite(options.threshold) || options.threshold < 0 ||
      options.threshold > double(std::numeric_limits<float>::max()))
    throw std::invalid_argument("Outlier threshold must be nonnegative and finite Float32");
  if (options.which != "bright" && options.which != "dark")
    throw std::invalid_argument("Outlier polarity must be bright or dark");
}

bool remove_outliers_lut_inverted(const DocumentSnapshot &source) {
  validate_source_metadata(source);
  const auto channels = recognized_channel_display(source.display(), source.data().dataset);
  ChannelDisplayTable table{};
  if (channels) {
    // Grayscale mode's live processor uses the grayscale LUT. Other modes use
    // the active channel LUT, including custom tables, independently of visibility.
    const auto &lut = channels->channels.at(source.display().c).lut;
    table = channels->mode == ChannelDisplayMode::Grayscale
        ? channel_display_lut("Grays") : lut.table.value_or(channel_display_lut(lut.name.empty() ? "Grays" : lut.name));
  } else {
    table = detail::legacy_rgb_palette(source.display().properties).value_or(channel_display_lut("Grays"));
  }
  // ImageJ tests summed RGB steps 0->1 through 253->254. The last step to
  // entry255 is deliberately excluded; flat/nonascending tables are inverted.
  for (std::size_t i = 1; i < 255; ++i) {
    const auto sum = [](const auto &rgb) { return int(rgb[0]) + int(rgb[1]) + int(rgb[2]); };
    if (sum(table[i-1]) < sum(table[i])) return false;
  }
  return true;
}

ResolvedRankFilterCommand resolve_remove_outliers_command(
    const DocumentSnapshot &source, const TargetRequest &request,
    RemoveOutliersCommandOptions options) {
  validate_remove_outliers_command_options(options);
  validate_source_metadata(source);
  if (request.mode != ExecutionMode::Planes)
    throw std::invalid_argument("Remove Outliers requires plane execution");
  const bool inverted = remove_outliers_lut_inverted(source);
  ResolvedRankFilterCommand result({RankFilterMethod::Median, options.radius},
      resolve_target(source, request, "remove_outliers_imagej"));
  result.outliers_ = std::move(options); result.inverted_lut_ = inverted;
  return result;
}

ResolvedRankFilterCommand resolve_top_hat_command(
    const DocumentSnapshot &source, const TargetRequest &request,
    TopHatCommandOptions options) {
  validate_top_hat_command_options(options);
  validate_source_metadata(source);
  if (request.mode != ExecutionMode::Planes)
    throw std::invalid_argument("Top Hat requires plane execution");
  const bool inverted = remove_outliers_lut_inverted(source);
  (void)detail::validate_top_hat_display_source(source,options.dont_subtract);
  ResolvedRankFilterCommand result({options.light_background != inverted
      ? RankFilterMethod::Maximum : RankFilterMethod::Minimum, options.radius},
      resolve_target(source, request, "top_hat_imagej"));
  result.top_hat_ = options; result.inverted_lut_ = inverted;
  return result;
}

void validate_rank_command_options(const RankFilterCommandOptions &options) {
  validate_options(options);
}

ResolvedRankFilterCommand
resolve_rank_command(const DocumentSnapshot &source,
                     const TargetRequest &request,
                     RankFilterCommandOptions options) {
  validate_options(options);
  validate_source_metadata(source);
  if (options.method == RankFilterMethod::Variance)
    (void)detail::validate_rank_variance_display_source(source);
  if (request.mode != ExecutionMode::Planes)
    throw std::invalid_argument("Rank command requires plane execution");
  return {options, resolve_target(source, request, operation_id)};
}

ResolvedRankFilterCommand
resolve_despeckle_command(const DocumentSnapshot &source,
                         const TargetRequest &request) {
  return resolve_rank_command(source, request, {RankFilterMethod::Median, 1.0});
}

void validate_rank_command_record(const MetadataObject &record) {
  (void)decode_rank_record(record);
}

ResolvedRankFilterCommand
rank_command_from_record(const DocumentSnapshot &source,
                         const MetadataObject &record) {
  const auto decoded = decode_rank_record(record);
  if(decoded.remove_nans) validate_remove_nans_source(source);
  else validate_source_metadata(source);
  if(decoded.top_hat)(void)detail::validate_top_hat_display_source(source,decoded.top_hat->dont_subtract);
  if (decoded.options.method == RankFilterMethod::Variance)
    (void)detail::validate_rank_variance_display_source(source);
  ResolvedRankFilterCommand result(decoded.options, detail::strict_target_from_record(
      source, decoded.target, decoded.remove_nans ? "remove_nans_imagej" : decoded.top_hat ? "top_hat_imagej" : decoded.outliers ? "remove_outliers_imagej" : operation_id));
  result.remove_nans_ = decoded.remove_nans;
  result.outliers_ = decoded.outliers; result.top_hat_ = decoded.top_hat; result.inverted_lut_ = decoded.inverted;
  return result;
}

ResolvedRankFilterCommand
rebind_rank_command(const DocumentSnapshot &destination,
                    const MetadataObject &record,
                    const TargetRequest &request) {
  // Original shape/ranges/mode/options are checked before the new destination
  // can influence resolution. No recorded identity field is ever rewritten.
  const auto decoded = decode_rank_record(record);
  if(decoded.remove_nans) return resolve_remove_nans_command(destination, request, *decoded.remove_nans);
  if (!decoded.outliers && !decoded.top_hat) return resolve_rank_command(destination, request, decoded.options);
  validate_source_metadata(destination);
  if(decoded.top_hat)(void)detail::validate_top_hat_display_source(destination,decoded.top_hat->dont_subtract);
  if (request.mode != ExecutionMode::Planes) throw std::invalid_argument("Remove Outliers requires plane execution");
  ResolvedRankFilterCommand result(decoded.options, resolve_target(destination, request, decoded.top_hat ? "top_hat_imagej" : "remove_outliers_imagej"));
  result.remove_nans_ = decoded.remove_nans;
  result.outliers_ = decoded.outliers; result.top_hat_ = decoded.top_hat; result.inverted_lut_ = decoded.inverted;
  return result;
}

RankFilterCommandRegion rank_command_region(const ResolvedTarget &target,
                                            std::size_t index) {
  validate_source_metadata(target.source());
  if (target.execution_mode() != ExecutionMode::Planes)
    throw std::invalid_argument("Rank command requires plane execution");
  // This adapter shares only the established ROI geometry planner. Rank
  // sampling, footprint and edge extension remain owned by the rank kernel.
  auto region = gaussian_command_region(target, index);
  std::optional<RankFilterExtent> extent;
  if (region.extent)
    extent = RankFilterExtent{region.extent->x, region.extent->y,
                              region.extent->width, region.extent->height};
  return {extent, std::move(region.mask)};
}

RankFilterCommandRegion remove_nans_command_region(const ResolvedTarget &target,
                                                   std::size_t index) {
  validate_remove_nans_source(target.source());
  if (target.execution_mode() != ExecutionMode::Planes)
    throw std::invalid_argument("Remove NaNs requires plane execution");
  auto region = gaussian_command_region(target, index);
  std::optional<RankFilterExtent> extent;
  if (region.extent) extent = RankFilterExtent{region.extent->x, region.extent->y,
                                             region.extent->width, region.extent->height};
  return {extent, std::move(region.mask)};
}

} // namespace imagejxx
