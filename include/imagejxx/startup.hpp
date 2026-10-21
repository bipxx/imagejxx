#pragma once

#include <imagejxx/metadata.hpp>
#include <imagejxx/preferences.hpp>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace imagejxx {
enum class StartupOverride { Inherit, Suppress, RequireConfigured };
enum class StartupSource { Empty, ExplicitPaths, ExplicitWorkspace, PersistedWorkspace };
enum class StartupStage { Planning, Reading, Decoding, Validation, Publication, Presentation };
enum class StartupReason {
  InvalidRequest, InvalidPath, PathLimitExceeded, DuplicatePath,
  WorkspaceNotConfigured, MissingPresentation, FuturePresentation,
  InvalidPresentation, ReadFailed, DecodeFailed, UnsupportedDocument,
  AdmissionFailed, PublicationFailed
};
const char *startup_stage_name(StartupStage stage);
const char *startup_reason_name(StartupReason reason);
struct StartupDiagnostic {
  StartupStage stage = StartupStage::Planning;
  StartupReason reason = StartupReason::InvalidRequest;
  std::string path, detail;
  bool recoverable = true;
};
// Error payloads are detached, bounded native diagnostics. The planner performs
// no file I/O; execution consumers use the same error for staged failures.
class StartupError : public std::runtime_error {
public:
  explicit StartupError(StartupDiagnostic diagnostic);
  const StartupDiagnostic &diagnostic() const noexcept { return diagnostic_; }
private:
  StartupDiagnostic diagnostic_;
};
struct StartupLimits {
  std::size_t max_explicit_paths = 16;
  std::size_t max_path_bytes = 4096;
  std::size_t max_layout_bytes = 1024 * 1024;
};
struct StartupRequest {
  std::vector<std::string> explicit_paths;
  std::optional<std::string> explicit_workspace;
  StartupOverride restore_workspace = StartupOverride::Inherit;
  bool reset_layout = false;
  std::string launch_cwd;
  std::string preferences_write_path;
};
struct StartupPlan {
  StartupSource source = StartupSource::Empty;
  std::vector<std::string> paths;
  bool reset_layout = false;
  std::vector<StartupDiagnostic> diagnostics;
  std::string ignored_persisted_reason;
};
// Lexical resolution only: explicit paths use the supplied absolute launch cwd,
// persisted paths use the selected preferences WRITE destination's parent.
// Never reads environment, files, process cwd, document owners or callbacks.
StartupPlan plan_startup(const PreferencesDocument &preferences,
                         const StartupRequest &request,
                         const StartupLimits &limits = {});

struct WorkspacePresentation {
  std::optional<std::size_t> active_index;
  bool supported = false;
  bool future_owned = false;
  std::vector<StartupDiagnostic> diagnostics;
};
struct WorkspacePresentationUpdate {
  MetadataObject extensions;
  std::vector<StartupDiagnostic> diagnostics;
};
// Explicit consumers of the reserved child; the generic session codec stays
// opaque. Missing/future children fall back to the final owner with a warning.
WorkspacePresentation workspace_presentation(const MetadataObject &extensions,
                                              std::size_t document_count);
// A future-owned child is returned unchanged with a diagnostic, never replaced.
WorkspacePresentationUpdate with_workspace_presentation(
    const MetadataObject &extensions, std::optional<std::size_t> active_index,
    std::size_t document_count);
} // namespace imagejxx
