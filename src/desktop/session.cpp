#include "desktop.hpp"
#include <imagejxx/session.hpp>
#include <filesystem>
#include <fstream>

namespace imagejxx::desktop {
namespace {
MetadataArray table(const Json &value) {
  if (!value.is_array())
    throw std::invalid_argument("Session tables must be arrays");
  MetadataArray result;
  result.reserve(value.size());
  for (const auto &entry : value)
    result.push_back(metadataFromJson(entry));
  return result;
}
Json table(const MetadataArray &value) {
  Json result = Json::array();
  for (const auto &entry : value)
    result.push_back(metadataJson(entry));
  return result;
}
} // namespace

bool nativeSessionFile(const std::string &path) {
  std::ifstream stream(std::filesystem::u8path(path), std::ios::binary);
  if (!stream.is_open())
    throw std::runtime_error("Could not open session");
  char header[10]{};
  stream.read(header, sizeof(header));
  return stream.gcount() == sizeof(header) &&
         (std::string_view(header, sizeof(header)) == "IMAGEJXX3\n" ||
          std::string_view(header, sizeof(header)) == "IMAGEJXX4\n" ||
          std::string_view(header, sizeof(header)) == "IMAGEJXX5\n");
}

void saveNativeSession(const std::string &path, const Documents &documents,
                       const Json &results, const Json &workflow,
                       const Json &extensions) {
  SessionCapture capture;
  std::vector<std::shared_ptr<imagejxx::Document>> owners;
  capture.documents.reserve(documents.size());
  owners.reserve(documents.size());
  for (const auto &document : documents) {
    if (!document)
      throw std::invalid_argument("Session document is required");
    owners.push_back(document->nativeOwner());
    capture.documents.push_back(owners.back()->snapshot());
  }
  capture.results = table(results);
  capture.workflow = table(workflow);
  capture.extensions = metadataObjectFromJson(extensions);
  imagejxx::save_session(path, capture);
  // Mark only the successfully published capture, retaining dirtiness if an
  // independent native caller edited an owner while the file was written.
  for (std::size_t index = 0; index < owners.size(); ++index)
    owners[index]->mark_saved(capture.documents[index]);
}

Documents loadNativeSession(const std::string &path, Json &results,
                            Json &workflow, Json *extensions) {
  auto decoded = imagejxx::load_session(path);
  auto nextResults = table(decoded.results);
  auto nextWorkflow = table(decoded.workflow);
  auto nextExtensions = metadataJson(decoded.extensions);
  if (!extensions && !decoded.extensions.empty())
    throw std::invalid_argument("A session extension destination is required");
  Documents documents;
  documents.reserve(decoded.documents.size());
  // Construct every constrained eager facade, including supported C/Z/T,
  // before publishing any owner or table. Unsupported sources/displays refuse.
  for (auto &document : decoded.documents)
    documents.push_back(std::make_shared<Document>(std::move(document)));
  results.swap(nextResults);
  workflow.swap(nextWorkflow);
  if (extensions)
    extensions->swap(nextExtensions);
  return documents;
}
} // namespace imagejxx::desktop
