#pragma once
#include "virtual_view_cache.hpp"
#include <cstddef>
#include <filesystem>
#include <functional>
#include <imagejxx/application.hpp>
#include <imagejxx/dataset.hpp>
#include <imagejxx/document.hpp>
#include <imagejxx/image.hpp>
#include <imagejxx/operations.hpp>
#include <imagejxx/roi_style.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <utility>
#include <vector>
struct ImDrawList;

namespace imagejxx {
class ResolvedGaussianBlurCommand;
class ResolvedRankFilterCommand;
class ResolvedImageJRoiImport;
class ResolvedImageJAnnotationImport;
}

namespace imagejxx::desktop {
using Json = nlohmann::json;
const Json &get(const Json &object, const char *key);
std::string string(const Json &value, const std::string &fallback = "");
double number(const Json &value, double fallback = 0);
int integer(const Json &value, int fallback = 0);
bool boolean(const Json &value, bool fallback = false);
std::array<unsigned char, 4> color(const std::string &value);
std::vector<unsigned char> readBytes(const std::string &path,
                                     size_t limit = 1024ULL * 1024 * 1024);
void writeBytes(const std::string &path,
                const std::vector<unsigned char> &data);
std::string base64(const std::vector<unsigned char> &data);
std::vector<unsigned char> unbase64(const std::string &data);
std::string extension(const std::string &path);
std::string filename(const std::string &path);
PixelType pixelType(const std::string &name);
std::string typeName(PixelType type);
Image canonical(Image image);
Image gray(const Image &image);
Image labelsImage(const Labels &labels);
Json catalog();
Json defaults(const Json &command, const Image &image);
void validateRecord(const std::string &id, const Json &parameters);
std::size_t extraWorkspace(const std::string &id, const Image &image,
                           const Json &parameters);
Image execute(const std::string &name, const Image &image,
              const Json &parameters, std::optional<int> *labelCount = nullptr);
PreparedOperation prepare(const std::string &name, const ResolvedTarget &target,
                          const Json &parameters);
struct Rect {
  int x, y, width, height;
};
struct RoiVisual {
  ResolvedRoiStyle style;
  std::vector<Rect> fill;
  std::vector<std::array<double, 4>> edges;
  std::vector<std::array<double, 2>> points;
  bool area = false, markers = false;
  double markerWidth = 1;
  // Device coverage unions joins before alpha blending and is reused until
  // the viewport transform changes. Image-space geometry remains immutable.
  mutable std::array<float, 8> screenKey{};
  mutable bool screenReady = false;
  mutable std::vector<Rect> screenStroke;
  mutable std::size_t screenBuilds = 0;
  mutable double screenWidth = 0;
};
RoiVisual roiVisual(const Json &selection, int width, int height,
                    RoiColor fallback);
void drawRoiVisual(const RoiVisual &visual, ImDrawList *draw,
                   std::array<float, 2> origin, std::array<float, 2> scale);
Json roiVisualJson(const RoiVisual &visual);
struct OverlayVisual {
  struct Span {
    Rect bounds;
    RoiColor color;
  };
  std::size_t index = 0;
  std::string kind;
  std::optional<RoiVisual> styled;
  std::vector<Span> raster;
};
// Mixed annotations retain metadata order. Legacy geometry and text remain
// image-pixel rasters while styled outlines use device-space coverage.
bool hasStyledOverlays(const Json &metadata);
std::vector<OverlayVisual> overlayVisuals(const Json &metadata, int width,
                                          int height);
void drawOverlayVisual(const OverlayVisual &visual, ImDrawList *draw,
                       std::array<float, 2> origin, std::array<float, 2> scale);
Json overlayVisualJson(const OverlayVisual &visual);
Image selectionMask(const Json &selection, int width, int height);
Selection selectionFromJson(const Json &value);
Json selectionJson(const Selection &value);
std::vector<Rect> selectionOutlineSpans(const Json &selection, int width,
                                        int height);
Json maskSelection(const Image &mask);
Rect selectionBounds(const Json &selection, int width, int height);
Image displayImage(const Image &image, const Json &metadata = Json::object());
void renderThresholdOverlay(Image &rgb, const Image &original,
                            const DocumentSnapshot &source, PlanePosition position);
Image renderImage(const Image &image, const Json &metadata,
                  bool includeOverlays = true);
Stack readImages(const std::string &path);
bool tiffAvailable();
void writeImages(const std::string &path, const Stack &images);
void writePdf(const std::string &path, const Image &image, double dpi = 150,
              bool landscape = false);
bool printImage(const Image &image);
void copySystemImage(const Image &image);
std::optional<Image> pasteSystemImage();
Stack readRaw(const std::string &path, int width, int height, int channels,
              int slices, PixelType type, size_t offset, bool bigEndian);

struct State {
  // For a scientific native owner this is a read-only Z view of the selected
  // T/C position. The source snapshot retains the complete TCZYXS dataset;
  // legacy State-based edits must never reconstruct that owner from this view.
  SharedPlanes frames;
  Json metadata;
  std::optional<FourierState> spectrum;
  std::uint64_t identity = 0;
  std::optional<DocumentSnapshot> source;
  Change dependencies = Change::Data;
};
using ComponentCounts = std::vector<std::pair<PlanePosition, int>>;
using ViewState = DisplayState;
Change stateChanges(const State &before, const State &after);
void preserveDisplay(Json &destination, const Json &source);
SpatialCalibration calibrationFromJson(const Json &value);
MetadataValue metadataFromJson(const Json &value);
MetadataObject metadataObjectFromJson(const Json &value);
Json metadataJson(const MetadataValue &value);
Json metadataJson(const MetadataObject &value);
class Document {
public:
  // Quiet-NaN source admission is explicit and only supported for immutable
  // eager grayscale Float32 storage. SharedPlanes/State constructors inherit
  // the policy already carried by their immutable plane table.
  explicit Document(Stack frames, std::string title = "Untitled",
                    Json metadata = Json::object(),
                    FloatingSamplePolicy sample_policy = FloatingSamplePolicy::FiniteOnly);
  explicit Document(SharedPlanes frames, std::string title = "Untitled",
                    Json metadata = Json::object());
  explicit Document(State initial);
  // Virtual viewing is explicit: providers must support immutable concurrent
  // reads on creator and processing threads. The cap bounds individual decoded
  // view buffers, not provider caches, renderer allocations or process RSS.
  explicit Document(std::shared_ptr<imagejxx::Document> document,
                    bool allow_virtual=false,
                    std::size_t max_view_bytes=128ULL*1024*1024,
                    bool allow_planar_paths=true);
  const State &state() const;
  DatasetShape shape() const;
  PlanePosition position() const;
  // Explicit ROI T/C restrictions also require scientific viewer capability,
  // even for singleton axes, so legacy editing cannot discard a present zero.
  bool scientific() const;
  void requireLegacyEditable() const;
  // Rendering-only indices into the stable full manager list in state().
  std::vector<std::size_t> visibleRoiIndices() const;
  // Detached rendering-only projection; the full manager list remains in state().
  Json visibleRois() const;
  FloatingSamplePolicy samplePolicy() const;
  ViewState view() const;
  Revisions revisions() const;
  std::uint64_t revision() const;
  std::uint64_t renderRevision() const;
  const std::uint64_t id;
  std::string path;
  std::shared_ptr<imagejxx::Document> nativeOwner() const;
  void bindController(std::shared_ptr<imagejxx::Application> controller);
  void releaseFrontend();
  void markSaved();
  void setHistoryPolicy(std::size_t limit, std::size_t pixelBudget);
  std::size_t historyLimit() const;
  std::size_t historyPixelBudget() const;
  std::size_t undoSteps() const;
  std::size_t redoSteps() const;
  int slice() const;
  const Image &frame() const;
  std::shared_ptr<const Image> sharedFrame() const;
  void admitView(bool orthogonal=false) const;
  std::string title() const;
  bool dirty() const;
  Json pixelStorage() const;
  std::size_t historyPixelBytes() const;
  void normalizeView();
  void setView(const Json &parameters);
  Json viewJson() const;
  std::array<double, 3> displaySpacing() const;
  void fitSelection(std::array<double, 2> extent);
  SpatialCalibration spatialCalibration() const;
  // View-only effective input; local metadata/persistence stays spatialCalibration().
  SpatialCalibration displayCalibration() const;
  Json displayCalibrationDiagnostics() const;
  Image plane(Plane orientation, int coordinate) const;
  void checkpoint();
  DocumentSnapshot nativeSnapshot() const;
  void setNativeDisplay(DisplayState display);
  void commitNative(DocumentTransaction &transaction);
  // Scientific opt-in: only the reserved planar-annotation extension may change.
  void commitPlanarAnnotation(DocumentTransaction &transaction);
  void commitDisplayAnnotation(DocumentTransaction &transaction);
  // Import opt-in: an immutable validated batch may publish exactly its two
  // reserved annotation/provenance extensions, with no other document edits.
  void commitImageJRoiImport(const ResolvedImageJRoiImport &);
  void commitImageJAnnotationImport(const ResolvedImageJAnnotationImport &);
  void undo();
  void redo();
  void select(const Json &selection);
  Document snapshot() const;
  State compute(const std::string &name, const Json &parameters, bool allSlices,
                ComponentCounts *counts = nullptr) const;
  void commit(State proposed);
  Json measure(const Json &selection = Json::object(),
               int sliceIndex = -1) const;

private:
  struct DisplayCalibrationCache;
  mutable std::shared_ptr<DisplayCalibrationCache> displayCalibrationCache_;
  struct PathAdmission;
  std::shared_ptr<PathAdmission> pathAdmission_;
  void publishNativePaths(const std::function<void()> &);
  void commitPlanarNative(DocumentTransaction &);
  struct WorkerTag {};
  friend class Window;
  // The exact facade is staged before native adoption; final attachment cannot
  // fail. Public bindController keeps its checked ownership contract.
  void bindPublishedController(std::shared_ptr<imagejxx::Application> controller) noexcept {
    displayCalibrationCache_.reset();
    controller_ = std::move(controller);
  }
  Document(const DocumentSnapshot &source, WorkerTag);
  void refresh() const;
  std::shared_ptr<imagejxx::Document> native_;
  std::shared_ptr<imagejxx::Application> controller_;
  std::shared_ptr<const DocumentValidator> validation_;
  std::optional<DocumentSnapshot> origin_;
  mutable std::optional<DocumentSnapshot> cached_;
  // The complete eager table is copied only when native data changes. T/C
  // navigation derives a Z subset of shared pointers without reading samples.
  mutable std::optional<SharedPlanes> cachedPlanes_;
  // Virtual State.frames stays empty; metadata projection never acquires pixels.
  // One current XY plane is retained independently of metadata/display changes.
  std::size_t maxViewBytes_=128ULL*1024*1024;
  std::shared_ptr<VirtualViewCache> viewCache_=std::make_shared<VirtualViewCache>();
  std::shared_ptr<VirtualViewCache::Slot> viewSlot_=viewCache_->slot();
  mutable State cachedState_;
};
using Documents = std::vector<std::shared_ptr<Document>>;
void saveSession(const std::string &path, const Documents &documents,
                 const Json &results, const Json &workflow);
Documents loadSession(const std::string &path, Json &results, Json &workflow,
                      Json *extensions = nullptr);
bool nativeSessionFile(const std::string &path);
void saveNativeSession(const std::string &path, const Documents &documents,
                       const Json &results, const Json &workflow,
                       const Json &extensions = Json::object());
Documents loadNativeSession(const std::string &path, Json &results,
                            Json &workflow, Json *extensions = nullptr);
Json documentJson(const Document &document);
std::shared_ptr<Document> documentFromJson(const Json &item);
std::shared_ptr<Document> documentFromJson(const Json &item, const Json &nativeState);
// Internal synchronous observation. References are borrowed only during the
// callback; observers must copy scalar/record values, never retain commands,
// snapshots, results or pixel buffers. Empty observers preserve legacy behavior.
struct GaussianReplayObservation {
  const char *phase;
  std::size_t step_index;
  std::uint64_t live_destination_id, captured_snapshot_owner_id, working_copy_id;
  const Json *original_record = nullptr;
  const ResolvedGaussianBlurCommand *resolved_command = nullptr;
  std::uint64_t scratch_publication_document_id = 0;
  const OperationParameters *execution_parameters = nullptr;
  const ResolvedRankFilterCommand *resolved_rank_command = nullptr;
};
using GaussianReplayObserver =
    std::function<void(const GaussianReplayObservation &)>;
State runWorkflow(
    const Document &document, const Json &steps,
    std::function<bool()> cancelled = [] { return false; },
    std::optional<TargetRequest> gaussian_rebind = std::nullopt,
    GaussianReplayObserver observer = {},
    std::optional<TargetRequest> rank_rebind = std::nullopt);
void writeJson(const std::string &path, const Json &object);
Json readJson(const std::string &path);
} // namespace imagejxx::desktop
