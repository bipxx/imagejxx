#pragma once
#include "desktop.hpp"
#include <imagejxx/axis_extraction_command.hpp>
#include <imagejxx/axis_orientation_command.hpp>
#include <imagejxx/axis_spacing_command.hpp>
#include <imagejxx/channel_display_command.hpp>
#include <imagejxx/gaussian_command.hpp>
#include <imagejxx/rank_command.hpp>
#include <imagejxx/saved_roi_geometry.hpp>
#include <imagejxx/selection_creation.hpp>
#include <imagejxx/settings.hpp>
#include <imagejxx/shortcuts.hpp>
#include <imagejxx/slicer_resize_command.hpp>
#include <imagejxx/startup.hpp>
#include <imagejxx/z_subset_command.hpp>
#include <thread>
namespace imagejxx::desktop {
class GaussianCancellationAccess;
class GaussianCancellationSession;
class HeadlessInputSession;
class AxisPlaneUiProbeAccess;
class AxisRangeUiProbeAccess;
class AxisRangeOrientationUiProbeAccess;
class RankRuntimeProbe;
class RankRuntimeProbeAccess;
class SavedRoiGeometryPythonAccess;
struct RunOptions {
  bool hidden = false;
  int frames = 0;
  std::string screenshot;
  RankRuntimeProbe *rank_probe = nullptr;
};
class Window {
public:
  explicit Window(std::string preferencesPath = "");
  ~Window();
  Window(const Window &) = delete;
  Window &operator=(const Window &) = delete;
  const Documents &documents() const;
  Json results = Json::array(), workflow = Json::array();
  int activeIndex() const;
  void setActiveIndex(int index);
  bool recording = false;
  void addDocument(std::shared_ptr<Document> document);
  ShortcutProfile shortcutProfile() const;
  void addNativeDocument(std::shared_ptr<imagejxx::Document> document);
  std::uint64_t publishZSubset(const ResolvedZSubsetCommand &command);
  std::uint64_t exportAxisPlane(const DocumentSnapshot &source,
                                AxisPlaneExportRequest request);
  std::uint64_t resliceAxisRange(const DocumentSnapshot &source,
                                 AxisRangeResliceRequest request);
  std::uint64_t
  replayAxisExtraction(const MetadataObject &record,
                       std::optional<AxisExtractionRequest> rebindRequest = {},
                       std::uint64_t documentId = 0);
  std::uint64_t resliceOrientedAxisRange(const DocumentSnapshot &source,
                                         AxisRangeOrientationRequest request);
  std::uint64_t replayAxisOrientation(
      const MetadataObject &record,
      std::optional<AxisRangeOrientationRequest> rebindRequest = {},
      std::uint64_t documentId = 0);
  std::uint64_t resliceSpacedAxisRange(const DocumentSnapshot &source,
                                       AxisRangeSpacingRequest request);
  std::uint64_t publishAxisSpacing(const ResolvedAxisSpacingCommand &command,
                                   bool requireCapturedPosition = true);
  std::uint64_t publishSlicerResize(const ResolvedSlicerResizeCommand &command,
                                    bool requireCapturedPosition = true);
  std::uint64_t resliceSlicerAxisRange(const DocumentSnapshot &source,
                                       AxisRangeSlicerResizeRequest request);
  std::shared_ptr<imagejxx::Document>
  nativeDocument(std::uint64_t id = 0) const;
  std::shared_ptr<Document> document() const;
  std::shared_ptr<Document> documentById(std::uint64_t id = 0) const;
  Json documentInfo(std::uint64_t id = 0) const;
  DocumentSnapshot nativeSnapshot(std::uint64_t id = 0) const;
  void setNativeDisplay(DisplayState display, std::uint64_t id = 0);
  void enableChannelDisplay(std::uint64_t documentId = 0);
  void applyChannelDisplay(const DocumentSnapshot &source,
                           const ChannelDisplayRequest &request);
  void
  replayChannelDisplay(const MetadataObject &record,
                       std::optional<ChannelDisplayRequest> rebindRequest = {},
                       std::uint64_t documentId = 0);
  void applyAutoContrast(const DocumentSnapshot &source,
                         const AutoContrastOptions &options);
  void replayAutoContrast(const MetadataObject &record,
                          std::optional<AutoContrastRequest> rebindRequest = {},
                          std::uint64_t documentId = 0);
  void applySelectionCreation(const DocumentSnapshot &source,
                              const SelectionCreationRequest &request);
  void replaySelectionCreation(
      const MetadataObject &record,
      std::optional<SelectionCreationRequest> rebindRequest = {},
      std::uint64_t documentId = 0);
  Json applySavedRoiGeometry(const SavedRoiGeometryRequest &request);
  Json replaySavedRoiGeometry(
      const MetadataObject &record,
      std::optional<SavedRoiGeometryRequest> completeRebindRequest = {});
  DocumentTransaction beginEdit(std::uint64_t id = 0,
                                Change dependencies = Change::Data) const;
  void commitEdit(DocumentTransaction &transaction);
  void closeDocument(std::uint64_t id = 0, bool discard = false);
  Settings settings() const;
  Json preferencesJson() const;
  void updateSettings(const Json &values, bool persist = true);
  // Fresh-only launch routing. Bare Window construction never auto-restores.
  void startStartup(const StartupRequest &request = {});
  Json startupState() const;
  // Bounded, detached diagnostic projection used by ordinary launcher checks.
  Json startupObservation(const std::string &context) const;
  void retryStartup();
  void startEmptyStartup();
  void disableStartupRestore();
  void resetLayout(bool persist = true);
  void openFile(const std::string &path);
  // Explicit compatibility import: TIFF pages become a flat Z stack in pixel
  // units.
  void openLegacyFile(const std::string &path);
  void applyOperation(const std::string &id, const Json &parameters,
                      bool allSlices = false, bool asynchronous = true,
                      std::uint64_t documentId = 0);
  void action(const std::string &name, const Json &parameters = Json::object());
  // Native scientific targets; the desktop lane belongs to the active owner.
  void applyGaussian(const TargetRequest &request,
                     const GaussianBlurCommandOptions &options,
                     std::uint64_t documentId = 0);
  void applyRank(const TargetRequest &request,
                 const RankFilterCommandOptions &options,
                 std::uint64_t documentId = 0);
  bool busy() const;
  std::optional<JobInfo> jobInfo() const;
  std::vector<JobInfo> jobs() const;
  bool cancelJob(std::uint64_t jobId);
  void poll();
  void wait();
  void cancel();
  std::string error() const;
  void clearError();
  void checkThread() const;
  int run(const RunOptions &options = {});
  Json renderHeadless(int frames = 3, bool exercise = false,
                      bool exerciseCamera = false,
                      const Json &inputEvents = Json::array(),
                      bool exercisePreferences = false,
                      bool exerciseOrthogonalBusy = false);
  int smokeTest(const std::string &directory);
  int binaryIterationsConsumerTest(const std::string &directory);
  int binaryPadEdgesConsumerTest(const std::string &directory);
  int orthogonalKeyboardTest(const std::string &directory);
  int gaussianHostConsumerTest(const std::string &directory);
  int gaussianCancellationConsumerTest(const std::string &directory);
  Json gaussianCancellationCase(
      const std::string &caseId, const std::string &context,
      const std::shared_ptr<GaussianCancellationSession> &session,
      const std::string &directory);
  int headlessSessionConsumerTest(const std::string &directory);
  Json headlessSessionCase(const std::string &caseId,
                           const std::string &context,
                           const std::string &directory);
  int axisRangeResliceUiConsumerTest(const std::string &directory);
  int axisRangeOrientationUiConsumerTest(const std::string &directory);
  Json axisRangeOrientationUiCase(const std::string &caseId,
                                  const std::string &context,
                                  const std::string &directory);
  Json axisRangeResliceUiCase(const std::string &caseId,
                              const std::string &context,
                              const std::string &directory);
  int axisPlaneExportUiConsumerTest(const std::string &directory);
  Json axisPlaneExportUiCase(const std::string &caseId,
                             const std::string &context,
                             const std::string &directory);
  int roiBooleanUiConsumerTest(const std::string &directory);
  Json roiBooleanUiCase(const std::string &caseId, const std::string &context,
                        const std::string &directory);
  int gaussianResourceConsumerTest(const std::string &directory);
  Json gaussianResourceCase(const std::string &context,
                            const std::string &directory);
  int gaussianRoiConsumerTest(const std::string &directory);
  Json gaussianRoiCase(const std::string &variant, const std::string &context,
                       const std::string &directory);
  int gaussianControlsConsumerTest(const std::string &directory);
  Json gaussianControlsCase(const std::string &variant,
                            const std::string &context,
                            const std::string &directory);
  int gaussianReplayConsumerTest(const std::string &directory);
  Json gaussianReplayCase(const std::string &variant,
                          const std::string &context,
                          const std::string &directory);
  int gaussianSigmaConsumerTest(const std::string &directory);
  Json gaussianSigmaCase(const std::string &variant, const std::string &context,
                         const std::string &directory);
  int gaussianPreviewConsumerTest(const std::string &directory);
  Json gaussianPreviewCase(const std::string &variant,
                           const std::string &context,
                           const std::string &directory);
  int gaussianStackConsumerTest(const std::string &directory);
  Json gaussianStackCase(const std::string &variant, const std::string &context,
                         const std::string &directory);
  int gaussianJobCancelConsumerTest(const std::string &directory);
  Json gaussianJobCancelCase(const std::string &variant,
                             const std::string &context,
                             const std::string &directory);
  int gaussianNonfiniteConsumerTest(const std::string &directory);
  Json gaussianNonfiniteCase(const std::string &variant,
                             const std::string &context,
                             const std::string &directory);
  int xyzNumericZoomConsumerTest(const std::string &directory);
  Json xyzNumericZoomCase(const std::string &caseId, const std::string &context,
                          const std::string &directory);

private:
  using StageSavedRoiReply = std::function<void(const Json &)>;
  void validateSavedRoiGeometryReplayRecord(const MetadataObject &record) const;
  Json applySavedRoiGeometryStaged(const SavedRoiGeometryRequest &request,
                                   const StageSavedRoiReply &stageReply);
  Json replaySavedRoiGeometryStaged(
      const MetadataObject &record,
      std::optional<SavedRoiGeometryRequest> completeRebindRequest,
      const StageSavedRoiReply &stageReply);
  ChannelDisplayState
  initialChannelDisplay(const DocumentSnapshot &source) const;
  void publishChannelDisplay(const ResolvedChannelDisplayCommand &command);
  void
  publishSelectionCreation(const ResolvedSelectionCreationCommand &command);
  Json publishSavedRoiGeometry(const SavedRoiGeometryCapture &capture,
                               const ResolvedSavedRoiGeometryCommand &command,
                               SavedRoiGeometryInteraction interaction,
                               const StageSavedRoiReply &stageReply = {});
  std::uint64_t
  publishAxisExtraction(const ResolvedAxisExtractionCommand &command,
                        bool requireCapturedPosition);
  using AxisCommandRequest =
      std::variant<AxisPlaneExportRequest, AxisRangeResliceRequest,
                   AxisRangeOrientationRequest, AxisRangeSpacingRequest,
                   AxisRangeSlicerResizeRequest>;
  std::uint64_t
  publishAxisOrientation(const ResolvedAxisOrientationCommand &command,
                         bool requireCapturedPosition);
  std::uint64_t publishPreparedAxisExtraction(const DocumentSnapshot &source,
                                              const AxisCommandRequest &request,
                                              const MetadataObject &record,
                                              PreparedOperation &prepared,
                                              bool requireCapturedPosition);
  void addSessionDocuments(const Documents &documents);
  HistoryPolicyAdoption
  attachStartupDocuments(const Documents &documents,
                         const std::vector<bool> &applyDefaults,
                         std::optional<std::size_t> active);
  void executeStartup(StartupRequest request, bool retry);
  friend class GaussianCancellationAccess;
  friend class HeadlessInputSession;
  friend class AxisPlaneUiProbeAccess;
  friend class AxisRangeUiProbeAccess;
  friend class AxisRangeOrientationUiProbeAccess;
  friend class RankRuntimeProbeAccess;
  friend class SavedRoiGeometryPythonAccess;
  friend Json roiImportQueueUiCase(Window &, const std::string &,
                                   const std::string &);
  std::shared_ptr<imagejxx::Application> controller_ =
      std::make_shared<imagejxx::Application>();
  Documents documents_;
  std::unordered_map<std::uint64_t, std::weak_ptr<Document>> documentAdapters_;
  const std::thread::id ownerThread = std::this_thread::get_id();
  struct Impl;
  std::unique_ptr<Impl> impl;
};
// Fixed private observation producers. Each case owns its HeadlessInputSession
// on the creator-thread stack; these functions expose no generic event API.
Json axisRangeSpacingUiCase(Window &window, const std::string &caseId,
                            const std::string &context,
                            const std::string &directory);
int axisRangeSpacingUiConsumerTest(Window &window,
                                   const std::string &directory);
} // namespace imagejxx::desktop
