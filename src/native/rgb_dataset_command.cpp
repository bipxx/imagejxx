#include <imagejxx/rgb_dataset_command.hpp>
#include <imagejxx/planar_annotations.hpp>
#include <imagejxx/planar_display_annotations.hpp>
#include <imagejxx/manual_threshold.hpp>
#include "rgb_dataset_internal.hpp"
#include "legacy_rgb_palette.hpp"
#include <imagejxx/indexed_palette.hpp>
#include "workflow_stage_metadata.hpp"
#include "axis_command_record.hpp"
#include "imagej_roi_import_internal.hpp"
#include "session_metadata.hpp"
#include "kernel_control.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace imagejxx {
namespace {
namespace wire = detail::axis_command_record;
namespace codec = detail::session_metadata;
constexpr const char *schema = "imagejxx.rgb-dataset-command";
struct Budget {
  std::size_t cap, used=0;
  void add(std::size_t n) { if(n>cap-used)throw std::invalid_argument("RGB dataset working-byte limit");used+=n; }
  void product(std::size_t n,std::size_t bytes) { if(n && bytes>(cap-used)/n)throw std::invalid_argument("RGB dataset working-byte limit");add(n*bytes); }
  void record(const MetadataObject &v) { add(detail::imagej_roi_record_bytes(v,cap-used)); }
};
MetadataValue dimensions(DatasetShape s) { MetadataArray a;for(int n:s.dimensions())a.emplace_back(n);return metadata_array(std::move(a)); }
MetadataValue pair_record(std::array<double,2> p) { return metadata_array({p[0],p[1]}); }
std::array<double,2> read_pair(const MetadataValue &v) {
  const auto &a=wire::array_value(v);if(a.size()!=2)throw std::invalid_argument("RGB range requires two endpoints");
  std::array<double,2> r{wire::number_value(a[0]),wire::number_value(a[1])};
  if(r[0]>r[1])throw std::invalid_argument("RGB range must be ordered");
  return r;
}
std::size_t metadata_bytes(const DocumentSnapshot &s,std::size_t cap) {
  Budget b{cap};const auto &d=s.data();b.record(d.metadata.properties);b.record(d.metadata.extensions);
  b.record(d.metadata.record_layout);b.record(s.display().properties);b.add(d.metadata.title.size());
  auto selection=[&](const Selection &v){b.add(sizeof(Selection));b.product(v.points.size(),sizeof(v.points[0]));b.add(v.packed_mask().size());b.product(v.path_commands().size(),sizeof(double));b.record(v.attributes);};
  if(d.selection)selection(*d.selection);
  if(d.previous_selection)selection(*d.previous_selection);
  for(const auto &r:d.rois){b.add(sizeof(Roi)+r.name.size());selection(r.selection);b.record(r.attributes);}
  for(const auto &o:d.overlays)b.record(o);
  return b.used;
}
const char *mode_name(RgbDatasetMode m) {
  if(m==RgbDatasetMode::PerPlane)return "per_plane";
  if(m==RgbDatasetMode::Composite)return "composite";
  throw std::invalid_argument("Unknown RGB dataset mode");
}
MetadataObject layout_record(const RgbDatasetLayout &l) {
  return {{"shape",dimensions(l.shape)},{"pixel_type",std::string("uint8")},
          {"output_bytes",std::uint64_t(l.output_bytes)},{"working_bytes",std::uint64_t(l.working_bytes)}};
}
MetadataObject request_record(const RgbDatasetRequest &r,const wire::SourceRecord &s) {
  MetadataValue channels,range,lut;
  if(r.channels)channels=metadata_object(encode_channel_display(*r.channels,s.shape,s.pixel_type,s.sample_policy));
  if(r.display_range)range=pair_record(*r.display_range);
  if(r.lut){MetadataArray rows;for(const auto &rgb:*r.lut)rows.push_back(metadata_array({int(rgb[0]),int(rgb[1]),int(rgb[2])}));lut=metadata_array(std::move(rows));}
  return {{"mode",std::string(mode_name(r.mode))},{"destination",std::string(r.destination==OutputDestination::InPlace?"in_place":"new_document")},
          {"channels",std::move(channels)},{"display_range",std::move(range)},{"lut",std::move(lut)},
          {"max_working_bytes",std::uint64_t(r.max_working_bytes)}};
}
bool is_null(const MetadataValue &v){return std::holds_alternative<std::monostate>(v);}
MetadataValue view_position(const DocumentSnapshot &s) {
  const auto &v=s.display();return metadata_array({v.t,v.c,v.crosshair[2]});
}
struct Decoded {wire::SourceRecord source;RgbDatasetRequest request;RgbDatasetLayout layout;std::uint64_t selection,display;std::size_t metadata;};
Decoded decode(const MetadataObject &record) {
  (void)detail::imagej_roi_record_bytes(record,16*1024*1024);
  wire::exact_fields(record,{"schema","version","source","selection_revision","display_revision","view_position","request","layout","metadata_bytes","policy"});
  if(wire::text_value(record.at("schema"))!=schema || wire::unsigned_value(record.at("version"))!=1 ||
     wire::text_value(record.at("policy"))!="spatial-preserved-all-channel-annotations-to-c0")
    throw std::invalid_argument("Unsupported RGB dataset command schema/policy");
  auto s=wire::read_source(wire::object_value(record.at("source")));
  const auto &position=wire::array_value(record.at("view_position"));
  if(position.size()!=3)throw std::invalid_argument("RGB view position requires T/C/Z");
  const int limits[]{s.shape.t,s.shape.c,s.shape.z};
  for(int i=0;i<3;++i)if(wire::unsigned_value(position[i])>=std::uint64_t(limits[i]))throw std::invalid_argument("RGB view position is outside source");
  const auto &r=wire::object_value(record.at("request"));
  wire::exact_fields(r,{"mode","destination","channels","display_range","lut","max_working_bytes"});
  RgbDatasetRequest request;const auto &mode=wire::text_value(r.at("mode"));
  if(mode=="per_plane")request.mode=RgbDatasetMode::PerPlane;
  else if(mode=="composite")request.mode=RgbDatasetMode::Composite;
  else throw std::invalid_argument("Unknown RGB dataset mode");
  const auto &dest=wire::text_value(r.at("destination"));
  if(dest=="in_place")request.destination=OutputDestination::InPlace;
  else if(dest=="new_document")request.destination=OutputDestination::NewDocument;
  else throw std::invalid_argument("Unknown RGB dataset destination");
  request.max_working_bytes=wire::size_value(r.at("max_working_bytes"));
  // Admit inline C-state storage before parsing/copying a channel table.
  Budget state{request.max_working_bytes};state.product(s.shape.c,2*sizeof(ChannelDisplayChannel));
  if(!is_null(r.at("channels")))request.channels=parse_channel_display(wire::object_value(r.at("channels")),s.shape,s.pixel_type,s.sample_policy);
  if(!is_null(r.at("display_range")))request.display_range=read_pair(r.at("display_range"));
  if(!is_null(r.at("lut"))) {
    const auto &rows=wire::array_value(r.at("lut"));if(rows.size()!=256)throw std::invalid_argument("RGB LUT must have256 entries");
    request.lut.emplace();for(std::size_t i=0;i<rows.size();++i){const auto &v=wire::array_value(rows[i]);if(v.size()!=3)throw std::invalid_argument("RGB LUT entry width");
      for(int c=0;c<3;++c){auto n=wire::unsigned_value(v[c]);if(n>255)throw std::invalid_argument("RGB LUT component range");(*request.lut)[i][c]=static_cast<unsigned char>(n);}}
  }
  auto layout=describe_rgb_dataset(s.shape,s.pixel_type,s.sample_policy,request);
  const auto metadata=wire::size_value(record.at("metadata_bytes"));Budget budget{request.max_working_bytes,layout.working_bytes};budget.product(metadata,8);layout.working_bytes=budget.used;
  if(!metadata_equal(wire::object_value(record.at("layout")),layout_record(layout)))throw std::invalid_argument("RGB derived layout differs from its request");
  return {s,std::move(request),layout,wire::unsigned_value(record.at("selection_revision")),wire::unsigned_value(record.at("display_revision")),metadata};
}
std::optional<int> overlay_channel(const codec::Json &row,int count) {
  if(!row.contains("c")||row.at("c").is_null())return {};
  const auto &v=row.at("c");if(!v.is_number_integer()||v<0||v>=count)throw std::invalid_argument("RGB overlay channel is outside source");return v.get<int>();
}
}
RgbDatasetLayout describe_rgb_dataset(DatasetShape shape,PixelType type,FloatingSamplePolicy policy,const RgbDatasetRequest &r) {
  shape.validate();(void)mode_name(r.mode);
  if(!r.max_working_bytes || (r.destination!=OutputDestination::InPlace && r.destination!=OutputDestination::NewDocument))throw std::invalid_argument("Invalid RGB dataset limit/destination");
  if(type!=PixelType::UInt8 && type!=PixelType::UInt16 && type!=PixelType::Float32 && type!=PixelType::Float64)throw std::invalid_argument("Invalid RGB dataset pixel type");
  if(policy!=FloatingSamplePolicy::FiniteOnly && (policy!=FloatingSamplePolicy::PreserveQuietNaNFloat32 || type!=PixelType::Float32 || shape.s!=1))throw std::invalid_argument("Invalid RGB source sample policy");
  if(r.mode==RgbDatasetMode::Composite) {
    if(shape.s!=1 || type==PixelType::Float64 || r.display_range)throw std::invalid_argument("Composite RGB requires scalar U8/U16/F32 and channel-state range options");
  } else if(shape.s!=1 && (type!=PixelType::UInt8 || (shape.s!=3&&shape.s!=4) || r.channels || r.display_range || r.lut))throw std::invalid_argument("Per-plane color conversion requires UInt8 RGB/RGBA without scalar options");
  if(r.channels && (r.display_range||(r.lut && r.mode!=RgbDatasetMode::Composite)))throw std::invalid_argument("RGB channel state and global scalar options are exclusive");
  if(r.display_range && (!std::isfinite((*r.display_range)[0])||!std::isfinite((*r.display_range)[1])||(*r.display_range)[0]>(*r.display_range)[1]))throw std::invalid_argument("RGB display range must be finite and ordered");
  if(r.channels)validate_channel_display(*r.channels,shape,type,policy);
  auto output=shape;output.s=3;if(r.mode==RgbDatasetMode::Composite)output.c=1;
  Budget budget{r.max_working_bytes};budget.add(65536);budget.product(shape.pixel_count(),sizeof(double));
  Budget backing{r.max_working_bytes};backing.product(output.pixel_count(),sizeof(double));
  budget.product(backing.used,3);budget.product(std::size_t(shape.x)*shape.y,5*sizeof(double));
  budget.product(output.plane_count(),512);budget.product(shape.c,2*sizeof(ChannelDisplayChannel));
  // Deferred composite palettes are a new V1 request shape. Preserve existing
  // PerPlane record estimates while admitting both copies of this inline LUT.
  if(r.mode==RgbDatasetMode::Composite && r.lut)budget.product(128*1024,2);
  if(r.channels) {
    Budget record{16*1024*1024};record.add(8192);
    for(const auto &channel:r.channels->channels){const auto bytes=channel.lut.table?128ULL*1024:4096ULL;record.add(bytes);budget.product(bytes,2);}
  }
  return {output,backing.used,budget.used};
}
ResolvedRgbDatasetCommand::ResolvedRgbDatasetCommand(DocumentSnapshot s,RgbDatasetRequest r,RgbDatasetLayout l,MetadataObject record)
  :source_(std::move(s)),request_(std::move(r)),layout_(l),record_(std::move(record)){}
ResolvedRgbDatasetCommand resolve_rgb_dataset_command(const DocumentSnapshot &source,const RgbDatasetRequest &request) {
  const auto &dataset=source.data().dataset;auto layout=describe_rgb_dataset(dataset.shape(),dataset.pixel_type(),dataset.sample_policy(),request);
  const auto metadata=metadata_bytes(source,request.max_working_bytes-layout.working_bytes);
  Budget budget{request.max_working_bytes,layout.working_bytes};budget.product(metadata,8);layout.working_bytes=budget.used;
  auto effective=request;
  if(dataset.shape().s==1) {
    const auto stored=recognized_channel_display(source.display(),dataset);
    if(!effective.channels && !effective.display_range && (!effective.lut || effective.mode==RgbDatasetMode::Composite)) {
      if(stored)effective.channels=stored;
      else if(effective.mode==RgbDatasetMode::PerPlane) {
        const auto found=source.display().properties.find("display_range");if(found!=source.display().properties.end())effective.display_range=read_pair(found->second);
      } else {
        const auto found=source.display().properties.find("display_range");
        if(found!=source.display().properties.end()) {
          ChannelDisplayState state;state.mode=dataset.shape().c>8?ChannelDisplayMode::Color:ChannelDisplayMode::Composite;
          const auto range=read_pair(found->second);const char *names[]{"Red","Green","Blue","White","Cyan","Magenta","Yellow","White"};
          state.channels.resize(dataset.shape().c);
          for(int c=0;c<dataset.shape().c;++c){state.channels[c].range=range;state.channels[c].lut.name=dataset.shape().c==1?"Grays":names[c%8];}
          effective.channels=std::move(state);
        }
      }
    }
    if(source.data().metadata.extensions.count(indexed_palette_extension) && !effective.lut && (!effective.channels || (effective.mode==RgbDatasetMode::Composite && !request.channels && !stored))){
      if(const auto current=detail::legacy_rgb_palette(source.display().properties))effective.lut=*current;
      else if(const auto stored_palette=indexed_palette(source.data()))effective.lut=stored_palette->palette;
    }
  } else if(dataset.shape().s!=3 && source.display().properties.count("channel_display"))
    (void)recognized_channel_display(source.display(),dataset);
  if(effective.mode==RgbDatasetMode::Composite && effective.lut && effective.channels) {
    // Bound inline table expansion before mutating/copying per-C LUT storage.
    Budget record_budget{16*1024*1024};record_budget.add(8192);
    record_budget.product(effective.channels->channels.size(),128*1024);
    Budget working{effective.max_working_bytes,layout.working_bytes};
    working.product(effective.channels->channels.size(),2*128*1024);
    for(auto &channel:effective.channels->channels){channel.lut.name.clear();channel.lut.table=*effective.lut;}
    if(effective.channels->mode==ChannelDisplayMode::Grayscale ||
       (effective.channels->mode==ChannelDisplayMode::Composite && dataset.shape().c==1)) {
      effective.channels->mode=ChannelDisplayMode::Color;
      effective.channels->projection=ChannelDisplayProjection::Sum;
    }
    effective.lut.reset();
  }
  // Captured display state can add a table absent from the caller's request.
  auto captured_layout=describe_rgb_dataset(dataset.shape(),dataset.pixel_type(),dataset.sample_policy(),effective);
  Budget captured{effective.max_working_bytes,captured_layout.working_bytes};captured.product(metadata,8);layout.working_bytes=captured.used;
  auto s=wire::source_metadata(source);
  MetadataObject record{{"schema",std::string(schema)},{"version",1},{"source",metadata_object(wire::source_record(s))},
      {"selection_revision",source.revisions().selection},{"display_revision",source.revisions().display},
      {"view_position",view_position(source)},
      {"request",metadata_object(request_record(effective,s))},{"layout",metadata_object(layout_record(layout))},
      {"metadata_bytes",std::uint64_t(metadata)},{"policy",std::string("spatial-preserved-all-channel-annotations-to-c0")}};
  (void)detail::imagej_roi_record_bytes(record,16*1024*1024);
  return {source,std::move(effective),layout,std::move(record)};
}
void validate_rgb_dataset_command_record(const MetadataObject &record){(void)decode(record);}
ResolvedRgbDatasetCommand rgb_dataset_command_from_record(const DocumentSnapshot &source,const MetadataObject &record) {
  const auto d=decode(record);const auto actual=wire::source_metadata(source);
  if(!metadata_equal(wire::source_record(actual),wire::source_record(d.source))||source.revisions().selection!=d.selection||source.revisions().display!=d.display)throw RevisionConflict();
  auto result=resolve_rgb_dataset_command(source,d.request);
  if(!metadata_equal(result.record(),record))throw std::invalid_argument("RGB command binding or metadata estimate differs from source");
  return result;
}
ResolvedRgbDatasetCommand rebind_rgb_dataset_command(const DocumentSnapshot &source,const MetadataObject &record,const RgbDatasetRequest &request){(void)decode(record);return resolve_rgb_dataset_command(source,request);}

namespace {
PreparedAxisRemap rgb_metadata(const ResolvedRgbDatasetCommand &command,Dataset replacement) {
  const auto &source=command.source();const auto &r=command.request();const auto &data=source.data();const auto shape=data.dataset.shape();const auto output=command.layout().shape;
  auto tx=source.begin_edit(Change::All);auto display=source.display();
  if(r.destination==OutputDestination::NewDocument)tx.set_title(data.metadata.title+" RGB");
  if(shape.s==3 && data.dataset.pixel_type()==PixelType::UInt8 && r.mode==RgbDatasetMode::PerPlane)return {std::move(tx),std::move(display)};
  tx.replace_dataset(std::move(replacement));
  const bool collapse=shape.c!=output.c;
  for(const auto *key:{"is_stack","labels"}) {
    const auto property=data.metadata.properties.find(key),extension=data.metadata.extensions.find(key);
    const bool value=std::string(key)=="is_stack" && output.plane_count()>1;
    if(property!=data.metadata.properties.end() && std::holds_alternative<bool>(property->second))tx.set_property(key,value);
    if(extension!=data.metadata.extensions.end() && std::holds_alternative<bool>(extension->second))tx.set_extension(key,value);
  }
  auto annotations=planar_annotations(data);for(auto &row:annotations)if(collapse && row.c)row.c=0;
  auto layers=planar_display_annotations(data);for(auto &row:layers)if(collapse && row.c)row.c=0;
  if(data.metadata.extensions.count(planar_annotations_extension))tx.set_extension(planar_annotations_extension,encode_planar_annotations(output,annotations));
  if(data.metadata.extensions.count(planar_display_annotations_extension))tx.set_extension(planar_display_annotations_extension,encode_planar_display_annotations(output,layers));
  if(collapse) {
    auto rois=data.rois;for(auto &row:rois)if(row.c)row.c=0;tx.set_rois(std::move(rois));
    auto overlays=data.overlays;for(auto &row:overlays){auto json=codec::to_json(row);if(overlay_channel(json,shape.c))json["c"]=0;row=codec::object_from_json(json);}tx.set_overlays(std::move(overlays));
    MetadataObject originals;
    auto labels=[&](const MetadataObject &input,bool properties) {
      MetadataObject retained;
      for(const auto *key:{"plane_labels","channel_labels","channel_names"}) {
        const auto found=input.find(key);if(found==input.end())continue;
        const auto array=codec::to_json(found->second);const bool planes=std::string(key)=="plane_labels";
        if(!array.is_array()||array.size()!=(planes?shape.plane_count():std::size_t(shape.c)))throw std::invalid_argument("RGB labels must match the source extent");
        for(const auto &v:array)if(!v.is_null()&&!v.is_string())throw std::invalid_argument("RGB labels require strings or null");
        codec::Json result=codec::Json::array();
        if(planes)for(int t=0;t<shape.t;++t)for(int z=0;z<shape.z;++z){std::string label;bool present=false;for(int c=0;c<shape.c;++c){const auto &v=array[(std::size_t(t)*shape.c+c)*shape.z+z];if(v.is_string()){if(present)label+=" | ";label+=v.get<std::string>();present=true;}}result.push_back(present?codec::Json(label):codec::Json(nullptr));}
        else result.push_back("RGB");
        retained[key]=found->second;if(properties)tx.set_property(key,codec::from_json(result));else tx.set_extension(key,codec::from_json(result));
      }
      originals[properties?"properties":"extensions"]=metadata_object(std::move(retained));
    };
    labels(data.metadata.properties,true);labels(data.metadata.extensions,false);
    MetadataObject provenance{{"schema",std::string("imagejxx.rgb-source-labels")},{"version",1},{"source_shape",dimensions(shape)},{"labels",metadata_object(std::move(originals))}};
    const auto prior=data.metadata.extensions.find("imagejxx.rgb_conversion");if(prior!=data.metadata.extensions.end())provenance["previous"]=prior->second;
    tx.set_extension("imagejxx.rgb_conversion",metadata_object(std::move(provenance)));
  }
  tx.set_spectrum({});
  if(collapse)display.c=0;
  display.properties.erase("channel_display");display.properties.erase("display_range");reset_manual_threshold_state(display);
  display.normalize(output);tx.set_axis_remap_display(display);
  return {std::move(tx),std::move(display)};
}
}
RgbDatasetRequest detail::rgb_dataset_request_from_record(const MetadataObject &record){return decode(record).request;}
DocumentSnapshot detail::rgb_dataset_metadata_preview(const ResolvedRgbDatasetCommand &command) {
  auto metadata=rgb_metadata(command,workflow_metadata_dataset(command.layout().shape,PixelType::UInt8,command.source().data().dataset.calibration()));
  return metadata.transaction.preview();
}
PreparedAxisRemap detail::prepare_rgb_dataset_edit(const ResolvedRgbDatasetCommand &command,const ExecutionControl &control,std::size_t cap) {
  if(!cap || command.layout().working_bytes>cap)throw std::invalid_argument("RGB execution working-byte limit");
  ExecutionScope scope(control);kernel_checkpoint();
  const auto &source=command.source();const auto &r=command.request();const auto &data=source.data();const auto shape=data.dataset.shape();const auto output=command.layout().shape;
  auto metadata=rgb_metadata(command,workflow_metadata_dataset(output,PixelType::UInt8,data.dataset.calibration()));
  if(shape.s==3 && data.dataset.pixel_type()==PixelType::UInt8 && r.mode==RgbDatasetMode::PerPlane)return metadata;
  std::optional<ChannelDisplayState> channels=r.channels;
  ChannelDisplayLimits limits{std::size_t(shape.x)*shape.y,command.layout().working_bytes};
  if(r.mode==RgbDatasetMode::Composite && !channels)channels=default_channel_display(data.dataset,source.display().t,source.display().crosshair[2],ChannelDisplayMode::Composite,limits,control);
  if(r.mode==RgbDatasetMode::Composite && r.lut) {
    for(auto &channel:channels->channels){channel.lut.name.clear();channel.lut.table=*r.lut;}
    if(channels->mode==ChannelDisplayMode::Grayscale ||
       (channels->mode==ChannelDisplayMode::Composite && shape.c==1)) {
      channels->mode=ChannelDisplayMode::Color;
      channels->projection=ChannelDisplayProjection::Sum;
    }
  }
  Stack planes;planes.reserve(output.plane_count());
  for(int t=0;t<output.t;++t)for(int c=0;c<output.c;++c)for(int z=0;z<output.z;++z) {
    kernel_checkpoint();
    if(r.mode==RgbDatasetMode::Composite)planes.push_back(compose_channel_display(data.dataset,*channels,Plane::XY,z,t,source.display().c,{},limits,control));
    else {
      auto range=r.display_range;auto lut=r.lut;
      if(channels){range=channels->channels[c].range;
        if(channels->mode==ChannelDisplayMode::Grayscale)lut=channel_display_lut("Grays");
        else if(channels->channels[c].lut.table)lut=*channels->channels[c].lut.table;
        else lut=channel_display_lut(channels->channels[c].lut.name);
      }
      planes.push_back(convert_rgb(*data.dataset.plane({t,c,z}),range,lut));
    }
    if(control.progress)control.progress(planes.size(),output.plane_count());
  }
  metadata.transaction.replace_dataset(Dataset::from_planes(output,std::move(planes),data.dataset.calibration()));
  kernel_checkpoint();return metadata;
}
} // namespace imagejxx
