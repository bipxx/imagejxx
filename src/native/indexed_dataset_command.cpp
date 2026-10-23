#include "indexed_dataset_internal.hpp"
#include "axis_command_record.hpp"
#include "workflow_stage_metadata.hpp"
#include "imagej_roi_import_internal.hpp"
#include "session_metadata.hpp"
#include "kernel_control.hpp"
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/planar_annotations.hpp>
#include <imagejxx/planar_display_annotations.hpp>
#include <algorithm>
#include <limits>
namespace imagejxx {
namespace {
namespace wire=detail::axis_command_record;
namespace codec=detail::session_metadata;
constexpr const char *algorithm="imagej154r-median-cut-bin-offset-5";
struct Budget {
  std::size_t cap,used=0;
  void add(std::size_t n){if(n>cap-used)throw std::invalid_argument("Indexed dataset working-byte limit");used+=n;}
  void product(std::size_t n,std::size_t bytes){if(n&&bytes>(cap-used)/n)throw std::invalid_argument("Indexed dataset working-byte limit");add(n*bytes);}
  void record(const MetadataObject &r){add(detail::imagej_roi_record_bytes(r,cap-used));}
};
MetadataValue dimensions(DatasetShape s){MetadataArray a;for(int n:s.dimensions())a.emplace_back(n);return metadata_array(std::move(a));}
MetadataObject layout_record(const IndexedDatasetLayout &l){return {{"shape",dimensions(l.shape)},{"pixel_type",std::string("uint8")},{"output_bytes",std::uint64_t(l.output_bytes)},{"working_bytes",std::uint64_t(l.working_bytes)}};}
MetadataObject request_record(const IndexedDatasetRequest &r){return {{"max_colors",r.max_colors},{"destination",std::string(r.destination==OutputDestination::InPlace?"in_place":"new_document")},{"max_working_bytes",std::uint64_t(r.max_working_bytes)}};}
IndexedDatasetRequest read_request(const MetadataObject &r){
  wire::exact_fields(r,{"max_colors","destination","max_working_bytes"});IndexedDatasetRequest out;out.max_colors=wire::index_value(r.at("max_colors"),true);out.max_working_bytes=wire::size_value(r.at("max_working_bytes"));
  const auto &name=wire::text_value(r.at("destination"));if(name=="in_place")out.destination=OutputDestination::InPlace;else if(name=="new_document")out.destination=OutputDestination::NewDocument;else throw std::invalid_argument("Unknown indexed destination");return out;
}
std::size_t metadata_bytes(const DocumentSnapshot &source,std::size_t cap){
  Budget b{cap};const auto &d=source.data();b.add(65536);b.add(d.metadata.title.size());b.record(d.metadata.properties);b.record(d.metadata.extensions);b.record(d.metadata.record_layout);b.record(source.display().properties);
  auto selection=[&](const Selection &v){b.add(sizeof(Selection));b.product(v.points.size(),sizeof(v.points[0]));b.add(v.packed_mask().size());b.product(v.path_commands().size(),sizeof(double));b.record(v.attributes);};
  if(d.selection)selection(*d.selection);if(d.previous_selection)selection(*d.previous_selection);
  for(const auto &row:d.rois){b.add(sizeof(Roi)+row.name.size());selection(row.selection);b.record(row.attributes);}for(const auto &row:d.overlays)b.record(row);
  Budget result{cap};result.product(b.used,16);return result.used;
}
struct Decoded {wire::SourceRecord source;IndexedDatasetRequest request;IndexedDatasetLayout layout;std::uint64_t selection,display;};
Decoded decode(const MetadataObject &record){
  (void)detail::imagej_roi_record_bytes(record,16*1024*1024);
  wire::exact_fields(record,{"schema","version","source","selection_revision","display_revision","view_position","request","layout","metadata_bytes","policy","algorithm"});
  if(wire::text_value(record.at("schema"))!="imagejxx.indexed-dataset-command"||wire::unsigned_value(record.at("version"))!=1||wire::text_value(record.at("policy"))!="global-tcz-preserve-axes-paired-palette-v1"||wire::text_value(record.at("algorithm"))!=algorithm)throw std::invalid_argument("Unsupported indexed command schema/policy/algorithm");
  auto source=wire::read_source(wire::object_value(record.at("source")));const auto &position=wire::array_value(record.at("view_position"));if(position.size()!=3)throw std::invalid_argument("Indexed pose requires T/C/Z");
  const int extents[]{source.shape.t,source.shape.c,source.shape.z};for(int i=0;i<3;++i)if(wire::unsigned_value(position[i])>=std::uint64_t(extents[i]))throw std::invalid_argument("Indexed pose outside source");
  auto request=read_request(wire::object_value(record.at("request")));auto layout=describe_indexed_dataset(source.shape,source.pixel_type,source.sample_policy,request);Budget b{request.max_working_bytes,layout.working_bytes};b.add(wire::size_value(record.at("metadata_bytes")));layout.working_bytes=b.used;
  if(!metadata_equal(wire::object_value(record.at("layout")),layout_record(layout)))throw std::invalid_argument("Indexed derived layout differs from request");
  return {source,request,layout,wire::unsigned_value(record.at("selection_revision")),wire::unsigned_value(record.at("display_revision"))};
}
PreparedAxisRemap metadata(const ResolvedIndexedDatasetCommand &command,Dataset replacement,const IndexedPalette &palette){
  const auto &source=command.source();const auto &data=source.data();const auto shape=command.layout().shape;auto edit=source.begin_edit(Change::All);edit.replace_dataset(std::move(replacement));edit.set_spectrum({});
  if(command.request().destination==OutputDestination::NewDocument)edit.set_title(data.metadata.title+" Indexed");
  if(data.metadata.extensions.count(planar_annotations_extension))edit.set_extension(planar_annotations_extension,encode_planar_annotations(shape,planar_annotations(data)));
  if(data.metadata.extensions.count(planar_display_annotations_extension))edit.set_extension(planar_display_annotations_extension,encode_planar_display_annotations(shape,planar_display_annotations(data)));
  const auto encoded=encode_indexed_palette(palette);edit.set_extension(indexed_palette_extension,metadata_object(encoded));
  auto original_display=codec::view_json(source.display());original_display["properties"]=codec::to_json(source.display().properties);original_display["t"]=source.display().t;original_display["c"]=source.display().c;
  edit.set_extension("imagejxx.indexed_color_source",metadata_object({{"schema",std::string("imagejxx.indexed-color-source")},{"version",1},{"command",metadata_object(command.record())},{"original_metadata",metadata_object({{"title",data.metadata.title},{"properties",metadata_object(data.metadata.properties)},{"extensions",metadata_object(data.metadata.extensions)},{"record_layout",metadata_object(data.metadata.record_layout)}})},{"original_display",codec::from_json(original_display)}}));
  for(const auto *key:{"labels"}){
    const auto property=data.metadata.properties.find(key),extension=data.metadata.extensions.find(key);
    if(property!=data.metadata.properties.end()&&std::holds_alternative<bool>(property->second))edit.set_property(key,false);
    if(extension!=data.metadata.extensions.end()&&std::holds_alternative<bool>(extension->second))edit.set_extension(key,false);
  }
  auto display=source.display();display.properties.erase("channel_display");display.properties.erase("lut");display.properties.erase("labels");reset_manual_threshold_state(display);
  display.properties["display_range"]=metadata_array({0.,255.});display.properties["custom_lut"]=encoded.at("palette");display.normalize(shape);edit.set_axis_remap_display(display);return {std::move(edit),std::move(display)};
}
}
IndexedDatasetLayout describe_indexed_dataset(DatasetShape shape,PixelType type,FloatingSamplePolicy policy,const IndexedDatasetRequest &request){
  shape.validate();if(type!=PixelType::UInt8||shape.s!=3||policy!=FloatingSamplePolicy::FiniteOnly)throw std::invalid_argument("Indexed dataset requires finite UInt8 RGB");
  if(request.max_colors<2||request.max_colors>256||!request.max_working_bytes||(request.destination!=OutputDestination::InPlace&&request.destination!=OutputDestination::NewDocument))throw std::invalid_argument("Invalid indexed color limit/destination/budget");
  Budget b{request.max_working_bytes};b.add(indexed_color_working_bytes(shape.x,shape.y,shape.plane_count()));b.product(shape.pixel_count(),sizeof(double));
  auto out=shape;out.s=1;Budget output{request.max_working_bytes};output.product(out.pixel_count(),sizeof(double));b.product(output.used,2);return {out,output.used,b.used};
}
ResolvedIndexedDatasetCommand::ResolvedIndexedDatasetCommand(DocumentSnapshot s,IndexedDatasetRequest r,IndexedDatasetLayout l,MetadataObject record):source_(std::move(s)),request_(r),layout_(l),record_(std::move(record)){}
ResolvedIndexedDatasetCommand resolve_indexed_dataset_command(const DocumentSnapshot &source,const IndexedDatasetRequest &request){
  const auto &d=source.data().dataset;auto layout=describe_indexed_dataset(d.shape(),d.pixel_type(),d.sample_policy(),request);const auto bytes=metadata_bytes(source,request.max_working_bytes-layout.working_bytes);Budget b{request.max_working_bytes,layout.working_bytes};b.add(bytes);layout.working_bytes=b.used;
  const auto &v=source.display();MetadataObject record{{"schema",std::string("imagejxx.indexed-dataset-command")},{"version",1},{"source",metadata_object(wire::source_record(wire::source_metadata(source)))},{"selection_revision",source.revisions().selection},{"display_revision",source.revisions().display},{"view_position",metadata_array({v.t,v.c,v.crosshair[2]})},{"request",metadata_object(request_record(request))},{"layout",metadata_object(layout_record(layout))},{"metadata_bytes",std::uint64_t(bytes)},{"policy",std::string("global-tcz-preserve-axes-paired-palette-v1")},{"algorithm",std::string(algorithm)}};
  (void)detail::imagej_roi_record_bytes(record,16*1024*1024);ResolvedIndexedDatasetCommand result(source,request,layout,std::move(record));
  // Private descriptor preview validates annotation/provenance transforms. Its
  // dummy palette never escapes; the published palette is computed on the worker.
  (void)metadata(result,detail::workflow_metadata_dataset(layout.shape,PixelType::UInt8,d.calibration()),IndexedPalette{}).transaction.preview();return result;
}
void validate_indexed_dataset_command_record(const MetadataObject &record){(void)decode(record);}
ResolvedIndexedDatasetCommand indexed_dataset_command_from_record(const DocumentSnapshot &source,const MetadataObject &record){
  const auto d=decode(record);if(!metadata_equal(wire::source_record(wire::source_metadata(source)),wire::source_record(d.source))||source.revisions().selection!=d.selection||source.revisions().display!=d.display)throw RevisionConflict();
  auto result=resolve_indexed_dataset_command(source,d.request);if(!metadata_equal(result.record(),record))throw std::invalid_argument("Indexed capture differs from source");return result;
}
ResolvedIndexedDatasetCommand rebind_indexed_dataset_command(const DocumentSnapshot &source,const MetadataObject &record,const IndexedDatasetRequest &request){(void)decode(record);return resolve_indexed_dataset_command(source,request);}
IndexedDatasetRequest detail::indexed_dataset_workflow_request(const OperationParameters &parameters,const MetadataObject &original){
  auto captured=decode(original).request;if(parameters.empty())return captured;
  if(parameters.size()!=2||!parameters.count("max_colors")||!parameters.count("max_working_bytes"))throw std::invalid_argument("Indexed workflow intent requires max_colors and max_working_bytes");
  auto record=request_record(captured);
  for(const auto *key:{"max_colors","max_working_bytes"}){
    const auto &v=parameters.at(key);
    if(const auto p=std::get_if<std::uint64_t>(&v))record[key]=*p;
    else if(const auto p=std::get_if<std::int64_t>(&v))record[key]=*p;
    else throw std::invalid_argument("Indexed workflow choices require integers");
  }
  auto result=read_request(record);
  if(result.max_colors<2||result.max_colors>256||!result.max_working_bytes)throw std::invalid_argument("Invalid indexed workflow colors/budget");
  return result;
}
OperationParameters detail::indexed_dataset_workflow_parameters(const IndexedDatasetRequest &r){return {{"max_colors",std::int64_t(r.max_colors)},{"max_working_bytes",std::uint64_t(r.max_working_bytes)}};}
DocumentSnapshot detail::indexed_dataset_metadata_preview(const ResolvedIndexedDatasetCommand &c){return metadata(c,detail::workflow_metadata_dataset(c.layout().shape,PixelType::UInt8,c.source().data().dataset.calibration(),true),IndexedPalette{}).transaction.preview();}
PreparedAxisRemap detail::prepare_indexed_dataset_edit(const ResolvedIndexedDatasetCommand &command,const ExecutionControl &control,std::size_t cap){
  if(command.layout().working_bytes>cap)throw std::invalid_argument("Indexed execution working-byte limit");const auto captured=control;ExecutionScope scope(captured);kernel_checkpoint();
  const auto &source=command.source();const auto &dataset=source.data().dataset;const auto shape=dataset.shape();Stack planes;planes.reserve(shape.plane_count());
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z){kernel_checkpoint();planes.push_back(*dataset.plane({t,c,z}));}
  auto result=quantize_indexed_color(planes,command.request().max_colors,{},cap);kernel_checkpoint();
  IndexedPalette palette{result.palette,result.color_count};auto replacement=Dataset::from_planes(command.layout().shape,std::move(result.indices),dataset.calibration());auto prepared=metadata(command,std::move(replacement),palette);kernel_checkpoint();return prepared;
}
}
