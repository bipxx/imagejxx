// C++17 public installed-SDK orientation96 + owned11 consumer. Source only until Root execution.
// No reference files, expected result bytes, private headers or JSON dependency.
#include <imagejxx/application.hpp>
#include <imagejxx/execution.hpp>
#include <imagejxx/metadata.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <map>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {
using namespace imagejxx;
using Clock = std::chrono::steady_clock;
const auto started = Clock::now();
constexpr std::size_t max_index = 2 * 1024 * 1024;
constexpr std::size_t max_raw = 16 * 1024 * 1024;
std::filesystem::path output_directory;
std::map<std::string,std::string> raw_members;
std::size_t raw_bytes_written = 0;
constexpr int reference_x = 32, reference_y = 17, reference_z = 9;

void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
void deadline() {
  require(Clock::now() - started < std::chrono::seconds(120),
          "Orientation SDK consumer exceeded its 120-second bound");
}
template <class Error, class Function>
void rejects(Function function, const char *message) {
  bool caught = false;
  try { function(); } catch (const Error &) { caught = true; }
  require(caught, message);
}

std::string quote(const std::string &value) {
  constexpr char hex[] = "0123456789abcdef";
  std::string output = "\"";
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') { output += '\\'; output += char(c); }
    else if (c < 32) {
      output += "\\u00"; output += hex[c >> 4]; output += hex[c & 15];
    } else output += char(c);
  }
  output += '"';
  return output;
}
std::string number(double value) {
  require(std::isfinite(value), "Nonfinite observation metadata");
  std::ostringstream output;
  output << std::setprecision(17) << value;
  return output.str();
}
template <class Values>
std::string numeric_array(const Values &values) {
  std::ostringstream output;
  output << '[' << std::setprecision(17);
  bool first = true;
  for (const auto &value : values) {
    if constexpr (std::is_floating_point_v<std::decay_t<decltype(value)>>)
      require(std::isfinite(value), "Nonfinite observation array");
    if (!first) output << ',';
    first = false; output << value;
  }
  output << ']';
  return output.str();
}
std::string boolean(bool value) { return value ? "true" : "false"; }
std::string metadata(const MetadataValue &value);
std::string metadata(const MetadataObject &object) {
  std::string output = "{";
  bool first = true;
  for (const auto &[name, value] : object) {
    if (!first) output += ',';
    first = false; output += quote(name) + ':' + metadata(value);
  }
  return output + '}';
}
std::string metadata(const MetadataArray &array) {
  std::string output = "[";
  bool first = true;
  for (const auto &value : array) {
    if (!first) output += ',';
    first = false; output += metadata(value);
  }
  return output + ']';
}
std::string metadata(const MetadataValue &value) {
  const MetadataVariant &base = value;
  return std::visit([](const auto &item) -> std::string {
    using T = std::decay_t<decltype(item)>;
    if constexpr (std::is_same_v<T, std::monostate>) return "null";
    else if constexpr (std::is_same_v<T, bool>) return boolean(item);
    else if constexpr (std::is_same_v<T, std::int64_t> ||
                       std::is_same_v<T, std::uint64_t>) return std::to_string(item);
    else if constexpr (std::is_same_v<T, double>) return number(item);
    else if constexpr (std::is_same_v<T, std::string>) return quote(item);
    else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
      std::string output = "[";
      bool first = true;
      for (const auto &text : item) {
        if (!first) output += ',';
        first = false; output += quote(text);
      }
      return output + ']';
    } else {
      require(bool(item), "Null observation metadata tree");
      return std::visit([](const auto &tree) { return metadata(tree); }, item->value);
    }
  }, base);
}

void append_le(std::string &hex, std::uint64_t bits, unsigned bytes) {
  constexpr char digits[] = "0123456789abcdef";
  for (unsigned i = 0; i < bytes; ++i) {
    const auto byte = static_cast<unsigned char>(bits >> (i * 8));
    hex += digits[byte >> 4]; hex += digits[byte & 15];
  }
}
std::string pixel_hex(const Image &image, FloatingSamplePolicy policy) {
  std::string hex;
  hex.reserve(image.pixels.size() * 16);
  for (double value : image.pixels) {
    switch (image.type) {
    case PixelType::UInt8:
      require(std::isfinite(value) && value >= 0 && value <= 255 &&
                  std::floor(value) == value, "Noncanonical UInt8 observation");
      append_le(hex, static_cast<std::uint8_t>(value), 1); break;
    case PixelType::UInt16:
      require(std::isfinite(value) && value >= 0 && value <= 65535 &&
                  std::floor(value) == value, "Noncanonical UInt16 observation");
      append_le(hex, static_cast<std::uint16_t>(value), 2); break;
    case PixelType::Float32: {
      const auto bits = float32_source_sample_bits(value, policy);
      append_le(hex, bits, 4); break;
    }
    case PixelType::Float64: {
      std::uint64_t bits; std::memcpy(&bits, &value, sizeof(bits));
      append_le(hex, bits, 8); break;
    }
    default: throw std::runtime_error("Unknown observation pixel type");
    }
  }
  return hex;
}
std::string dataset_hex(const Dataset &dataset) {
  std::string output;
  const auto shape = dataset.shape();
  for (int t = 0; t < shape.t; ++t)
    for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z) {
        deadline(); output += pixel_hex(*dataset.plane({t, c, z}), dataset.sample_policy());
      }
  return output;
}
const char *type_name(PixelType type) {
  switch (type) {
  case PixelType::UInt8: return "UInt8";
  case PixelType::UInt16: return "UInt16";
  case PixelType::Float32: return "Float32";
  case PixelType::Float64: return "Float64";
  }
  throw std::runtime_error("Unknown observed pixel type");
}
const char *encoding(PixelType type, int samples) {
  if (type == PixelType::UInt8 && samples == 3) return "rgb_interleaved_uint8";
  if (type == PixelType::UInt8 && samples == 4) return "rgba_interleaved_uint8";
  if (type == PixelType::UInt8) return "uint8";
  if (type == PixelType::UInt16) return "uint16_le";
  if (type == PixelType::Float32) return "float32_le";
  return "float64_le";
}
const char *plane_name(Plane plane) {
  if (plane == Plane::XY) return "XY";
  if (plane == Plane::XZ) return "XZ";
  if (plane == Plane::YZ) return "YZ";
  throw std::runtime_error("Unknown plane");
}
std::array<int, 3> axes(Plane plane) {
  if (plane == Plane::XY) return {0, 1, 2};
  if (plane == Plane::XZ) return {0, 2, 1};
  return {1, 2, 0};
}
std::array<int, 3> source_voxel(Plane plane, int k, int u, int v) {
  if (plane == Plane::XY) return {u, v, k};
  if (plane == Plane::XZ) return {u, k, v};
  return {k, u, v};
}
// Independent literal affine evaluation; not Dataset::plane_geometry().
std::array<double, 3> world(const SpatialCalibration &cal,
                          const std::array<int, 3> &voxel) {
  auto output = cal.origin;
  for (int row = 0; row < 3; ++row)
    for (int column = 0; column < 3; ++column)
      output[row] += cal.direction[row * 3 + column] *
                     cal.spacing[column] * voxel[column];
  return output;
}
void close(double actual, double expected) {
  require(std::abs(actual - expected) <=
              1e-12 * (1 + std::max(std::abs(actual), std::abs(expected))),
          "Observed full-world affine differs from independent formula");
}
std::string calibration(const SpatialCalibration &cal) {
  return "{\"spacing\":" + numeric_array(cal.spacing) +
      ",\"origin\":" + numeric_array(cal.origin) +
      ",\"direction\":" + numeric_array(cal.direction) +
      ",\"unit\":" + quote(cal.unit) + ",\"time_unit\":" + quote(cal.time_unit) +
      ",\"frame_interval\":" + number(cal.frame_interval) +
      ",\"time_origin\":" + number(cal.time_origin) + '}';
}
std::string geometry(const PlaneGeometry &value) {
  return "{\"origin\":" + numeric_array(value.origin) +
      ",\"horizontal_step\":" + numeric_array(value.horizontal_step) +
      ",\"vertical_step\":" + numeric_array(value.vertical_step) +
      ",\"horizontal_axis\":" + quote(value.horizontal_axis) +
      ",\"vertical_axis\":" + quote(value.vertical_axis) +
      ",\"unit\":" + quote(value.unit) + '}';
}
std::string raw_record(const Dataset &dataset) {
  const auto hex = dataset_hex(dataset);
  auto found = raw_members.find(hex);
  std::string member;
  if (found != raw_members.end()) member = found->second;
  else {
    const auto size = hex.size()/2;
    require(size <= max_raw-raw_bytes_written, "Compact SDK raw storage exceeds16MiB");
    std::ostringstream name; name << "raw/" << std::setw(6) << std::setfill('0') << raw_members.size()+1 << ".bin";
    member=name.str();
    std::ofstream stream(output_directory/member,std::ios::binary|std::ios::out);
    require(bool(stream),"Cannot create fresh raw observation");
    for (std::size_t i=0;i<hex.size();i+=2) {
      auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};
      const char byte=char((nibble(hex[i])<<4)|nibble(hex[i+1])); stream.write(&byte,1);
    }
    stream.close(); require(bool(stream),"Cannot write complete raw observation");
    raw_members.emplace(hex,member); raw_bytes_written+=size;
  }
  return "{\"path\":"+quote(member)+",\"bytes\":"+std::to_string(hex.size()/2)+'}';
}
std::string dataset_record(const Dataset &dataset) {
  const auto shape = dataset.shape();
  return "{\"shape\":" + numeric_array(shape.dimensions()) +
      ",\"pixel_type\":" + quote(type_name(dataset.pixel_type())) +
      ",\"S\":" + std::to_string(shape.s) +
      ",\"encoding\":" + quote(encoding(dataset.pixel_type(), shape.s)) +
      ",\"sample_policy\":" + quote(dataset.sample_policy() == FloatingSamplePolicy::FiniteOnly
          ? "FiniteOnly" : "PreserveQuietNaNFloat32") +
      ",\"raw\":" + raw_record(dataset) +
      ",\"calibration\":" + calibration(dataset.calibration()) + '}';
}
std::string history(const HistoryInfo &value) {
  return "{\"undo_steps\":" + std::to_string(value.undo_steps) +
      ",\"redo_steps\":" + std::to_string(value.redo_steps) +
      ",\"step_limit\":" + std::to_string(value.step_limit) +
      ",\"pixel_budget\":" + std::to_string(value.pixel_budget) +
      ",\"pixels\":{\"logical_bytes\":" + std::to_string(value.pixels.logical_bytes) +
      ",\"current_bytes\":" + std::to_string(value.pixels.current_bytes) +
      ",\"retained_bytes\":" + std::to_string(value.pixels.retained_bytes) +
      ",\"obsolete_bytes\":" + std::to_string(value.pixels.obsolete_bytes) +
      ",\"retained_planes\":" + std::to_string(value.pixels.retained_planes) +
      ",\"exact\":" + boolean(value.pixels.exact) + "}}";
}
std::string optional_int(std::optional<int> value) {
  return value ? std::to_string(*value) : "null";
}
std::string selection(const std::optional<Selection> &value) {
  if (!value) return "null";
  std::string points = "[";
  for (std::size_t i = 0; i < value->points.size(); ++i) {
    if (i) points += ',';
    points += numeric_array(value->points[i]);
  }
  std::string packed;
  if (value->kind == "mask")
    for (auto byte : value->packed_mask()) append_le(packed, byte, 1);
  return "{\"kind\":" + quote(value->kind) + ",\"points\":" + points +
      "],\"width\":" + number(value->width) +
      ",\"attributes\":" + metadata(value->attributes) +
      ",\"mask_width\":" + std::to_string(value->mask_width()) +
      ",\"mask_height\":" + std::to_string(value->mask_height()) +
      ",\"packed_hex\":" + quote(packed) + '}';
}
std::string snapshot_record(const DocumentSnapshot &snapshot,
                            const HistoryInfo &observed_history) {
  const auto &data = snapshot.data();
  const auto revisions = snapshot.revisions();
  const auto &view = snapshot.display();
  std::string rois = "[", overlays = "[", spectrum = "null", pan = "[";
  for (std::size_t i = 0; i < data.rois.size(); ++i) {
    if (i) rois += ',';
    const auto &roi = data.rois[i];
    rois += "{\"name\":" + quote(roi.name) + ",\"selection\":" + selection(roi.selection) +
        ",\"t\":" + optional_int(roi.t) + ",\"c\":" + optional_int(roi.c) +
        ",\"z\":" + optional_int(roi.z) + ",\"attributes\":" + metadata(roi.attributes) + '}';
  }
  for (std::size_t i = 0; i < data.overlays.size(); ++i) {
    if (i) overlays += ',';
    overlays += metadata(data.overlays[i]);
  }
  for (std::size_t i = 0; i < view.pan.size(); ++i) {
    if (i) pan += ',';
    pan += numeric_array(view.pan[i]);
  }
  if (data.spectrum) {
    const auto &value = data.spectrum->spectrum();
    std::string raw;
    for (const auto &sample : value.pixels)
      for (double component : {sample.real(), sample.imag()}) {
        std::uint64_t bits; std::memcpy(&bits, &component, sizeof(bits));
        append_le(raw, bits, 8);
      }
    spectrum = "{\"width\":" + std::to_string(value.width) +
        ",\"height\":" + std::to_string(value.height) +
        ",\"original_shape\":" + (data.spectrum->original_shape()
            ? numeric_array(*data.spectrum->original_shape()) : "null") +
        ",\"encoding\":\"complex_float64_le_interleaved\",\"raw_hex\":" + quote(raw) + '}';
  }
  return "{\"document_id\":" + std::to_string(snapshot.id()) +
      ",\"data_identity\":" + (snapshot.data_identity()
          ? std::to_string(*snapshot.data_identity()) : "null") +
      ",\"dirty\":" + boolean(snapshot.dirty()) +
      ",\"revisions\":{\"pixels\":" + std::to_string(revisions.pixels) +
      ",\"metadata\":" + std::to_string(revisions.metadata) +
      ",\"selection\":" + std::to_string(revisions.selection) +
      ",\"display\":" + std::to_string(revisions.display) + "},\"history\":" + history(observed_history) +
      ",\"dataset\":" + dataset_record(data.dataset) +
      ",\"title\":" + quote(data.metadata.title) +
      ",\"properties\":" + metadata(data.metadata.properties) +
      ",\"extensions\":" + metadata(data.metadata.extensions) +
      ",\"record_layout\":" + metadata(data.metadata.record_layout) +
      ",\"selection\":" + selection(data.selection) +
      ",\"previous_selection\":" + selection(data.previous_selection) +
      ",\"rois\":" + rois + "],\"overlays\":" + overlays +
      "],\"spectrum\":" + spectrum + ",\"display\":{\"t\":" + std::to_string(view.t) +
      ",\"c\":" + std::to_string(view.c) + ",\"orthogonal\":" + boolean(view.orthogonal) +
      ",\"linked_zoom\":" + boolean(view.linkedZoom) +
      ",\"crosshair\":" + numeric_array(view.crosshair) + ",\"zoom\":" + number(view.zoom) +
      ",\"plane_zoom\":" + numeric_array(view.planeZoom) + ",\"pan\":" + pan +
      "],\"revision\":" + std::to_string(view.revision) +
      ",\"properties\":" + metadata(view.properties) + "}}";
}

struct Case {
  std::string id, profile;
  PixelType type;
  int samples;
  DatasetShape shape;
  AxisRangeOrientationRequest request;
};
AxisRangeOrientationRequest request(Plane plane,int first,int last,int step,
                                    int t,int c,bool reverse,bool flip,bool transpose) {
  return {{plane,first,last,step,t,c},reverse,flip,transpose};
}
std::vector<Case> reference_cases() {
  std::vector<Case> result;
  for (const std::string group : {"interior_stride_unaligned","full","upper_singleton_stride"})
    for (const std::string profile : {"u8","u16","f32","rgb"}) {
      if (group!="interior_stride_unaligned" && profile!="u8") continue;
      for (auto plane : {Plane::XZ,Plane::YZ}) {
        const int size=plane==Plane::XZ?reference_y:reference_x;
        const int first=group=="interior_stride_unaligned"?1:group=="full"?0:size-1;
        const int last=group=="interior_stride_unaligned"?size-2:size-1;
        const int step=group=="full"?1:3;
        for (bool reverse : {false,true}) for (bool flip : {false,true}) for (bool transpose : {false,true}) {
          const auto id=profile+'.'+(plane==Plane::XZ?"xz":"yz")+'.'+group+
              ".reverse"+std::to_string(int(reverse))+".flip"+std::to_string(int(flip))+
              ".transpose"+std::to_string(int(transpose));
          const auto type=profile=="u16"?PixelType::UInt16:profile=="f32"?PixelType::Float32:PixelType::UInt8;
          const int samples=profile=="rgb"?3:1;
          result.push_back({id,profile,type,samples,{1,1,9,17,32,samples},
                            request(plane,first,last,step,0,0,reverse,flip,transpose)});
        }
      }
    }
  require(result.size()==96,"Exact96 reference input declarations required");
  return result;
}
std::vector<Case> owned_cases() {
  std::vector<Case> result;
  const auto add=[&](std::string id,PixelType type,int s,Plane plane,int first,int last,int step,int t,int c,bool r,bool f,bool tr) {
    result.push_back({std::move(id),"owned",type,s,{2,2,4,5,7,s},request(plane,first,last,step,t,c,r,f,tr)});
  };
  add("owned.xy.f64.s1",PixelType::Float64,1,Plane::XY,0,3,2,1,1,true,true,true);
  add("owned.xz.u8.rgba.s4",PixelType::UInt8,4,Plane::XZ,0,4,3,1,0,true,true,true);
  add("owned.yz.f64.rgb.s3",PixelType::Float64,3,Plane::YZ,1,6,2,1,1,true,true,true);
  add("owned.xz.u16.rgba.s4",PixelType::UInt16,4,Plane::XZ,0,4,3,1,0,false,true,false);
  add("owned.xy.u16.rgb.s3",PixelType::UInt16,3,Plane::XY,0,3,2,0,1,false,false,true);
  add("owned.xz.f32.rgb.s3",PixelType::Float32,3,Plane::XZ,0,4,3,1,1,true,true,false);
  add("owned.yz.f32.rgba.s4",PixelType::Float32,4,Plane::YZ,1,6,2,1,0,false,true,true);
  add("owned.xy.f64.rgba.s4",PixelType::Float64,4,Plane::XY,0,3,2,1,1,true,false,false);
  result.push_back({"owned.xy.f32.quiet.s1","quiet_f32",PixelType::Float32,1,{1,1,3,2,4097,1},
                    request(Plane::XY,0,1,1,0,0,true,true,true)});
  add("owned.xy.u16.c2.s1",PixelType::UInt16,1,Plane::XY,0,3,2,1,1,true,false,true);
  result.push_back({"owned.yz.f32.c5.s1","owned",PixelType::Float32,1,{2,5,4,5,7,1},
                    request(Plane::YZ,1,6,2,1,4,false,true,true)});
  return result;
}
constexpr std::array<std::uint32_t,8> quiet_bits{0,0x80000000u,1,0x007fffffu,
    0x7fc12345u,0xffc34567u,0x3f800000u,0xbf800000u};
double literal(const Case &item,int t,int c,int z,int y,int x,int s) {
  if (item.profile=="quiet_f32")
    return float32_source_sample(quiet_bits[(std::size_t(y)*item.shape.x+x+17*z)%8],FloatingSamplePolicy::PreserveQuietNaNFloat32);
  if (item.profile=="u8") return (37*x+61*y+13*x*y+43*z)%251;
  if (item.profile=="u16") return 6000*z+32*y+x;
  if (item.profile=="f32") return 10000*z+100*y+x+.25;
  if (item.profile=="rgb") {
    if (s==0) return (17*x+31*y+47*z)%256;
    if (s==1) return (71*x+11*y+29*z)%256;
    return (13*x+61*y+23*z)%256;
  }
  if (item.type==PixelType::UInt8) return (53*t+37*c+43*z+17*y+11*x+29*s)%251;
  if (item.type==PixelType::UInt16) return 10000*t+5000*c+600*z+70*y+5*x+s;
  const double code=10000*t+1000*c+100*z+10*y+x+.25*s-.5;
  return item.type==PixelType::Float32?double(float(code)):code+.125;
}
SpatialCalibration source_calibration(const Case &item) {
  SpatialCalibration cal;
  if (item.profile=="owned" || item.profile=="quiet_f32" || item.profile=="f32" || item.profile=="rgb")
    cal.spacing={.4,1.2,3};
  cal.origin={-2,4,9};cal.direction={-1,.5,.25,0,1,.5,.5,0,1};
  cal.unit="um";cal.time_unit="min";cal.frame_interval=2.5;cal.time_origin=-3;
  return cal;
}
Dataset source(const Case &item) {
  Stack planes;
  for (int t=0;t<item.shape.t;++t) for (int c=0;c<item.shape.c;++c) for (int z=0;z<item.shape.z;++z) {
    deadline(); Image image(item.shape.x,item.shape.y,item.samples,item.type);
    for (int y=0;y<item.shape.y;++y) for (int x=0;x<item.shape.x;++x) for (int s=0;s<item.samples;++s)
      image.at(x,y,s)=literal(item,t,c,z,y,x,s);
    planes.push_back(std::move(image));
  }
  return Dataset::from_planes(item.shape,std::move(planes),source_calibration(item),
      item.profile=="quiet_f32"?FloatingSamplePolicy::PreserveQuietNaNFloat32:FloatingSamplePolicy::FiniteOnly);
}
std::shared_ptr<Document> annotated(Dataset dataset,const std::string &title) {
  DocumentData data{std::move(dataset)};data.metadata.title=title;
  data.metadata.properties["source_only"]=std::string("retained property");
  data.metadata.extensions["source_only"]=metadata_object({{"retained",true}});
  data.metadata.record_layout["source_layout"]=true;
  data.selection=Selection("rectangle",{{2,3},{7,9}});
  data.previous_selection=Selection("line",{{1,1},{4,5}});
  data.rois={{"saved",Selection("rectangle",{{1,2},{5,6}}),0,0,2,{{"source_style",std::string("retained")}}}};
  data.overlays={{{"source_overlay",std::string("retained")}}};
  data.spectrum=FourierState(Spectrum{2,2,std::vector<std::complex<double>>(4,{0,0})},std::array<int,2>{2,2});
  auto owner=std::make_shared<Document>(std::move(data));
  auto seed=owner->begin_edit();seed.set_property("revision_probe",std::int64_t(7));owner->commit(seed);
  auto display=owner->snapshot().display();const auto shape=owner->snapshot().data().dataset.shape();
  display.crosshair={std::min(7,shape.x-1),std::min(5,shape.y-1),std::min(4,shape.z-1)};
  display.orthogonal=true;display.zoom=2.25;display.properties["source_display"]=std::string("retained");owner->set_display(display);
  return owner;
}
std::string request_json(const AxisRangeOrientationRequest &r) {
  const auto &q=r.range;
  return "{\"range\":{\"plane\":"+quote(plane_name(q.plane))+",\"first\":"+std::to_string(q.first)+
      ",\"last\":"+std::to_string(q.last)+",\"step\":"+std::to_string(q.step)+
      ",\"t\":"+std::to_string(q.t)+",\"c\":"+std::to_string(q.c)+
      ",\"max_working_bytes\":"+std::to_string(q.max_working_bytes)+"},\"reverse_order\":"+boolean(r.reverse_order)+
      ",\"flip_vertical\":"+boolean(r.flip_vertical)+",\"transpose\":"+boolean(r.transpose)+'}';
}
std::array<int,3> mapped(const Case &item,int k,int u,int v) {
  const auto &q=item.request.range;const auto order=axes(q.plane);
  const std::array<int,3> sizes{item.shape.x,item.shape.y,item.shape.z};
  const int count=1+(q.last-q.first)/q.step,last_selected=q.first+(count-1)*q.step;
  std::array<int,3> xyz{};
  xyz[order[0]]=item.request.transpose?v:u;
  const int vertical=item.request.transpose?u:v;
  xyz[order[1]]=item.request.flip_vertical?sizes[order[1]]-1-vertical:vertical;
  xyz[order[2]]=item.request.reverse_order?last_selected-k*q.step:q.first+k*q.step;
  return xyz;
}
std::size_t working_bytes(const Case &item) {
  const auto order=axes(item.request.range.plane);const std::array<int,3> sizes{item.shape.x,item.shape.y,item.shape.z};
  const auto plane=std::size_t(sizes[order[0]])*sizes[order[1]]*item.samples;
  const auto count=std::size_t(1+(item.request.range.last-item.request.range.first)/item.request.range.step);
  return sizeof(double)*(plane*(count+1)+std::size_t(item.shape.x)*item.shape.y*item.samples+
      (item.request.range.plane==Plane::XY?0:std::size_t(sizes[order[0]])*item.samples));
}
void check_result(const Case &item,const Dataset &input,const AxisRangeOrientationResult &result) {
  const auto &q=item.request.range;const auto order=axes(q.plane);
  const std::array<int,3> sizes{item.shape.x,item.shape.y,item.shape.z};
  const int count=1+(q.last-q.first)/q.step;
  const std::array<int,6> expected_shape{1,1,count,item.request.transpose?sizes[order[0]]:sizes[order[1]],
      item.request.transpose?sizes[order[1]]:sizes[order[0]],item.samples};
  require(result.dataset.shape().dimensions()==expected_shape && result.dataset.pixel_type()==item.type &&
          result.dataset.sample_policy()==input.sample_policy() && !result.dataset.shares_pixels(input),
          "Orientation dimensions/type/sample-policy or independent pixel ownership differ");
  require(request_json(result.request)==request_json(item.request) && result.working_bytes==working_bytes(item),
          "Orientation result lost captured nested request or working estimate");
  const auto &cal=result.dataset.calibration();const auto original=input.calibration();
  auto out_axes=order;if(item.request.transpose)std::swap(out_axes[0],out_axes[1]);
  const auto origin=world(original,mapped(item,0,0,0));
  for(int row=0;row<3;++row) {
    close(cal.origin[row],origin[row]);close(result.first_plane_geometry.origin[row],origin[row]);
    for(int column=0;column<3;++column) {
      const int source_axis=out_axes[column];
      const int sign=source_axis==order[1]?(item.request.flip_vertical?-1:1):source_axis==order[2]?(item.request.reverse_order?-1:1):1;
      close(cal.direction[row*3+column],sign*original.direction[row*3+source_axis]);
    }
    close(result.first_plane_geometry.horizontal_step[row],cal.direction[row*3]*cal.spacing[0]);
    close(result.first_plane_geometry.vertical_step[row],cal.direction[row*3+1]*cal.spacing[1]);
  }
  for(int column=0;column<3;++column)close(cal.spacing[column],original.spacing[out_axes[column]]*(column==2?q.step:1));
  close(cal.time_origin,original.time_origin+q.t*original.frame_interval);
  require(cal.unit==original.unit && cal.time_unit==original.time_unit && cal.frame_interval==original.frame_interval,
          "Orientation lost spatial/time units");
  const std::array<std::string,3> names{"X","Y","Z"};
  require(result.first_plane_geometry.horizontal_axis==names[out_axes[0]] && result.first_plane_geometry.vertical_axis==names[out_axes[1]],
          "First-plane geometry axis labels differ");
  for(int k=0;k<count;++k) {
    deadline();const auto image=result.dataset.plane({0,0,k});
    for(int v=0;v<image->height;++v)for(int u=0;u<image->width;++u)for(int s=0;s<item.samples;++s) {
      const auto xyz=mapped(item,k,u,v);const auto expected=literal(item,q.t,q.c,xyz[2],xyz[1],xyz[0],s);
      if(item.profile=="quiet_f32") require(float32_source_sample_bits(image->at(u,v,s),input.sample_policy())==
          float32_source_sample_bits(expected,input.sample_policy()),"Orientation changed admitted quiet Float32 sample bits");
      else require(image->at(u,v,s)==expected,"Orientation changed independently authored literal source sample");
    }
    for(int v:{0,image->height-1})for(int u:{0,image->width-1}) {
      const auto expected=world(original,mapped(item,k,u,v));const auto actual=cal.voxel_to_world({double(u),double(v),double(k)});
      for(int row=0;row<3;++row)close(actual[row],expected[row]);
    }
  }
}
std::string error_record(const std::exception &error) {
  const std::string type=dynamic_cast<const OperationCancelled*>(&error)?"OperationCancelled":
      dynamic_cast<const DocumentUnavailable*>(&error)?"DocumentUnavailable":
      dynamic_cast<const RevisionConflict*>(&error)?"RevisionConflict":
      dynamic_cast<const std::out_of_range*>(&error)?"out_of_range":
      dynamic_cast<const std::invalid_argument*>(&error)?"invalid_argument":"std::exception";
  return "\"exception_type\":"+quote(type)+",\"error\":"+quote(error.what());
}
std::string successful_row(const Case &item) {
  deadline();auto owner=annotated(source(item),item.id);Application app;app.adopt(owner);
  const auto before=owner->snapshot();const auto before_record=snapshot_record(before,owner->history());
  const auto ids_before=app.document_ids();const auto active_before=app.active_document_id();
  ExecutionControl control;control.checkpoint=deadline;
  const auto direct=reslice_oriented_axis_range(before.data().dataset,item.request,control);check_result(item,before.data().dataset,direct);
  auto prepared=prepare_axis_range_orientation(before,item.request,control);auto candidate=prepared.prepared_new_document();
  require(candidate && prepared.active() && !app.contains(candidate->id),"Orientation candidate was not unregistered");
  const auto candidate_history=candidate->history();const auto candidate_id=candidate->id;
  require(!candidate->snapshot().dirty() && !candidate_history.undo_steps && !candidate_history.redo_steps,
          "Prepared candidate is not clean with empty history");
  const bool active_before_publish=prepared.active(),registered_before=app.contains(candidate_id);
  const auto published=app.publish(prepared);const auto output=published.document->snapshot();
  require(published.created && published.document==candidate && published.document_id==candidate_id && !prepared.active() &&
      app.document_ids()==std::vector<std::uint64_t>{owner->id,candidate_id} && app.active_document_id()==candidate_id &&
      app.snapshot(candidate_id).same_document(candidate->snapshot()),"Publication changed exact staged owner/membership/activation");
  require(dataset_hex(output.data().dataset)==dataset_hex(direct.dataset),"Direct and published typed results differ");
  const auto &data=output.data();require(!data.selection && !data.previous_selection && data.rois.empty() && data.overlays.empty() &&
      !data.spectrum && data.metadata.properties.empty() && data.metadata.record_layout.empty() &&
      data.metadata.extensions.size()==1 && data.metadata.extensions.count("axis_range_orientation") &&
      !output.dirty() && !published.document->history().undo_steps && !published.document->history().redo_steps,
      "Output retained source annotations or history");
  auto after_record=snapshot_record(owner->snapshot(),owner->history());require(before_record==after_record &&
      owner->snapshot().shares_data(before),"Orientation changed source state/history/data ownership");
  const auto repeat_ids=app.document_ids();const auto repeat_active=app.active_document_id();
  const auto repeat_output_before=snapshot_record(published.document->snapshot(),published.document->history());
  std::string repeat_error;try{app.publish(prepared);}catch(const std::exception &error){repeat_error=error_record(error);}
  const auto repeat_output_after=snapshot_record(published.document->snapshot(),published.document->history());
  after_record=snapshot_record(owner->snapshot(),owner->history());
  require(!repeat_error.empty() && repeat_ids==app.document_ids() && repeat_active==app.active_document_id() && !prepared.active(),
          "Repeated publication was not atomic");
  require(repeat_output_before==repeat_output_after && after_record==before_record,"Repeated publication changed output/source state");
  return "{\"case_id\":"+quote(item.id)+",\"profile\":"+quote(item.profile)+",\"captured_request\":"+request_json(item.request)+
      ",\"direct_result\":{\"dataset\":"+dataset_record(direct.dataset)+",\"first_plane_geometry\":"+geometry(direct.first_plane_geometry)+
      ",\"request\":"+request_json(direct.request)+",\"working_bytes\":"+std::to_string(direct.working_bytes)+
      "},\"source_before\":"+before_record+",\"source_after\":"+after_record+",\"output_snapshot\":"+snapshot_record(output,published.document->history())+
      ",\"publication\":{\"source_id\":"+std::to_string(owner->id)+",\"candidate_id\":"+std::to_string(candidate_id)+
      ",\"output_id\":"+std::to_string(published.document_id)+",\"member_ids_before\":"+numeric_array(ids_before)+
      ",\"member_ids_after\":"+numeric_array(app.document_ids())+",\"active_id_before\":"+std::to_string(active_before)+
      ",\"active_id_after\":"+std::to_string(app.active_document_id())+",\"candidate_history_before\":"+history(candidate_history)+
      ",\"prepared_active_before\":"+boolean(active_before_publish)+",\"prepared_active_after\":"+boolean(prepared.active())+
      ",\"candidate_registered_before\":"+boolean(registered_before)+",\"published_created\":"+boolean(published.created)+
      ",\"same_candidate_owner\":"+boolean(published.document==candidate)+"},\"repeat_publication\":{"+repeat_error+
      ",\"member_ids_before\":"+numeric_array(repeat_ids)+",\"member_ids_after\":"+numeric_array(app.document_ids())+
      ",\"active_id_before\":"+std::to_string(repeat_active)+",\"active_id_after\":"+std::to_string(app.active_document_id())+
      ",\"prepared_active_before\":false,\"prepared_active_after\":"+boolean(prepared.active())+
      ",\"output_before\":"+repeat_output_before+",\"output_after\":"+repeat_output_after+"}}";
}

class CountingSource final : public PlaneSource {
public:
  Case item;
  mutable std::vector<std::array<int,3>> planes;
  mutable std::vector<std::array<int,7>> regions;
  explicit CountingSource(Case value):item(std::move(value)) {}
  DatasetShape shape() const override {return item.shape;}
  PixelType pixel_type() const override {return item.type;}
  void fill(Image &image,PlanePosition p,int x,int y) const {
    for(int v=0;v<image.height;++v)for(int u=0;u<image.width;++u)for(int s=0;s<item.samples;++s)
      image.at(u,v,s)=literal(item,p.t,p.c,p.z,y+v,x+u,s);
  }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    planes.push_back({p.t,p.c,p.z});auto image=std::make_shared<Image>(item.shape.x,item.shape.y,item.samples,item.type);
    fill(*image,p,0,0);return image;
  }
  Image read_region(PlanePosition p,int x,int y,int width,int height) const override {
    regions.push_back({p.t,p.c,p.z,x,y,width,height});Image image(width,height,item.samples,item.type);
    fill(image,p,x,y);return image;
  }
};
template<class T>
std::string array_records(const std::vector<T> &values) {
  std::string record="[";for(std::size_t i=0;i<values.size();++i){if(i)record+=',';record+=numeric_array(values[i]);}return record+']';
}
std::string optional_bool(const std::optional<bool> &value) {return value?boolean(*value):"null";}
std::string rejection_row(int scenario) {
  const std::array<std::string,8> ids{"reject.invalid_first","reject.memory_cap","reject.pre_canceled","reject.copy_canceled",
      "reject.stale_pixels","reject.stale_calibration","reject.closed_owner","reject.candidate_tamper"};
  Case item{ids.at(scenario),"owned",PixelType::Float64,1,{2,2,4,5,7,1},request(Plane::XY,0,3,2,1,1,true,true,true)};
  if(scenario==0)item.request=request(Plane::XY,-1,1,1,1,1,true,true,true);
  if(scenario==1){item.request=request(Plane::XZ,0,4,3,1,1,true,true,true);item.request.range.max_working_bytes=1007;}
  if(scenario==2)item.request=request(Plane::XY,0,1,1,1,1,true,true,true);
  if(scenario==3){item.shape={1,1,3,2,4097,1};item.request=request(Plane::XY,0,1,1,0,0,true,true,true);}
  auto counting=std::make_shared<CountingSource>(item);auto owner=annotated(Dataset(counting,source_calibration(item)),item.id);
  Application app;app.adopt(owner);const auto captured=owner->snapshot();const auto captured_record=snapshot_record(captured,owner->history());
  auto token=std::make_shared<CancellationToken>();ExecutionControl control;control.cancellation=token;control.checkpoint=deadline;
  std::vector<std::string> stages;
  control.stage_progress=[&](std::string_view stage,std::size_t done,std::size_t total) {
    require(stages.size()<4096,"Rejection stage event count exceeded4096");
    stages.push_back("{\"stage\":"+quote(std::string(stage))+",\"done\":"+std::to_string(done)+",\"total\":"+std::to_string(total)+'}');
    if(scenario==3 && stage=="axis_range_orientation_copy" && done>=1024)token->cancel();
  };
  std::optional<PreparedOperation> prepared;std::shared_ptr<Document> candidate;
  if(scenario>=4) {
    prepared=app.prepare_axis_range_orientation(item.request,owner->id,control);candidate=prepared->prepared_new_document();
    require(candidate && !app.contains(candidate->id),"Rejected publication did not have an unregistered candidate");
    if(scenario==4) {
      auto image=*owner->snapshot().data().dataset.plane({1,1,0});image.pixels[0]+=1;
      auto edit=owner->begin_edit();edit.replace_plane({1,1,0},std::move(image));owner->commit(edit);
    } else if(scenario==5) {
      auto cal=owner->snapshot().data().dataset.calibration();cal.origin[0]+=1;
      auto edit=owner->begin_edit();edit.set_calibration(cal);owner->commit(edit);
    } else if(scenario==6)app.close(owner->id,true);
    else {auto edit=candidate->begin_edit();edit.set_title("Tampered");candidate->commit(edit);}
  }
  if(scenario==2)token->cancel();
  const auto before=snapshot_record(owner->snapshot(),owner->history());const auto member_before=app.document_ids();
  const auto active_before=app.active_document_id();
  const auto candidate_before=candidate?snapshot_record(candidate->snapshot(),candidate->history()):"null";
  const std::optional<bool> active_pre=prepared?std::optional<bool>(prepared->active()):std::nullopt;
  const std::optional<bool> registered_pre=candidate?std::optional<bool>(app.contains(candidate->id)):std::nullopt;
  counting->planes.clear();counting->regions.clear();stages.clear();
  std::string error;
  try {
    if(prepared)app.publish(*prepared);
    else app.prepare_axis_range_orientation(item.request,owner->id,control);
  } catch(const std::exception &caught) {error=error_record(caught);}
  require(!error.empty(),"Required SDK rejection was admitted");
  const auto read_events="{\"planes\":"+array_records(counting->planes)+",\"regions\":"+array_records(counting->regions)+'}';
  std::string stage_events="[";for(std::size_t i=0;i<stages.size();++i){if(i)stage_events+=',';stage_events+=stages[i];}stage_events+=']';
  // Frozen kernel events precede these source/raw observations, which may read a lazy source.
  const auto after=snapshot_record(owner->snapshot(),owner->history());
  const auto candidate_after=candidate?snapshot_record(candidate->snapshot(),candidate->history()):"null";
  require(before==after && member_before==app.document_ids() && active_before==app.active_document_id() && candidate_before==candidate_after,
          "Rejected preparation/publication changed current source/candidate/membership/active state");
  if(prepared)require(prepared->active() && !app.contains(candidate->id),"Rejected prepared owner was consumed or adopted");
  return "{\"case_id\":"+quote(item.id)+",\"captured_request\":"+request_json(item.request)+
      ",\"captured_source\":"+captured_record+",\"before\":"+before+",\"after\":"+after+
      ",\"member_ids_before\":"+numeric_array(member_before)+",\"member_ids_after\":"+numeric_array(app.document_ids())+
      ",\"active_id_before\":"+std::to_string(active_before)+",\"active_id_after\":"+std::to_string(app.active_document_id())+
      ",\"prepared_active_before\":"+optional_bool(active_pre)+",\"prepared_active_after\":"+optional_bool(prepared?std::optional<bool>(prepared->active()):std::nullopt)+
      ",\"candidate_before\":"+candidate_before+",\"candidate_after\":"+candidate_after+
      ",\"candidate_registered_before\":"+optional_bool(registered_pre)+",\"candidate_registered_after\":"+
      optional_bool(candidate?std::optional<bool>(app.contains(candidate->id)):std::nullopt)+','+error+
      ",\"read_events\":"+read_events+",\"stage_events\":"+stage_events+'}';
}
std::string admission_refusal_row(int scenario) {
  Case item=scenario==0?
      Case{"owned.yz.u16.s2","owned",PixelType::UInt16,2,{2,2,4,5,7,2},request(Plane::YZ,1,6,2,0,1,true,false,true)}:
      Case{"owned.xy.f32.s5","owned",PixelType::Float32,5,{2,2,4,5,7,5},request(Plane::XY,0,3,2,1,1,false,true,true)};
  auto counting=std::make_shared<CountingSource>(item);
  const Case sentinel{item.id+" sentinel","owned",PixelType::Float64,1,{2,2,4,5,7,1},request(Plane::XY,0,3,2,1,1,true,true,true)};
  auto owner=annotated(source(sentinel),sentinel.id);Application app;app.adopt(owner);
  const auto before=snapshot_record(owner->snapshot(),owner->history());
  const auto members_before=app.document_ids();const auto active_before=app.active_document_id();
  std::optional<Dataset> admitted;std::size_t orientation_invocations=0;std::string error;
  try {
    admitted.emplace(counting,source_calibration(item));
    ++orientation_invocations;
    reslice_oriented_axis_range(*admitted,item.request);
  } catch(const std::exception &caught) {error=error_record(caught);}
  const auto read_events="{\"planes\":"+array_records(counting->planes)+",\"regions\":"+array_records(counting->regions)+'}';
  const auto after=snapshot_record(owner->snapshot(),owner->history());
  require(!error.empty() && !admitted && !orientation_invocations && counting->planes.empty() && counting->regions.empty(),
          "Unsupported S2/S5 did not reject Dataset admission before orientation or source reads");
  require(before==after && members_before==app.document_ids() && active_before==app.active_document_id(),
          "Unsupported sample admission changed existing source/app state");
  return "{\"case_id\":"+quote(item.id)+",\"captured_request\":"+request_json(item.request)+
      ",\"attempted_shape\":"+numeric_array(counting->shape().dimensions())+
      ",\"attempted_pixel_type\":"+quote(type_name(counting->pixel_type()))+
      ",\"attempted_sample_policy\":\"FiniteOnly\",\"admission_phase\":\"Dataset(PlaneSource, SpatialCalibration)\""+
      ",\"constructed_dataset\":"+boolean(admitted.has_value())+",\"orientation_invocation_count\":"+std::to_string(orientation_invocations)+
      ",\"source_before\":"+before+",\"source_after\":"+after+",\"member_ids_before\":"+numeric_array(members_before)+
      ",\"member_ids_after\":"+numeric_array(app.document_ids())+",\"active_id_before\":"+std::to_string(active_before)+
      ",\"active_id_after\":"+std::to_string(app.active_document_id())+",\"read_events\":"+read_events+','+error+'}';
}
} // namespace
int main(int argc,char **argv) {
  try {
    require(argc==3 && std::string(argv[1])=="--output-dir","Usage: axis_range_orientation --output-dir NEW_DIRECTORY; no expected/reference input accepted");
    output_directory=std::filesystem::absolute(argv[2]).lexically_normal();
    require(!std::filesystem::exists(output_directory),"Output directory must be fresh");
    for(auto parent=output_directory.parent_path();!parent.empty() && parent!=parent.root_path();parent=parent.parent_path())
      require(!std::filesystem::is_symlink(std::filesystem::symlink_status(parent)),"Output ancestor is aliased");
    std::filesystem::create_directories(output_directory/"raw");
    std::string index="{\"schema_version\":1,\"context\":\"cpp_api\",\"requirement_id\":\"G029.axis_range_orientation\",\"reference_cases\":[";
    auto append=[&](const std::string &row){require(row.size()<=max_index-index.size(),"Compact SDK index exceeds2MiB");index+=row;};
    bool first=true;for(const auto &item:reference_cases()){if(!first)append(",");first=false;append(successful_row(item));}
    append("],\"owned_cases\":[");first=true;
    for(const auto &item:owned_cases()){if(!first)append(",");first=false;append(successful_row(item));}
    append("],\"rejection_cases\":[");for(int i=0;i<8;++i){if(i)append(",");append(rejection_row(i));}
    append("],\"admission_refusals\":[");for(int i=0;i<2;++i){if(i)append(",");append(admission_refusal_row(i));}
    append("],\"bounds\":{\"seconds\":120,\"index_bytes\":2097152,\"raw_bytes\":16777216},\"raw_member_count\":"+
        std::to_string(raw_members.size())+",\"raw_bytes_written\":"+std::to_string(raw_bytes_written)+
        ",\"scope\":\"96 finite ImageJ reference pixels/shape/type/S/in-plane scalar spacing; independent owned affine/time/XY/type/S/quiet-policy and public admission/cancellation/publication checks. No GUI/fullG029/resource/release claim.\"}");
    deadline();std::ofstream stream(output_directory/"index.json",std::ios::binary|std::ios::out);
    require(bool(stream),"Cannot create compact SDK index");stream.write(index.data(),std::streamsize(index.size()));stream.close();
    require(bool(stream),"Cannot write complete compact SDK index");
    std::cout<<"{\"index_path\":"<<quote((output_directory/"index.json").string())<<",\"index_bytes\":"<<index.size()<<
        ",\"raw_member_count\":"<<raw_members.size()<<",\"raw_bytes\":"<<raw_bytes_written<<"}\n";
    require(bool(std::cout),"Cannot emit compact index descriptor");return 0;
  }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}

