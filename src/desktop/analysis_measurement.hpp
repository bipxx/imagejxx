#pragma once
#include "desktop.hpp"
#include <imagejxx/target_analysis.hpp>
#include <imagejxx/settings.hpp>

namespace imagejxx::desktop {
bool captured_measurement_available(const DocumentSnapshot&);
bool captured_roi_measurement_available(const DocumentSnapshot&);
bool captured_profile_available(const DocumentSnapshot&);
// Extra frontend row storage, separately admitted before any source acquisition.
std::size_t captured_measurement_row_bytes(const ResolvedTargetAnalysis&);
Json captured_measurement_rows(const ResolvedTargetAnalysis&,const TargetAnalysisResult&);
// Portable recorder intent; no retained source, provider, target or result objects.
struct MeasurementRecordSource {
  std::uint64_t document_id=0;
  Revisions revisions;
  DatasetShape shape;
  PixelType pixel_type=PixelType::UInt8;
  PlanePosition position;
  Json threshold;
  std::string input_fingerprint;
  std::optional<bool> composite;
  std::uint64_t global_generation=0,bypass_generation=0;
  bool uses_global=false,ignores_global=false;
};
struct MeasurementManagerRecord {
  std::vector<std::size_t> indices;
  std::size_t total=0;
  std::string fingerprint;
};
struct MeasurementRecord {
  MeasurementRecordSource source;
  std::optional<MeasurementRecordSource> redirect;
  Settings settings;
  std::optional<MeasurementManagerRecord> manager;
};
bool isMeasurementRecord(const Json&);
std::size_t measurementRecordWorkingBytes(const Json&);
MeasurementRecord decodeMeasurementRecord(const Json&);
Json captureMeasurementRecord(const ResolvedTargetAnalysis&,const Settings&);
MeasurementRecordSource captureMeasurementReplaySource(const ResolvedTarget&,bool consumeComposite=false);
void validateMeasurementReplaySource(const MeasurementRecordSource&,const ResolvedTarget&,bool rebind,bool ignoreSelection=false);
std::string measurementManagerFingerprint(const DocumentSnapshot&,const std::vector<std::size_t>&);
void validateMeasurementManager(const MeasurementManagerRecord&,const DocumentSnapshot&,bool rebind);
std::size_t measurementManagerReviewBytes(const DocumentSnapshot&,const std::vector<std::size_t>&);
std::string measurementManagerReview(const DocumentSnapshot&,const std::vector<std::size_t>&);
Json measurementSettingsJson(const Settings&);

}
