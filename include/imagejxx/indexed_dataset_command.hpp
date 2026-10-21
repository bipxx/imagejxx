#pragma once
#include <imagejxx/indexed_color.hpp>
#include <imagejxx/indexed_palette.hpp>
namespace imagejxx {
struct IndexedDatasetRequest {
  int max_colors=256;
  OutputDestination destination=OutputDestination::NewDocument;
  std::size_t max_working_bytes=512ULL*1024*1024;
};
struct IndexedDatasetLayout {DatasetShape shape;std::size_t output_bytes,working_bytes;};
IndexedDatasetLayout describe_indexed_dataset(DatasetShape,PixelType,FloatingSamplePolicy,const IndexedDatasetRequest &);
class ResolvedIndexedDatasetCommand {
public:
  const DocumentSnapshot &source()const{return source_;}
  const IndexedDatasetRequest &request()const{return request_;}
  const IndexedDatasetLayout &layout()const{return layout_;}
  const MetadataObject &record()const{return record_;}
private:
  friend ResolvedIndexedDatasetCommand resolve_indexed_dataset_command(const DocumentSnapshot &,const IndexedDatasetRequest &);
  ResolvedIndexedDatasetCommand(DocumentSnapshot,IndexedDatasetRequest,IndexedDatasetLayout,MetadataObject);
  DocumentSnapshot source_;IndexedDatasetRequest request_;IndexedDatasetLayout layout_;MetadataObject record_;
};
ResolvedIndexedDatasetCommand resolve_indexed_dataset_command(const DocumentSnapshot &,const IndexedDatasetRequest & = {});
void validate_indexed_dataset_command_record(const MetadataObject &);
ResolvedIndexedDatasetCommand indexed_dataset_command_from_record(const DocumentSnapshot &,const MetadataObject &);
ResolvedIndexedDatasetCommand rebind_indexed_dataset_command(const DocumentSnapshot &,const MetadataObject &,const IndexedDatasetRequest &);
PreparedOperation prepare_indexed_dataset(const ResolvedIndexedDatasetCommand &,const ExecutionControl & = {},std::size_t execution_max_working_bytes=std::numeric_limits<std::size_t>::max());
}
