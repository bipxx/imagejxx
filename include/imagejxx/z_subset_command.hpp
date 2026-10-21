#pragma once
#include <imagejxx/axis_remap.hpp>

namespace imagejxx {
class ResolvedZSubsetCommand {
public:
  const DocumentSnapshot &source() const { return source_; }
  const ZSubsetRequest &request() const { return request_; }
  const MetadataObject &record() const { return record_; }
private:
  friend ResolvedZSubsetCommand resolve_z_subset_command(const DocumentSnapshot &,const ZSubsetRequest &);
  ResolvedZSubsetCommand(DocumentSnapshot,ZSubsetRequest,MetadataObject);
  DocumentSnapshot source_;
  ZSubsetRequest request_;
  MetadataObject record_;
};
ResolvedZSubsetCommand resolve_z_subset_command(const DocumentSnapshot &,const ZSubsetRequest &);
void validate_z_subset_command_record(const MetadataObject &);
ResolvedZSubsetCommand z_subset_command_from_record(const DocumentSnapshot &,const MetadataObject &);
ResolvedZSubsetCommand rebind_z_subset_command(const DocumentSnapshot &,const MetadataObject &,const ZSubsetRequest &);
PreparedOperation prepare_z_subset(const ResolvedZSubsetCommand &,const ExecutionControl & = {});
} // namespace imagejxx
