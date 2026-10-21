#pragma once

#include <imagejxx/targets.hpp>

namespace imagejxx::detail {

// Structural claims from a record, not evidence that its original source still
// exists or that its recorded revisions describe trustworthy pixels. No source,
// plane or ROI mask is read while decoding these fields.
struct TargetRecord {
  std::uint64_t document_id;
  Revisions source_revisions;
  DatasetShape source_shape;
  TargetRequest request;
};

// Validate all schema1 fields and the resolved target against its own recorded
// shape. Strict replay separately checks identity/revisions against a snapshot;
// explicit command rebinding supplies a new request instead of modifying this
// record's source identity or frozen target.
TargetRecord decode_target_record(const MetadataObject &record);

// Bind validated claims strictly to their actual source; this is not rebinding.
ResolvedTarget strict_target_from_record(const DocumentSnapshot &source,
                                        const TargetRecord &record,
                                        const std::string &operation);

} // namespace imagejxx::detail
