#include "desktop.hpp"
#include "../native/session_metadata.hpp"

namespace imagejxx::desktop {
namespace session_metadata = imagejxx::detail::session_metadata;
MetadataValue metadataFromJson(const Json &value) {
  return session_metadata::from_json(value);
}
MetadataObject metadataObjectFromJson(const Json &value) {
  return session_metadata::object_from_json(value);
}
Json metadataJson(const MetadataObject &value) {
  return session_metadata::to_json(value);
}
Json metadataJson(const MetadataValue &value) {
  return session_metadata::to_json(value);
}
} // namespace imagejxx::desktop
