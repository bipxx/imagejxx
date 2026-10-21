#pragma once

#include "desktop.hpp"
#include "rank_runtime_probe.hpp"
#include <cmath>
#include <cstddef>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace imagejxx::desktop {
namespace actual_observation_transport_detail {

inline constexpr std::size_t minimum_blob_bytes = 1024;
inline constexpr std::size_t maximum_blobs = 8192;
inline constexpr std::size_t maximum_references = 8192;
inline constexpr std::size_t maximum_payload_bytes = 64 * 1024 * 1024;
inline constexpr std::size_t maximum_depth = 64;

inline bool reference(const Json &value) {
  return value.is_object() && value.size() == 1 && value.contains("$actual_blob");
}

inline bool identityField(const std::string &key) {
  return key == "runtime" || key == "runtime_before" || key == "runtime_after" ||
      key == "provenance" || key == "source" || key == "sources" ||
      key == "source_pins" || key == "producer_source" ||
      key == "producer_artifact" || key == "artifacts";
}

inline std::string digest(const std::string &serialized) {
  return rankRuntimeSha256(std::vector<unsigned char>(serialized.begin(), serialized.end()));
}

inline bool containsReference(const Json &value, std::size_t depth = 0) {
  if (depth > maximum_depth)
    throw std::length_error("Actual observation transport nesting exceeds64");
  if (value.is_number_float() && !std::isfinite(value.get<double>()))
    throw std::invalid_argument("Actual observation JSON must be finite");
  if (reference(value)) return true;
  if (value.is_structured())
    for (const auto &child : value)
      if (containsReference(child, depth + 1)) return true;
  return false;
}

class Interner {
public:
  explicit Interner(const Json &report) {
    const auto found = report.find("actual_blob_transport");
    if (found == report.end()) return;
    if (!found->is_object() || found->size() != 3 ||
        !found->contains("version") || !found->at("version").is_number_integer() ||
        found->at("version") != 1 ||
        !found->contains("encoding") || found->at("encoding") != "utf8-json" ||
        !found->contains("blobs") || !found->at("blobs").is_object())
      throw std::invalid_argument("Invalid existing actual observation transport");
    blobs_ = found->at("blobs");
    if (blobs_.size() > maximum_blobs)
      throw std::length_error("Actual observation transport exceeds8192 blobs");
    for (auto item = blobs_.begin(); item != blobs_.end(); ++item) {
      if (!item.value().is_string())
        throw std::invalid_argument("Actual observation blob must contain JSON text");
      const auto &serialized = item.value().get_ref<const std::string &>();
      charge(serialized.size());
      if (digest(serialized) != item.key() ||
          containsReference(Json::parse(serialized)))
        throw std::invalid_argument("Existing actual blob digest or reference-free payload differs");
    }
  }

  void validateReferences(const Json &value, std::size_t depth = 0) {
    if (depth > maximum_depth)
      throw std::length_error("Actual observation transport nesting exceeds64");
    if (value.is_number_float() && !std::isfinite(value.get<double>()))
      throw std::invalid_argument("Actual observation JSON must be finite");
    if (reference(value)) {
      if (!value.at("$actual_blob").is_string() ||
          !blobs_.contains(value.at("$actual_blob").get_ref<const std::string &>()))
        throw std::invalid_argument("Actual observation reference has no matching blob");
      if (++references_ > maximum_references)
        throw std::length_error("Actual observation transport exceeds8192 references");
      used_blobs_.insert(value.at("$actual_blob").get_ref<const std::string &>());
      return;
    }
    if (value.is_object()) {
      for (auto item = value.begin(); item != value.end(); ++item)
        if (item.key() != "actual_blob_transport")
          validateReferences(item.value(), depth + 1);
    } else if (value.is_array()) {
      for (const auto &child : value) validateReferences(child, depth + 1);
    }
  }

  void validateMembership() const {
    if (used_blobs_.size() != blobs_.size())
      throw std::invalid_argument("Existing actual observation transport has orphan blobs");
  }

  void field(Json &value) {
    // Existing references are leaves. Never put references inside blob text.
    if (reference(value) || containsReference(value)) return;
    auto serialized = value.dump(); // Exact compact UTF-8 JSON; no projection.
    if (serialized.size() < minimum_blob_bytes) return;
    if (++references_ > maximum_references)
      throw std::length_error("Actual observation transport exceeds8192 references");
    const auto key = digest(serialized);
    const auto found = blobs_.find(key);
    if (found != blobs_.end()) {
      if (found->get_ref<const std::string &>() != serialized)
        throw std::runtime_error("Actual observation SHA256 collision");
    } else {
      if (blobs_.size() == maximum_blobs)
        throw std::length_error("Actual observation transport exceeds8192 blobs");
      charge(serialized.size());
      blobs_.emplace(key, std::move(serialized));
    }
    replacements_.push_back({&value, Json{{"$actual_blob", key}}});
    selected_.insert(&value);
  }

  void pixels(Json &value, std::size_t depth = 0) {
    if (depth > maximum_depth)
      throw std::length_error("Actual observation transport nesting exceeds64");
    if (reference(value) || selected_.count(&value)) return;
    if (value.is_object()) {
      for (auto item = value.begin(); item != value.end(); ++item) {
        if (item.key() == "actual_blob_transport" ||
            (depth == 0 && identityField(item.key()))) continue;
        auto &child = item.value();
        if (item.key() == "pixels" && child.is_object() &&
            child.contains("hex") && child.contains("storage_f64le_hex"))
          field(child);
        if (!selected_.count(&child)) pixels(child, depth + 1);
      }
    } else if (value.is_array()) {
      for (auto &child : value) pixels(child, depth + 1);
    }
  }

  void publish(Json &report) {
    Json transport{{"version", 1}, {"encoding", "utf8-json"},
        {"blobs", std::move(blobs_)}};
    // All serialization, hashing, allocation and collision checks precede
    // replacement. Map insertion keeps the selected report nodes stable.
    report["actual_blob_transport"] = std::move(transport);
    for (auto &replacement : replacements_) replacement.first->swap(replacement.second);
  }

private:
  void charge(std::size_t bytes) {
    if (bytes > maximum_payload_bytes - payload_bytes_)
      throw std::length_error("Actual observation blob payloads exceed64MiB");
    payload_bytes_ += bytes;
  }
  Json blobs_ = Json::object();
  std::size_t payload_bytes_ = 0, references_ = 0;
  std::set<const Json *> selected_;
  std::set<std::string> used_blobs_;
  std::vector<std::pair<Json *, Json>> replacements_;
};
} // namespace actual_observation_transport_detail

// Lossless private wire transport only. Runtime/source/provenance fields,
// events, frame membership, checkpoints and all actual values are preserved.
inline void internActualObservationFields(Json &report) {
  using namespace actual_observation_transport_detail;
  if (!report.is_object())
    throw std::invalid_argument("Actual observation report must be an object");
  Interner interner(report);
  interner.validateReferences(report);
  interner.validateMembership();
  const auto frames = report.find("frames");
  if (frames != report.end()) {
    if (!frames->is_array())
      throw std::invalid_argument("Actual observation frames must be an array");
    for (auto &frame : *frames) {
      if (!frame.is_object())
        throw std::invalid_argument("Actual observation frame must be an object");
      const auto fields = frame.find("report");
      if (fields == frame.end()) continue;
      if (!fields->is_object())
        throw std::invalid_argument("Actual frame report must be an object");
      for (auto field = fields->begin(); field != fields->end(); ++field)
        if (!identityField(field.key())) interner.field(field.value());
    }
  }
  interner.pixels(report);
  interner.publish(report);
}
} // namespace imagejxx::desktop
