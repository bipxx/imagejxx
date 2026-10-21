#pragma once

#include <cstddef>
#include <filesystem>
#include <imagejxx/execution.hpp>
#include <memory>
#include <string_view>

namespace imagejxx::detail {
// Internal bounded streaming sibling. Construction freezes an absolute
// destination and plans a unique name without creating a file;
// open reserves it exactly once, and destruction removes only an owned sibling.
// Windows retains the created file handle through staging: rename/deletion
// operate on that object even when another actor reuses the temporary pathname.
// POSIX anchors operations to the opened parent and pins the created inode.
// Publication/cleanup refuse substituted entries at their final identity check;
// concurrent mutation between that check and renameat/unlinkat is not excluded.
// Linux no-replace publication links the held descriptor (AT_EMPTY_PATH or
// trusted procfs); it never falls back to linking a mutable stage pathname.
class StagedAtomicStream {
public:
  explicit StagedAtomicStream(const std::filesystem::path &);
  ~StagedAtomicStream();
  StagedAtomicStream(const StagedAtomicStream &) = delete;
  StagedAtomicStream &operator=(const StagedAtomicStream &) = delete;
  const std::filesystem::path &temporary_path() const;
  void open(const ExecutionControl &);
  void append(const unsigned char *, std::size_t, const ExecutionControl &);
  // Replace bytes wholly inside the successfully appended extent. Never
  // extend the file or reopen its pathname; subsequent append keeps its end
  // position. A write/seek/callback failure poisons this stream so a partial
  // patch cannot be published. Argument errors leave the stream usable.
  void patch(std::size_t offset, const unsigned char *, std::size_t,
             const ExecutionControl &);
  void close(const ExecutionControl &);
  void publish(bool replace_existing);

private:
  struct State;
  std::unique_ptr<State> state_;
};

// IO controls are explicit: a preferences write with its default empty control
// does not inherit an unrelated processing scope on the calling thread.
void file_checkpoint(const ExecutionControl &control);
void file_stage_progress(const ExecutionControl &control, std::string_view name,
                         std::size_t completed, std::size_t total);

// Destination parent creation and validation belong to the caller. Reserve an
// exclusive sibling, write/flush/close it, then replace once. Any exception
// before replacement removes only the temporary owned by this invocation.
// The diagnostic noun preserves the existing preferences error messages.
// Publication is the final fallible operation; callbacks never run afterward.
void atomic_replace_file(const std::filesystem::path &destination,
                         const unsigned char *bytes, std::size_t count,
                         const char *diagnostic_noun,
                         const ExecutionControl &control = {});

// Stage on a worker, then publish after the owner revalidates its source on the
// UI thread. Destruction before publication removes only the exclusive sibling.
// No callbacks run after publication. Existing atomic_replace_file is
// unchanged.
class StagedAtomicFile {
public:
  ~StagedAtomicFile();
  StagedAtomicFile(const StagedAtomicFile &) = delete;
  StagedAtomicFile &operator=(const StagedAtomicFile &) = delete;
  void publish(bool replace_existing);

private:
  struct State;
  std::unique_ptr<State> state_;
  explicit StagedAtomicFile(std::unique_ptr<State>);
  friend std::unique_ptr<StagedAtomicFile>
  stage_atomic_file(const std::filesystem::path &, const unsigned char *,
                    std::size_t, const ExecutionControl &);
};
std::unique_ptr<StagedAtomicFile>
stage_atomic_file(const std::filesystem::path &, const unsigned char *,
                  std::size_t, const ExecutionControl & = {});

} // namespace imagejxx::detail
