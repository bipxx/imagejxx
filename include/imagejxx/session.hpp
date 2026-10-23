#pragma once

#include <imagejxx/document.hpp>
#include <imagejxx/execution.hpp>

namespace imagejxx {

// Wire contract, extension policy and examples: docs/SESSION_FORMAT.md.
// All limits are positive; limits apply together, rather than independently
// promising that every individual maximum can be reached by one capture.
struct SessionLimits {
  std::size_t max_documents = 64;
  std::size_t max_planes = 65536;
  std::size_t max_samples = 128ULL * 1024 * 1024;
  std::size_t max_json_bytes = 1024ULL * 1024 * 1024;
  std::size_t max_encoded_bytes = 1024ULL * 1024 * 1024;
  std::size_t max_working_bytes = 4ULL * 1024 * 1024 * 1024;
  std::size_t max_nodes = 1024 * 1024;
  std::size_t max_depth = 160;
};
struct SessionEstimate {
  std::size_t documents = 0, planes = 0, samples = 0;
  // Logical double backing; complete planned JSON; compressed upper bound;
  // conservative codec workspace. None is a whole-process allocation counter.
  std::size_t source_bytes = 0, json_bytes = 0, encoded_bytes = 0,
              working_bytes = 0;
};
struct SessionCapture {
  std::vector<DocumentSnapshot> documents;
  MetadataArray results, workflow;
  // Opaque session metadata. The reserved per-document future channel-display
  // location is instead snapshot.display().properties["channel_display"]. Its
  // producer supplies an object with integer "version"; P06a transports it
  // opaquely, including future payloads. P06c owns interpretation/validation.
  MetadataObject extensions;
};
struct DecodedSession {
  // Fresh clean owners with zero revisions and empty history. Recorded actions
  // remain data; loading does not execute or implicitly rebind them.
  std::vector<std::shared_ptr<Document>> documents;
  MetadataArray results, workflow;
  MetadataObject extensions;
  int source_version = 3;
};

// Eager sources only. Estimates inspect descriptors and annotation metadata,
// never pixel samples, and allocate no image/selection raster or document owner.
// Bytes are conservative codec workspace bounds excluding borrowed sources,
// allocator bookkeeping and unrelated application allocations.
SessionEstimate estimate_session(const SessionCapture &capture,
                                  const SessionLimits &limits = {});
// V3/V4/V5 framing: "IMAGEJXX3\n", "IMAGEJXX4\n" or "IMAGEJXX5\n", uint64 little-endian
// JSON length, one zlib stream.
// It stores native double-backed sample bits in explicit little-endian order;
// declared PixelType and sample policy remain separate. T,C,Z,Y,X,S singleton
// axes, native metadata and presentation survive without inference from rank.
std::vector<unsigned char> encode_session(
    const SessionCapture &capture, const SessionLimits &limits = {},
    const ExecutionControl &control = {});
DecodedSession decode_session(const std::vector<unsigned char> &bytes,
                               const SessionLimits &limits = {},
                               const ExecutionControl &control = {});
// The decoder also migrates the legacy V2 frame, retaining its declared-type
// canonicalization and marked-metadata checks. Encoding emits V5 when a typed
// active/previous/saved selection contains a native path, otherwise V4 when typed
// selections contain regional geometry, and V3 for legacy-only captures.
// Atomic replacement never marks an owner saved. Call owner.mark_saved(saved)
// with the particular captured DocumentSnapshot after successful publication
// when that is desired; a newer edit remains dirty.
void save_session(const std::string &path, const SessionCapture &capture,
                  const SessionLimits &limits = {},
                  const ExecutionControl &control = {});
DecodedSession load_session(const std::string &path,
                             const SessionLimits &limits = {},
                             const ExecutionControl &control = {});

} // namespace imagejxx
