#include "atomic_file.hpp"
#include "content_sha256.hpp"
#include "kernel_control.hpp"
#include "native_workflow_batch_internal.hpp"
#include "native_workflow_program_internal.hpp"
#include "session_io_internal.hpp"
#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace imagejxx {
namespace {
namespace fs = std::filesystem;
void require(bool b, const char *m) {
  if (!b)
    throw std::invalid_argument(m);
}
bool alias(const fs::path &a, const fs::path &b) {
#ifdef _WIN32
  const auto x = a.native(), y = b.native();
  return CompareStringOrdinal(x.c_str(), int(x.size()), y.c_str(),
                              int(y.size()), TRUE) == CSTR_EQUAL;
#else
  return a == b;
#endif
}
bool flat(const std::string &s) {
  return !s.empty() && s.size() <= 180 && s != "." && s != ".." &&
         s.find_first_of("/\\:\0", 0, 4) == std::string::npos;
}
void checkpoint(const ExecutionControl &c) { detail::file_checkpoint(c); }
SessionLimits phase_limits(const NativeWorkflowBatchOptions &o,
                           std::size_t retained) {
  require(retained < o.max_working_bytes, "Batch aggregate working-byte limit");
  auto l = o.session;
  auto available = o.max_working_bytes - retained;
  l.max_working_bytes = std::min(l.max_working_bytes, available);
  l.max_encoded_bytes = std::min(l.max_encoded_bytes, available);
  l.max_json_bytes = std::min(l.max_json_bytes, available);
  return l;
}
bool fingerprint_valid(const std::string &s) {
  return s.empty() ||
         (s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
          }));
}
struct InputGuard {
  std::size_t input_index;
  std::string path, digest;
  std::size_t max_bytes, size;
  void verify(const ExecutionControl &c = {}) const {
    require(fs::file_size(fs::u8path(path)) == size,
            "Batch input changed since review");
    require(detail::file_content_sha256(path, max_bytes, c) == digest,
            "Batch input changed since review");
  }
};
struct Buffer {
  NativeWorkflowBatchOutput info;
  std::vector<unsigned char> bytes;
};
} // namespace
struct PreparedNativeWorkflowBatch::State {
  std::vector<NativeWorkflowBatchItem> items;
  std::vector<NativeWorkflowBatchOutput> outputs;
  std::vector<std::unique_ptr<detail::StagedAtomicFile>> files;
  std::size_t bytes = 0;
  bool attempted = false;
  std::vector<InputGuard> guards;
};
struct detail::NativeWorkflowBatchAccess {
  static PreparedNativeWorkflowBatch
  make(std::vector<NativeWorkflowBatchItem> items, std::vector<Buffer> buffers,
       std::size_t bytes, const ExecutionControl &control,
       const NativeWorkflowBatchProgress &progress,
       std::vector<InputGuard> guards) {
    auto s = std::make_unique<PreparedNativeWorkflowBatch::State>();
    s->items = std::move(items);
    s->guards = std::move(guards);
    s->bytes = bytes;
    s->outputs.reserve(buffers.size());
    s->files.reserve(buffers.size());
    std::size_t current = 0;
    try {
      for (auto &buffer : buffers) {
        current = buffer.info.input_index;
        checkpoint(control);
        auto staged =
            stage_atomic_file(fs::u8path(buffer.info.path), buffer.bytes.data(),
                              buffer.bytes.size(), control);
        s->outputs.push_back(std::move(buffer.info));
        s->files.push_back(std::move(staged));
        buffer.bytes.clear();
        buffer.bytes.shrink_to_fit();
      }
      checkpoint(control);
    } catch (...) {
      auto error = std::current_exception();
      bool canceled = control.cancellation && control.cancellation->canceled();
      try {
        std::rethrow_exception(error);
      } catch (const OperationCancelled &) {
        canceled = true;
      } catch (...) {
      }
      for (std::size_t i = current; i < s->items.size(); ++i) {
        auto state = i == current
                         ? (canceled ? NativeWorkflowBatchState::Canceled
                                     : NativeWorkflowBatchState::Failed)
                         : NativeWorkflowBatchState::Skipped;
        s->items[i].state = state;
        if (progress)
          try {
            progress(i, state);
          } catch (...) {
          }
      }
      std::rethrow_exception(error);
    }
    return PreparedNativeWorkflowBatch(std::move(s));
  }
};
NativeWorkflowBatchReview
review_native_workflow_batch_input(const std::string &path,
                                   const SessionLimits &limits,
                                   const ExecutionControl &control) {
  NativeWorkflowBatchReview result;
  require(!path.empty() && path.size() <= 131072 &&
              path.find('\0') == std::string::npos,
          "Invalid input review path");
  const auto canonical = fs::canonical(fs::u8path(path));
  auto ext = canonical.extension().u8string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  require(fs::is_regular_file(canonical) && ext == ".ijx",
          "Input review requires a native session");
  auto bytes =
      detail::read_session_bytes(canonical.u8string(), limits, control);
  result.encoded_bytes_ = bytes.size();
  result.fingerprint_ = detail::content_sha256(bytes, control);
  auto decoded = decode_session(bytes, limits, control);
  SessionCapture capture;
  for (const auto &doc : decoded.documents)
    capture.documents.push_back(doc->snapshot());
  capture.results = std::move(decoded.results);
  capture.workflow = std::move(decoded.workflow);
  capture.extensions = std::move(decoded.extensions);
  result.working_bytes_ = estimate_session(capture, limits).working_bytes;
  result.documents_ = std::move(capture.documents);
  result.path_ = canonical.u8string();
  result.max_file_bytes_ =
      std::min(limits.max_encoded_bytes, limits.max_working_bytes);
  result.verify(control);
  return result;
}
void NativeWorkflowBatchReview::verify(const ExecutionControl &control) const {
  require(fs::file_size(fs::u8path(path_)) == encoded_bytes_,
          "Batch input changed since review");
  require(detail::file_content_sha256(path_, max_file_bytes_, control) ==
              fingerprint_,
          "Batch input changed since review");
}
PreparedNativeWorkflowBatch::PreparedNativeWorkflowBatch(
    std::unique_ptr<State> s)
    : state_(std::move(s)) {}
PreparedNativeWorkflowBatch::~PreparedNativeWorkflowBatch() = default;
PreparedNativeWorkflowBatch::PreparedNativeWorkflowBatch(
    PreparedNativeWorkflowBatch &&) noexcept = default;
PreparedNativeWorkflowBatch &PreparedNativeWorkflowBatch::operator=(
    PreparedNativeWorkflowBatch &&) noexcept = default;
const std::vector<NativeWorkflowBatchItem> &
PreparedNativeWorkflowBatch::items() const {
  require(bool(state_), "Moved batch");
  return state_->items;
}
const std::vector<NativeWorkflowBatchOutput> &
PreparedNativeWorkflowBatch::outputs() const {
  require(bool(state_), "Moved batch");
  return state_->outputs;
}
std::size_t PreparedNativeWorkflowBatch::encoded_bytes() const {
  require(bool(state_), "Moved batch");
  return state_->bytes;
}
bool PreparedNativeWorkflowBatch::attempted() const {
  require(bool(state_), "Moved batch");
  return state_->attempted;
}
void PreparedNativeWorkflowBatch::abandon() {
  require(bool(state_), "Moved batch");
  auto &s = *state_;
  s.attempted = true;
  s.files.clear();
  for (auto &item : s.items)
    if (item.state == NativeWorkflowBatchState::Prepared)
      item.state = NativeWorkflowBatchState::Canceled;
}
void PreparedNativeWorkflowBatch::publish() {
  require(bool(state_) && !state_->attempted,
          "Batch publication already attempted");
  auto &s = *state_;
  s.attempted = true;
  for (const auto &guard : s.guards) {
    try {
      guard.verify();
    } catch (...) {
      for (std::size_t i = 0; i < s.items.size(); ++i)
        s.items[i].state = i == guard.input_index
                               ? NativeWorkflowBatchState::Failed
                               : NativeWorkflowBatchState::Skipped;
      for (auto &out : s.outputs)
        if (out.input_index == guard.input_index) {
          out.failed = true;
          break;
        }
      s.files.clear();
      throw;
    }
  }
  for (std::size_t i = 0; i < s.files.size(); ++i) {
    auto &output = s.outputs[i];
    try {
      s.files[i]->publish(false);
      output.published = true;
      if (output.session)
        s.items[output.input_index].state = NativeWorkflowBatchState::Committed;
    } catch (...) {
      output.failed = true;
      bool partial =
          std::any_of(s.outputs.begin(), s.outputs.end(), [&](const auto &v) {
            return v.input_index == output.input_index && v.published;
          });
      s.items[output.input_index].state =
          partial ? NativeWorkflowBatchState::Partial
                  : NativeWorkflowBatchState::Failed;
      for (std::size_t k = output.input_index + 1; k < s.items.size(); ++k)
        s.items[k].state = NativeWorkflowBatchState::Skipped;
      s.files.clear();
      throw;
    }
  }
  s.files.clear();
}
PreparedNativeWorkflowBatch prepare_native_workflow_batch(
    const std::vector<NativeWorkflowBatchInput> &inputs,
    const std::vector<NativeWorkflowStep> &steps, const std::string &directory,
    const NativeWorkflowBatchOptions &options, const ExecutionControl &control,
    const NativeWorkflowBatchProgress &progress) {
  std::vector<detail::NativeWorkflowStepRef> refs;
  require(steps.size() <= options.workflow.max_steps,
          "Batch workflow step limit");
  refs.reserve(steps.size());
  for (const auto &step : steps)
    std::visit([&](const auto &v) { refs.emplace_back(&v); }, step);
  return detail::prepare_native_workflow_batch_borrowed(
      inputs, refs, directory, options, control, progress);
}
PreparedNativeWorkflowBatch detail::prepare_native_workflow_batch_borrowed(
    const std::vector<NativeWorkflowBatchInput> &inputs,
    const std::vector<NativeWorkflowStepRef> &steps,
    const std::string &directory, const NativeWorkflowBatchOptions &o,
    const ExecutionControl &control,
    const NativeWorkflowBatchProgress &progress) {
  require(o.max_inputs > 0 && o.max_inputs <= 4096 && !inputs.empty() &&
              inputs.size() <= o.max_inputs,
          "Batch input limit");
  require(o.max_outputs > 0 && o.max_outputs <= 65536 &&
              o.max_working_bytes > 0,
          "Batch output/working limit");
  require(steps.size() <= o.workflow.max_steps, "Batch workflow step limit");
  checkpoint(control);
  std::size_t path_bytes = 0;
  require(directory.size() <= 131072, "Batch directory path length limit");
  for (const auto &input : inputs) {
    require(input.path.size() <= 131072 && input.output_stem.size() <= 180,
            "Batch input path/stem length limit");
    const auto n = input.path.size() + input.output_stem.size() +
                   input.expected_fingerprint.size() +
                   sizeof(NativeWorkflowBatchInput);
    require(n <= o.max_working_bytes / 4 - path_bytes,
            "Batch input path budget");
    path_bytes += n;
  }
  ExecutionScope intent_scope(control);
  std::size_t request_bytes = 0;
  auto admit_program =
      [&](const std::vector<detail::NativeWorkflowStepRef> &refs) {
        if (refs.empty())
          return;
        const auto used = detail::native_workflow_intent_bytes(
            refs, o.workflow.max_steps,
            o.max_working_bytes / 4 - path_bytes - request_bytes);
        require(used <= o.max_working_bytes / 4 - path_bytes - request_bytes,
                "Batch aggregate program limit");
        request_bytes += used;
      };
  std::set<const void *> admitted;
  for (const auto &input : inputs) {
    require(fingerprint_valid(input.expected_fingerprint),
            "Invalid Batch input fingerprint");
    if (input.selection)
      require(!input.selection->all_documents ||
                  input.selection->document_index == 0,
              "All-document override cannot carry an index");
    if (input.program && admitted.insert(&input.program->steps()).second) {
      std::vector<detail::NativeWorkflowStepRef> refs;
      for (const auto &step : input.program->steps())
        std::visit([&](const auto &v) { refs.emplace_back(&v); }, step);
      admit_program(refs);
    }
  }
  require(!directory.empty() && directory.find('\0') == std::string::npos,
          "Choose a batch output directory");
  const auto dir = fs::canonical(fs::u8path(directory));
  require(fs::is_directory(dir), "Batch output parent must exist");
  std::vector<NativeWorkflowBatchItem> items;
  for (const auto &i : inputs)
    items.push_back({i.path, NativeWorkflowBatchState::Queued});
  std::vector<fs::path> sources, planned;
  std::vector<Buffer> buffers;
  std::size_t retained = request_bytes, current = 0;
  std::vector<InputGuard> guards;
  auto notify = [&](std::size_t i, NativeWorkflowBatchState state) {
    items[i].state = state;
    if (progress)
      progress(i, state);
  };
  auto output_path = [&](const fs::path &p) {
    require(planned.size() < o.max_outputs, "Batch output count limit");
    require(p.filename().u8string().size() <= 240,
            "Batch output filename length limit");
    for (const auto &s : sources)
      require(!alias(s, p), "Batch output aliases an input");
    for (const auto &prior : planned)
      require(!alias(prior, p), "Batch outputs address the same file");
    std::error_code ec;
    auto status = fs::symlink_status(p, ec);
    if (ec && ec != std::errc::no_such_file_or_directory)
      throw fs::filesystem_error("Inspect batch output", p, ec);
    require(status.type() == fs::file_type::not_found,
            "Batch output already exists");
    planned.push_back(p);
  };
  auto add_buffer = [&](std::size_t i, const fs::path &p, bool session,
                        std::vector<unsigned char> bytes) {
    require(bytes.size() <= o.max_working_bytes - retained,
            "Batch encoded-output limit");
    retained += bytes.size();
    buffers.push_back(
        {{i, p.u8string(), session, false, false}, std::move(bytes)});
  };
  try {
    // Reserve every complete-session destination and admit every input path
    // first.
    for (current = 0; current < inputs.size(); ++current) {
      checkpoint(control);
      notify(current, NativeWorkflowBatchState::Validating);
      const auto &input = inputs[current];
      require(flat(input.output_stem), "Batch output stem must be a flat name");
      require(input.path.find('\0') == std::string::npos,
              "Invalid batch input path");
      auto path = fs::canonical(fs::u8path(input.path));
      require(fs::is_regular_file(path),
              "Batch input must be a regular session file");
      auto ext = path.extension().u8string();
      std::transform(ext.begin(), ext.end(), ext.begin(),
                     [](unsigned char c) { return char(std::tolower(c)); });
      require(ext == ".ijx",
              "Scientific batch inputs must be native sessions (.ijx)");
      for (const auto &prior : sources)
        require(!alias(prior, path) && !fs::equivalent(prior, path),
                "Duplicate/aliased batch input");
      sources.push_back(path);
      output_path(dir / fs::u8path(input.output_stem + ".ijx"));
    }
    for (current = 0; current < inputs.size(); ++current) {
      checkpoint(control);
      notify(current, NativeWorkflowBatchState::Reading);
      const auto &input = inputs[current];
      auto bytes = detail::read_session_bytes(
          sources[current].u8string(), phase_limits(o, retained), control);
      if (!input.expected_fingerprint.empty()) {
        require(detail::content_sha256(bytes, control) ==
                    input.expected_fingerprint,
                "Batch input changed since review");
        guards.push_back(
            {current, sources[current].u8string(), input.expected_fingerprint,
             std::min(o.session.max_encoded_bytes, o.session.max_working_bytes),
             bytes.size()});
      }
      auto decoded = decode_session(bytes, phase_limits(o, retained), control);
      bytes.clear();
      bytes.shrink_to_fit();
      const auto selection = input.selection.value_or(
          NativeWorkflowBatchSelection{o.all_documents, o.document_index});
      std::vector<NativeWorkflowStepRef> input_refs;
      if (input.program)
        for (const auto &step : input.program->steps())
          std::visit([&](const auto &v) { input_refs.emplace_back(&v); }, step);
      const auto &effective_steps = input.program ? input_refs : steps;
      require(!decoded.documents.empty(),
              "Batch input session has no documents");
      require(selection.all_documents ||
                  selection.document_index < decoded.documents.size(),
              "Batch document index outside input session");
      SessionCapture capture;
      capture.results = std::move(decoded.results);
      capture.workflow = std::move(decoded.workflow);
      capture.extensions = std::move(decoded.extensions);
      for (const auto &doc : decoded.documents)
        capture.documents.push_back(doc->snapshot());
      for (std::size_t d = 0; d < capture.documents.size(); ++d) {
        if (!selection.all_documents && d != selection.document_index)
          continue;
        checkpoint(control);
        notify(current, NativeWorkflowBatchState::Planning);
        auto input_estimate =
            estimate_session(capture, phase_limits(o, retained));
        require(input_estimate.working_bytes < o.max_working_bytes - retained,
                "Batch planner phase limit");
        auto available =
            o.max_working_bytes - retained - input_estimate.working_bytes;
        auto limits = o.workflow;
        limits.max_retained_bytes =
            std::min(limits.max_retained_bytes, available);
        auto prepared = prepare_native_workflow_borrowed(
            capture.documents[d], effective_steps, limits, control);
        for (const auto &out : prepared.outputs()) {
          require(flat(out.name),
                  "Batch workflow output requires a flat token");
          auto path = dir / fs::u8path(inputs[current].output_stem + ".d" +
                                       std::to_string(d + 1) + "." + out.name);
          output_path(path);
          require(out.value.data().size() <=
                      available - prepared.retained_bytes(),
                  "Batch export-copy phase limit");
          add_buffer(current, path, false, out.value.data());
          available -= out.value.data().size();
        }
        capture.documents[d] = prepared.preview();
        decoded.documents[d].reset();
      }
      notify(current, NativeWorkflowBatchState::Encoding);
      auto encoded =
          encode_session(capture, phase_limits(o, retained), control);
      add_buffer(current,
                 dir / fs::u8path(inputs[current].output_stem + ".ijx"), true,
                 std::move(encoded));
      notify(current, NativeWorkflowBatchState::Prepared);
    }
    for (const auto &guard : guards) {
      current = guard.input_index;
      guard.verify(control);
    }
    current = inputs.size();
    return NativeWorkflowBatchAccess::make(std::move(items), std::move(buffers),
                                           retained - request_bytes, control,
                                           progress, std::move(guards));
  } catch (...) {
    auto error = std::current_exception();
    if (current < inputs.size()) {
      bool canceled = control.cancellation && control.cancellation->canceled();
      try {
        std::rethrow_exception(error);
      } catch (const OperationCancelled &) {
        canceled = true;
      } catch (...) {
      }
      try {
        notify(current, canceled ? NativeWorkflowBatchState::Canceled
                                 : NativeWorkflowBatchState::Failed);
      } catch (...) {
      }
      for (std::size_t i = current + 1; i < inputs.size(); ++i)
        try {
          notify(i, NativeWorkflowBatchState::Skipped);
        } catch (...) {
        }
    }
    std::rethrow_exception(error);
  }
}
} // namespace imagejxx
