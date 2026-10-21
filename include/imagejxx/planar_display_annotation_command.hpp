#pragma once
#include <imagejxx/planar_display_annotations.hpp>
#include <utility>
namespace imagejxx {
enum class PlanarDisplayAnnotationOperation { Store, Update, Delete };
struct PlanarDisplayAnnotationRequest {
  PlanarDisplayAnnotationOperation operation =
      PlanarDisplayAnnotationOperation::Store;
  std::optional<std::size_t> index;
  std::optional<PlanarDisplayAnnotation> annotation;
};
class ResolvedPlanarDisplayAnnotationCommand {
public:
  const DocumentSnapshot &source() const { return source_; }
  const PlanarDisplayAnnotationRequest &request() const { return request_; }
  const MetadataObject &record() const { return record_; }

private:
  friend ResolvedPlanarDisplayAnnotationCommand
  resolve_planar_display_annotation_command(
      const DocumentSnapshot &, const PlanarDisplayAnnotationRequest &);
  ResolvedPlanarDisplayAnnotationCommand(DocumentSnapshot source,
                                         PlanarDisplayAnnotationRequest request,
                                         MetadataObject record)
      : source_(std::move(source)), request_(std::move(request)),
        record_(std::move(record)) {}
  DocumentSnapshot source_;
  PlanarDisplayAnnotationRequest request_;
  MetadataObject record_;
};
// Metadata-only, closed V1 equation: captured before + complete request =
// after. Each encoded set retains the native annotation limits. No pixel
// acquisition.
ResolvedPlanarDisplayAnnotationCommand
resolve_planar_display_annotation_command(
    const DocumentSnapshot &, const PlanarDisplayAnnotationRequest &);
PlanarDisplayAnnotationRequest
decode_planar_display_annotation_request(DatasetShape, const MetadataObject &);
void validate_planar_display_annotation_command_record(const MetadataObject &);
ResolvedPlanarDisplayAnnotationCommand
planar_display_annotation_command_from_record(const DocumentSnapshot &,
                                              const MetadataObject &);
ResolvedPlanarDisplayAnnotationCommand rebind_planar_display_annotation_command(
    const DocumentSnapshot &, const MetadataObject &,
    const PlanarDisplayAnnotationRequest &explicit_request);
DocumentTransaction prepare_planar_display_annotation(
    const ResolvedPlanarDisplayAnnotationCommand &);
} // namespace imagejxx
