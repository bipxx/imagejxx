#pragma once
#include <imagejxx/native_workflow_program.hpp>
#include <imagejxx/session.hpp>
namespace imagejxx {
struct NativeWorkflowBatchSelection {
  bool all_documents = false;
  std::size_t document_index = 0;
};
class NativeWorkflowBatchReview {
public:
  const std::string &path() const { return path_; }
  const std::string &fingerprint() const { return fingerprint_; }
  const std::vector<DocumentSnapshot> &documents() const { return documents_; }
  std::size_t working_bytes() const { return working_bytes_; }
  void verify(const ExecutionControl & = {}) const;

private:
  friend NativeWorkflowBatchReview
  review_native_workflow_batch_input(const std::string &, const SessionLimits &,
                                     const ExecutionControl &);
  std::string path_, fingerprint_;
  std::vector<DocumentSnapshot> documents_;
  std::size_t working_bytes_ = 0, max_file_bytes_ = 0, encoded_bytes_ = 0;
};
// Reads one bounded native session; snapshots and SHA-256 identify the exact
// decoded bytes. Review never executes embedded tables or modifies a file.
NativeWorkflowBatchReview
review_native_workflow_batch_input(const std::string &,
                                   const SessionLimits & = {},
                                   const ExecutionControl & = {});
struct NativeWorkflowBatchInput {
  std::string path, output_stem;
  // Null inherits the common program/selector. Programs are
  // immutable/shallow-copy.
  std::shared_ptr<const NativeWorkflowProgram> program;
  std::optional<NativeWorkflowBatchSelection> selection;
  // Empty preserves unreviewed caller behavior; otherwise 64 lowercase SHA-256
  // hex.
  std::string expected_fingerprint;
};
enum class NativeWorkflowBatchState {
  Queued,
  Validating,
  Reading,
  Planning,
  Encoding,
  Prepared,
  Committed,
  Partial,
  Failed,
  Canceled,
  Skipped
};
struct NativeWorkflowBatchOptions {
  bool all_documents = false;
  std::size_t document_index = 0;
  std::size_t max_inputs = 64, max_outputs = 4096;
  std::size_t max_working_bytes = 512ULL * 1024 * 1024;
  NativeWorkflowLimits workflow;
  SessionLimits session;
};
struct NativeWorkflowBatchItem {
  std::string input;
  NativeWorkflowBatchState state = NativeWorkflowBatchState::Queued;
};
struct NativeWorkflowBatchOutput {
  std::size_t input_index;
  std::string path;
  bool session = false, published = false, failed = false;
};
using NativeWorkflowBatchProgress =
    std::function<void(std::size_t, NativeWorkflowBatchState)>;
namespace detail {
struct NativeWorkflowBatchAccess;
}
class PreparedNativeWorkflowBatch {
public:
  ~PreparedNativeWorkflowBatch();
  PreparedNativeWorkflowBatch(PreparedNativeWorkflowBatch &&) noexcept;
  PreparedNativeWorkflowBatch &
  operator=(PreparedNativeWorkflowBatch &&) noexcept;
  PreparedNativeWorkflowBatch(const PreparedNativeWorkflowBatch &) = delete;
  PreparedNativeWorkflowBatch &
  operator=(const PreparedNativeWorkflowBatch &) = delete;
  const std::vector<NativeWorkflowBatchItem> &items() const;
  const std::vector<NativeWorkflowBatchOutput> &outputs() const;
  std::size_t encoded_bytes() const;
  bool attempted() const;
  // New files only, in input order: exports then that input's complete session.
  // An error retains earlier published files and marks the partial/failed item
  // and later skipped items. No retry of a partially attempted bundle.
  // No user callbacks run during this synchronous publication boundary.
  void publish();
  void abandon();

private:
  friend struct detail::NativeWorkflowBatchAccess;
  struct State;
  std::unique_ptr<State> state_;
  explicit PreparedNativeWorkflowBatch(std::unique_ptr<State>);
};
// Native V3/V2 session inputs. Only the selected document(s) are processed;
// untouched documents, input results/workflow tables and session extensions
// are retained. Embedded workflows are never executed. The input files and
// live documents are never modified. All semantic plans and output conflicts
// pass before any exclusive temporary file is created. Parent must exist.
// Inputs decode one at a time; aggregate encoded output and codec/planner phase
// admission is bounded, separately from custom caches/process allocations.
PreparedNativeWorkflowBatch
prepare_native_workflow_batch(const std::vector<NativeWorkflowBatchInput> &,
                              const std::vector<NativeWorkflowStep> &,
                              const std::string &output_directory,
                              const NativeWorkflowBatchOptions & = {},
                              const ExecutionControl & = {},
                              const NativeWorkflowBatchProgress & = {});
} // namespace imagejxx
