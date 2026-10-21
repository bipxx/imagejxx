#include "display_annotation_panel.hpp"
#include "display_annotation_renderer.hpp"
#include "geometry_number.hpp"
#include "rendering.hpp"
#include <imagejxx/channel_display.hpp>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
namespace imagejxx::desktop {
namespace {
std::uint32_t argb(const std::string &text, bool required) {
  const auto c = parse_roi_color(text);
  if (!c) {
    if (required)
      throw std::invalid_argument("Choose a foreground color");
    return 0;
  }
  return (std::uint32_t((*c)[3]) << 24) | (std::uint32_t((*c)[0]) << 16) |
         (std::uint32_t((*c)[1]) << 8) | (*c)[2];
}
std::string color_string(std::uint32_t c) {
  return roi_color_string(RoiColor{
      static_cast<unsigned char>(c >> 16), static_cast<unsigned char>(c >> 8),
      static_cast<unsigned char>(c), static_cast<unsigned char>(c >> 24)});
}
bool matches(const DocumentSnapshot &a, const DocumentSnapshot &b) {
  return a.same_document(b) &&
         a.revisions().matches(b.revisions(),
                               Change::Pixels | Change::Metadata);
}
} // namespace
struct DisplayAnnotationPanel::State {
  bool open = false, focus = false;
  bool draft=false;
  std::optional<PlanarDisplayAnnotation> completed_draft;
  std::optional<DocumentSnapshot> source;
  std::vector<PlanarDisplayAnnotation> rows;
  std::optional<std::size_t> selected;
  PlanarDisplayAnnotation base;
  int plane = 0, coordinate = 0, time = 1, channel = 1, justify = 0,
      opacity = 255;
  bool all_t = false, all_c = false, bold = false, italic = false,
       baseline = false, zero = false;
  std::array<std::string, 4> bounds;
  std::string name, text, font, font_size, angle, foreground, background, error,
      reason;
  Json controls = Json::object();
  Json window=Json::array(),fields_window=Json::array();
  void observe(const std::string &key, float width = 0, ImGuiID id = 0) {
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    controls[key] = {
        {"id", id ? id : ImGui::GetItemID()},
        {"visible",ImGui::IsItemVisible()},
        {"enabled", !(ImGui::GetCurrentContext()->LastItemData.ItemFlags &
                      ImGuiItemFlags_Disabled)},
        {"rect", {a.x, a.y, b.x, b.y}},
        {"point",
         {width ? a.x + width / 2 : (a.x + b.x) / 2, (a.y + b.y) / 2}}};
  }
  void input(const char *label, std::string &value, const char *key,
             float width = 180) {
    ImGui::SetNextItemWidth(width);
    const auto id = ImGui::GetID(label);
    ImGui::InputText(label, &value);
    observe(key, width, id);
  }
  void seed(PlanarDisplayAnnotation row,
            std::optional<std::size_t> index = {}) {
    base = std::move(row);
    selected = index;
    name = base.name;
    plane = int(base.plane);
    coordinate = base.coordinate;
    all_t = !base.t;
    all_c = !base.c;
    time = base.t.value_or(0) + 1;
    channel = base.c.value_or(0) + 1;
    const auto b =
        std::visit([](const auto &p) { return p.bounds; }, base.payload);
    for (int i = 0; i < 4; ++i)
      bounds[i] = geometry_number_text(b[i]);
    if (const auto p = std::get_if<TextAnnotationPayload>(&base.payload)) {
      text = p->text;
      font = p->font_name;
      font_size = geometry_number_text(p->font_size);
      angle = geometry_number_text(p->angle);
      bold = p->font_style & 1;
      italic = p->font_style & 2;
      justify = p->justification;
      baseline = p->draw_string_mode;
      foreground = color_string(p->color);
      background = p->background ? color_string(*p->background) : "none";
    } else {
      const auto &image_payload =
          std::get<ImageAnnotationPayload>(base.payload);
      opacity = image_payload.opacity;
      zero = image_payload.zero_transparent;
    }
    error.clear();
  }
  void fresh(bool image) {
    PlanarDisplayAnnotation row;
    row.name = image ? "Image" : "Text";
    row.coordinate = source->display().crosshair[2];
    row.t = source->display().t;
    row.c = source->display().c;
    if (image)
      row.payload = ImageAnnotationPayload{};
    else {
      TextAnnotationPayload p;
      p.text = "Label";
      p.bounds = {1, 1, 20, 8};
      p.font_size = 8;
      row.payload = p;
    }
    seed(std::move(row));
  }
  PlanarDisplayAnnotation value() const {
    auto row = base;
    row.name = name;
    row.plane = Plane(plane);
    row.coordinate = coordinate;
    if ((!all_t && time < 1) || (!all_c && channel < 1))
      throw std::invalid_argument("T/C must be positive or wildcard");
    row.t = all_t ? std::optional<int>{} : time - 1;
    row.c = all_c ? std::optional<int>{} : channel - 1;
    std::array<double, 4> b;
    for (int i = 0; i < 4; ++i)
      b[i] = parse_geometry_number(bounds[i]);
    if (auto p = std::get_if<TextAnnotationPayload>(&row.payload)) {
      p->bounds = b;
      p->text = text;
      p->font_name = font;
      p->font_size = parse_geometry_number(font_size);
      p->angle = parse_geometry_number(angle);
      p->font_style = (bold ? 1 : 0) | (italic ? 2 : 0);
      p->justification = static_cast<unsigned char>(justify);
      p->draw_string_mode = baseline;
      p->color = argb(foreground, true);
      p->background =
          parse_roi_color(background)
              ? std::optional<std::uint32_t>(argb(background, false))
              : std::optional<std::uint32_t>{};
    } else {
      if (opacity < 0 || opacity > 255)
        throw std::invalid_argument("Image opacity must be 0..255");
      auto &image_payload = std::get<ImageAnnotationPayload>(row.payload);
      image_payload.bounds = b;
      image_payload.opacity = static_cast<unsigned char>(opacity);
      image_payload.zero_transparent = zero;
    }
    row.validate(source->data().dataset.shape());
    return row;
  }
};
DisplayAnnotationPanel::DisplayAnnotationPanel()
    : state_(std::make_unique<State>()) {}
DisplayAnnotationPanel::~DisplayAnnotationPanel() = default;
bool DisplayAnnotationPanel::visible() const { return state_->open; }
void DisplayAnnotationPanel::cancel(const char *reason) {
  auto &s = *state_;
  s.open = false;
  s.source.reset();
  s.rows.clear();
  s.selected.reset();
  s.completed_draft.reset();
  s.controls = Json::object();
  s.reason = reason;
}
void DisplayAnnotationPanel::show(const DocumentSnapshot &source) {
  cancel("reopened");
  auto &s = *state_;
  s.source = source;
  s.draft=false;
  s.rows = planar_display_annotations(source.data());
  s.fresh(false);
  s.open = s.focus = true;
  s.reason.clear();
}
void DisplayAnnotationPanel::show_draft(const DocumentSnapshot &source,std::size_t row){
  show(source);auto &s=*state_;s.seed(s.rows.at(row),row);s.draft=true;
}
std::optional<PlanarDisplayAnnotation> DisplayAnnotationPanel::take_draft(){
  auto result=std::move(state_->completed_draft);state_->completed_draft.reset();return result;
}
void DisplayAnnotationPanel::draw(
    Document *document, bool busy, std::size_t cap,
    const std::function<void(const ResolvedPlanarDisplayAnnotationCommand &)>
        &publish,
    const std::function<Json()> &capture_metadata) {
  auto &s = *state_;
  s.controls = Json::object();
  if (!s.open)
    return;
  if (!document || !s.source ||
      !matches(*s.source, document->nativeSnapshot())) {
    cancel("source_changed");
    return;
  }
  if (busy) {
    cancel("busy");
    return;
  }
  if (s.focus)
    ImGui::SetNextWindowFocus();
  s.focus = false;
  ImGui::SetNextWindowSize(s.draft?ImVec2{650,std::min(760.f,ImGui::GetMainViewport()->WorkSize.y-40)}:ImVec2{650,740},ImGuiCond_FirstUseEver);
  ImGui::Begin(s.draft?"Import display payload draft":"Text and image annotations", &s.open);
  const auto a=ImGui::GetCurrentWindow()->InnerRect.Min,b=ImGui::GetCurrentWindow()->InnerRect.Max;
  s.window={a.x,a.y,b.x,b.y};s.fields_window=s.window;
  if(s.draft){
    ImGui::TextWrapped("Edits stay inside this import draft. Apply the annotation, then Close to return. Parent replacement confirmation and complete import Apply are still required.");
    ImGui::BeginChild("Draft annotation fields",{0,std::max(100.f,ImGui::GetContentRegionAvail().y-90)},false);
    const auto p=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();s.fields_window={p.x,p.y,p.x+size.x,p.y+size.y};
  }
  if (ImGui::Button("New text"))
    s.fresh(false);
  s.observe("new_text");
  ImGui::SameLine();
  if (ImGui::Button("New image"))
    s.fresh(true);
  s.observe("new_image");
  if (ImGui::BeginCombo("Stored display annotation",
                        s.selected ? s.rows[*s.selected].name.c_str()
                                   : "Choose a row")) {
    ImGuiListClipper clip;
    clip.Begin(int(s.rows.size()));
    while (clip.Step())
      for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
        ImGui::PushID(i);
        if (ImGui::Selectable("##row", s.selected == std::size_t(i)))
          s.seed(s.rows[i], std::size_t(i));
        s.observe("row." + std::to_string(i));
        ImGui::GetWindowDrawList()->AddText(ImGui::GetItemRectMin(),
                                            ImGui::GetColorU32(ImGuiCol_Text),
                                            s.rows[i].name.c_str());
        ImGui::PopID();
      }
    ImGui::EndCombo();
  }
  s.observe("rows");
  s.input("Name", s.name, "name", 350);
  const char *planes[]{"XY", "XZ", "YZ"};
  auto combo = [&](const char *label, int &value, const char *const *names,
                   int count, const std::string &key) {
    ImGui::SetNextItemWidth(150);
    if (ImGui::BeginCombo(label, names[value])) {
      for (int i = 0; i < count; ++i) {
        if (ImGui::Selectable(names[i], value == i))
          value = i;
        s.observe(key + "." + std::to_string(i));
      }
      ImGui::EndCombo();
    }
    s.observe(key);
  };
  combo("Plane", s.plane, planes, 3, "plane");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(100);
  auto id = ImGui::GetID("Fixed coordinate (0-based)");
  ImGui::InputInt("Fixed coordinate (0-based)", &s.coordinate, 0, 0);
  s.observe("coordinate", 100, id);
  ImGui::Checkbox("All times", &s.all_t);
  s.observe("all_t");
  ImGui::SameLine();
  ImGui::BeginDisabled(s.all_t);
  ImGui::SetNextItemWidth(80);
  id = ImGui::GetID("Time (1-based)");
  ImGui::InputInt("Time (1-based)", &s.time, 0, 0);
  s.observe("time", 80, id);
  ImGui::EndDisabled();
  ImGui::Checkbox("All channels", &s.all_c);
  s.observe("all_c");
  ImGui::SameLine();
  ImGui::BeginDisabled(s.all_c);
  ImGui::SetNextItemWidth(80);
  id = ImGui::GetID("Channel (1-based)");
  ImGui::InputInt("Channel (1-based)", &s.channel, 0, 0);
  s.observe("channel", 80, id);
  ImGui::EndDisabled();
  s.input("U", s.bounds[0], "u");
  ImGui::SameLine();
  s.input("V", s.bounds[1], "v");
  s.input("Width", s.bounds[2], "width");
  ImGui::SameLine();
  s.input("Height", s.bounds[3], "height");
  if (std::holds_alternative<TextAnnotationPayload>(s.base.payload)) {
    ImGui::InputTextMultiline("Text", &s.text, {450, 65});
    s.observe("text", 400);
    s.input("Font alias", s.font, "font");
    ImGui::SameLine();
    if (ImGui::Button("Use UI sans serif"))
      s.font = "SansSerif";
    s.observe("ui_font");
    s.input("Font size", s.font_size, "font_size");
    ImGui::SameLine();
    s.input("Angle", s.angle, "angle");
    ImGui::Checkbox("Bold (synthetic)", &s.bold);
    s.observe("bold");
    ImGui::SameLine();
    ImGui::Checkbox("Italic (synthetic)", &s.italic);
    s.observe("italic");
    const char *align[]{"Left", "Center", "Right"};
    combo("Justification", s.justify, align, 3, "justify");
    ImGui::Checkbox("First-line origin is baseline", &s.baseline);
    s.observe("baseline");
    s.input("Foreground", s.foreground, "foreground");
    ImGui::SameLine();
    s.input("Background/none", s.background, "background");
    ImGui::TextWrapped("SansSerif uses the current UI font. Other families "
                       "require mapping; unavailable glyphs use its fallback. "
                       "Rotation is about the bounds center.");
  } else {
    const auto &image_payload =
        std::get<ImageAnnotationPayload>(s.base.payload);
    ImGui::Text("Retained pixels: %d x %d, %d channels", image_payload.width,
                image_payload.height, image_payload.channels);
    if (ImGui::Button("Capture current plane display as RGB"))
      try {
        const auto source = document->nativeSnapshot();
        const auto shape = source.data().dataset.shape();
        const auto geometry = planar_annotation_geometry(shape, Plane(s.plane));
        if (std::size_t(geometry.width) * geometry.height > 1024 * 1024 ||
            std::size_t(geometry.width) * geometry.height > cap / 256)
          throw std::invalid_argument(
              "Display image capture exceeds pixel/memory limit");
        // This explicit button is the only editor operation that reads image
        // pixels.
        auto rgb = [&] {
          if (const auto channels = recognized_channel_display(
                  source.display(), source.data().dataset)) {
            ChannelDisplayLimits limits;
            limits.max_pixels = 1024 * 1024;
            limits.max_working_bytes = cap / 2;
            return compose_channel_display(
                source.data().dataset, *channels, Plane(s.plane), s.coordinate,
                source.display().t, source.display().c, {}, limits);
          }
          auto image = source.data().dataset.extract_plane(
              Plane(s.plane), s.coordinate, source.display().t,
              source.display().c);
          return displayImage(image,
                              capture_metadata ? capture_metadata()
                                               : document->state().metadata,
                              source.data().dataset.sample_policy());
        }();
        ImageAnnotationPayload result;
        result.width = rgb.width;
        result.height = rgb.height;
        result.channels = 3;
        result.pixels.clear();
        result.pixels.reserve(rgb.pixels.size());
        for (double n : rgb.pixels)
          result.pixels.push_back(static_cast<unsigned char>(n));
        result.bounds = {parse_geometry_number(s.bounds[0]),
                         parse_geometry_number(s.bounds[1]), double(rgb.width),
                         double(rgb.height)};
        result.opacity =
            std::get<ImageAnnotationPayload>(s.base.payload).opacity;
        result.zero_transparent = s.zero;
        result.validate();
        s.base.payload = std::move(result);
        s.bounds[2] = geometry_number_text(rgb.width);
        s.bounds[3] = geometry_number_text(rgb.height);
        s.error.clear();
      } catch (const std::exception &e) {
        s.error = e.what();
      }
    s.observe("capture_image");
    ImGui::SetNextItemWidth(100);
    id = ImGui::GetID("Opacity 0-255");
    ImGui::InputInt("Opacity 0-255", &s.opacity, 0, 0);
    s.observe("opacity", 100, id);
    ImGui::Checkbox("Black RGB is transparent", &s.zero);
    s.observe("zero");
  }
  auto publish_request = [&](const PlanarDisplayAnnotationRequest &request) {
    const auto command =
        resolve_planar_display_annotation_command(*s.source, request);
    admit_display_annotation_visuals(
        decode_planar_display_annotations(s.source->data().dataset.shape(),
                                          command.record().at("after")),
        cap);
    const auto index = request.index.value_or(s.rows.size());
    publish(command);
    s.source = document->nativeSnapshot();
    s.rows = planar_display_annotations(s.source->data());
    if (request.operation == PlanarDisplayAnnotationOperation::Delete)
      s.fresh(false);
    else
      s.seed(s.rows.at(index), index);
  };
  if(s.draft)ImGui::EndChild();
  if (ImGui::Button(s.selected ? "Apply complete annotation"
                               : "Store annotation"))
    try {
      publish_request({s.selected ? PlanarDisplayAnnotationOperation::Update
                                  : PlanarDisplayAnnotationOperation::Store,
                       s.selected, s.value()});
    } catch (const std::exception &e) {
      s.error = e.what();
    }
  s.observe("apply");
  ImGui::SameLine();
  ImGui::BeginDisabled(!s.selected || s.draft);
  if (ImGui::Button("Delete annotation"))
    try {
      publish_request(
          {PlanarDisplayAnnotationOperation::Delete, s.selected, {}});
    } catch (const std::exception &e) {
      s.error = e.what();
    }
  s.observe("delete");
  ImGui::EndDisabled();
  if (ImGui::Button("Go to annotation plane"))
    try {
      const auto row = s.value();
      auto view = document->nativeSnapshot().display();
      if (row.t)
        view.t = *row.t;
      if (row.c)
        view.c = *row.c;
      const auto axes = planar_annotation_geometry(
          document->nativeSnapshot().data().dataset.shape(), row.plane);
      view.crosshair[axes.fixed_axis] = row.coordinate;
      view.orthogonal = true;
      document->setNativeDisplay(std::move(view));
    } catch (const std::exception &e) {
      s.error = e.what();
    }
  s.observe("go_to");
  ImGui::SameLine();
  const bool close = ImGui::Button("Close");
  s.observe("close");
  if (!s.error.empty())
    ImGui::TextWrapped("%s", s.error.c_str());
  const bool escape =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      ImGui::IsKeyPressed(ImGuiKey_Escape);
  ImGui::End();
  if (!s.open || close || escape) {
    auto accepted=s.draft&&s.selected?std::optional<PlanarDisplayAnnotation>(s.rows.at(*s.selected)):std::optional<PlanarDisplayAnnotation>{};
    cancel("canceled");
    s.completed_draft=std::move(accepted);
  }
}
Json DisplayAnnotationPanel::diagnostics() const {
  const auto &s = *state_;
  return {{"visible", s.open},
          {"row_count", s.rows.size()},
          {"draft",s.draft},
          {"window",s.window},{"fields_window",s.fields_window},
          {"selected", s.selected ? Json(*s.selected) : Json(nullptr)},
          {"source_id", s.source ? s.source->id() : 0},
          {"kind", std::holds_alternative<TextAnnotationPayload>(s.base.payload)
                       ? "text"
                       : "image"},
          {"error", s.error},
          {"cancel_reason", s.reason},
          {"controls", s.controls}};
}
} // namespace imagejxx::desktop
