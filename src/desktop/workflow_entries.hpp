#pragma once
// Private authored-row codec. Execution remains with the existing command owners.
#include <algorithm>
#include "analysis_measurement.hpp"
#include "display_export_record.hpp"
#include "render_memory.hpp"
#include <imagejxx/gaussian_command.hpp>
#include <imagejxx/rank_command.hpp>

namespace imagejxx::desktop {
inline bool isWorkflowEntry(const Json& value) {
  return value.is_object() && value.value("schema",Json{})=="imagejxx.workflow-entry";
}
inline const Json& workflowRecord(const Json& value) {
  if(!isWorkflowEntry(value))return value;
  if(value.size()!=4 || !value.contains("version") ||
      !value.at("version").is_number_integer() || value.at("version")!=1 ||
      !value.contains("enabled") || !value.at("enabled").is_boolean() ||
      !value.contains("record") || !value.at("record").is_object() ||
      isWorkflowEntry(value.at("record")))
    throw std::invalid_argument("Invalid workflow entry schema/version/fields");
  return value.at("record");
}
inline bool workflowEnabled(const Json& value) {
  (void)workflowRecord(value);
  return !isWorkflowEntry(value) || value.at("enabled").get<bool>();
}
inline bool hasWorkflowEntries(const Json& table) {
  return table.is_array() && std::any_of(table.begin(),table.end(),isWorkflowEntry);
}
inline std::size_t workflowEntryBytes(const Json& value) {
  const auto& record=workflowRecord(value);
  if(!record.is_object())throw std::invalid_argument("Workflow command must be an object");
  auto bytes=isDisplayExportRecord(record)?displayExportRecordWorkingBytes(record):measurementRecordWorkingBytes(record);
  return RenderMemory::add(bytes,isWorkflowEntry(value)?1024:0);
}
inline void validateWorkflowEntry(const Json& value) {
  const auto& record=workflowRecord(value);
  if(isMeasurementRecord(record))(void)decodeMeasurementRecord(record);
  else if(isProfileRecord(record))(void)decodeProfileRecord(record);
  else if(isDisplayExportRecord(record))(void)decodeDisplayExportRecord(record);
  else if(record.contains("schema_version")) {
    const auto operation=record.value("operation",Json{});
    if(operation=="gaussian_blur_imagej")validate_gaussian_command_record(metadataObjectFromJson(record));
    else if(operation=="rank_filter_imagej" || operation=="remove_outliers_imagej" || operation=="top_hat_imagej" || operation=="remove_nans_imagej")validate_rank_command_record(metadataObjectFromJson(record));
    // Other families remain opaque here; their owning execution validates them.
  }
}
inline std::size_t workflowTableBytes(const Json& table) {
  if(!table.is_array() || table.size()>256)throw std::invalid_argument("Workflow editor supports at most256 commands");
  std::size_t bytes=8192;
  for(const auto& value:table)bytes=RenderMemory::add(bytes,workflowEntryBytes(value));
  return bytes;
}
inline Json workflowEntry(Json record,bool enabled=true) {
  if(isWorkflowEntry(record)){(void)workflowRecord(record);return record;}
  Json entry=Json::object();entry["schema"]="imagejxx.workflow-entry";entry["version"]=1;
  entry["enabled"]=enabled;entry["record"]=std::move(record);return entry;
}
inline Json canonicalWorkflowEntries(const Json& table) {
  Json entries=Json::array();auto& rows=entries.get_ref<Json::array_t&>();rows.reserve(table.size());
  for(const auto& record:table)rows.push_back(workflowEntry(record));
  return entries;
}
} // namespace imagejxx::desktop
