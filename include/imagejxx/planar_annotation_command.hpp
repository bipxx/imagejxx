#pragma once
#include <utility>
#include <imagejxx/planar_annotations.hpp>
namespace imagejxx {
enum class PlanarAnnotationOperation {Store,Update,Delete};
struct PlanarAnnotationRequest {
  PlanarAnnotationOperation operation=PlanarAnnotationOperation::Store;
  std::optional<std::size_t> index;
  std::optional<PlanarAnnotation> annotation;
};
class ResolvedPlanarAnnotationCommand {
public:
  const DocumentSnapshot &source() const {return source_;}
  const PlanarAnnotationRequest &request() const {return request_;}
  const MetadataObject &record() const {return record_;}
private:
  friend ResolvedPlanarAnnotationCommand resolve_planar_annotation_command(const DocumentSnapshot &,const PlanarAnnotationRequest &);
  ResolvedPlanarAnnotationCommand(DocumentSnapshot source,PlanarAnnotationRequest request,MetadataObject record)
      :source_(std::move(source)),request_(std::move(request)),record_(std::move(record)) {}
  DocumentSnapshot source_;
  PlanarAnnotationRequest request_;
  MetadataObject record_;
};
// Metadata-only equation: captured before + complete request = after. V1 stays
// closed to regional sets. V2 requires a regional V2 set in before/request/after.
// Each encoded set retains the native annotation limits. No pixel acquisition.
ResolvedPlanarAnnotationCommand resolve_planar_annotation_command(const DocumentSnapshot &,const PlanarAnnotationRequest &);
// Explicit copy-back from the active XY selection. Keep the annotation's name,
// parent positions and row attributes, with no continuing source association.
// Bounded planar/selection staging precedes copies; unrelated source/provider
// storage follows the existing command/transaction contract.
ResolvedPlanarAnnotationCommand resolve_planar_annotation_from_selection(
    const DocumentSnapshot &,std::size_t index,
    std::size_t max_working_bytes=256*1048576);
PlanarAnnotationRequest decode_planar_annotation_request(DatasetShape,const MetadataObject &);
void validate_planar_annotation_command_record(const MetadataObject &);
ResolvedPlanarAnnotationCommand planar_annotation_command_from_record(const DocumentSnapshot &,const MetadataObject &);
ResolvedPlanarAnnotationCommand rebind_planar_annotation_command(const DocumentSnapshot &,const MetadataObject &,const PlanarAnnotationRequest &explicit_request);
DocumentTransaction prepare_planar_annotation(const ResolvedPlanarAnnotationCommand &);
} // namespace imagejxx
