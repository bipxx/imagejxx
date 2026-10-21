#include <imagejxx/startup.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <set>
#include <utility>

namespace imagejxx {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
std::string boundedText(const std::string &text, std::size_t limit) {
  auto result = Json::parse(Json(text.substr(0, limit)).dump(
      -1, ' ', false, Json::error_handler_t::replace)).get<std::string>();
  std::replace(result.begin(), result.end(), '\0', '?');
  if (result.size() <= limit)
    return result;
  result.resize(limit);
  // Truncate at a UTF-8 boundary; replacement above admitted only strict text.
  while (!result.empty()) {
    try {
      (void)Json(result).dump();
      break;
    } catch (const Json::exception &) {
      result.pop_back();
    }
  }
  return result;
}
StartupDiagnostic bounded(StartupDiagnostic diagnostic) {
  diagnostic.path = boundedText(diagnostic.path, 4096);
  diagnostic.detail = boundedText(diagnostic.detail, 1024);
  return diagnostic;
}
std::string errorMessage(const StartupDiagnostic &diagnostic) {
  return std::string(startup_stage_name(diagnostic.stage)) + ": " +
         startup_reason_name(diagnostic.reason) + ": " +
         boundedText(diagnostic.detail, 1024);
}
[[noreturn]] void refuse(StartupStage stage, StartupReason reason,
                         const std::string &path, const std::string &detail) {
  throw StartupError({stage, reason, path, detail, true});
}
void validateText(const std::string &text, std::size_t limit,
                  const char *name, bool empty = false) {
  if ((!empty && text.empty()) || text.find('\0') != std::string::npos)
    refuse(StartupStage::Planning, StartupReason::InvalidPath, text,
           std::string(name) + " must be nonempty UTF-8 without NUL");
  if (text.size() > limit)
    refuse(StartupStage::Planning, StartupReason::PathLimitExceeded, text,
           std::string(name) + " exceeds the UTF-8 byte limit");
  try {
    (void)Json(text).dump();
  } catch (const Json::exception &) {
    refuse(StartupStage::Planning, StartupReason::InvalidPath, text,
           std::string(name) + " must be strict UTF-8");
  }
}
fs::path resolvedPath(const std::string &text, const fs::path &base,
                      std::size_t limit, const char *name) {
  validateText(text, limit, name);
  const auto input = fs::u8path(text);
  if (!input.is_absolute() &&
      (input.has_root_name() || input.has_root_directory()))
    refuse(StartupStage::Planning, StartupReason::InvalidPath, text,
           "Drive-relative and root-relative paths require an absolute drive");
  const auto result = (input.is_absolute() ? input : base / input).lexically_normal();
  validateText(result.u8string(), limit, name);
  return result;
}
void validateLimits(const StartupLimits &limits) {
  const StartupLimits ceiling;
  if (!limits.max_explicit_paths ||
      limits.max_explicit_paths > ceiling.max_explicit_paths ||
      !limits.max_path_bytes || limits.max_path_bytes > ceiling.max_path_bytes ||
      !limits.max_layout_bytes || limits.max_layout_bytes > ceiling.max_layout_bytes)
    refuse(StartupStage::Planning, StartupReason::InvalidRequest, "",
           "Startup limits must be positive and within the native ceilings");
}
const MetadataObject *object(const MetadataValue &value) {
  const auto tree = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  return tree && *tree ? std::get_if<MetadataObject>(&(*tree)->value) : nullptr;
}
std::optional<std::uint64_t> unsignedInteger(const MetadataValue &value) {
  if (const auto number = std::get_if<std::int64_t>(&value))
    return *number >= 0 ? std::optional<std::uint64_t>(*number) : std::nullopt;
  if (const auto number = std::get_if<std::uint64_t>(&value))
    return *number;
  return std::nullopt;
}
[[noreturn]] void invalidPresentation(const char *detail) {
  refuse(StartupStage::Presentation, StartupReason::InvalidPresentation, "", detail);
}
void validateActive(std::optional<std::size_t> active, std::size_t count) {
  if ((count == 0 && active) || (count != 0 && (!active || *active >= count)))
    invalidPresentation("active_index must be null exactly for zero documents, otherwise an in-range ordinal");
}
} // namespace

const char *startup_stage_name(StartupStage stage) {
  switch (stage) {
  case StartupStage::Planning: return "planning";
  case StartupStage::Reading: return "reading";
  case StartupStage::Decoding: return "decoding";
  case StartupStage::Validation: return "validation";
  case StartupStage::Publication: return "publication";
  case StartupStage::Presentation: return "presentation";
  }
  throw std::invalid_argument("Unknown startup stage");
}
const char *startup_reason_name(StartupReason reason) {
  switch (reason) {
  case StartupReason::InvalidRequest: return "invalid_request";
  case StartupReason::InvalidPath: return "invalid_path";
  case StartupReason::PathLimitExceeded: return "path_limit_exceeded";
  case StartupReason::DuplicatePath: return "duplicate_path";
  case StartupReason::WorkspaceNotConfigured: return "workspace_not_configured";
  case StartupReason::MissingPresentation: return "missing_presentation";
  case StartupReason::FuturePresentation: return "future_presentation";
  case StartupReason::InvalidPresentation: return "invalid_presentation";
  case StartupReason::ReadFailed: return "read_failed";
  case StartupReason::DecodeFailed: return "decode_failed";
  case StartupReason::UnsupportedDocument: return "unsupported_document";
  case StartupReason::AdmissionFailed: return "admission_failed";
  case StartupReason::PublicationFailed: return "publication_failed";
  }
  throw std::invalid_argument("Unknown startup reason");
}
StartupError::StartupError(StartupDiagnostic diagnostic)
    : std::runtime_error(errorMessage(diagnostic)), diagnostic_(bounded(std::move(diagnostic))) {}

StartupPlan plan_startup(const PreferencesDocument &preferences,
                         const StartupRequest &request,
                         const StartupLimits &limits) {
  validateLimits(limits);
  switch (request.restore_workspace) {
  case StartupOverride::Inherit:
  case StartupOverride::Suppress:
  case StartupOverride::RequireConfigured: break;
  default:
    refuse(StartupStage::Planning, StartupReason::InvalidRequest, "",
           "Unknown startup restore override");
  }
  validateText(request.launch_cwd, limits.max_path_bytes, "launch_cwd");
  const auto cwd = fs::u8path(request.launch_cwd).lexically_normal();
  if (!cwd.is_absolute())
    refuse(StartupStage::Planning, StartupReason::InvalidRequest, request.launch_cwd,
           "launch_cwd must be an absolute captured launch directory");
  if (request.explicit_paths.size() > limits.max_explicit_paths)
    refuse(StartupStage::Planning, StartupReason::PathLimitExceeded, "",
           "Too many explicit startup paths");
  if (request.explicit_workspace &&
      (!request.explicit_paths.empty() || request.restore_workspace == StartupOverride::Suppress))
    refuse(StartupStage::Planning, StartupReason::InvalidRequest, "",
           "An explicit workspace cannot be combined with images or suppressed restore");
  if (!request.reset_layout) {
    try {
      const auto extras = Json::parse(preferences.extras_json);
      if (!extras.is_object())
        throw std::invalid_argument("Preferences extras must be an object");
      const auto saved = extras.find("layout");
      if (saved != extras.end()) {
        if (!saved->is_string())
          throw std::invalid_argument("Saved window layout must be text");
        const auto layout = saved->get<std::string>();
        validate_preferences_layout(layout);
        if (layout.size() > limits.max_layout_bytes)
          throw std::invalid_argument("Saved window layout exceeds the requested startup byte limit");
      }
    } catch (const std::exception &error) {
      refuse(StartupStage::Validation, StartupReason::InvalidRequest, "", error.what());
    }
  }

  StartupPlan plan;
  plan.reset_layout = request.reset_layout;
  if (!request.explicit_paths.empty()) {
    plan.source = StartupSource::ExplicitPaths;
    plan.ignored_persisted_reason = "explicit_paths";
    std::set<std::string> seen;
    for (const auto &path : request.explicit_paths) {
      const auto resolved = resolvedPath(path, cwd, limits.max_path_bytes, "explicit path").u8string();
      auto identity = resolved;
#ifdef _WIN32
      // Lexical matching only; never follows links or probes filesystem state.
      std::transform(identity.begin(), identity.end(), identity.begin(),
                     [](unsigned char c) { return static_cast<char>(
                         c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c); });
#endif
      if (!seen.insert(identity).second)
        refuse(StartupStage::Planning, StartupReason::DuplicatePath, resolved,
               "Duplicate normalized explicit startup path");
      plan.paths.push_back(resolved);
    }
  } else if (request.explicit_workspace) {
    plan.source = StartupSource::ExplicitWorkspace;
    plan.ignored_persisted_reason = "explicit_workspace";
    plan.paths.push_back(resolvedPath(*request.explicit_workspace, cwd,
                                      limits.max_path_bytes, "explicit workspace").u8string());
  } else if (request.restore_workspace == StartupOverride::Suppress) {
    plan.ignored_persisted_reason = "suppressed";
  } else if (preferences.settings.text("startup_mode") == "workspace") {
    if (request.preferences_write_path.empty())
      refuse(StartupStage::Planning, StartupReason::InvalidRequest, "",
             "Persisted workspace resolution requires the selected preferences write path");
    const auto destination = resolvedPath(request.preferences_write_path, cwd,
                                           limits.max_path_bytes, "preferences write path");
    plan.source = StartupSource::PersistedWorkspace;
    plan.paths.push_back(resolvedPath(preferences.settings.text("startup_workspace"),
                                      destination.parent_path(), limits.max_path_bytes,
                                      "persisted workspace").u8string());
  } else if (request.restore_workspace == StartupOverride::RequireConfigured) {
    refuse(StartupStage::Planning, StartupReason::WorkspaceNotConfigured, "",
           "Workspace restoration was required but no workspace is configured");
  } else {
    plan.ignored_persisted_reason = "startup_mode_empty";
  }
  return plan;
}

WorkspacePresentation workspace_presentation(const MetadataObject &extensions,
                                              std::size_t document_count) {
  WorkspacePresentation result;
  if (document_count)
    result.active_index = document_count - 1;
  const auto found = extensions.find("imagejxx_workspace");
  if (found == extensions.end()) {
    result.diagnostics.push_back({StartupStage::Presentation, StartupReason::MissingPresentation,
                                  "", "Workspace has no active-owner metadata; using the final owner", true});
    return result;
  }
  const auto child = object(found->second);
  if (!child)
    invalidPresentation("imagejxx_workspace must be an object");
  const auto versionField = child->find("version");
  if (versionField == child->end())
    invalidPresentation("Workspace presentation requires an integer version");
  const auto version = unsignedInteger(versionField->second);
  if (!version || *version == 0)
    invalidPresentation("Workspace presentation version must be a positive integer");
  if (*version > 1) {
    result.future_owned = true;
    result.diagnostics.push_back({StartupStage::Presentation, StartupReason::FuturePresentation,
                                  "", "Future workspace presentation remains opaque; using the final owner", true});
    return result;
  }
  const auto activeField = child->find("active_index");
  if (activeField == child->end())
    invalidPresentation("Version 1 workspace presentation requires active_index");
  std::optional<std::size_t> active;
  if (!std::holds_alternative<std::monostate>(activeField->second)) {
    const auto ordinal = unsignedInteger(activeField->second);
    if (!ordinal || *ordinal > std::numeric_limits<std::size_t>::max())
      invalidPresentation("Workspace active_index must be null or an unsigned ordinal");
    active = static_cast<std::size_t>(*ordinal);
  }
  validateActive(active, document_count);
  result.active_index = active;
  result.supported = true;
  return result;
}

WorkspacePresentationUpdate with_workspace_presentation(
    const MetadataObject &extensions, std::optional<std::size_t> active_index,
    std::size_t document_count) {
  validateActive(active_index, document_count);
  WorkspacePresentationUpdate result{extensions, {}};
  MetadataObject child;
  const auto found = extensions.find("imagejxx_workspace");
  if (found != extensions.end()) {
    const auto existing = object(found->second);
    if (!existing)
      invalidPresentation("imagejxx_workspace must be an object");
    const auto versionField = existing->find("version");
    const auto version = versionField == existing->end()
                             ? std::nullopt : unsignedInteger(versionField->second);
    if (!version || !*version)
      invalidPresentation("Workspace presentation version must be a positive integer");
    if (*version > 1) {
      result.diagnostics.push_back({StartupStage::Presentation, StartupReason::FuturePresentation,
                                    "", "Future workspace presentation remains opaque and was preserved unchanged", true});
      return result;
    }
    const auto activeField = existing->find("active_index");
    if (activeField == existing->end() ||
        (!std::holds_alternative<std::monostate>(activeField->second) &&
         !unsignedInteger(activeField->second)))
      invalidPresentation("Version 1 workspace presentation requires null or integer active_index");
    // The previous ordinal belongs to the previous saved membership. It may
    // be outside the new count after documents close; replace it with the
    // caller's validated current ordinal without reinterpreting old IDs.
    child = *existing;
  }
  child["version"] = std::int64_t(1);
  child["active_index"] = active_index ? MetadataValue(std::uint64_t(*active_index))
                                        : MetadataValue(std::monostate{});
  result.extensions["imagejxx_workspace"] = metadata_object(std::move(child));
  return result;
}
} // namespace imagejxx
