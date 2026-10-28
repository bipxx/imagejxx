#pragma once
#include <imagejxx/imagej_projection_execution.hpp>
namespace imagejxx {
// Complete raw request grammar shared by C++, Python and the ImGui controller.
MetadataObject imagej_projection_request_record(const ImageJProjectionDocumentRequest&);
ImageJProjectionDocumentRequest imagej_projection_request_from_record(const MetadataObject&);
class ResolvedImageJProjectionCommand {
public:
  const DocumentSnapshot& source() const { return source_; }
  const ImageJProjectionDocumentRequest& request() const { return request_; }
  const ImageJProjectionDocumentEstimate& estimate() const { return estimate_; }
  const MetadataObject& record() const { return record_; }
private:
  friend ResolvedImageJProjectionCommand resolve_imagej_projection_command(
      const DocumentSnapshot&,const ImageJProjectionDocumentRequest&);
  ResolvedImageJProjectionCommand(DocumentSnapshot s,ImageJProjectionDocumentRequest r,
      ImageJProjectionDocumentEstimate e,MetadataObject v)
      :source_(std::move(s)),request_(std::move(r)),estimate_(std::move(e)),record_(std::move(v)) {}
  DocumentSnapshot source_;
  ImageJProjectionDocumentRequest request_;
  ImageJProjectionDocumentEstimate estimate_;
  MetadataObject record_;
};
ResolvedImageJProjectionCommand resolve_imagej_projection_command(
    const DocumentSnapshot&,const ImageJProjectionDocumentRequest&);
void validate_imagej_projection_command_record(const MetadataObject&);
ResolvedImageJProjectionCommand imagej_projection_command_from_record(
    const DocumentSnapshot&,const MetadataObject&);
ResolvedImageJProjectionCommand rebind_imagej_projection_command(
    const DocumentSnapshot&,const MetadataObject&,const ImageJProjectionDocumentRequest&);
PreparedOperation prepare_imagej_projection(const ResolvedImageJProjectionCommand&,
    const ExecutionControl& = {},std::size_t runtime_cap = std::numeric_limits<std::size_t>::max());
} // namespace imagejxx
