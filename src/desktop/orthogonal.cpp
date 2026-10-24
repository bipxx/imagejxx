#include "orthogonal.hpp"
#include "path_visual.hpp"
#include "rendering.hpp"
#include "viewport_renderer.hpp"
#include "channel_display_sources.hpp"
#include "saved_roi_edit.hpp"
#include <imagejxx/planar_annotations.hpp>
#include <imagejxx/planar_display_annotations.hpp>
#include "roi_import_preview.hpp"
#include <imgui_internal.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace imagejxx::desktop {
namespace {
constexpr std::array<Plane, 3> views{Plane::XY, Plane::XZ, Plane::YZ};
constexpr std::array<const char *, 3> names{"XY", "XZ", "YZ"};
constexpr std::array<int, 3> horizontal{0, 0, 1}, vertical{1, 2, 2},
    fixed{2, 1, 0};
constexpr std::array<ImGuiKey, 7> navigationKeys{
    ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow,
    ImGuiKey_DownArrow, ImGuiKey_PageUp, ImGuiKey_PageDown, ImGuiKey_Escape};
constexpr std::array<const char *, 7> keyNames{
    "Left", "Right", "Up", "Down", "PageUp", "PageDown", "Escape"};
} // namespace

void OrthogonalViews::releaseKeyOwners(ImGuiID keep) {
  if (!ImGui::GetCurrentContext() ||
      ImGui::GetCurrentContext() != inputContext_)
    return;
  for (auto key : navigationKeys) {
    auto owner = ImGui::GetKeyOwner(key);
    if (owner != keep &&
        std::any_of(caches_.begin(), caches_.end(), [&](const Cache &cache) {
          return cache.canvasId != 0 && owner == cache.canvasId;
        }))
      ImGui::SetKeyOwner(key, ImGuiKeyOwner_NoOwner);
  }
  // Delete belongs only to an explicitly edited saved row, never to linked
  // crosshair navigation. Re-admit it from the actual focused XY item below.
  const auto deleteOwner = ImGui::GetKeyOwner(ImGuiKey_Delete);
  if (std::any_of(caches_.begin(), caches_.end(), [&](const Cache &cache) {
        return cache.canvasId != 0 && deleteOwner == cache.canvasId;
      }))
    ImGui::SetKeyOwner(ImGuiKey_Delete, ImGuiKeyOwner_NoOwner);
}

void OrthogonalViews::resetInput() {
  planar_.cancel("canvas_reset");
  releaseKeyOwners();
  for (auto &cache : caches_) {
    cache.canvasId = cache.sliceId = 0;
    cache.canvasVisible = cache.sliceFocused = cache.keyboardAdmitted = false;
    cache.canvasPoint = cache.slicePoint = {};
  }
  inputContext_ = nullptr;
  inputDocument_ = 0;
  inputFrame_ = keyFrame_ = -1;
  xyFocusPending_ = true;
}

int OrthogonalViews::focusedPlane() const {
  auto context = ImGui::GetCurrentContext();
  if (!context || context != inputContext_ ||
      inputFrame_ != ImGui::GetFrameCount())
    return -1;
  for (int i = 0; i < 3; ++i)
    if (caches_[i].canvasVisible && caches_[i].canvasId != 0 &&
        context->NavId == caches_[i].canvasId)
      return i;
  return -1;
}

bool OrthogonalViews::canvasAdmission(const OrthogonalInputContext &input,
                                     ImGuiID canvas) const {
  if (!input.active_document || !input.input_enabled ||
      !input.keyboard_admitted || !ImGui::GetCurrentContext() ||
      ImGui::GetCurrentWindowRead()->SkipItems ||
      (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled))
    return false;
  const auto &io = ImGui::GetIO();
  auto active = ImGui::GetActiveID();
  return !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper &&
         (active == 0 || active == canvas);
}

OrthogonalInputResult
OrthogonalViews::draw(Document &d, bool gl, bool showRois,
                      bool linearInterpolation, std::string &status,
                      const OrthogonalInputContext &input,
                      const DocumentSnapshot *transient,
                      std::uint64_t preview_generation,
                      bool capture_readback,
                      const ResolvedTarget *transient_target,
                      ChannelDisplayLimits channel_limits,
                      const SavedRoiCanvasHooks *saved_roi) {
  OrthogonalInputResult result;
  auto live = d.nativeSnapshot();
  const auto liveDataset = live.data().dataset;
  const auto shape = liveDataset.shape();
  const auto xySamples=RenderMemory::mul(RenderMemory::mul(std::size_t(shape.x),std::size_t(shape.y)),std::size_t(shape.s));
  auto savedOwner = [&] {
    return saved_roi && saved_roi->edit && saved_roi->edit->active() &&
           saved_roi->edit->source().id() == d.id;
  };
  auto cancelSaved = [&] {
    if (!savedOwner()) return;
    try {
      if (saved_roi->cancel) saved_roi->cancel();
      else saved_roi->edit->cancel();
    } catch (...) { saved_roi->edit->cancel(); throw; }
  };
  auto beforeDisplayChange = [&](bool tcz) {
    planar_.cancel("navigation");
    if (savedOwner() && (tcz || saved_roi->edit->pointer_active())) cancelSaved();
  };
  auto savedSourceMatches = [&](const DocumentSnapshot &current) {
    if (!savedOwner()) return false;
    const auto &source = saved_roi->edit->source();
    return source.same_document(current) && source.shares_data(current) &&
        source.revisions().matches(current.revisions(), Change::Data) &&
        source.display().t == current.display().t &&
        source.display().c == current.display().c &&
        source.display().crosshair[2] == current.display().crosshair[2] &&
        (!saved_roi->edit->pointer_active() ||
         source.revisions().matches(current.revisions(), Change::Display));
  };
  if (savedOwner() && !savedSourceMatches(live)) cancelSaved();
  for (auto &cache : caches_) {
    cache.savedRoiInput = Json::object();
    cache.savedRoiHandles = Json::array();
    for (auto &layer : cache.savedRoiLayers) layer["drawn"] = false;
  }
  // Interpretation is explicit and precedes navigation or plane reads. A
  // present future/malformed state never silently falls back to legacy pixels.
  auto channelState = recognized_channel_display(live.display(), liveDataset);
  if (transient_target) {
    if (!transient)
      throw std::invalid_argument("Orthogonal preview target requires pixels");
    transient_target->validate_source(live);
  }
  if(!input.preview_targets.empty()){
    if(!input.preview_display_range || input.preview_targets.size()>64)throw std::invalid_argument("RGB target union requires a bounded neutral preview");
    for(const auto &target:input.preview_targets)target.validate_source(live);
  }
  if(input.preview_display_range && (!transient || shape.s!=3 || liveDataset.pixel_type()!=PixelType::UInt8 || *input.preview_display_range!=std::array<double,2>{0,255}))
    throw std::invalid_argument("Neutral preview display requires mapped UInt8 RGB pixels");
  if (transient) {
    if (!transient->same_document(live))
      throw std::invalid_argument(
          "Orthogonal preview belongs to another document");
    if (!live.revisions().matches(transient->revisions(), Change::Data))
      throw std::invalid_argument(
          "Orthogonal preview source data is stale");
    const auto &previewDataset = transient->data().dataset;
    if (shape.dimensions() != previewDataset.shape().dimensions() ||
        liveDataset.pixel_type() != previewDataset.pixel_type())
      throw std::invalid_argument(
          "Orthogonal preview must preserve the scientific shape and pixel type");
    // Eager-source discovery copies only shared ownership metadata. It never
    // reads a plane, including when retained-resource guards wrap the source.
    if ((!liveDataset.shared_planes() || !previewDataset.shared_planes()) && !(input.viewport_jobs && input.preview_display_range))
      throw std::invalid_argument(
          "Orthogonal preview requires eager plane sources");
  }
  auto context = ImGui::GetCurrentContext();
  const int frame = ImGui::GetFrameCount();
  if (inputContext_ != context || inputDocument_ != d.id ||
      (inputFrame_ >= 0 &&
       (frame < inputFrame_ || frame > inputFrame_ + 1)))
    resetInput();
  inputContext_ = context;
  inputDocument_ = d.id;
  inputFrame_ = frame;
  xyFocusPending_ |= input.request_xy_focus;
  d.normalizeView();
  const std::array<int, 3> sizes{shape.x, shape.y, shape.z};
  // Read the actual retained ImGui focus before extracting any linked plane.
  // Focus IDs are context/document scoped; hover is never a keyboard target.
  const int focused = focusedPlane();
  const bool admitted =
      focused >= 0 && canvasAdmission(input, caches_[focused].canvasId);
  // A Tab/click handoff must not leave held keys attached to the old canvas.
  // Only this panel's IDs can be released; another widget keeps its ownership.
  releaseKeyOwners(admitted ? caches_[focused].canvasId : 0);
  if (admitted && keyFrame_ != frame) {
    keyFrame_ = frame;
    const auto canvas = caches_[focused].canvasId;
    auto pressed = [&](ImGuiKey key, bool repeat = true) {
      return ImGui::IsKeyPressed(
          key, repeat ? ImGuiInputFlags_Repeat : ImGuiInputFlags_None, canvas);
    };
    const auto &io = ImGui::GetIO();
    if (pressed(ImGuiKey_Escape, false) && (savedOwner() || !io.KeyShift)) {
      if (planar_.active()) {
        planar_.cancel("escape");
        result.keyboard_consumed = true;
      } else if (savedOwner()) {
        cancelSaved();
        result.keyboard_consumed = true;
      } else {
        result.escape_requested = result.keyboard_consumed = true;
        resetInput();
        return result;
      }
    }
    std::array<int, 3> delta{};
    auto direction = [&](ImGuiKey increase, ImGuiKey decrease,
                         ImGuiKey increaseAlias = ImGuiKey_None,
                         ImGuiKey decreaseAlias = ImGuiKey_None) {
      bool positive = pressed(increase);
      bool negative = pressed(decrease);
      if (increaseAlias != ImGuiKey_None)
        positive |= pressed(increaseAlias);
      if (decreaseAlias != ImGuiKey_None)
        negative |= pressed(decreaseAlias);
      result.keyboard_consumed |= positive || negative;
      bool positiveDown = ImGui::IsKeyDown(increase, canvas);
      bool negativeDown = ImGui::IsKeyDown(decrease, canvas);
      if (increaseAlias != ImGuiKey_None)
        positiveDown |= ImGui::IsKeyDown(increaseAlias, canvas);
      if (decreaseAlias != ImGuiKey_None)
        negativeDown |= ImGui::IsKeyDown(decreaseAlias, canvas);
      // Opposing holds cancel even when their repeat clocks are staggered.
      if (positiveDown && negativeDown)
        return 0;
      return int(positive) - int(negative);
    };
    const bool savedGeometry = focused == 0 && savedOwner() &&
        saved_roi->pointer_enabled && saved_roi->accept_numeric &&
        !saved_roi->edit->pointer_active();
    if (savedGeometry) {
      const std::array<double, 2> nudge{
          double(direction(ImGuiKey_RightArrow, ImGuiKey_LeftArrow)) * (io.KeyShift ? 10 : 1),
          double(direction(ImGuiKey_DownArrow, ImGuiKey_UpArrow)) * (io.KeyShift ? 10 : 1)};
      const bool remove = pressed(ImGuiKey_Delete);
      result.keyboard_consumed |= remove;
      if (nudge[0] || nudge[1] || remove) {
        try {
          const auto &selection = saved_roi->edit->candidate()
              ? *saved_roi->edit->candidate() : saved_roi->edit->original();
          const auto &caps = saved_roi->edit->limits();
          const SelectionEditLimits limits{caps.max_xy_pixels, caps.max_vertices,
                                           caps.max_working_bytes};
          SelectionEditor editor(selection, shape.x, shape.y, limits);
          auto *vertex = saved_roi->selected_vertex;
          if (vertex && *vertex &&
              ((!selection.is_path() && selection.kind != "polygon" && selection.kind != "freehand") ||
               **vertex >= selectionControlPointCount(selection))) vertex->reset();
          const auto selected = vertex ? *vertex : std::optional<std::size_t>{};
          auto &observed = caches_[0].savedRoiInput;
          observed = {{"phase", "keyboard"}, {"frame", frame},
              {"canvas_id", canvas}, {"kind", "linked_xy"},
              {"row_index", saved_roi->edit->row_index()}, {"nudge", nudge},
              {"delete", remove}, {"shift", io.KeyShift},
              {"selected_vertex", selected ? Json(*selected) : Json()}};
          if (nudge[0] || nudge[1]) {
            if (selected) {
              auto target = selectionControlPoint(selection,*selected);
              target[0] += nudge[0]; target[1] += nudge[1];
              observed["phase"] = "nudged_vertex";
              saved_roi->accept_numeric(editor.moved_vertex(*selected, target, limits));
            } else {
              observed["phase"] = "nudged";
              saved_roi->accept_numeric(editor.moved(nudge, true, limits));
            }
          } else if (remove && selected && !selection.is_path()) {
            observed["phase"] = "deleted_vertex";
            saved_roi->accept_numeric(editor.removed_vertex(*selected, limits));
          }
        } catch (...) { cancelSaved(); throw; }
      }
      // Page keys retain linked slice navigation; XY arrows edit geometry.
      delta[fixed[focused]] = direction(ImGuiKey_PageUp, ImGuiKey_PageDown);
    } else if (io.KeyShift) {
      delta[0] = direction(ImGuiKey_RightArrow, ImGuiKey_LeftArrow);
      delta[1] = direction(ImGuiKey_DownArrow, ImGuiKey_UpArrow);
    } else {
      delta[fixed[focused]] =
          direction(ImGuiKey_UpArrow, ImGuiKey_DownArrow,
                    ImGuiKey_PageUp, ImGuiKey_PageDown);
    }
    auto next = d.view().crosshair;
    for (int axis = 0; axis < 3; ++axis)
      next[axis] = std::clamp(next[axis] + delta[axis], 0, sizes[axis] - 1);
    if (next != d.view().crosshair) {
      beforeDisplayChange(next[2] != d.view().crosshair[2]);
      d.setView({{"crosshair", next}});
    }
  }
  for (auto &cache : caches_)
    cache.canvasVisible = cache.sliceFocused = cache.keyboardAdmitted = false;
  const auto cal = d.spatialCalibration();
  const auto spacing = d.displaySpacing();
  auto cursor = d.view().crosshair;
  bool changed = false;
  ImGui::TextUnformatted("Voxel indices (zero based)");
  for (int i = 0; i < 3; ++i) {
    if (i)
      ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    changed |= ImGui::DragInt(std::array<const char *, 3>{"X", "Y", "Z"}[i],
                              &cursor[i], 1, 0, sizes[i] - 1);
    cursor[i] = std::clamp(cursor[i], 0, sizes[i] - 1);
  }
  if (changed) {
    beforeDisplayChange(cursor[2] != d.view().crosshair[2]);
    d.setView({{"crosshair", cursor}});
  }
  ImGui::SameLine();
  if (ImGui::Button("Fit views")) {
    beforeDisplayChange(false);
    d.setView({{"zoom", 0},
               {"plane_zoom", std::array<double, 3>{}},
               {"pan", std::array<std::array<double, 2>, 3>{}}});
  }
  ImGui::SameLine();
  bool linked = d.view().linkedZoom;
  if (ImGui::Checkbox("Linked zoom", &linked)) {
    beforeDisplayChange(false);
    d.setView({{"linked_zoom", linked}});
  }
  // Reserve the readout's layout now; draw it only after every plane input.
  auto worldDraw = ImGui::GetWindowDrawList();
  const auto worldPoint = ImGui::GetCursorScreenPos();
  const auto worldClip = ImGui::GetCurrentWindowRead()->ClipRect;
  ImGui::Dummy({0, ImGui::GetTextLineHeight()});
  ImGui::TextUnformatted("Click/drag: crosshair  |  Wheel: slice  |  "
                         "Ctrl-wheel: linked zoom  |  Middle drag: pan");
  if (savedOwner()) {
    const bool path=saved_roi->edit->candidate()?saved_roi->edit->candidate()->is_path():saved_roi->edit->original().is_path();
    ImGui::TextUnformatted(path?"Saved path in XY: drag endpoints and curve controls  |  Arrows: nudge (Shift: 10)  |  Escape: cancel":
        "Saved ROI in XY: drag handles; Alt-click: insert vertex  |  Arrows: nudge (Shift: 10); Delete: vertex  |  Escape: cancel");
  }
  else
    ImGui::TextUnformatted("Focused canvas: Up/Down or PageUp/PageDown: slice  |  "
                           "Shift-arrows: global XY  |  Escape: exit views");
  const bool importReview=input.import_preview && input.import_preview->matches(d.nativeSnapshot(),input.annotation_max_working_bytes);
  if (input.read_only) planar_.begin_draw(d,input.annotation_max_working_bytes);
  else planar_.controls(d,input.active_document && input.input_enabled && !savedOwner() && !importReview,input.annotation_max_working_bytes,input.planar_publish);
  auto available = ImGui::GetContentRegionAvail();
  float height = std::max(64.f, available.y - 4);
  double zoom = d.view().zoom;
  if (zoom == 0) {
    zoom = std::numeric_limits<double>::max();
    for (int i = 0; i < 3; ++i)
      zoom = std::min(
          zoom, std::min(std::max(32.f, available.x / 3 - 18) /
                             (sizes[horizontal[i]] * spacing[horizontal[i]]),
                         std::max(32.f, height - 46) /
                             (sizes[vertical[i]] * spacing[vertical[i]])));
    zoom *= .94;
  }
  if (!std::isfinite(zoom) || zoom <= 0)
    throw std::invalid_argument("Physical display extent is unsupported");
  struct PlaneFrame {
    ImDrawList *draw = nullptr;
    ImVec2 minimum{}, maximum{}, extent{}, heading{};
    ImRect clip;
    bool hovered = false;
  };
  std::array<PlaneFrame, 3> planes;
  // Geometry needs only immutable dimensions, so the input pass never extracts
  // a plane or updates its cache. Recompute it after input for linked zoom/pan.
  auto prepareGeometry = [&](int i, ImVec2 a, ImVec2 extent) {
    auto &cache = caches_[i];
    cache.spacing = {spacing[horizontal[i]], spacing[vertical[i]]};
    double planeZoom = d.view().zoom == 0 ? zoom : d.view().zoom;
    if (!d.view().linkedZoom) {
      planeZoom = d.view().planeZoom[i];
      if (planeZoom == 0)
        planeZoom =
            .94 * std::min(extent.x / (sizes[horizontal[i]] * cache.spacing[0]),
                           extent.y / (sizes[vertical[i]] * cache.spacing[1]));
    }
    cache.scale = {float(planeZoom * cache.spacing[0]),
                   float(planeZoom * cache.spacing[1])};
    if (!std::isfinite(cache.scale.x) || !std::isfinite(cache.scale.y) ||
        cache.scale.x <= 0 || cache.scale.y <= 0)
      throw std::invalid_argument("Physical display scale is unsupported");
    cache.extent = extent;
    cache.origin = {a.x + (extent.x - sizes[horizontal[i]] * cache.scale.x) / 2 +
                        float(d.view().pan[i][0]),
                    a.y + (extent.y - sizes[vertical[i]] * cache.scale.y) / 2 +
                        float(d.view().pan[i][1])};
    for (float value : {cache.origin.x, cache.origin.y,
                        cache.origin.x + sizes[horizontal[i]] * cache.scale.x,
                        cache.origin.y + sizes[vertical[i]] * cache.scale.y})
      if (!std::isfinite(value))
        throw std::invalid_argument("Physical display extent is unsupported");
    return planeZoom;
  };
  if (ImGui::BeginTable("orthogonal_planes", 3,
                        ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_SizingStretchSame)) {
    // Native admission/publication may throw while a plane owns ImGui scopes.
    // Unwind them before the host catches and presents the error modal.
    struct EndPlaneTable { ~EndPlaneTable() { ImGui::EndTable(); } } endTable;
    for (int i = 0; i < 3; ++i) {
      ImGui::TableNextColumn();
      ImGui::PushID(i);
      struct PopPlaneId { ~PopPlaneId() { ImGui::PopID(); } } popPlaneId;
      ImGui::BeginChild("plane", {0, height},
                        ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened,
                        ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoScrollWithMouse);
      struct EndPlaneChild { ~EndPlaneChild() { ImGui::EndChild(); } } endChild;
      auto &cache = caches_[i];
      cache.styledLayers = Json::array();
      cache.overlayLayers = Json::array();
      const auto headingPoint = ImGui::GetCursorScreenPos();
      ImGui::Dummy({0, ImGui::GetTextLineHeight()});
      int index = d.view().crosshair[fixed[i]];
      ImGui::SetNextItemWidth(-1);
      if (ImGui::SliderInt("##slice", &index, 0, sizes[fixed[i]] - 1)) {
        cursor = d.view().crosshair;
        cursor[fixed[i]] = index;
        beforeDisplayChange(cursor[2] != d.view().crosshair[2]);
        d.setView({{"crosshair", cursor}});
      }
      cache.sliceId = ImGui::GetItemID();
      cache.sliceFocused = ImGui::IsItemFocused();
      auto sliceMin = ImGui::GetItemRectMin(),
           sliceMax = ImGui::GetItemRectMax();
      cache.slicePoint = {(sliceMin.x + sliceMax.x) * .5f,
                          (sliceMin.y + sliceMax.y) * .5f};
      auto extent = ImGui::GetContentRegionAvail();
      extent.x = std::max(16.f, extent.x);
      extent.y = std::max(16.f, extent.y);
      const bool requestFocus = i == 0 && xyFocusPending_ &&
                                canvasAdmission(input, ImGui::GetID("image")) &&
                                ImGui::IsRectVisible(extent);
      if (requestFocus) {
        ImGui::SetKeyboardFocusHere();
      }
      // Navigation activation is intentionally ignored: Enter/Space must not
      // reuse pointer placement or change the scientific crosshair.
      ImGui::InvisibleButton("image", extent,
                             ImGuiButtonFlags_MouseButtonLeft |
                                 ImGuiButtonFlags_MouseButtonMiddle |
                                 ImGuiButtonFlags_EnableNav);
      auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
      cache.canvasId = ImGui::GetItemID();
      cache.canvasVisible = ImGui::IsItemVisible();
      if (requestFocus && cache.canvasVisible)
        xyFocusPending_ = false;
      cache.canvasPoint = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
      cache.keyboardAdmitted = ImGui::IsItemFocused() &&
                               cache.canvasVisible &&
                               canvasAdmission(input, cache.canvasId);
      if (cache.keyboardAdmitted) {
        for (auto key : navigationKeys) {
          auto owner = ImGui::GetKeyOwner(key);
          if (owner == ImGuiKeyOwner_NoOwner || owner == cache.canvasId)
            ImGui::SetKeyOwner(key, cache.canvasId,
                                ImGuiInputFlags_LockThisFrame);
        }
        if (i == 0 && savedOwner() && saved_roi->pointer_enabled &&
            saved_roi->accept_numeric) {
          const auto owner = ImGui::GetKeyOwner(ImGuiKey_Delete);
          if (owner == ImGuiKeyOwner_NoOwner || owner == cache.canvasId)
            ImGui::SetKeyOwner(ImGuiKey_Delete, cache.canvasId,
                                ImGuiInputFlags_LockThisFrame);
        }
      }
      auto &plane = planes[i];
      plane.draw = ImGui::GetWindowDrawList();
      plane.minimum = a;
      plane.maximum = b;
      plane.extent = extent;
      plane.heading = headingPoint;
      plane.clip = ImGui::GetCurrentWindowRead()->ClipRect;
      plane.hovered = ImGui::IsItemHovered();
      const double planeZoom = prepareGeometry(i, a, extent);
      bool savedConsumedLeft = false;
      try {
        savedConsumedLeft = planar_.pointer(d,
            {views[i],d.view().crosshair[fixed[i]],sizes[horizontal[i]],sizes[vertical[i]],cache.canvasId,
             {cache.origin.x,cache.origin.y},{cache.scale.x,cache.scale.y},{cache.extent.x,cache.extent.y},{a.x,a.y},{b.x,b.y},input.annotation_gui_scale},
            input.annotation_admitted && !input.read_only && input.input_enabled && input.active_document && cache.canvasVisible && !savedOwner(),plane.hovered,input.planar_publish);
      } catch (...) {planar_.cancel("error");throw;}
      if (i == 0 && savedOwner()) {
        auto &io = ImGui::GetIO();
        const SavedRoiCanvasTransform transform{
            {cache.origin.x, cache.origin.y}, {cache.extent.x, cache.extent.y},
            {cache.scale.x, cache.scale.y}, cache.canvasId, SavedRoiCanvasKind::LinkedXY};
        const std::array<double, 2> point{
            (double(io.MousePos.x) - cache.origin.x) / cache.scale.x,
            (double(io.MousePos.y) - cache.origin.y) / cache.scale.y};
        const bool linkedPointer = saved_roi->edit->pointer_active() &&
            saved_roi->edit->transform() &&
            saved_roi->edit->transform()->kind == SavedRoiCanvasKind::LinkedXY;
        const bool allowed = saved_roi->pointer_enabled && input.active_document &&
            input.input_enabled && cache.canvasVisible && !io.WantTextInput &&
            !io.AppFocusLost && !io.KeySuper &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
            !(context->CurrentItemFlags & ImGuiItemFlags_Disabled) &&
            (context->ActiveId == 0 || context->ActiveId == cache.canvasId);
        auto event = [&](const char *phase) {
          cache.savedRoiInput = {{"phase", phase}, {"frame", frame},
              {"canvas_id", cache.canvasId}, {"point", point},
              {"origin", transform.origin}, {"extent", transform.extent},
              {"scale", transform.scale}, {"kind", "linked_xy"},
              {"shift", io.KeyShift}, {"ctrl", io.KeyCtrl}, {"alt", io.KeyAlt}};
        };
        try {
          if (linkedPointer) {
            savedConsumedLeft = true;
            if (!allowed || !(*saved_roi->edit->transform() == transform) ||
                !savedSourceMatches(d.nativeSnapshot()) || io.MouseWheel != 0 ||
                ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
              event("canceled"); cancelSaved();
            } else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
              event("released");
              if (saved_roi->release) saved_roi->release(transform);
              else cancelSaved();
            } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
              // A click selects a vertex without inventing a delivered drag.
              if (io.MouseDelta.x || io.MouseDelta.y || saved_roi->edit->path_transform()) {
                event("updated");
                if (saved_roi->update)
                  saved_roi->update(point, {io.KeyShift, io.KeyCtrl, io.KeyAlt}, transform);
                else cancelSaved();
              }
            } else { event("canceled_missing_release"); cancelSaved(); }
          } else if (allowed && plane.hovered && saved_roi->begin &&
                     ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const auto &selection = saved_roi->edit->candidate()
                ? *saved_roi->edit->candidate() : saved_roi->edit->original();
            if (io.KeyAlt && saved_roi->accept_numeric &&
                (selection.kind == "polygon" || selection.kind == "freehand")) {
              const auto &caps = saved_roi->edit->limits();
              const SelectionEditLimits limits{caps.max_xy_pixels, caps.max_vertices,
                                               caps.max_working_bytes};
              SelectionEditor editor(selection, shape.x, shape.y, limits);
              const auto index = editor.insertion_index(point,
                  {1, cache.scale.y / cache.scale.x}, limits);
              const auto inserted = editor.inserted_vertex(index, point, limits);
              savedConsumedLeft = true; event("inserted_vertex");
              cache.savedRoiInput["vertex_index"] = index;
              if (saved_roi->selected_vertex) *saved_roi->selected_vertex = index;
              saved_roi->accept_numeric(inserted);
            } else {
              // Actual style sizes already include the selected UI scale.
              const double radius = std::max(1., double(ImGui::GetStyle().GrabMinSize) / 2);
              const auto rotation = saved_roi->edit->rotation_handle(radius * 4,
                                                      {cache.scale.x, cache.scale.y});
              const auto hit = saved_roi->edit->hit_test(point,
                  {radius / cache.scale.x, radius / cache.scale.y}, rotation);
              if (hit) {
                savedConsumedLeft = true; event("began");
                cache.savedRoiInput["handle_kind"] = int(hit->kind);
                cache.savedRoiInput["handle_index"] = hit->index;
                if (saved_roi->selected_vertex)
                  *saved_roi->selected_vertex = hit->kind == SelectionHandleKind::Vertex
                      ? std::optional<std::size_t>(hit->index) : std::nullopt;
                saved_roi->begin(transform, *hit, point);
              }
            }
          }
        } catch (...) { cancelSaved(); throw; }
      }
      if (plane.hovered) {
        auto &io = ImGui::GetIO();
        double mouseX =
            (double(io.MousePos.x) - cache.origin.x) / cache.scale.x;
        double mouseY =
            (double(io.MousePos.y) - cache.origin.y) / cache.scale.y;
        bool inside = mouseX >= 0 && mouseX < sizes[horizontal[i]] &&
                      mouseY >= 0 && mouseY < sizes[vertical[i]];
        int x = inside ? int(std::floor(mouseX)) : 0,
            y = inside ? int(std::floor(mouseY)) : 0;
        if (!savedConsumedLeft && inside && (ImGui::IsMouseClicked(0) ||
                       (ImGui::IsItemActive() && ImGui::IsMouseDown(0)))) {
          cursor = d.view().crosshair;
          cursor[horizontal[i]] = x;
          cursor[vertical[i]] = y;
          beforeDisplayChange(cursor[2] != d.view().crosshair[2]);
          d.setView({{"crosshair", cursor}});
        }
        if (io.MouseWheel != 0) {
          if (io.KeyCtrl) {
            beforeDisplayChange(false);
            double next =
                std::clamp(planeZoom * std::pow(1.2, io.MouseWheel), 1e-9, 1e9);
            auto pan = d.view().pan;
            pan[i][0] -= (io.MousePos.x - (a.x + extent.x / 2) - pan[i][0]) *
                         (next / planeZoom - 1);
            pan[i][1] -= (io.MousePos.y - (a.y + extent.y / 2) - pan[i][1]) *
                         (next / planeZoom - 1);
            if (d.view().linkedZoom)
              d.setView({{"zoom", next}, {"pan", pan}});
            else {
              auto zooms = d.view().planeZoom;
              zooms[i] = next;
              d.setView({{"plane_zoom", zooms}, {"pan", pan}});
            }
          } else {
            cursor = d.view().crosshair;
            cursor[fixed[i]] =
                std::clamp(cursor[fixed[i]] + (io.MouseWheel > 0 ? 1 : -1), 0,
                           sizes[fixed[i]] - 1);
            beforeDisplayChange(cursor[2] != d.view().crosshair[2]);
            d.setView({{"crosshair", cursor}});
          }
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
          beforeDisplayChange(false);
          auto pan = d.view().pan;
          pan[i][0] += io.MouseDelta.x;
          pan[i][1] += io.MouseDelta.y;
          d.setView({{"pan", pan}});
        }
      }
    }
  }
  // Every numeric, slice, pointer and wheel change has now been applied.
  // All retained draw lists receive the same final scientific XYZ this frame.
  const auto displayFinal = d.view();
  const auto cursorFinal = displayFinal.crosshair;
  const PlanePosition currentPosition{displayFinal.t, displayFinal.c,
                                      cursorFinal[2]};
  // Navigation does not retarget or release a READY command. An uncaptured
  // current T/C/Z uses the live Dataset for all linked planes this frame.
  live=d.nativeSnapshot();
  const auto pixelRevision=live.revisions().pixels;
  if(pixelMetadataOwner_!=live.id() ||
     !pixelMetadataSeen_.matches(live.revisions(),Change::Metadata|Change::Selection)) {
    Json key=Json::object();
    for(const auto &entry:d.state().metadata.items())
      if(entry.key()!=planar_annotations_extension && entry.key()!=planar_display_annotations_extension && entry.key()!=imagej_roi_import_sources_extension && entry.key()!="slice_index")key[entry.key()]=entry.value();
    if(pixelMetadataOwner_!=live.id() || key!=pixelMetadataKey_) {
      pixelMetadataKey_=std::move(key);++pixelMetadataGeneration_;
    }
    pixelMetadataOwner_=live.id();pixelMetadataSeen_=live.revisions();
  }
  std::optional<ChannelDisplaySources> channelSources;
  if (channelState)
    channelSources.emplace(
        live, transient ? std::optional<DocumentSnapshot>(*transient) : std::nullopt,
        transient_target ? std::optional<ResolvedTarget>(*transient_target) : std::nullopt,
        preview_generation, currentPosition);
  const bool previewActive = channelSources
      ? channelSources->usesPreview(currentPosition.c)
      : transient && (input.preview_targets.empty()?(!transient_target || transient_target->contains(currentPosition)):std::any_of(input.preview_targets.begin(),input.preview_targets.end(),[&](const auto &target){return target.contains(currentPosition);}));
  const auto displayEffects = collectPlanePreviewDisplayEffects(live, currentPosition,
      channelState, channelSources ? &*channelSources : nullptr, previewActive,
      input.preview_display_effects, channel_limits);
  const Dataset *pixelsDataset = previewActive ? &transient->data().dataset : &liveDataset;
  const auto previewGeneration = previewActive ? preview_generation : 0;
  // Borrowed snapshots/targets never enter the caches; keys retain only value
  // metadata and never prolong a preview/source lease.
  const bool uncachedPreview = previewActive && previewGeneration == 0;
  const auto dataIdentity = channelSources
      ? channelSources->dataIdentity(currentPosition.c)
      : previewActive?reinterpret_cast<std::uintptr_t>(&transient->data()):0;
  const auto world = cal.voxel_to_world(
      {double(cursorFinal[0]), double(cursorFinal[1]), double(cursorFinal[2])});
  char worldText[256];
  ImFormatString(worldText, IM_ARRAYSIZE(worldText),
                 "World XYZ: %.6g, %.6g, %.6g %s", world[0], world[1], world[2],
                 cal.unit.c_str());
  worldDraw->PushClipRect(worldClip.Min, worldClip.Max, false);
  worldDraw->AddText(worldPoint, ImGui::GetColorU32(ImGuiCol_Text), worldText);
  worldDraw->PopClipRect();
  auto displayProperties = displayFinal.properties;
  const auto previewRange=previewActive?input.preview_display_range:std::nullopt;
  if(previewRange)displayProperties["display_range"]=metadata_array({(*previewRange)[0],(*previewRange)[1]});
  Json displayMetadata{{"display_range", get(d.state().metadata, "display_range")}};
  if (previewRange) displayMetadata["display_range"] = *previewRange;
  projectPlanePreviewDisplayEffects(displayMetadata, channelState, currentPosition, displayEffects);
  const auto *activeDisplayEffect = planePreviewDisplayEffect(displayEffects, currentPosition);
  if (!channelState && activeDisplayEffect && activeDisplayEffect->range)
    displayProperties["display_range"] = metadata_array(
        {(*activeDisplayEffect->range)[0], (*activeDisplayEffect->range)[1]});
  const auto displayKey = planePreviewDisplayKey(live, currentPosition, displayMetadata, channelState, displayEffects);
  const bool background=input.viewport_jobs!=nullptr;
  if(!background){
  if (channelState) {
    range_ = channelState->channels[static_cast<std::size_t>(currentPosition.c)].range;
    rangeRevision_ = pixelRevision;
    rangePixelMetadataGeneration_ = pixelMetadataGeneration_;
    rangeDocument_ = live.id();
    rangeDataIdentity_ = reinterpret_cast<std::uintptr_t>(&live.data());
    rangeTransient_ = false;
    rangePreviewGeneration_ = activeDisplayEffect && activeDisplayEffect->range ? activeDisplayEffect->generation : 0;
    rangeT_ = currentPosition.t;
    rangeC_ = currentPosition.c;
    rangeSlice_ = currentPosition.z;
    rangeProperties_ = displayProperties;
  } else if (uncachedPreview || rangeDocument_ != live.id() ||
      rangeDataIdentity_ != dataIdentity ||
      rangeTransient_ != previewActive ||
      rangePreviewGeneration_ != previewGeneration ||
      rangeRevision_ != pixelRevision || rangePixelMetadataGeneration_ != pixelMetadataGeneration_ || rangeT_ != currentPosition.t ||
      rangeC_ != currentPosition.c || rangeSlice_ != currentPosition.z ||
      !metadata_equal(rangeProperties_, displayProperties)) {
    const auto &range = get(displayMetadata, "display_range");
    if(previewRange)range_=*previewRange;
    else if (range.is_array() && range.size() == 2)
      range_ = {number(range[0]), number(range[1])};
    else {
      auto rangeMemory=memory_->acquire(RenderMemory::mul(xySamples,sizeof(double)));
      const auto image = liveDataset.plane(currentPosition);
      range_ = sourceDisplayRange(*image, liveDataset.sample_policy());
    }
    rangeRevision_ = pixelRevision;
    rangePixelMetadataGeneration_ = pixelMetadataGeneration_;
    rangeDocument_ = live.id();
    rangeDataIdentity_ = dataIdentity;
    rangeTransient_ = previewActive;
    rangePreviewGeneration_ = previewGeneration;
    rangeT_ = currentPosition.t;
    rangeC_ = currentPosition.c;
    rangeSlice_ = currentPosition.z;
    rangeProperties_ = displayProperties;
  }
  }
  for (int i = 0; i < 3; ++i) {
    const auto &plane = planes[i];
    if (!plane.draw)
      continue;
    auto &cache = caches_[i];
    const int coordinate = cursorFinal[fixed[i]];
    const auto channelKey = channelSources
        ? channelDisplayDiagnostics(*channelState, *channelSources, views[i],
                                    coordinate, currentPosition.t,
                                    currentPosition.c, false, channel_limits, displayEffects)
        : Json(nullptr);
    auto channelCacheKey=channelKey;
    if(channelSources)for(auto &contributor:channelCacheKey["contributors"])
      if(!contributor.at("preview").get<bool>())contributor["data_identity"]=0;
    bool channelUncachedPreview = false;
    bool activeContributes = !channelSources;
    if (channelSources)
      for (const auto &contributor : channelKey.at("contributors")) {
        activeContributes |= contributor.at("c").get<int>() == currentPosition.c;
        channelUncachedPreview |= contributor.at("preview").get<bool>() &&
                                   contributor.at("generation").get<std::uint64_t>() == 0;
      }
    const bool rebuild =
        cache.pending || uncachedPreview || channelUncachedPreview || cache.documentId != live.id() ||
        cache.dataIdentity != (previewActive?dataIdentity:0) || cache.transient != previewActive ||
        cache.previewGeneration != previewGeneration ||
        cache.revision != pixelRevision || cache.pixelMetadataGeneration != pixelMetadataGeneration_ || cache.coordinate != coordinate ||
        cache.t != currentPosition.t || cache.c != currentPosition.c ||
        ((channelState || background) && cache.z != currentPosition.z) ||
        cache.channelDisplayKey != channelCacheKey || cache.previewDisplayKey != displayKey ||
        (!background && !sameDisplayRange(cache.range,range_)) ||
        !metadata_equal(cache.displayProperties, displayProperties) ||
        (gl && (!cache.texture || cache.textureDirty));
    const bool missingReadback =
        capture_readback && (!cache.readbackSource || !cache.readbackDisplay);
    if (rebuild || missingReadback) {
      const auto rowSamples=RenderMemory::mul(std::size_t(sizes[horizontal[i]]),std::size_t(shape.s));
      const auto fallback=i && !pixelsDataset->has_eager_planes()?xySamples:0;
      const auto memoryPlan=RenderMemory::plan(sizes[horizontal[i]],sizes[vertical[i]],shape.s,gl,capture_readback,false,i?RenderMemory::add(rowSamples,fallback):0);
      RenderMemory::Lease memory;
      std::optional<ViewportJobs::Frame> asynchronous;
      std::optional<ViewportSurface> completed;
      const auto declared=background && !channelState?RenderMemory::add(memoryPlan.peak,RenderMemory::mul(xySamples,sizeof(double))):memoryPlan.peak;
      if(background){
        d.admitView(i!=0);
        ViewportRender spec{live};spec.position=currentPosition;spec.plane=views[i];spec.coordinate=coordinate;
        if(transient)spec.preview=*transient;if(transient_target)spec.target=*transient_target;spec.previewTargets=input.preview_targets;
        spec.previewGeneration=preview_generation;spec.metadata=d.state().metadata;if(previewRange)spec.metadata["display_range"]=*previewRange;spec.previewDisplayEffects=displayEffects;spec.linked=true;
        spec.layered=i==0 && hasStyledOverlays(spec.metadata,currentPosition);spec.capture=capture_readback;spec.channelLimits=channel_limits;
        const auto key=Json{{"data",live.data_identity().value_or(0)},{"pixels",pixelRevision},{"meta",pixelMetadataGeneration_},
          {"display",metadataJson(displayProperties)},{"tcz",{currentPosition.t,currentPosition.c,currentPosition.z}},
          {"plane",i},{"coordinate",coordinate},{"preview",previewGeneration},{"preview_data",dataIdentity},
          {"channels",channelKey},{"display_effects",displayKey},{"capture",capture_readback},{"gl",gl}}.dump();
        auto ready=input.viewport_jobs->request(d.id,i+1,key,declared,[spec=std::move(spec)](const ExecutionControl &){return renderViewport(spec);});
        if(!ready){
          cache.pending=true;cache.readbackSource.reset();cache.readbackDisplay.reset();cache.readbackMemory.reset();
          plane.draw->PushClipRect(plane.clip.Min,plane.clip.Max,false);
          plane.draw->AddRectFilled(plane.minimum,plane.maximum,ImGui::GetColorU32(ImGuiCol_ChildBg));
          plane.draw->AddText(plane.heading,ImGui::GetColorU32(ImGuiCol_Text),"Loading view...");
          plane.draw->PopClipRect();status="Loading XYZ views...";continue;
        }
        asynchronous.emplace(std::move(*ready));memory=std::move(asynchronous->reservation);
        completed=std::move(asynchronous->surface);
        range_=completed->range;
        rangeDocument_=live.id();rangeT_=currentPosition.t;rangeC_=currentPosition.c;rangeSlice_=currentPosition.z;
        rangePreviewGeneration_=previewGeneration;
      }else memory=memory_->acquire(memoryPlan.peak);
      try {
      cache.textureDirty = true;
      // Rebuilding without capture cannot leave copies of a prior plane under
      // the new cache identity. Missing copies are filled through this actual
      // extraction/render path, including when only diagnostics need a build.
      cache.readbackSource.reset();
      cache.readbackDisplay.reset();cache.readbackMemory.reset();
      cache.readbackSamplePolicy = FloatingSamplePolicy::FiniteOnly;
      cache.channelDisplay = nullptr;
      std::optional<Image> readbackPixels;
      auto metadata = d.state().metadata;
      const bool ieee = pixelsDataset->sample_policy()==FloatingSamplePolicy::PreserveIEEEFloat32;
      const auto computed = ieee && !channelState
          ? std::optional<std::array<std::uint32_t,2>>(displayRangeBits(range_)) : std::nullopt;
      if (std::isfinite(range_[0]) && std::isfinite(range_[1])) metadata["display_range"] = range_;
      else metadata.erase("display_range");
      if (i == 0)
        metadata["slice_index"] = coordinate;
      // XY annotations retain image coordinates; XZ/YZ need positioned-ROI
      // projection rather than incorrectly drawing XY annotations on them.
      const bool layered = i == 0 && hasStyledOverlays(metadata, currentPosition);
      const auto samplePolicy = pixelsDataset->sample_policy();
      auto display = [&] {
        if(completed){if(capture_readback)readbackPixels=*completed->source;return std::move(*completed->rgb);}
        if (channelSources) {
          auto provider = channelSources->provider();
          auto rgb = compose_channel_display(liveDataset, *channelState,
              views[i], coordinate, currentPosition.t, currentPosition.c,
              provider, channel_limits);
          if (i == 0 && !layered)
            renderDisplayOverlays(rgb, metadata, currentPosition);
          if (capture_readback)
            readbackPixels = *provider(views[i], coordinate, currentPosition.t,
                                       currentPosition.c);
          return rgb;
        }
        auto pixels = pixelsDataset->extract_plane(
            views[i], coordinate, currentPosition.t, currentPosition.c);
        auto rgb = i == 0 ? renderImage(pixels, metadata, !layered, samplePolicy, currentPosition,computed)
                          : displayImage(pixels, metadata, samplePolicy,computed);
        if (capture_readback)
          readbackPixels = std::move(pixels);
        return rgb;
      }();
      if (!completed && i == 0 && activeContributes &&
          !(activeDisplayEffect && activeDisplayEffect->suppress_threshold_overlay)) {
        const auto original = liveDataset.plane(currentPosition);
        renderThresholdOverlay(display, *original, live, currentPosition);
      }
      cache.width = display.width;
      cache.height = display.height;
      if (i == 0) {
        cache.overlays =
            layered ? overlayVisuals(metadata, display.width, display.height, currentPosition)
                    : std::vector<OverlayVisual>{};
      }
      if (gl) {
        std::vector<unsigned char> bytes(display.pixels.size());
        for (std::size_t k = 0; k < bytes.size(); ++k)
          bytes[k] = static_cast<unsigned char>(display.pixels[k]);
        if (!cache.texture)
          glGenTextures(1, &cache.texture);
        glBindTexture(GL_TEXTURE_2D, cache.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        linearInterpolation ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                        linearInterpolation ? GL_LINEAR : GL_NEAREST);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, display.width, display.height,
                     0, GL_RGB, GL_UNSIGNED_BYTE, bytes.data());
        cache.textureMemory=memory.retain(memoryPlan.texture);
        cache.textureDirty = false;
      }
      cache.pending=false;
      cache.coordinate = coordinate;
      cache.t = currentPosition.t;
      cache.c = currentPosition.c;
      cache.revision = pixelRevision;
      cache.pixelMetadataGeneration = pixelMetadataGeneration_;
      cache.documentId = live.id();
      cache.dataIdentity = previewActive?dataIdentity:0;
      cache.transient = previewActive;
      cache.previewGeneration = previewGeneration;
      cache.range = range_;
      cache.displayProperties = displayProperties;
      cache.channelDisplayKey = channelCacheKey;
      cache.previewDisplayKey = displayKey;
      if (channelSources)
        cache.channelDisplay = capture_readback
            ? channelDisplayDiagnostics(*channelState, *channelSources, views[i],
                                        coordinate, currentPosition.t,
                                        currentPosition.c, true, channel_limits, displayEffects)
            : channelKey;
      if (capture_readback) {
        cache.readbackSource = std::move(readbackPixels);
        cache.readbackDisplay = display;
        cache.readbackMemory=memory.retain(RenderMemory::mul(RenderMemory::add(cache.readbackSource->pixels.capacity(),cache.readbackDisplay->pixels.capacity()),sizeof(double)));
        cache.readbackSamplePolicy = samplePolicy;
      }
      if (missingReadback && !rebuild)
        ++cache.diagnosticRebuilds;
      ++cache.builds;
      }catch(...){cache.pending=false;cache.readbackSource.reset();cache.readbackDisplay.reset();cache.readbackMemory.reset();cache.revision=~std::uint64_t(0);cache.textureDirty=true;throw;}
    }
    if(channelSources && !rebuild && !missingReadback)
      cache.channelDisplay=channelDisplayDiagnostics(*channelState,*channelSources,views[i],coordinate,
          currentPosition.t,currentPosition.c,capture_readback,channel_limits,displayEffects);
    if (i == 0) {
      // A saved-row draft changes annotations only. Keep this key out of the
      // scalar/RGB/texture rebuild predicate, including the XZ/YZ providers.
      const auto annotationSource = d.nativeSnapshot();
      if (savedOwner() && !savedSourceMatches(annotationSource)) cancelSaved();
      Json draft = nullptr;
      std::optional<std::size_t> draftRow;
      if (savedOwner() && saved_roi->edit->candidate()) {
        draftRow = saved_roi->edit->row_index();
        draft = selectionJson(*saved_roi->edit->candidate());
      }
      const auto revisions = annotationSource.revisions();
      Json annotationKey{{"document_id", annotationSource.id()},
          {"data_identity", annotationSource.data_identity().value_or(0)},
          {"metadata_revision", revisions.metadata}, {"selection_revision", revisions.selection},
          {"position", {currentPosition.t, currentPosition.c, currentPosition.z}},
          {"shape", {cache.width, cache.height}},
          {"private_row", draftRow ? Json(*draftRow) : Json()}, {"private_geometry", draft}};
      const bool rootPaths=(annotationSource.data().selection && annotationSource.data().selection->is_path()) ||
          std::any_of(annotationSource.data().rois.begin(),annotationSource.data().rois.end(),[](const auto &row){return row.selection.is_path();}) ||
          (!draft.is_null() && get(draft,"kind")=="path");
      if(rootPaths)annotationKey["native_path_scale"]={cache.scale.x,cache.scale.y};
      if(rootPaths)annotationKey["native_path_cap"]=channel_limits.max_working_bytes;
      if (cache.annotationKey != annotationKey) {
        cache.selectionOutline.clear(); cache.roiOutline.clear();
        cache.styledSelection.clear(); cache.styledRois.clear(); cache.styledRoiIndices.clear();
        cache.savedRoiLayers = Json::array();
        auto visual=[&](const Json &geometry,RoiColor fallback) {
          if(get(geometry,"kind")!="path")return roiVisual(geometry,cache.width,cache.height,fallback);
          const auto cap=channel_limits.max_working_bytes;std::size_t retained=0;
          auto charge=[&](const RoiVisual &v){const auto n=pathVisualBytes(v);if(n>cap-retained)throw std::invalid_argument("Linked root path visuals exceed processing memory");retained+=n;};
          for(const auto &v:cache.styledSelection)charge(v);for(const auto &v:cache.styledRois)charge(v);
          return pathVisual(selectionFromJson(geometry),cache.width,cache.height,{cache.scale.x,cache.scale.y},cap-retained,fallback);
        };
        const auto &selected = get(d.state().metadata, "selection");
        if (!selected.empty() && (selected.contains(roi_style_attribute) || get(selected,"kind")=="path"))
          cache.styledSelection.push_back(visual(selected, {255, 213, 75, 220}));
        else if (!selected.empty())
          cache.selectionOutline = selectionOutlineSpans(selected, cache.width, cache.height);
        const auto visible = d.visibleRoiIndices();
        const auto &rois = get(d.state().metadata, "rois");
        for (const auto index : visible) {
          const bool privateGeometry = draftRow && *draftRow == index;
          const auto &selection = privateGeometry ? draft : get(rois[index], "selection");
          cache.savedRoiLayers.push_back({{"row_index", index}, {"preview", privateGeometry},
              {"document_id", annotationSource.id()},
              {"data_identity", annotationSource.data_identity().value_or(0)},
              {"metadata_revision", revisions.metadata}, {"selection_revision", revisions.selection},
              {"tcz", {currentPosition.t, currentPosition.c, currentPosition.z}},
              {"geometry", selection}});
          if (selection.contains(roi_style_attribute) || get(selection,"kind")=="path") {
            cache.styledRoiIndices.push_back(index);
            cache.styledRois.push_back(visual(selection, {64, 210, 240, 180}));
          } else {
            auto spans = selectionOutlineSpans(selection, cache.width, cache.height);
            cache.roiOutline.insert(cache.roiOutline.end(), spans.begin(), spans.end());
          }
        }
        cache.annotationKey = annotationKey;
        ++cache.annotationBuilds;
      }
    }
    cache.z = currentPosition.z;
    if (gl && cache.texture) {
      glBindTexture(GL_TEXTURE_2D, cache.texture);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                      linearInterpolation ? GL_LINEAR : GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                      linearInterpolation ? GL_LINEAR : GL_NEAREST);
    }
    const auto a = plane.minimum, b = plane.maximum;
    prepareGeometry(i, a, plane.extent);
    // EndChild has restored its draw-list clip stack; reinstate the actual
    // child clip before appending the retained heading and clipped canvas.
    plane.draw->PushClipRect(plane.clip.Min, plane.clip.Max, false);
    char heading[64];
    ImFormatString(heading, IM_ARRAYSIZE(heading), "%s  %s=%d", names[i],
                   std::array<const char *, 3>{"X", "Y", "Z"}[fixed[i]],
                   coordinate);
    plane.draw->AddText(plane.heading, ImGui::GetColorU32(ImGuiCol_Text),
                        heading);
    auto draw = plane.draw;
    draw->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_ChildBg));
    draw->PushClipRect(a, b, true);
    draw->AddImage(static_cast<ImTextureID>(gl ? cache.texture : 1),
                   cache.origin,
                   {cache.origin.x + cache.width * cache.scale.x,
                    cache.origin.y + cache.height * cache.scale.y});
    const auto importSource=d.nativeSnapshot();
    const auto importActive=input.import_preview && input.import_preview->matches(importSource,input.annotation_max_working_bytes);
    const PlanarCanvas annotationCanvas{views[i],coordinate,cache.width,cache.height,cache.canvasId,
        {cache.origin.x,cache.origin.y},{cache.scale.x,cache.scale.y},{cache.extent.x,cache.extent.y},{a.x,a.y},{b.x,b.y},input.annotation_gui_scale};
    cache.importPreviewActive=importActive;cache.importPreviewReplace=importActive && input.import_preview->replaces();
    cache.importPreviewLayers=Json::array();cache.importPreviewError.clear();
    planar_.draw(d,annotationCanvas,draw,showRois && !cache.importPreviewReplace,!importActive && !input.read_only);
    if(importActive)try {
      cache.importPreviewLayers=input.import_preview->draw(importSource,annotationCanvas,draw,input.import_preview_diagnostics);
    }catch(const std::exception &error){cache.importPreviewError=error.what();if(input.import_preview_error)input.import_preview_error(cache.importPreviewError);}
    cache.displayLayers=input.display_annotations?input.display_annotations(annotationCanvas,draw):Json::array();
    auto outline = [&](const std::vector<Rect> &spans, ImU32 tint) {
      for (const auto &span : spans) {
        ImVec2 start{cache.origin.x + span.x * cache.scale.x,
                     cache.origin.y + span.y * cache.scale.y};
        draw->AddRectFilled(start,
                            {start.x + span.width * cache.scale.x,
                             start.y + span.height * cache.scale.y},
                            tint);
      }
    };
    if (i == 0) {
      draw->PushClipRect(cache.origin,
                         {cache.origin.x + cache.width * cache.scale.x,
                          cache.origin.y + cache.height * cache.scale.y},
                         true);
      for (const auto &overlay : cache.overlays) {
        drawOverlayVisual(overlay, draw, {cache.origin.x, cache.origin.y},
                          {cache.scale.x, cache.scale.y});
        auto summary = overlayVisualJson(overlay);
        cache.overlayLayers.push_back(summary);
        if (overlay.styled)
          cache.styledLayers.push_back(std::move(summary));
      }
      draw->PopClipRect();
      if (showRois)
        outline(cache.roiOutline, IM_COL32(64, 210, 240, 180));
      for (auto &layer : cache.savedRoiLayers) {
        layer["drawn"] = showRois;
        layer["frame"] = frame;
        layer["canvas_id"] = cache.canvasId;
        layer["origin"] = {cache.origin.x, cache.origin.y};
        layer["scale"] = {cache.scale.x, cache.scale.y};
      }
      if (showRois)
        for (std::size_t n = 0; n < cache.styledRois.size(); ++n) {
          const auto &visual = cache.styledRois[n];
          drawRoiVisual(visual, draw, {cache.origin.x, cache.origin.y},
                        {cache.scale.x, cache.scale.y});
          auto summary = roiVisualJson(visual);
          summary["role"] = "stored_roi";
          const auto row = cache.styledRoiIndices[n];
          for (const auto &layer : cache.savedRoiLayers)
            if (layer.at("row_index") == row) {
              summary["row_index"] = row;
              summary["saved_roi_geometry_preview"] = layer.at("preview");
              summary["metadata_revision"] = layer.at("metadata_revision");
              summary["selection_revision"] = layer.at("selection_revision");
              summary["source_data_identity"] = layer.at("data_identity");
              break;
            }
          cache.styledLayers.push_back(std::move(summary));
        }
      outline(cache.selectionOutline, IM_COL32(255, 213, 75, 220));
      for (const auto &visual : cache.styledSelection) {
        drawRoiVisual(visual, draw, {cache.origin.x, cache.origin.y},
                      {cache.scale.x, cache.scale.y});
        auto summary = roiVisualJson(visual);
        summary["role"] = "selection";
        cache.styledLayers.push_back(std::move(summary));
      }
      if (savedOwner()) {
        try {
          auto handles = saved_roi->edit->handles();
          const double radius = std::max(1., double(ImGui::GetStyle().GrabMinSize) / 4);
          if (const auto rotation = saved_roi->edit->rotation_handle(radius * 8,
                                                     {cache.scale.x, cache.scale.y}))
            handles.push_back(*rotation);
          for (const auto &handle : handles) {
            const ImVec2 p{cache.origin.x + float(handle.position[0]) * cache.scale.x,
                           cache.origin.y + float(handle.position[1]) * cache.scale.y};
            const bool selected = saved_roi->selected_vertex &&
                *saved_roi->selected_vertex && handle.kind == SelectionHandleKind::Vertex &&
                handle.index == **saved_roi->selected_vertex;
            const auto tint = selected ? IM_COL32(255, 255, 255, 255)
                                       : IM_COL32(255, 213, 75, 255);
            if (handle.kind == SelectionHandleKind::Rotate) {
              draw->AddCircleFilled(p, float(radius + 1), IM_COL32(20, 20, 20, 255));
              draw->AddCircle(p, float(radius + 1), tint);
            } else {
              draw->AddRectFilled({p.x - float(radius), p.y - float(radius)},
                                  {p.x + float(radius), p.y + float(radius)}, IM_COL32(20, 20, 20, 255));
              draw->AddRect({p.x - float(radius), p.y - float(radius)},
                            {p.x + float(radius), p.y + float(radius)}, tint);
            }
            cache.savedRoiHandles.push_back({{"kind", int(handle.kind)}, {"index", handle.index},
                {"image_point", handle.position}, {"screen_point", {p.x, p.y}},
                {"row_index", saved_roi->edit->row_index()}, {"canvas_id", cache.canvasId},
                {"selected", selected}});
          }
        } catch(const std::exception &error) {
          if(!savedOwner() || !saved_roi->edit->original().is_path()) {cancelSaved();throw;}
          cache.savedRoiHandles=Json::array();
          cache.savedRoiInput["handle_error"]=error.what();
          cache.savedRoiInput["handle_notice"]="Saved path editing: "+std::string(error.what())+
              ". Adjust Processing memory in Preferences.";
        }
      }
    }
    float hx = cache.origin.x +
               (cursorFinal[horizontal[i]] + .5f) * cache.scale.x;
    float vy = cache.origin.y +
               (cursorFinal[vertical[i]] + .5f) * cache.scale.y;
    draw->AddLine({hx, a.y}, {hx, b.y}, IM_COL32(255, 213, 75, 220));
    draw->AddLine({a.x, vy}, {b.x, vy}, IM_COL32(255, 213, 75, 220));
    draw->PopClipRect();
    if(cache.savedRoiInput.contains("handle_notice")) {
      const auto notice=cache.savedRoiInput.at("handle_notice").get<std::string>();
      draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),
          {a.x+8*input.annotation_gui_scale,a.y+8*input.annotation_gui_scale},IM_COL32(255,213,75,255),
          notice.c_str(),nullptr,std::max(1.f,cache.extent.x-16*input.annotation_gui_scale));
    }
    if (context->NavId == cache.canvasId && cache.canvasVisible)
      draw->AddRect(a, b, ImGui::GetColorU32(ImGuiCol_NavCursor), 0, 0, 2.f);
    plane.draw->PopClipRect();
    if (plane.hovered) {
      std::ostringstream text;
      text << names[i] << " | XYZ=" << cursorFinal[0] << ',' << cursorFinal[1]
           << ',' << cursorFinal[2];
      status = text.str();
    }
  }
  result.focused_plane = focusedPlane();
  return result;
}
void OrthogonalViews::setRenderMemory(std::shared_ptr<RenderMemory> memory){
  if(memory_==memory)return;
  if(!memory)throw std::invalid_argument("Render memory pool required");
  for(const auto &cache:caches_)if(cache.textureMemory.bytes() || cache.readbackMemory.bytes())throw std::logic_error("Cannot rebind live render reservations");
  memory_=std::move(memory);
}
void OrthogonalViews::releaseTextures() {
  for (auto &cache : caches_) {
    if (cache.texture)
      glDeleteTextures(1, &cache.texture);
    cache.texture = 0;cache.textureMemory.reset();
    cache.textureDirty = true;
  }
}
void OrthogonalViews::clearReadback() {
  for (auto &cache : caches_) {
    cache.readbackSource.reset();
    cache.readbackDisplay.reset();cache.readbackMemory.reset();
    cache.readbackSamplePolicy = FloatingSamplePolicy::FiniteOnly;
  }
}
Json OrthogonalViews::diagnostics(bool include_readback) const {
  Json result = Json::array();
  auto context = ImGui::GetCurrentContext();
  const bool current = context && context == inputContext_ &&
                       inputFrame_ == ImGui::GetFrameCount();
  Json owners = Json::object();
  for (std::size_t key = 0; key < navigationKeys.size(); ++key) {
    auto owner = current ? ImGui::GetKeyOwner(navigationKeys[key])
                         : ImGuiKeyOwner_NoOwner;
    owners[keyNames[key]] = owner == ImGuiKeyOwner_NoOwner
                                ? Json(nullptr)
                                : Json(owner);
  }
  const auto deleteOwner = current ? ImGui::GetKeyOwner(ImGuiKey_Delete)
                                   : ImGuiKeyOwner_NoOwner;
  owners["Delete"] = deleteOwner == ImGuiKeyOwner_NoOwner ? Json(nullptr) : Json(deleteOwner);
  for (int i = 0; i < 3; ++i) {
    const auto &cache = caches_[i];
    const bool focused = current && cache.canvasVisible &&
                         cache.canvasId != 0 &&
                         context->NavId == cache.canvasId;
    result.push_back({{"plane", names[i]},
                      {"coordinate", cache.coordinate},
                      {"t", cache.t}, {"c", cache.c}, {"z", cache.z},
                      {"current_tcz", {cache.t, cache.c, cache.z}},
                      {"width", cache.width},
                      {"height", cache.height},
                      {"spacing", cache.spacing},
                      {"loading",cache.pending},
                      {"builds", cache.builds},
                      {"selection_spans", cache.selectionOutline.size()},
                      {"roi_spans", cache.roiOutline.size()},
                      {"scale", {cache.scale.x, cache.scale.y}}});
    result.back()["extent"] = {cache.extent.x, cache.extent.y};
    result.back()["origin"] = {cache.origin.x, cache.origin.y};
    result.back()["style_layers"] = cache.styledLayers;
    result.back()["saved_roi_geometry_layers"] = cache.savedRoiLayers;
    result.back()["saved_roi_geometry_input"] = cache.savedRoiInput;
    result.back()["saved_roi_geometry_handles"] = cache.savedRoiHandles;
    result.back()["annotation_builds"] = cache.annotationBuilds;
    result.back()["overlay_layers"] = cache.overlayLayers;
    result.back()["planar_annotations"] = planar_.diagnostics();
    result.back()["display_annotations"] = cache.displayLayers;
    result.back()["import_preview"]={{"active",current && cache.importPreviewActive},{"replace",current && cache.importPreviewReplace},
        {"layers",current?cache.importPreviewLayers:Json::array()},{"error",cache.importPreviewError}};
    result.back()["focused"] = focused;
    result.back()["slice_focused"] = current && cache.sliceId != 0 &&
                                     context->NavId == cache.sliceId;
    result.back()["visible"] = current && cache.canvasVisible;
    result.back()["canvas_id"] = cache.canvasId;
    result.back()["slice_id"] = cache.sliceId;
    result.back()["canvas_point"] = {cache.canvasPoint.x, cache.canvasPoint.y};
    result.back()["slice_point"] = {cache.slicePoint.x, cache.slicePoint.y};
    result.back()["keyboard_admitted"] = focused && cache.keyboardAdmitted;
    result.back()["key_owners"] = owners;
    result.back()["pixels_source"] =
        cache.transient ? "transient" : "live_document";
    result.back()["preview_generation"] = cache.previewGeneration;
    result.back()["source_document_id"] = cache.documentId;
    result.back()["range"] = displayRangeValue(cache.range);
    result.back()["range_policy"] = cache.previewDisplayKey.value("contrast_source", "live") == "top_hat_preview"
        ? "top_hat_preview" : cache.channelDisplay.is_null() ? "live_document" : "channel_display";
    result.back()["contrast_source"] = cache.previewDisplayKey.value("contrast_source", "live");
    result.back()["threshold_source"] = i == 0
        ? cache.previewDisplayKey.value("threshold_source", "live") : "not_applicable";
    result.back()["preview_display_effects"] = cache.previewDisplayKey.value("effects", Json::array());
    if (!cache.channelDisplay.is_null())
      result.back()["channel_display"] = cache.channelDisplay;
    result.back()["range_source_document_id"] = rangeDocument_;
    result.back()["range_preview_generation"] = rangePreviewGeneration_;
    result.back()["range_tcz"] = {rangeT_, rangeC_, rangeSlice_};
    if (include_readback) {
      const bool available = cache.readbackSource && cache.readbackDisplay;
      auto image = [](const Image &pixels, FloatingSamplePolicy sample_policy) -> Json {
        if (sample_policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 ||
            sample_policy == FloatingSamplePolicy::PreserveIEEEFloat32) {
          if (pixels.type != PixelType::Float32 || pixels.channels != 1)
            throw std::invalid_argument("Quiet-NaN readback requires grayscale Float32");
          Json bytes = Json::array();
          auto &output = bytes.get_ref<Json::array_t &>();
          if (pixels.pixels.size() > output.max_size() / 4)
            throw std::length_error("Float32 readback byte array is too large");
          output.reserve(pixels.pixels.size() * 4);
          for (double sample : pixels.pixels) {
            const auto bits = float32_source_sample_bits(sample, sample_policy);
            for (unsigned int byte = 0; byte < 4; ++byte)
              output.emplace_back(static_cast<unsigned int>((bits >> (8 * byte)) & 0xffu));
          }
          return {{"shape", {pixels.height, pixels.width, pixels.channels}},
                  {"dtype", typeName(pixels.type)},
                  {"encoding", "IEEE754 Float32LE"}, {"bytes", std::move(bytes)}};
        }
        return Json{{"shape", {pixels.height, pixels.width, pixels.channels}},
                    {"dtype", typeName(pixels.type)},
                    {"pixels", pixels.pixels}};
      };
      result.back()["readback"] =
          {{"available", available},
           {"source_tcz", {cache.t, cache.c, cache.z}},
           {"diagnostic_rebuilds", cache.diagnosticRebuilds},
           {"source", available ? image(*cache.readbackSource, cache.readbackSamplePolicy) : Json(nullptr)},
           {"rendered_rgb", available ? image(*cache.readbackDisplay, FloatingSamplePolicy::FiniteOnly)
                                      : Json(nullptr)}};
    }
  }
  return result;
}
ImVec2 OrthogonalViews::screenPoint(int view, int h, int v) const {
  const auto &cache = caches_.at(view);
  return {cache.origin.x + (h + .5f) * cache.scale.x,
          cache.origin.y + (v + .5f) * cache.scale.y};
}
} // namespace imagejxx::desktop
