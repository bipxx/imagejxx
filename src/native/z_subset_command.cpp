#include <imagejxx/z_subset_command.hpp>
#include "axis_command_record.hpp"
#include <utility>

namespace imagejxx {
namespace {
using namespace detail::axis_command_record;
struct Record {SourceRecord source;ZSubsetRequest request;std::uint64_t selection,display;};
void describe(const SourceRecord &source,const ZSubsetRequest &request) {
  const RegularAxisMapping map(source.shape.z,request.first,request.last,request.step);
  if(!request.max_planes || source.shape.plane_count()>request.max_planes ||
      (request.destination!=OutputDestination::NewDocument && request.destination!=OutputDestination::InPlace))
    throw std::invalid_argument("Invalid Z subset limits or destination");
  auto cal=source.calibration;
  cal.origin=cal.voxel_to_world({0,0,double(request.first)});
  cal.spacing[2]*=request.step;cal.validate();
}
Record decode(const MetadataObject &record) {
  validate_metadata(record);
  exact_fields(record,{"schema","version","operation","source","selection_revision","display_revision","request"});
  if(text_value(record.at("schema"))!="imagejxx.z-subset-command" ||
     unsigned_value(record.at("version"))!=1 || text_value(record.at("operation"))!="z_subset")
    throw std::invalid_argument("Unsupported Z subset record");
  const auto &r=object_value(record.at("request"));
  exact_fields(r,{"first","last","step","destination","max_planes"});
  const auto &destination=text_value(r.at("destination"));
  if(destination!="in_place" && destination!="new_document")throw std::invalid_argument("Invalid Z subset destination");
  ZSubsetRequest request{index_value(r.at("first")),index_value(r.at("last")),index_value(r.at("step"),true),
      destination=="in_place"?OutputDestination::InPlace:OutputDestination::NewDocument,size_value(r.at("max_planes"))};
  auto source=read_source(object_value(record.at("source")));describe(source,request);
  return {std::move(source),request,unsigned_value(record.at("selection_revision")),unsigned_value(record.at("display_revision"))};
}
}
ResolvedZSubsetCommand::ResolvedZSubsetCommand(DocumentSnapshot s,ZSubsetRequest r,MetadataObject record)
    :source_(std::move(s)),request_(r),record_(std::move(record)) {}
ResolvedZSubsetCommand resolve_z_subset_command(const DocumentSnapshot &source,const ZSubsetRequest &request) {
  const auto metadata=detail::axis_command_record::source_metadata(source);describe(metadata,request);
  MetadataObject record{{"schema",std::string("imagejxx.z-subset-command")},{"version",1},{"operation",std::string("z_subset")},
    {"source",metadata_object(detail::axis_command_record::source_record(metadata))},
    {"selection_revision",source.revisions().selection},{"display_revision",source.revisions().display},
    {"request",metadata_object({{"first",request.first},{"last",request.last},{"step",request.step},
        {"destination",std::string(request.destination==OutputDestination::InPlace?"in_place":"new_document")},
        {"max_planes",static_cast<std::uint64_t>(request.max_planes)}})}};
  return {source,request,std::move(record)};
}
void validate_z_subset_command_record(const MetadataObject &record) {(void)decode(record);}
ResolvedZSubsetCommand z_subset_command_from_record(const DocumentSnapshot &source,const MetadataObject &record) {
  const auto r=decode(record);const auto current=detail::axis_command_record::source_metadata(source);
  if(current.document_id!=r.source.document_id)throw std::invalid_argument("Z subset record belongs to another document");
  auto revisions=r.source.revisions;revisions.selection=r.selection;revisions.display=r.display;
  if(!source.revisions().matches(revisions,Change::All))throw RevisionConflict();
  if(current.shape.dimensions()!=r.source.shape.dimensions() || current.pixel_type!=r.source.pixel_type ||
     current.sample_policy!=r.source.sample_policy || !detail::axis_command_record::same_calibration(current.calibration,r.source.calibration))
    throw std::invalid_argument("Z subset source descriptor differs");
  return resolve_z_subset_command(source,r.request);
}
ResolvedZSubsetCommand rebind_z_subset_command(const DocumentSnapshot &source,const MetadataObject &record,const ZSubsetRequest &request) {
  (void)decode(record);return resolve_z_subset_command(source,request);
}
} // namespace imagejxx
