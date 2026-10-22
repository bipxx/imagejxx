#pragma once
#include "render_memory.hpp"
#include "desktop.hpp"
#include "planar_annotation_panel.hpp"
#include <imagejxx/channel_display.hpp>
#include <imagejxx/targets.hpp>
#include <cstdint>
#include <imgui.h>

namespace imagejxx::desktop {
struct SavedRoiCanvasHooks;
class RoiImportPreview;
class ViewportJobs;
struct OrthogonalInputContext {
  ViewportJobs *viewport_jobs = nullptr;
  // Mapped RGB contrast pixels require a neutral display range.
  std::optional<std::array<double,2>> preview_display_range;
  bool active_document = false;
  bool input_enabled = false;
  bool keyboard_admitted = false;
  bool request_xy_focus = false;
  bool annotation_admitted = false;
  bool read_only = false;
  std::size_t annotation_max_working_bytes = 256*1048576;
  PlanarAnnotationPublisher planar_publish;
  std::function<Json(const PlanarCanvas &,ImDrawList *)> display_annotations;
  std::shared_ptr<const RoiImportPreview> import_preview;
  bool import_preview_diagnostics=false;
  std::function<void(const std::string &)> import_preview_error;
};

struct OrthogonalInputResult {
  bool escape_requested = false;
  bool keyboard_consumed = false;
  int focused_plane = -1;
};

// The panel consumes native document/plane APIs; it owns rendering caches and
// transient ImGui input state. Textures are released while GL is alive.
class OrthogonalViews {
public:
  // Borrowed pixels only: the snapshot is never retained past this draw.
  // Change generation when replacing preview contents; zero samples afresh.
  // Live navigation, calibration, annotations and contrast remain authoritative.
  // A supplied target is borrowed only for this draw; final T/C/Z membership
  // after all inputs gates all three actual plane providers and cache markers.
  OrthogonalInputResult
  draw(Document &document, bool openGL, bool showRois,
       bool linearInterpolation, std::string &status,
       const OrthogonalInputContext &input = {},
       const DocumentSnapshot *transient = nullptr,
       std::uint64_t preview_generation = 0,
       bool capture_readback = false,
       const ResolvedTarget *transient_target = nullptr,
       ChannelDisplayLimits channel_limits = {},
       const SavedRoiCanvasHooks *saved_roi = nullptr);
  void setRenderMemory(std::shared_ptr<RenderMemory> memory);
  void resetInput();
  int focusedPlane() const;
  void releaseTextures();
  void clearReadback();
  Json diagnostics(bool include_readback = false) const;
  ImVec2 screenPoint(int view, int horizontal, int vertical) const;

private:
  std::shared_ptr<RenderMemory> memory_=std::make_shared<RenderMemory>();
  PlanarAnnotationPanel planar_;
  struct Cache {
    unsigned int texture = 0;
    RenderMemory::Lease textureMemory,readbackMemory;
    std::uint64_t revision = ~std::uint64_t(0);
    std::uint64_t documentId = 0, previewGeneration = 0;
    std::uintptr_t dataIdentity = 0;
    bool transient = false, textureDirty = true, pending = false;
    int coordinate = -1, width = 0, height = 0;
    // T/C identify extracted pixels; Z records the current linked crosshair.
    // XZ/YZ pixels also depend on their fixed spatial coordinate above.
    int t = -1, c = -1, z = -1;
    std::array<double, 2> range{};
    std::array<double, 2> spacing{1, 1};
    ImVec2 origin{}, extent{}, scale{};
    ImGuiID canvasId = 0, sliceId = 0;
    ImVec2 canvasPoint{}, slicePoint{};
    bool canvasVisible = false, sliceFocused = false;
    bool keyboardAdmitted = false;
    std::size_t builds = 0;
    std::uint64_t pixelMetadataGeneration = 0;
    std::vector<Rect> selectionOutline, roiOutline;
    std::vector<RoiVisual> styledSelection, styledRois;
    // Annotation-only key/provenance. A private saved-row preview never changes
    // the pixel-source identity or forces an XZ/YZ resample/upload.
    Json annotationKey = nullptr, savedRoiLayers = Json::array();
    Json savedRoiInput = Json::object(), savedRoiHandles = Json::array();
    std::vector<std::size_t> styledRoiIndices;
    std::size_t annotationBuilds = 0;
    Json styledLayers = Json::array();
    Json importPreviewLayers=Json::array();
    Json displayLayers=Json::array();
    bool importPreviewActive=false,importPreviewReplace=false;
    std::string importPreviewError;
    std::vector<OverlayVisual> overlays;
    Json overlayLayers = Json::array();
    MetadataObject displayProperties;
    // Scalar identity above describes active-C readback. Composite output also
    // depends on every contributing channel's retained source and display state.
    Json channelDisplayKey = nullptr, channelDisplay = nullptr;
    // Diagnostic value copies only: they never prolong preview/source leases.
    std::optional<Image> readbackSource, readbackDisplay;
    FloatingSamplePolicy readbackSamplePolicy = FloatingSamplePolicy::FiniteOnly;
    std::size_t diagnosticRebuilds = 0;
  };
  std::array<Cache, 3> caches_;
  Json pixelMetadataKey_ = nullptr;
  std::uint64_t pixelMetadataOwner_ = 0, pixelMetadataGeneration_ = 0;
  Revisions pixelMetadataSeen_{~std::uint64_t(0),~std::uint64_t(0),~std::uint64_t(0),0};
  std::uint64_t rangePixelMetadataGeneration_ = 0;
  std::array<double, 2> range_{};
  std::uint64_t rangeRevision_ = ~std::uint64_t(0);
  std::uint64_t rangeDocument_ = 0, rangePreviewGeneration_ = 0;
  std::uintptr_t rangeDataIdentity_ = 0;
  bool rangeTransient_ = false;
  int rangeT_ = -1, rangeC_ = -1, rangeSlice_ = -1;
  MetadataObject rangeProperties_;
  ImGuiContext *inputContext_ = nullptr;
  std::uint64_t inputDocument_ = 0;
  int inputFrame_ = -1, keyFrame_ = -1;
  bool xyFocusPending_ = true;
  void releaseKeyOwners(ImGuiID keep = 0);
  bool canvasAdmission(const OrthogonalInputContext &input,
                       ImGuiID canvas) const;
};
} // namespace imagejxx::desktop
