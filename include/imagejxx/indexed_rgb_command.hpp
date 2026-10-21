#pragma once
#include <imagejxx/indexed_palette.hpp>
#include <imagejxx/rgb_dataset_command.hpp>
namespace imagejxx {
enum class IndexedRgbPaletteSource { Display, Stored };
struct IndexedRgbRequest {
  IndexedRgbPaletteSource palette_source=IndexedRgbPaletteSource::Display;
  OutputDestination destination=OutputDestination::NewDocument;
  std::size_t max_working_bytes=512ULL*1024*1024;
};
// Rebinding resolves the selected palette on the CURRENT indexed stage. The
// nested RGB capture is immutable evidence for replay, not execution intent for
// another input. Existing literal RGB dataset command semantics are unchanged.
class ResolvedIndexedRgbCommand {
public:
  const DocumentSnapshot &source()const{return source_;}
  const IndexedRgbRequest &request()const{return request_;}
  const RgbDatasetLayout &layout()const{return layout_;}
  const MetadataObject &record()const{return record_;}
  const ResolvedRgbDatasetCommand &rendering()const{return rendering_;}
private:
  friend ResolvedIndexedRgbCommand resolve_indexed_rgb_command(const DocumentSnapshot &,const IndexedRgbRequest &);
  ResolvedIndexedRgbCommand(DocumentSnapshot,IndexedRgbRequest,ResolvedRgbDatasetCommand,RgbDatasetLayout,MetadataObject);
  DocumentSnapshot source_;IndexedRgbRequest request_;ResolvedRgbDatasetCommand rendering_;RgbDatasetLayout layout_;MetadataObject record_;
};
ResolvedIndexedRgbCommand resolve_indexed_rgb_command(const DocumentSnapshot &,const IndexedRgbRequest & = {});
void validate_indexed_rgb_command_record(const MetadataObject &);
ResolvedIndexedRgbCommand indexed_rgb_command_from_record(const DocumentSnapshot &,const MetadataObject &);
ResolvedIndexedRgbCommand rebind_indexed_rgb_command(const DocumentSnapshot &,const MetadataObject &,const IndexedRgbRequest &);
PreparedOperation prepare_indexed_rgb(const ResolvedIndexedRgbCommand &,const ExecutionControl & = {},std::size_t execution_max_working_bytes=std::numeric_limits<std::size_t>::max());
}
