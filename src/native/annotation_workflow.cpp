#include "annotation_workflow.hpp"
#include "axis_command_record.hpp"
#include "imagej_roi_import_internal.hpp"
#include "kernel_control.hpp"
#include "regional_geometry.hpp"
#include <algorithm>
#include <limits>

namespace imagejxx::detail {
namespace {
using namespace axis_command_record;
void require(bool value,const char *message) {
  if(!value)throw std::invalid_argument(message);
}
struct Budget {
  std::size_t cap,used=0;
  void add(std::size_t value) {
    require(used<=cap && value<=cap-used,"Annotation workflow intent byte limit");used+=value;
  }
  void product(std::size_t n,std::size_t unit) {
    require(used<=cap && n<=(cap-used)/unit,"Annotation workflow intent byte limit");used+=n*unit;
  }
  void metadata(const MetadataObject &value) {
    for(const auto &[key,child]:value)regional_wire_depth(child,10);
    product(imagej_roi_record_bytes(value,cap-used),8);
  }
  void selection(const Selection &value,int width,int height) {
    value.validate(width,height);
    add(sizeof(Selection));add(value.kind.size());
    product(value.points.size(),256);product(value.packed_mask().size(),8);
    product(value.path_commands().size(),128);metadata(value.attributes);
  }
};
DatasetShape extent(const MetadataValue &value) {
  const auto &a=array_value(value);require(a.size()==6,"Annotation workflow requires TCZYXS extent");
  DatasetShape result{index_value(a[0],true),index_value(a[1],true),index_value(a[2],true),
                      index_value(a[3],true),index_value(a[4],true),index_value(a[5],true)};
  result.validate();return result;
}
MetadataValue extent_value(DatasetShape shape) {
  MetadataArray values;for(auto n:shape.dimensions())values.push_back(n);
  return metadata_array(std::move(values));
}
const char *operation_name(PlanarAnnotationOperation op) {
  switch(op) {
  case PlanarAnnotationOperation::Store:return "store";
  case PlanarAnnotationOperation::Update:return "update";
  case PlanarAnnotationOperation::Delete:return "delete";
  }
  throw std::invalid_argument("Unknown planar workflow operation");
}
SavedRoiGeometryLimits saved_limits(std::size_t cap) {
  SavedRoiGeometryLimits result;
  result.max_record_bytes=std::min(result.max_record_bytes,cap);
  result.max_working_bytes=std::min(result.max_working_bytes,cap);return result;
}
PlanarAnnotation geometry_row(const Selection &geometry) {
  return {"geometry",geometry,Plane::XY,0,{},{},{}};
}
} // namespace
bool annotation_workflow_operation(const std::string &name) {
  return name=="planar_annotation" || name=="saved_roi_geometry";
}
std::size_t annotation_workflow_request_bytes(DatasetShape shape,const PlanarAnnotationRequest &r,std::size_t cap) {
  shape.validate();Budget b{cap};b.add(16384);(void)operation_name(r.operation);
  require(r.operation==PlanarAnnotationOperation::Store ? !r.index && bool(r.annotation) :
      r.index && (r.operation==PlanarAnnotationOperation::Update ? bool(r.annotation) : !r.annotation),
      "Planar workflow request requires complete operation/index/annotation choices");
  if(r.annotation) {
    const auto &row=*r.annotation;row.validate(shape);b.add(row.name.size());b.metadata(row.attributes);
    const auto dims=planar_annotation_geometry(shape,row.plane);b.selection(row.selection,dims.width,dims.height);
  }
  return b.used;
}
std::size_t annotation_workflow_request_bytes(DatasetShape shape,const SavedRoiGeometryRequest &r,std::size_t cap) {
  shape.validate();Budget b{cap};b.add(16384);
  geometry_row(r.replacement).validate(shape);b.selection(r.replacement,shape.x,shape.y);return b.used;
}
std::size_t annotation_workflow_intent_bytes(const NativeWorkflowAnnotationIntent &v,std::size_t cap) {
  return std::visit([&](const auto &request){return annotation_workflow_request_bytes(v.extent,request,cap);},v.request);
}
MetadataObject annotation_workflow_intent_value(const NativeWorkflowAnnotationIntent &v,std::size_t cap) {
  (void)annotation_workflow_intent_bytes(v,cap);
  return std::visit([&](const auto &request)->MetadataObject {
    using T=std::decay_t<decltype(request)>;
    MetadataObject value{{"extent",extent_value(v.extent)}};
    if constexpr(std::is_same_v<T,PlanarAnnotationRequest>) {
      value["kind"]=std::string("planar");
      value["request"]=metadata_object({{"operation",std::string(operation_name(request.operation))},
          {"index",request.index?MetadataValue(std::uint64_t(*request.index)):MetadataValue{}},
          {"annotation",request.annotation?encode_planar_annotations(v.extent,{*request.annotation}):MetadataValue{}}});
    } else {
      value["kind"]=std::string("saved_roi");
      value["request"]=metadata_object({{"index",std::uint64_t(request.row_index)},
          {"geometry",encode_planar_annotations(v.extent,{geometry_row(request.replacement)})}});
    }
    return value;
  },v.request);
}
NativeWorkflowAnnotationIntent annotation_workflow_intent_from_value(const MetadataObject &value,std::size_t cap) {
  (void)imagej_roi_record_bytes(value,cap);regional_record_depth(value);
  exact_fields(value,{"kind","extent","request"});
  const auto shape=extent(value.at("extent"));const auto &r=object_value(value.at("request"));
  const auto &kind=text_value(value.at("kind"));
  if(kind=="planar") {
    NativeWorkflowAnnotationIntent result{shape,decode_planar_annotation_request(shape,r)};
    (void)annotation_workflow_intent_bytes(result,cap);return result;
  }
  require(kind=="saved_roi","Unknown annotation workflow intent kind");exact_fields(r,{"index","geometry"});
  const auto index=unsigned_value(r.at("index"));require(index<=std::numeric_limits<std::size_t>::max(),"Saved row index overflow");
  const auto rows=decode_planar_annotations(shape,r.at("geometry"));
  require(rows.size()==1 && rows[0].name=="geometry" && rows[0].plane==Plane::XY && rows[0].coordinate==0 &&
      !rows[0].t && !rows[0].c && rows[0].attributes.empty(),"Saved geometry carrier must contain only geometry");
  NativeWorkflowAnnotationIntent result{shape,SavedRoiGeometryRequest{std::size_t(index),rows[0].selection}};
  (void)annotation_workflow_intent_bytes(result,cap);return result;
}
void validate_annotation_workflow_original(const std::string &operation,const MetadataObject &original,std::size_t cap) {
  (void)imagej_roi_record_bytes(original,cap);
  if(operation=="planar_annotation")validate_planar_annotation_command_record(original);
  else {
    require(operation=="saved_roi_geometry","Unknown annotation workflow operation");
    validate_saved_roi_geometry_record(original,saved_limits(cap));
  }
}
void validate_annotation_workflow_intent(const NativeWorkflowOperation &v,std::size_t cap) {
  require(annotation_workflow_operation(v.operation) && v.parameters.empty() && !v.filter_options &&
      !v.selection_creation && !v.use_destination_calibration,"Annotation workflow requires typed annotation choices only");
  require(v.target.scope==TargetScope::CurrentPlane && v.target.roi==RoiPolicy::WholePlane &&
      v.target.mode==ExecutionMode::Planes && v.target.destination==OutputDestination::InPlace &&
      !v.target.t && !v.target.c && !v.target.z && v.target.positions.empty(),"Annotation workflow uses explicit geometry and the current stage");
  require(v.original || v.annotation_intent,"Annotation workflow requires a capture or authored intent");
  if(v.original)validate_annotation_workflow_original(v.operation,*v.original,cap);
  if(v.annotation_intent) {
    require((v.operation=="planar_annotation")==std::holds_alternative<PlanarAnnotationRequest>(v.annotation_intent->request),
        "Annotation workflow operation and request kind differ");
    (void)annotation_workflow_intent_bytes(*v.annotation_intent,cap);
  }
}
NativeWorkflowAnnotationIntent annotation_workflow_request(const NativeWorkflowOperation &v,std::size_t cap) {
  validate_annotation_workflow_intent(v,cap);
  if(v.annotation_intent)return *v.annotation_intent;
  const auto &original=*v.original;const auto shape=extent(object_value(original.at("source")).at("shape"));
  if(v.operation=="saved_roi_geometry")return {shape,decode_saved_roi_geometry_request(original,saved_limits(cap))};
  MetadataObject request{{"operation",original.at("operation")},{"index",original.at("index")},{"annotation",original.at("annotation")}};
  return {shape,decode_planar_annotation_request(shape,request)};
}
DocumentTransaction prepare_annotation_workflow(const DocumentSnapshot &source,
    const NativeWorkflowOperation &v,std::size_t cap,const ExecutionControl &control,std::size_t *working_bytes) {
  const auto intent=annotation_workflow_request(v,cap);
  require(source.data().dataset.shape().dimensions()==intent.extent.dimensions(),
      "Annotation workflow extent requires an explicit authored rebind");
  if(control.cancellation && control.cancellation->canceled())throw OperationCancelled();
  if(const auto request=std::get_if<PlanarAnnotationRequest>(&intent.request)) {
    Budget held{cap};held.add(annotation_workflow_intent_bytes(intent,cap));
    if(const auto found=source.data().metadata.extensions.find(planar_annotations_extension);
        found!=source.data().metadata.extensions.end())
      held.metadata({{planar_annotations_extension,found->second}});
    const auto command=resolve_planar_annotation_command(source,*request);
    (void)imagej_roi_record_bytes(command.record(),cap);
    if(working_bytes)*working_bytes=held.used;
    return prepare_planar_annotation(command);
  }
  const auto &request=std::get<SavedRoiGeometryRequest>(intent.request);
  const auto limits=saved_limits(cap);
  const auto command=v.original?rebind_saved_roi_geometry(source,*v.original,request,limits,control):
      resolve_saved_roi_geometry(source,request,limits,control);
  if(working_bytes)*working_bytes=command.estimate().working_bytes;
  // Use the resolved command's admitted row and preserve every other carrier.
  auto rows=source.data().rois;rows.at(command.row_index()).selection=command.replacement();
  auto edit=source.begin_edit(Change::All);edit.set_rois(std::move(rows));return edit;
}
} // namespace imagejxx::detail
