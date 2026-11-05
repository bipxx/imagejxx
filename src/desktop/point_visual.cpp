#include "point_visual.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <initializer_list>
namespace imagejxx::desktop {
PointStyleEdit drawPointStyleControls(PointStyle& style,bool& enabled,std::string& error,
    unsigned counter,const std::function<void(const char*,bool)>& control) {
  PointStyleEdit result;
  if(!error.empty())ImGui::TextWrapped("%s. Clear marker style to recover.",error.c_str());
  const bool open=ImGui::CollapsingHeader("Point marker presentation");
  control("Point marker presentation",false);
  if(!open)return result;
  ImGui::BeginDisabled(!error.empty());
  bool styled=enabled;
  if(ImGui::Checkbox("Styled markers",&styled)) {
    enabled=styled;result.clear=!styled;result.changed=true;
  }
  control("Styled markers",!error.empty());
  ImGui::BeginDisabled(!styled);
  int marker=int(style.marker),size=int(style.size);bool styleChanged=false;
  auto choice=[&](const char* label,int& value,std::initializer_list<const char*> values) {
    bool edited=false;
    const bool open=ImGui::BeginCombo(label,values.begin()[value]);
    control(label,!styled || !error.empty());
    if(open) {
      for(std::size_t i=0;i<values.size();++i) {
        if(ImGui::Selectable(values.begin()[i],int(i)==value)){value=int(i);edited=true;}
        control((std::string(label)+"."+values.begin()[i]).c_str(),!styled || !error.empty());
      }
      ImGui::EndCombo();
    }
    return edited;
  };
  styleChanged|=choice("Marker",marker,{"Hybrid","Cross","Dot","Circle"});
  styleChanged|=choice("Marker size",size,{"Tiny","Small","Medium","Large","Extra Large","XXL","XXXL"});
  style.marker=PointMarker(marker);style.size=unsigned(size);
  styleChanged|=ImGui::Checkbox("Point labels",&style.labels);control("Point labels",!styled || !error.empty());
  styleChanged|=ImGui::Checkbox("Show points on all slices",&style.show_all);control("Show points on all slices",!styled || !error.empty());
  auto editColor=[&](const char* label,RoiColor& value) {
    float rgba[4];for(unsigned i=0;i<4;++i)rgba[i]=value[i]/255.f;
    const bool edited=ImGui::ColorEdit4(label,rgba);control(label,!styled || !error.empty());
    if(edited)for(unsigned i=0;i<4;++i)value[i]=static_cast<unsigned char>(std::clamp(std::floor(rgba[i]*255+.5f),0.f,255.f));
    return edited;
  };
  if(style.marker==PointMarker::Hybrid)styleChanged|=editColor("Hybrid cross color",style.cross_color);
  {
    const auto code=counter;
    bool override=style.counter_colors.count(code)!=0;
    if(ImGui::Checkbox("Override counter color",&override)) {
      if(override)style.counter_colors[code]=point_counter_color(code);else style.counter_colors.erase(code);
      styleChanged=true;
    }
    control("Override counter color",!styled || !error.empty());
    if(override)styleChanged|=editColor("Counter marker color",style.counter_colors.at(code));
  }
  if(ImGui::Button("Reset marker style")){style=PointStyle{};styleChanged=true;}
  control("Reset marker style",!styled || !error.empty());
  ImGui::EndDisabled();ImGui::EndDisabled();
  if(styleChanged)result.changed=true;
  if(ImGui::Button("Clear marker style")) {
    style=PointStyle{};enabled=false;
    error.clear();result.clear=true;result.changed=true;
  }
  control("Clear marker style",false);
  ImGui::TextDisabled("Marker size is a display setting; raster diameter remains separate.");
  return result;
}
PointStyle pointCreationStyle(const Settings& settings) {
  const auto& markers=Settings::definition("point_marker").choices;
  const auto& sizes=Settings::definition("point_marker_size").choices;
  PointStyle out;
  out.marker=PointMarker(std::distance(markers.begin(),std::find(markers.begin(),markers.end(),settings.text("point_marker"))));
  out.size=unsigned(std::distance(sizes.begin(),std::find(sizes.begin(),sizes.end(),settings.text("point_marker_size"))));
  out.labels=settings.boolean("point_labels");return out;
}
RoiColor pointCreationColor(const Settings& settings) {
  // Settings use #RRGGBBAA, unlike ImageJ ROI style text (#AARRGGBB).
  return color(settings.text("point_color"));
}
Selection pointCreationPresentation(Selection selection,const PointStyle& style,RoiColor color) {
  selection=with_point_style(selection,style);
  return with_roi_style(selection,{{"stroke_color",metadata_array({std::int64_t(color[0]),
      std::int64_t(color[1]),std::int64_t(color[2]),std::int64_t(color[3])})}});
}
std::shared_ptr<PointVisual> pointVisual(const Selection& selection,RoiColor color,std::size_t cap) {
  PointPresentationOptions options;options.color=color;options.round_centers=false;
  options.relative_primitives=true;options.show_all=true;
  PointPresentationLimits limits;limits.max_working_bytes=cap;
  auto out=std::make_shared<PointVisual>();out->cap=cap;
  try {
    out->layout=point_presentation(selection,options,limits);
    out->style=point_style(selection).value_or(PointStyle{});
  }catch(const std::invalid_argument& error) {
    // A future/malformed semantic tag stays intact. Display an explicit
    // unavailable marker notice while keeping Properties/clear reachable.
    out->error=error.what();
  }
  return out;
}
std::size_t pointVisualBytes(const RoiVisual& v) {
  return v.pointVisual?v.pointVisual->layout.working_bytes:0;
}
void drawPointVisual(const PointVisual& visual,ImDrawList* draw,std::array<float,2> origin,
    std::array<float,2> scale,std::optional<PlanePosition> current,Plane plane) {
  visual.visible_count=visual.emitted_primitives=0;
  const auto low=draw->GetClipRectMin(),high=draw->GetClipRectMax();
  if(!visual.error.empty()) {
    draw->AddText({low.x+4,low.y+4},IM_COL32(255,150,70,255),
        "Point markers unavailable: clear unsupported style/annotations in Properties.");return;
  }
  for(float v:{origin[0],origin[1],scale[0],scale[1],low.x,low.y,high.x,high.y})
    if(!std::isfinite(v))return;
  if(scale[0]<=0 || scale[1]<=0 || low.x>=high.x || low.y>=high.y)return;
  // The host rebuilds the font atlas at GUI/DPI scale; a current/base font-size
  // ratio would stay 1 and lose that scale. Glyph size uses the host's value.
  auto* font=ImGui::GetFont();const double dpi=visual.gui_scale;
  if(!std::isfinite(dpi) || dpi<=0 || dpi>16)throw std::invalid_argument("Unsupported point marker DPI scale");
  // Seven digits cover the bounded original vertex numbering. This conservative
  // clip margin includes text ascenders/descenders, cross and circle strokes.
  const double margin=200*dpi;
  auto center=[&](const PointGlyph& glyph,std::array<double,2>& xy) {
    if(!glyph.visible(current,plane,visual.style.show_all || visual.show_all_override))return false;
    for(unsigned d=0;d<2;++d)xy[d]=std::floor(std::fma(glyph.center[d],double(scale[d]),double(origin[d]))+.5);
    return std::isfinite(xy[0]) && std::isfinite(xy[1]) && xy[0]>=double(low.x)-margin &&
        xy[0]<=double(high.x)+margin && xy[1]>=double(low.y)-margin && xy[1]<=double(high.y)+margin;
  };
  // Admit this visual's retained template and conservative ImGui vertex/index
  // expansion before painting. The existing owner admits aggregate cache bytes.
  // Fixed 32-segment ellipses make expansion independent of zoom/user widths.
  std::size_t used=visual.layout.working_bytes;
  for(const auto& glyph:visual.layout.glyphs) {
    std::array<double,2> xy{};if(!center(glyph,xy))continue;
    for(std::size_t i=glyph.first_primitive;i<glyph.first_primitive+glyph.primitive_count;++i) {
      const auto& p=visual.layout.primitives[i];
      std::size_t vertices=16,indices=72;
      if(p.kind==PointPrimitiveKind::FillEllipse || p.kind==PointPrimitiveKind::StrokeEllipse){vertices=256;indices=1536;}
      if(p.kind==PointPrimitiveKind::Text){vertices=4*p.text.size();indices=6*p.text.size();}
      const auto bytes=vertices*sizeof(ImDrawVert)+indices*sizeof(ImDrawIdx);
      if(used>visual.cap || bytes>visual.cap-used)throw std::length_error("Point draw expansion exceeds processing memory cap");
      used+=bytes;
    }
  }
  auto tint=[](RoiColor c){return IM_COL32(c[0],c[1],c[2],c[3]);};
  for(const auto& glyph:visual.layout.glyphs) {
    std::array<double,2> xy{};if(!center(glyph,xy))continue;
    if(visual.visible_count<visual.visible_indices.size())visual.visible_indices[visual.visible_count]=glyph.index;
    ++visual.visible_count;
    for(std::size_t i=glyph.first_primitive;i<glyph.first_primitive+glyph.primitive_count;++i) {
      const auto& p=visual.layout.primitives[i];const auto& c=p.coordinates;
      const ImVec2 a{float(xy[0]+c[0]*dpi),float(xy[1]+c[1]*dpi)};
      const ImVec2 b{float(xy[0]+c[2]*dpi),float(xy[1]+c[3]*dpi)};
      const auto ink=tint(p.color);const float width=float(p.stroke_width*dpi);
      switch(p.kind) {
      case PointPrimitiveKind::Line:draw->AddLine(a,b,ink,width);break;
      case PointPrimitiveKind::FillRectangle:draw->AddRectFilled(a,b,ink);break;
      case PointPrimitiveKind::FillEllipse:
      case PointPrimitiveKind::StrokeEllipse: {
        ImVec2 mid{(a.x+b.x)/2,(a.y+b.y)/2},radius{(b.x-a.x)/2,(b.y-a.y)/2};
        if(p.kind==PointPrimitiveKind::FillEllipse)draw->AddEllipseFilled(mid,radius,ink,0,32);
        else draw->AddEllipse(mid,radius,ink,0,32,width);
        break;
      }
      case PointPrimitiveKind::Text: {
        const float size=float(p.font_size*dpi);
        draw->AddText(font,size,{a.x,a.y-font->Ascent*size/font->FontSize},ink,p.text.c_str());break;
      }
      }
      ++visual.emitted_primitives;
    }
  }
}
Json pointVisualJson(const PointVisual& v) {
  Json indices=Json::array();
  for(std::size_t i=0;i<std::min(v.visible_count,v.visible_indices.size());++i)indices.push_back(v.visible_indices[i]);
  return {{"marker",unsigned(v.style.marker)},{"size",v.style.size},{"labels",v.style.labels},
      {"show_all",v.style.show_all},{"show_all_override",v.show_all_override},{"error",v.error},
      {"marker_scale",v.gui_scale},
      {"glyphs",v.layout.glyphs.size()},{"primitives",v.layout.primitives.size()},
      {"visible_glyphs",v.visible_count},{"visible_indices",indices},{"emitted_primitives",v.emitted_primitives},
      {"working_bytes",v.layout.working_bytes},{"template_builds",1}};
}
}
