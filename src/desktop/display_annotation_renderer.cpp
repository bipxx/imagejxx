#include "display_annotation_renderer.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <imgui_internal.h>
#include <map>
namespace imagejxx::desktop {
void admit_display_annotation_visuals(
    const std::vector<PlanarDisplayAnnotation> &rows, std::size_t cap) {
  std::size_t bytes = 0;
  auto charge = [&](std::size_t n, std::size_t unit) {
    if (!cap || n > (cap - bytes) / unit)
      throw std::invalid_argument(
          "Display annotation visuals exceed processing memory cap");
    bytes += n * unit;
  };
  for (const auto &row : rows) {
    charge(1, 2048);
    if (const auto p = std::get_if<TextAnnotationPayload>(&row.payload))
      charge(p->text.size(), 256);
    else {
      const auto &image_payload = std::get<ImageAnnotationPayload>(row.payload);
      charge(std::size_t(image_payload.width) * image_payload.height, 12);
    }
  }
}
namespace {
ImU32 color(std::uint32_t argb) {
  return IM_COL32((argb >> 16) & 255, (argb >> 8) & 255, argb & 255,
                  argb >> 24);
}
ImVec2 screen(const PlanarCanvas &c, double u, double v) {
  return {float(c.origin[0] + u * c.scale[0]),
          float(c.origin[1] + v * c.scale[1])};
}
struct TextLayout {
  std::vector<std::vector<unsigned int>> lines;
  std::size_t missing = 0;
};
TextLayout layout(const TextAnnotationPayload &p, ImFont *font) {
  TextLayout result;
  result.lines.emplace_back();
  const char *s = p.text.data(), *end = s + p.text.size();
  while (s < end) {
    unsigned int ch = 0;
    const auto n = ImTextCharFromUtf8(&ch, s, end);
    if (n <= 0)
      throw std::invalid_argument("Invalid display text UTF-8");
    s += n;
    if (ch == '\r')
      continue;
    if (ch == '\n') {
      result.lines.emplace_back();
      continue;
    }
    if (ch != '\t' && !font->FindGlyphNoFallback(ch))
      ++result.missing;
    result.lines.back().push_back(ch);
  }
  return result;
}
ImVec2 mapped_screen(const PlanarDisplayAnnotation &row,const PlanarCanvas &canvas,double u,double v) {
  if(!row.has_transform())return screen(canvas,u,v);
  const auto &a=row.transform;
  return screen(canvas,a[0]*u+a[1]*v+a[2],a[3]*u+a[4]*v+a[5]);
}
Json draw_text(const PlanarDisplayAnnotation &row,const TextAnnotationPayload &p, const PlanarCanvas &canvas,
               ImDrawList *draw) {
  if (p.font_name != "SansSerif" && p.font_name != "ImGui UI font")
    throw std::invalid_argument("Font is not mapped: " + p.font_name +
                                ". Choose the UI sans serif font explicitly.");
  const auto font = ImGui::GetFont();
  const auto l = layout(p, font);
  const double scale = p.font_size / font->FontSize;
  const auto &b = p.bounds;
  const double radians = std::remainder(p.angle, 360.) * std::acos(-1.) / 180.;
  const double cs = std::cos(radians), sn = std::sin(radians),
               cx = b[0] + b[2] / 2, cy = b[1] + b[3] / 2;
  auto point = [&](double x, double y) {
    const auto dx = x - cx, dy = y - cy;
    return mapped_screen(row,canvas, cx + cs * dx - sn * dy, cy + sn * dx + cs * dy);
  };
  if (p.background)
    draw->AddQuadFilled(point(b[0], b[1]), point(b[0] + b[2], b[1]),
                        point(b[0] + b[2], b[1] + b[3]),
                        point(b[0], b[1] + b[3]), color(*p.background));
  auto advance = [&](unsigned int ch) {
    return (ch == '\t' ? 4 * font->GetCharAdvance(' ')
                       : font->GetCharAdvance(ch)) *
           scale;
  };
  std::size_t glyphs = 0;
  double top = b[1] - (p.draw_string_mode ? font->Ascent * scale : 0);
  draw->PushTextureID(font->ContainerAtlas->TexID);
  for (const auto &line : l.lines) {
    double width = 0;
    for (auto ch : line)
      width += advance(ch);
    double x = b[0] + (p.justification == 1   ? (b[2] - width) / 2
                       : p.justification == 2 ? b[2] - width
                                              : 0);
    for (auto ch : line) {
      const auto g = ch == '\t' ? nullptr : font->FindGlyph(ch);
      if (g && g->Visible) {
        const double x0 = x + g->X0 * scale, x1 = x + g->X1 * scale,
                     y0 = top + g->Y0 * scale, y1 = top + g->Y1 * scale;
        for (int bold = 0; bold < ((p.font_style & 1) ? 2 : 1); ++bold) {
          const auto shift = bold * p.font_size / 16;
          auto italic = [&](double px, double py) {
            return px + shift +
                   ((p.font_style & 2) ? .2 * (top + p.font_size - py) : 0);
          };
          draw->PrimReserve(6, 4);
          draw->PrimQuadUV(point(italic(x0, y0), y0), point(italic(x1, y0), y0),
                           point(italic(x1, y1), y1), point(italic(x0, y1), y1),
                           {g->U0, g->V0}, {g->U1, g->V0}, {g->U1, g->V1},
                           {g->U0, g->V1}, color(p.color));
        }
        ++glyphs;
      }
      x += advance(ch);
    }
    top += p.font_size;
  }
  draw->PopTextureID();
  return {{"font_alias", p.font_name},
          {"resolved_font", font->GetDebugName()},
          {"missing_glyphs", l.missing},
          {"glyphs", glyphs},
          {"lines", l.lines.size()},
          {"angle", p.angle},
          {"font_style", p.font_style},
          {"justification", p.justification},
          {"baseline_mode", p.draw_string_mode},
          {"rotation_origin", "bounds_center"}};
}
} // namespace
struct DisplayAnnotationRenderer::State {
  struct Texture {
    unsigned int id = 0;
    std::vector<unsigned char> rgba;
  };
  std::uint64_t owner = 0, revision = ~std::uint64_t(0);
  std::vector<PlanarDisplayAnnotation> rows;
  std::optional<MetadataValue> extension_key;
  std::map<std::size_t, Texture> textures;
  void release() {
    for (auto &[i, t] : textures)
      if (t.id) {
        glDeleteTextures(1, &t.id);
        t.id = 0;
      }
  }
};
DisplayAnnotationRenderer::DisplayAnnotationRenderer()
    : state_(std::make_unique<State>()) {}
DisplayAnnotationRenderer::~DisplayAnnotationRenderer() = default;
DisplayAnnotationRenderer::DisplayAnnotationRenderer(
    DisplayAnnotationRenderer &&) noexcept = default;
DisplayAnnotationRenderer &DisplayAnnotationRenderer::operator=(
    DisplayAnnotationRenderer &&) noexcept = default;
void DisplayAnnotationRenderer::release_textures() { state_->release(); }
void DisplayAnnotationRenderer::clear() {
  auto &s = *state_;
  s.release();
  s.textures.clear();
  s.rows.clear();
  s.extension_key.reset();
  s.owner = 0;
  s.revision = ~std::uint64_t(0);
}
Json DisplayAnnotationRenderer::draw(const DocumentSnapshot &source,
                                     const PlanarCanvas &canvas,
                                     ImDrawList *draw, bool gl, bool show,
                                     std::size_t cap, bool diagnostics) {
  auto &s = *state_;
  Json result = Json::array();
  if (!show)
    return result;
  const auto found = source.data().metadata.extensions.find(
      planar_display_annotations_extension);
  const std::optional<MetadataValue> key =
      found == source.data().metadata.extensions.end()
          ? std::optional<MetadataValue>{}
          : found->second;
  if (s.owner != source.id() || s.revision != source.revisions().metadata ||
      s.extension_key != key) {
    auto rows = planar_display_annotations(source.data());
    admit_display_annotation_visuals(rows, cap);
    // Retain immutable textures across unrelated metadata edits and exact undo.
    for (auto it = s.textures.begin(); it != s.textures.end();) {
      const auto i = it->first;
      if (s.owner != source.id() || i >= rows.size() || i >= s.rows.size() ||
          !(rows[i].payload == s.rows[i].payload)) {
        if (it->second.id) {
          if (!gl)
            throw std::logic_error("Display texture context was not released");
          glDeleteTextures(1, &it->second.id);
        }
        it = s.textures.erase(it);
      } else
        ++it;
    }
    s.rows = std::move(rows);
    s.owner = source.id();
    s.revision = source.revisions().metadata;
    s.extension_key = key;
  }
  admit_display_annotation_visuals(s.rows, cap);
  draw->PushClipRect({canvas.origin[0], canvas.origin[1]},
                     {canvas.origin[0] + canvas.width * canvas.scale[0],
                      canvas.origin[1] + canvas.height * canvas.scale[1]},
                     true);
  struct Clip {
    ImDrawList *d;
    ~Clip() { d->PopClipRect(); }
  } clip{draw};
  for (std::size_t i = 0; i < s.rows.size(); ++i) {
    const auto &row = s.rows[i];
    if (!planar_display_annotation_visible(row, canvas.plane, canvas.coordinate,
                                           source.display().t,
                                           source.display().c))
      continue;
    Json info = {{"row", i},
                 {"name", row.name},
                 {"plane", std::array<const char *, 3>{"XY", "XZ", "YZ"}.at(
                               std::size_t(row.plane))},
                 {"coordinate", row.coordinate},
                 {"clipped_to_canvas", true}};
    if(row.has_transform())info["transform"]=row.transform;
    const auto vertex_begin=draw->VtxBuffer.Size;
    const auto draw_begin = draw->IdxBuffer.Size;
    try {
      if (const auto p = std::get_if<TextAnnotationPayload>(&row.payload)) {
        info["kind"] = "text";
        info["layout"] = draw_text(row,*p, canvas, draw);
      } else {
        info["kind"] = "image";
        const auto &image_payload =
            std::get<ImageAnnotationPayload>(row.payload);
        auto &t = s.textures[i];
        if (t.rgba.empty()) {
          t.rgba.resize(std::size_t(image_payload.width) *
                        image_payload.height * 4);
          for (std::size_t j = 0; j < t.rgba.size() / 4; ++j) {
            for (int k = 0; k < 3; ++k)
              t.rgba[j * 4 + k] =
                  image_payload.pixels[j * image_payload.channels + k];
            const auto alpha = image_payload.channels == 4
                                   ? image_payload.pixels[j * 4 + 3]
                                   : 255;
            const bool zero = image_payload.zero_transparent &&
                              !t.rgba[j * 4] && !t.rgba[j * 4 + 1] &&
                              !t.rgba[j * 4 + 2];
            t.rgba[j * 4 + 3] =
                zero ? 0
                     : static_cast<unsigned char>(
                           (unsigned(alpha) * image_payload.opacity + 127) /
                           255);
          }
        }
        if (gl && !t.id) {
          GLint binding = 0;
          glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
          glGenTextures(1, &t.id);
          if (!t.id)
            throw std::runtime_error("Cannot allocate display image texture");
          glBindTexture(GL_TEXTURE_2D, t.id);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_payload.width,
                       image_payload.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                       t.rgba.data());
          glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(binding));
        }
        const auto &b = image_payload.bounds;
        if(row.has_transform())draw->AddImageQuad(static_cast<ImTextureID>(gl ? t.id : 1),
            mapped_screen(row,canvas,b[0],b[1]),mapped_screen(row,canvas,b[0]+b[2],b[1]),
            mapped_screen(row,canvas,b[0]+b[2],b[1]+b[3]),mapped_screen(row,canvas,b[0],b[1]+b[3]));
        else draw->AddImage(static_cast<ImTextureID>(gl ? t.id : 1),
                       screen(canvas, b[0], b[1]),screen(canvas, b[0] + b[2], b[1] + b[3]));
        if (diagnostics) {
          info["rgba_bytes"] = t.rgba.size();
          info["rgba_prefix"] = std::vector<unsigned char>(
              t.rgba.begin(),
              t.rgba.begin() + std::min<std::size_t>(64, t.rgba.size()));
          info["texture_uploaded"] = gl && bool(t.id);
        }
      }
      info["error"] = "";
    } catch (const std::exception &e) {
      info["error"] = e.what();
      const auto bounds =
          std::visit([](const auto &p) { return p.bounds; }, row.payload);
      draw->AddText(mapped_screen(row,canvas, bounds[0], bounds[1]),
                    IM_COL32(255, 100, 100, 255), e.what());
    }
    if (diagnostics) {
      info["canvas_origin"]=canvas.origin;info["canvas_scale"]=canvas.scale;
      info["screen_vertices"]=Json::array();
      for(int v=vertex_begin;v<std::min(vertex_begin+4,draw->VtxBuffer.Size);++v) {
        const auto &vertex=draw->VtxBuffer[v];
        info["screen_vertices"].push_back({{"position",{vertex.pos.x,vertex.pos.y}},
            {"uv",{vertex.uv.x,vertex.uv.y}},{"color",vertex.col}});
      }
      info["draw_index_begin"] = draw_begin;
      info["draw_index_end"] = draw->IdxBuffer.Size;
      result.push_back(std::move(info));
    }
  }
  return result;
}
} // namespace imagejxx::desktop
