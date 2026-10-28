#include <imagejxx/imagej_intensity_calibration.hpp>
#include <imagejxx/document.hpp>
#include <imagejxx/imagej_dataset_projection.hpp>
#include "axis_command_record.hpp"
#include "session_metadata.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace imagejxx {
namespace {
namespace wire = detail::axis_command_record;
constexpr const char* schema = "imagejxx.intensity-calibration";
const MetadataObject& object(const MetadataValue& value) { return wire::object_value(value); }
const std::string& text(const MetadataValue& value) { return wire::text_value(value); }
void validate_unit(const std::string& unit) {
  if (unit.size() > 1024U * 1024U || unit.find('\0') != std::string::npos)
    throw std::invalid_argument("Intensity value unit exceeds1MiB or contains NUL");
  try { (void)detail::session_metadata::Json(unit).dump(); }
  catch (const detail::session_metadata::Json::exception&) {
    throw std::invalid_argument("Intensity value unit must be strict UTF-8");
  }
}
int depth(PixelType type) {
  if (type == PixelType::UInt8) return 8;
  if (type == PixelType::UInt16) return 16;
  if (type == PixelType::Float32) return 32;
  if (type == PixelType::Float64) return 64;
  throw std::invalid_argument("Invalid intensity pixel type");
}
std::size_t table_size(int bits) {
  if (bits == 8) return 256;
  if (bits == 16) return 65536;
  throw std::invalid_argument("Intensity lookup requires8/16-bit integer storage");
}
unsigned nibble(char c) {
  if (c >= '0' && c <= '9') return unsigned(c - '0');
  if (c >= 'a' && c <= 'f') return unsigned(c - 'a' + 10);
  throw std::invalid_argument("Intensity table requires lowercase hexadecimal words");
}
bool boolean(const MetadataValue& value) {
  if (const auto flag = std::get_if<bool>(&value)) return *flag;
  throw std::invalid_argument("Intensity signed16 flag requires Boolean");
}
MetadataObject base(const char* mode, std::string unit) {
  validate_unit(unit);
  return {{"schema", std::string(schema)}, {"version", std::int64_t(1)},
          {"mode", std::string(mode)}, {"value_unit", std::move(unit)}};
}
} // namespace

ImageJIntensityCalibration::ImageJIntensityCalibration(MetadataValue record,
    ImageJIntensityMode mode, int bits, bool signed16)
    : record_(std::move(record)), mode_(mode), depth_(bits), signed16_(signed16) {}

ImageJIntensityCalibration ImageJIntensityCalibration::uncalibrated(std::string unit) {
  return from_record(metadata_object(base("none", std::move(unit))));
}
ImageJIntensityCalibration ImageJIntensityCalibration::signed16(std::string unit) {
  return from_record(metadata_object(base("signed16", std::move(unit))));
}
ImageJIntensityCalibration ImageJIntensityCalibration::lookup(PixelType type,
    const std::vector<std::uint32_t>& words, std::string unit,
    MetadataObject parameters, bool signed16) {
  const int bits = depth(type);
  if (words.size() != table_size(bits))
    throw std::invalid_argument("Intensity lookup has wrong word count");
  if (signed16 && bits != 16)
    throw std::invalid_argument("Signed16 correction requires16-bit lookup storage");
  auto record = base("lookup", std::move(unit));
  auto provenance = metadata_object(std::move(parameters));
  try { (void)detail::session_metadata::to_json(provenance).dump(); }
  catch (const detail::session_metadata::Json::exception&) {
    throw std::invalid_argument("Intensity provenance must be UTF-8 metadata");
  }
  constexpr const char* hex = "0123456789abcdef";
  std::string encoded(words.size() * 8, '0');
  for (std::size_t i = 0; i < words.size(); ++i)
    for (unsigned digit = 0; digit < 8; ++digit)
      encoded[i * 8 + digit] = hex[(words[i] >> (28 - 4 * digit)) & 15];
  record.emplace("bit_depth", std::int64_t(bits));
  record.emplace("table_f32_hex", std::move(encoded));
  record.emplace("source_parameters", std::move(provenance));
  record.emplace("signed16", signed16);
  return {metadata_object(std::move(record)), ImageJIntensityMode::Lookup, bits, signed16};
}
ImageJIntensityCalibration ImageJIntensityCalibration::function(PixelType type,
    const std::string& name, const std::vector<double>& p, std::string unit,
    bool zero_clip, bool inverted_lut) {
  const int bits=depth(type);const auto count=table_size(bits);validate_unit(unit);
  static const char* names[]={"straight_line","poly2","poly3","poly4","exponential",
    "power","log","rodbard","gamma_variate","log2","rodbard2","exp_with_offset",
    "gaussian","exp_recovery","uncalibrated_od"};
  static const std::size_t lengths[]={2,3,4,5,2,2,2,4,4,3,4,3,4,3,0};
  int id=-1;for(int i=0;i<15;++i)if(name==names[i]){id=i;break;}
  if(id<0 || p.size()!=lengths[id])throw std::invalid_argument("Unknown intensity function or wrong coefficient count");
  if(id==14 && bits!=8)throw std::invalid_argument("Uncalibrated OD requires UInt8 storage");
  for(double value:p)if(!std::isfinite(value))throw std::invalid_argument("Intensity coefficients must be finite");
  const auto evaluate=[&](double x) {
    switch(id) {
    case 0:return p[0]+x*p[1];
    case 1:return p[0]+x*(p[1]+x*p[2]);
    case 2:return p[0]+x*(p[1]+x*(p[2]+x*p[3]));
    case 3:return p[0]+x*(p[1]+x*(p[2]+x*(p[3]+x*p[4])));
    case 4:return p[0]*std::exp(p[1]*x);
    case 5:return p[0]*std::pow(x,p[1]);
    case 6:return x==0.0?-1000*p[0]:p[0]*std::log(p[1]*x);
    case 7:return p[3]+(p[0]-p[3])/(1.0+std::pow(x/p[2],p[1]));
    case 8:
      if(p[0]>=x)return 0.0;
      if(p[1]<=0 || p[2]<=0 || p[3]<=0)return std::numeric_limits<double>::quiet_NaN();
      return p[1]*std::pow(x-p[0],p[2])*std::exp((-(x-p[0]))/p[3]);
    case 9:return x-p[2]<=0?std::numeric_limits<double>::quiet_NaN():p[0]+p[1]*std::log(x-p[2]);
    case 10:
      if(p[3]-x<2*std::numeric_limits<double>::denorm_min() || x<p[0])return 0.0;
      return std::pow((x-p[0])/(p[3]-x),1.0/p[1])*p[2];
    case 11:return p[0]*std::exp(-p[1]*x)+p[2];
    case 12:return p[0]+(p[1]-p[0])*std::exp(-(x-p[2])*(x-p[2])/(2.0*p[3]*p[3]));
    case 13:return p[0]*(1-std::exp(-p[1]*x))+p[2];
    default:
      if(inverted_lut){if(x==255.0)x=254.5;return 0.434294481*std::log(255.0/(255.0-x));}
      if(x==0.0)x=0.5;
      return 0.434294481*std::log(255.0/x);
    }
  };
  std::vector<std::uint32_t> words(count);
  for(std::size_t i=0;i<count;++i) {
    double value=evaluate(double(i));
    if(bits==8 && id!=14 && zero_clip && value<0.0)value=0.0;
    const float converted=static_cast<float>(value);
    if(std::isnan(converted))words[i]=0x7fc00000U;
    else std::memcpy(&words[i],&converted,sizeof(converted));
  }
  MetadataArray coefficients;for(double value:p)coefficients.emplace_back(value);
  MetadataObject parameters{{"producer",std::string("imagej154r.function")},{"version",std::int64_t(1)},
    {"function",name},{"coefficients",metadata_array(std::move(coefficients))},
    {"zero_clip",zero_clip},{"inverted_lut",inverted_lut},{"canonical_nan",true}};
  const bool signed16=bits==16 && p.size()>=2 && p[0]==-32768.0 && p[1]==1.0;
  return lookup(type,words,std::move(unit),std::move(parameters),signed16);
}
ImageJIntensityCalibration ImageJIntensityCalibration::from_record(const MetadataValue& value) {
  const auto& fields = object(value);
  const auto found = fields.find("mode");
  if (found == fields.end()) throw std::invalid_argument("Intensity mode is missing");
  const auto& mode = text(found->second);
  if (mode == "lookup")
    wire::exact_fields(fields, {"schema", "version", "mode", "value_unit", "bit_depth",
                                "table_f32_hex", "source_parameters", "signed16"});
  else if (mode == "none" || mode == "signed16")
    wire::exact_fields(fields, {"schema", "version", "mode", "value_unit"});
  else throw std::invalid_argument("Unsupported intensity mode");
  if (text(fields.at("schema")) != schema || wire::unsigned_value(fields.at("version")) != 1)
    throw std::invalid_argument("Unsupported intensity schema/version");
  validate_unit(text(fields.at("value_unit")));
  if (mode != "lookup")
    return {value, mode == "none" ? ImageJIntensityMode::None : ImageJIntensityMode::Signed16,
            mode == "none" ? 0 : 16, mode == "signed16"};
  const auto bits = wire::index_value(fields.at("bit_depth"));
  const auto& encoded = text(fields.at("table_f32_hex"));
  if (encoded.size() != table_size(bits) * 8)
    throw std::invalid_argument("Intensity hexadecimal table has wrong length");
  for (char c : encoded) (void)nibble(c);
  const bool signed16 = boolean(fields.at("signed16"));
  if (signed16 && bits != 16)
    throw std::invalid_argument("Signed16 correction requires16-bit lookup storage");
  const auto& parameters = object(fields.at("source_parameters"));
  validate_metadata(parameters);
  try { (void)detail::session_metadata::to_json(parameters).dump(); }
  catch (const detail::session_metadata::Json::exception&) {
    throw std::invalid_argument("Intensity provenance must be UTF-8 metadata");
  }
  return {value, ImageJIntensityMode::Lookup, bits, signed16};
}
const std::string& ImageJIntensityCalibration::value_unit() const {
  return text(object(record_).at("value_unit"));
}
const MetadataObject& ImageJIntensityCalibration::source_parameters() const {
  static const MetadataObject empty;
  return mode_ == ImageJIntensityMode::Lookup
      ? object(object(record_).at("source_parameters")) : empty;
}
std::vector<std::uint32_t> ImageJIntensityCalibration::table_words() const {
  if (mode_ != ImageJIntensityMode::Lookup) return {};
  const auto& encoded = text(object(record_).at("table_f32_hex"));
  std::vector<std::uint32_t> words(table_size(depth_));
  for (std::size_t i = 0; i < words.size(); ++i)
    for (unsigned digit = 0; digit < 8; ++digit)
      words[i] = (words[i] << 4) | nibble(encoded[i * 8 + digit]);
  return words;
}
void ImageJIntensityCalibration::validate_for(PixelType type, int samples) const {
  const int bits = depth(type);
  if (samples != 1 && samples != 3 && samples != 4)
    throw std::invalid_argument("Invalid intensity sample count");
  if (has_density() && (samples != 1 || (bits != 8 && bits != 16) || bits != depth_))
    throw std::invalid_argument("Intensity calibration differs from source storage");
}
bool ImageJIntensityCalibration::shares_record(const ImageJIntensityCalibration& other) const {
  return std::get<std::shared_ptr<const MetadataTree>>(record_) ==
         std::get<std::shared_ptr<const MetadataTree>>(other.record_);
}
ImageJIntensityCalibration read_imagej_intensity_calibration(
    const DocumentMetadata& metadata, PixelType type, int samples) {
  const auto found = metadata.properties.find(imagej_intensity_property);
  auto calibration = found == metadata.properties.end()
      ? ImageJIntensityCalibration::uncalibrated()
      : ImageJIntensityCalibration::from_record(found->second);
  calibration.validate_for(type, samples);
  return calibration;
}
ImageJIntensityCalibration read_imagej_intensity_calibration(const DocumentSnapshot& source) {
  return read_imagej_intensity_calibration(source.data().metadata,
      source.data().dataset.pixel_type(), source.data().dataset.shape().s);
}
void set_imagej_intensity_calibration(Document& document, const ImageJIntensityCalibration& calibration) {
  const auto source = document.snapshot();
  calibration.validate_for(source.data().dataset.pixel_type(), source.data().dataset.shape().s);
  auto edit = source.begin_edit(Change::Pixels | Change::Metadata);
  if (!calibration.has_density() && calibration.value_unit() == "Gray Value")
    edit.remove_property(imagej_intensity_property);
  else edit.set_property(imagej_intensity_property, calibration.record());
  document.commit(edit);
}
void apply_imagej_intensity_calibration(const ImageJIntensityCalibration& calibration,
    PixelType type, int samples, ImageJDatasetProjectionRequest& request) {
  calibration.validate_for(type, samples);
  auto words = calibration.table_words();
  request.calibration_table = std::move(words);
  request.signed16_density = calibration.signed16_density();
}
ImageJIntensityCalibration projected_imagej_intensity_calibration(
    const ImageJIntensityCalibration& source, PixelType type, int samples, bool preserve) {
  (void)depth(type);
  if (samples != 1 && samples != 3 && samples != 4)
    throw std::invalid_argument("Invalid projection intensity sample count");
  if (preserve) {
    if (!source.has_density()) throw std::invalid_argument("No source density to preserve");
    source.validate_for(type, samples);
    return source;
  }
  if (samples != 1) return ImageJIntensityCalibration::uncalibrated();
  if (source.has_density() && (type == PixelType::UInt8 || type == PixelType::UInt16))
    throw std::invalid_argument("Projection integer output must preserve source density");
  return ImageJIntensityCalibration::uncalibrated(source.value_unit());
}
} // namespace imagejxx
