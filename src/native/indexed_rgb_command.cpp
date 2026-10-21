#include "indexed_rgb_internal.hpp"
#include "rgb_dataset_internal.hpp"
#include "axis_command_record.hpp"
#include "imagej_roi_import_internal.hpp"
#include "session_metadata.hpp"
#include "kernel_control.hpp"
#include <algorithm>
namespace imagejxx {
namespace {
namespace wire=detail::axis_command_record;
struct Budget {
  std::size_t cap,used=0;
  void add(std::size_t n){if(n>cap-used)throw std::invalid_argument("Indexed RGB working-byte limit");used+=n;}
  void record(const MetadataObject &r){add(detail::imagej_roi_record_bytes(r,cap-used));}
};
const char *name(IndexedRgbPaletteSource s){if(s==IndexedRgbPaletteSource::Display)return "display";if(s==IndexedRgbPaletteSource::Stored)return "stored";throw std::invalid_argument("Unknown indexed RGB palette source");}
MetadataObject request_record(const IndexedRgbRequest &r){return {{"palette_source",std::string(name(r.palette_source))},{"destination",std::string(r.destination==OutputDestination::InPlace?"in_place":"new_document")},{"max_working_bytes",std::uint64_t(r.max_working_bytes)}};}
IndexedRgbRequest read_request(const MetadataObject &r){
  wire::exact_fields(r,{"palette_source","destination","max_working_bytes"});IndexedRgbRequest out;
  const auto &palette=wire::text_value(r.at("palette_source"));if(palette=="display")out.palette_source=IndexedRgbPaletteSource::Display;else if(palette=="stored")out.palette_source=IndexedRgbPaletteSource::Stored;else throw std::invalid_argument("Unknown indexed RGB palette source");
  const auto &destination=wire::text_value(r.at("destination"));if(destination=="in_place")out.destination=OutputDestination::InPlace;else if(destination=="new_document")out.destination=OutputDestination::NewDocument;else throw std::invalid_argument("Unknown indexed RGB destination");
  out.max_working_bytes=wire::size_value(r.at("max_working_bytes"));if(!out.max_working_bytes)throw std::invalid_argument("Indexed RGB requires positive working limit");return out;
}
MetadataObject layout_record(const RgbDatasetLayout &l){MetadataArray dims;for(int d:l.shape.dimensions())dims.emplace_back(d);return {{"shape",metadata_array(std::move(dims))},{"pixel_type",std::string("uint8")},{"output_bytes",std::uint64_t(l.output_bytes)},{"working_bytes",std::uint64_t(l.working_bytes)}};}
struct Decoded {wire::SourceRecord source;IndexedRgbRequest request;std::uint64_t selection,display;};
Decoded decode(const MetadataObject &r){
  (void)detail::imagej_roi_record_bytes(r,16*1024*1024);wire::exact_fields(r,{"schema","version","source","selection_revision","display_revision","view_position","request","rendering","layout","metadata_bytes","policy"});
  if(wire::text_value(r.at("schema"))!="imagejxx.indexed-rgb-command"||wire::unsigned_value(r.at("version"))!=1||wire::text_value(r.at("policy"))!="current-stage-indexed-palette-rgb-v1")throw std::invalid_argument("Unsupported indexed RGB command");
  auto source=wire::read_source(wire::object_value(r.at("source")));if(source.shape.s!=1||source.pixel_type!=PixelType::UInt8||source.sample_policy!=FloatingSamplePolicy::FiniteOnly)throw std::invalid_argument("Indexed RGB requires finite UInt8 index planes");
  auto request=read_request(wire::object_value(r.at("request")));const auto &rendering=wire::object_value(r.at("rendering"));auto rgb=detail::rgb_dataset_request_from_record(rendering);
  if(rgb.mode!=RgbDatasetMode::PerPlane||rgb.destination!=OutputDestination::InPlace||rgb.max_working_bytes>request.max_working_bytes)throw std::invalid_argument("Indexed RGB nested rendering must be bounded in-place per-plane intent");
  for(const char *field:{"source","selection_revision","display_revision","view_position"})if(!metadata_equal(r.at(field),rendering.at(field)))throw std::invalid_argument("Indexed RGB nested source differs");
  if(request.palette_source==IndexedRgbPaletteSource::Stored&&(!rgb.lut||rgb.channels||!rgb.display_range||*rgb.display_range!=std::array<double,2>{0,255}))throw std::invalid_argument("Stored indexed RGB must use its literal full-range table");
  const auto &nested_layout=wire::object_value(rendering.at("layout"));auto shape=source.shape;shape.s=3;
  RgbDatasetLayout layout{shape,wire::size_value(nested_layout.at("output_bytes")),wire::size_value(nested_layout.at("working_bytes"))};Budget b{request.max_working_bytes,layout.working_bytes};b.add(wire::size_value(r.at("metadata_bytes")));layout.working_bytes=b.used;
  if(!metadata_equal(wire::object_value(r.at("layout")),layout_record(layout)))throw std::invalid_argument("Indexed RGB derived layout differs");
  return {source,request,wire::unsigned_value(r.at("selection_revision")),wire::unsigned_value(r.at("display_revision"))};
}
std::size_t metadata_bytes(const DocumentSnapshot &s,std::size_t cap){
  Budget b{cap};b.add(65536);b.record(s.display().properties);const auto raw=b.used;if(raw>(cap/8))throw std::invalid_argument("Indexed RGB metadata working-byte limit");return raw*8;
}
void provenance(const ResolvedIndexedRgbCommand &c,PreparedAxisRemap &edit){
  if(c.request().destination==OutputDestination::NewDocument)edit.transaction.set_title(c.source().data().metadata.title+" RGB");
  auto view=detail::session_metadata::view_json(c.source().display());view["properties"]=detail::session_metadata::to_json(c.source().display().properties);view["t"]=c.source().display().t;view["c"]=c.source().display().c;
  edit.transaction.set_extension("imagejxx.indexed_rgb_source",metadata_object({{"schema",std::string("imagejxx.indexed-rgb-source")},{"version",1},{"command",metadata_object(c.record())},{"original_display",detail::session_metadata::from_json(view)}}));
}
}
ResolvedIndexedRgbCommand::ResolvedIndexedRgbCommand(DocumentSnapshot source,IndexedRgbRequest request,ResolvedRgbDatasetCommand rendering,RgbDatasetLayout layout,MetadataObject record):source_(std::move(source)),request_(request),rendering_(std::move(rendering)),layout_(layout),record_(std::move(record)){}
ResolvedIndexedRgbCommand resolve_indexed_rgb_command(const DocumentSnapshot &source,const IndexedRgbRequest &request){
  (void)name(request.palette_source);if(!request.max_working_bytes||(request.destination!=OutputDestination::InPlace&&request.destination!=OutputDestination::NewDocument))throw std::invalid_argument("Invalid indexed RGB destination/budget");
  const auto &d=source.data().dataset;if(d.shape().s!=1||d.pixel_type()!=PixelType::UInt8||d.sample_policy()!=FloatingSamplePolicy::FiniteOnly)throw std::invalid_argument("Indexed RGB requires finite UInt8 index planes");
  const auto palette=indexed_palette(source.data());if(!palette)throw std::invalid_argument("Indexed RGB requires an indexed palette definition");
  auto view=source.display();view.properties.erase("labels");RgbDatasetRequest rgb;rgb.destination=OutputDestination::InPlace;rgb.max_working_bytes=request.max_working_bytes;
  if(request.palette_source==IndexedRgbPaletteSource::Stored){view.properties.erase("channel_display");rgb.display_range=std::array<double,2>{0,255};rgb.lut=palette->palette;}
  auto rendering=resolve_rgb_dataset_command(source.with_display(std::move(view)),rgb);auto layout=rendering.layout();const auto bytes=metadata_bytes(source,request.max_working_bytes-layout.working_bytes);Budget b{request.max_working_bytes,layout.working_bytes};b.add(bytes);layout.working_bytes=b.used;
  const auto &v=source.display();MetadataObject record{{"schema",std::string("imagejxx.indexed-rgb-command")},{"version",1},{"source",metadata_object(wire::source_record(wire::source_metadata(source)))},{"selection_revision",source.revisions().selection},{"display_revision",source.revisions().display},{"view_position",metadata_array({v.t,v.c,v.crosshair[2]})},{"request",metadata_object(request_record(request))},{"rendering",metadata_object(rendering.record())},{"layout",metadata_object(layout_record(layout))},{"metadata_bytes",std::uint64_t(bytes)},{"policy",std::string("current-stage-indexed-palette-rgb-v1")}};
  (void)detail::imagej_roi_record_bytes(record,16*1024*1024);return {source,request,std::move(rendering),layout,std::move(record)};
}
void validate_indexed_rgb_command_record(const MetadataObject &r){(void)decode(r);}
ResolvedIndexedRgbCommand indexed_rgb_command_from_record(const DocumentSnapshot &source,const MetadataObject &record){
  const auto d=decode(record);if(!metadata_equal(wire::source_record(wire::source_metadata(source)),wire::source_record(d.source))||source.revisions().selection!=d.selection||source.revisions().display!=d.display)throw RevisionConflict();auto result=resolve_indexed_rgb_command(source,d.request);if(!metadata_equal(result.record(),record))throw std::invalid_argument("Indexed RGB capture differs from source");return result;
}
ResolvedIndexedRgbCommand rebind_indexed_rgb_command(const DocumentSnapshot &source,const MetadataObject &original,const IndexedRgbRequest &request){(void)decode(original);return resolve_indexed_rgb_command(source,request);}
IndexedRgbRequest detail::indexed_rgb_workflow_request(const OperationParameters &parameters,const MetadataObject &original){
  auto captured=decode(original).request;if(parameters.empty())return captured;
  if(parameters.size()!=2||!parameters.count("palette_source")||!parameters.count("max_working_bytes"))throw std::invalid_argument("Indexed RGB authored intent requires palette_source and max_working_bytes");
  const auto *text=std::get_if<std::string>(&parameters.at("palette_source"));if(!text)throw std::invalid_argument("Indexed RGB palette source requires text");
  auto record=request_record(captured);record["palette_source"]=*text;const auto &v=parameters.at("max_working_bytes");if(const auto p=std::get_if<std::uint64_t>(&v))record["max_working_bytes"]=*p;else if(const auto p=std::get_if<std::int64_t>(&v))record["max_working_bytes"]=*p;else throw std::invalid_argument("Indexed RGB working bytes requires integer");return read_request(record);
}
OperationParameters detail::indexed_rgb_workflow_parameters(const IndexedRgbRequest &request){return {{"palette_source",std::string(name(request.palette_source))},{"max_working_bytes",std::uint64_t(request.max_working_bytes)}};}
DocumentSnapshot detail::indexed_rgb_metadata_preview(const ResolvedIndexedRgbCommand &c){auto preview=detail::rgb_dataset_metadata_preview(c.rendering());auto edit=preview.begin_edit(Change::All);PreparedAxisRemap result{std::move(edit),preview.display()};provenance(c,result);return result.transaction.preview();}
PreparedAxisRemap detail::prepare_indexed_rgb_edit(const ResolvedIndexedRgbCommand &c,const ExecutionControl &control,std::size_t cap){if(c.layout().working_bytes>cap)throw std::invalid_argument("Indexed RGB execution working-byte limit");auto edit=detail::prepare_rgb_dataset_edit(c.rendering(),control,cap);provenance(c,edit);kernel_checkpoint();return edit;}
}
