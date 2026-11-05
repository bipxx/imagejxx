#include "point_presentation_internal.hpp"
#include <imagejxx/point_annotations.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace imagejxx {
namespace {
[[noreturn]] void invalid(const char* s) { throw std::invalid_argument(s); }
template<class T> const T& container(const MetadataValue& value) {
  const auto p=std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  if(!p || !*p || !std::holds_alternative<T>((*p)->value))invalid("Invalid point style container");
  return std::get<T>((*p)->value);
}
std::uint64_t integer(const MetadataValue& v) {
  if(auto p=std::get_if<std::uint64_t>(&v))return *p;
  if(auto p=std::get_if<std::int64_t>(&v))if(*p>=0)return std::uint64_t(*p);
  invalid("Point style requires nonnegative integers");
}
bool boolean(const MetadataValue& v) {
  if(auto p=std::get_if<bool>(&v))return *p;
  invalid("Point style requires Boolean flags");
}
RoiColor color(const MetadataValue& v) {
  const auto& a=container<MetadataArray>(v);
  if(a.size()!=4)invalid("Point style requires four RGBA bytes");
  RoiColor out{};
  for(std::size_t i=0;i<4;++i) {
    auto n=integer(a[i]);if(n>255)invalid("Point style RGBA byte exceeds 255");out[i]=static_cast<unsigned char>(n);
  }
  return out;
}
MetadataValue color_value(RoiColor c) {
  return metadata_array({std::int64_t(c[0]),std::int64_t(c[1]),std::int64_t(c[2]),std::int64_t(c[3])});
}
void point_selection(const Selection& s) {
  if((s.kind!="point" && s.kind!="multipoint") || s.points.empty())
    invalid("Point presentation requires a nonempty point selection");
}
void validate(const PointStyle& s) {
  if(unsigned(s.marker)>3 || s.size>6)invalid("Invalid point marker or size index");
  if(s.counter_colors.size()>100 || (!s.counter_colors.empty() && s.counter_colors.rbegin()->first>99))
    invalid("Point palette counters must be in 0..99");
}
constexpr int sizes[]{1,3,5,7,11,17,25};
constexpr int fonts[]{8,9,11,12,14,18,20};
constexpr int offsets[]{2,2,2,3,4,5,7};
// Each glyph has at most two cross lines, a center, a label and an outline.
struct Commands {
  std::array<PointPrimitive,5> values;
  std::size_t count=0;
  void add(PointPrimitiveKind kind,std::array<double,4> xy,RoiColor c,double width=1,
           std::string text={},double font=0) {
    values[count++]={kind,xy,c,width,font,std::move(text)};
  }
};
Commands glyph(const PointStyle& style,const PointPresentationOptions& view,
    std::array<double,2> center,std::size_t index,unsigned counter,bool categories,bool labels) {
  Commands out;const int size=sizes[style.size],half=size/2;
  const double m=view.marker_scale,x=center[0],y=center[1];
  const auto override=style.counter_colors.find(counter);
  const auto ink=categories?(override==style.counter_colors.end()?point_counter_color(counter):override->second):view.color;
  const auto marker=style.marker;
  auto shape=[&](PointPrimitiveKind kind,int delta,int diameter,RoiColor c,double width=1) {
    out.add(kind,{x-delta*m,y-delta*m,x+(diameter-delta)*m,y+(diameter-delta)*m},c,width*m);
  };
  if(marker==PointMarker::Hybrid || marker==PointMarker::Cross) {
    const auto cross=marker==PointMarker::Hybrid?style.cross_color:ink;
    const double r=(size+2)*m,w=(size>17?5:size>7?3:1)*m;
    out.add(PointPrimitiveKind::Line,{x-r,y,x+r,y},cross,w);
    out.add(PointPrimitiveKind::Line,{x,y-r,x,y+r},cross,w);
  }
  if(marker==PointMarker::Hybrid || marker==PointMarker::Dot) {
    if(size>7 && marker==PointMarker::Dot)shape(PointPrimitiveKind::FillEllipse,half,size,ink);
    else if(size>7 && marker==PointMarker::Hybrid)shape(PointPrimitiveKind::FillRectangle,half-2,size-4,ink);
    else if(size>3 && marker==PointMarker::Hybrid)shape(PointPrimitiveKind::FillRectangle,half-1,size-2,ink);
    else shape(PointPrimitiveKind::FillRectangle,half,size,ink);
  }
  if(labels) {
    const int dx=offsets[style.size],dy=dx-(size>=7?1:0);
    out.add(PointPrimitiveKind::Text,{x+dx*m,y+(dy+fonts[style.size])*m,0,0},ink,m,
        std::to_string(categories?counter:index+1),fonts[style.size]*m);
  }
  if((size>1 || marker==PointMarker::Dot) && (marker==PointMarker::Hybrid || marker==PointMarker::Dot)) {
    const auto black=RoiColor{0,0,0,255};
    if(size>7 && marker==PointMarker::Hybrid)shape(PointPrimitiveKind::StrokeEllipse,half-1,size-3,black);
    else if(size>3 && marker==PointMarker::Hybrid)shape(PointPrimitiveKind::StrokeEllipse,half,size-1,black);
    else shape(PointPrimitiveKind::StrokeEllipse,half+1,size+1,black);
  }
  if(marker==PointMarker::Circle)shape(PointPrimitiveKind::StrokeEllipse,(size+1)/2,size+1,ink,size>7?2:1);
  return out;
}
bool visible(const Commands& g,const PointPresentationOptions& view,std::array<double,2> center) {
  bool hit=!view.clip;
  for(std::size_t i=0;i<g.count;++i) {
    const auto& p=g.values[i];auto xy=p.coordinates;
    if(view.relative_primitives) {
      xy[0]+=center[0];xy[1]+=center[1];
      if(p.kind!=PointPrimitiveKind::Text){xy[2]+=center[0];xy[3]+=center[1];}
    }
    for(double v:xy)if(!std::isfinite(v))invalid("Point device coordinates exceed finite range");
    if(!std::isfinite(p.stroke_width) || !std::isfinite(p.font_size))invalid("Point device scale exceeds finite range");
    if(!view.clip)continue;
    auto x0=std::min(xy[0],xy[2]),x1=std::max(xy[0],xy[2]);
    auto y0=std::min(xy[1],xy[3]),y1=std::max(xy[1],xy[3]);
    if(p.kind==PointPrimitiveKind::Text) {
      // Conservative logical bounds only; actual font rasterization is host-owned.
      x0=xy[0]-p.font_size;x1=xy[0]+p.font_size*(p.text.size()+1);
      y0=xy[1]-p.font_size*2;y1=xy[1]+p.font_size;
    }
    const double pad=p.stroke_width/2;const auto& c=*view.clip;
    hit=hit || (x1+pad>=c[0] && x0-pad<=c[2] && y1+pad>=c[1] && y0-pad<=c[3]);
  }
  return hit;
}
void charge(std::size_t& used,std::size_t n,std::size_t width,std::size_t maximum,
    const std::function<void(std::size_t)>& admit) {
  if(used>maximum || (n && width>(maximum-used)/n))throw std::length_error("Point presentation working byte limit");
  const auto next=used+n*width;if(admit)admit(next);used=next;
}
}
std::optional<PointStyle> point_style(const Selection& selection) {
  const auto found=selection.attributes.find(point_style_attribute);
  if(found==selection.attributes.end())return {};
  point_selection(selection);const auto& o=container<MetadataObject>(found->second);
  if(o.size()!=7 || !o.count("version") || !o.count("marker") || !o.count("size") ||
     !o.count("labels") || !o.count("show_all") || !o.count("cross_color") || !o.count("counter_colors") || integer(o.at("version"))!=1)
    invalid("Unsupported point style record");
  const auto type=integer(o.at("marker")),size=integer(o.at("size"));
  if(type>3 || size>6)invalid("Invalid point marker or size index");
  PointStyle out;out.marker=PointMarker(type);out.size=unsigned(size);
  out.labels=boolean(o.at("labels"));out.show_all=boolean(o.at("show_all"));out.cross_color=color(o.at("cross_color"));
  const auto& palette=container<MetadataObject>(o.at("counter_colors"));
  if(palette.size()>100)invalid("Point palette limit");
  for(const auto& entry:palette) {
    const auto& key=entry.first;
    if(key.empty() || key.size()>2 || (key.size()>1 && key[0]=='0'))invalid("Invalid point palette counter key");
    unsigned code=0;
    for(char c:key){if(c<'0' || c>'9')invalid("Invalid point palette counter key");code=code*10+unsigned(c-'0');}
    out.counter_colors.emplace(code,color(entry.second));
  }
  return out;
}
Selection with_point_style(const Selection& selection,const PointStyle& value) {
  point_selection(selection);validate(value);
  (void)point_style(selection); // Unknown versions require explicit clearing.
  MetadataObject palette;
  for(const auto& c:value.counter_colors)palette.emplace(std::to_string(c.first),color_value(c.second));
  auto out=selection;
  out.attributes[point_style_attribute]=metadata_object({{"version",std::int64_t(1)},
      {"marker",std::int64_t(value.marker)},{"size",std::int64_t(value.size)},
      {"labels",value.labels},{"show_all",value.show_all},{"cross_color",color_value(value.cross_color)},
      {"counter_colors",metadata_object(std::move(palette))}});
  return out;
}
Selection without_point_style(const Selection& selection) {
  auto out=selection;out.attributes.erase(point_style_attribute);return out;
}
RoiColor point_counter_color(unsigned counter) {
  if(counter>99)invalid("Point palette counter must be in 0..99");
  static constexpr RoiColor fixed[]{
    {255,255,0,255},{255,0,255,255},{0,255,255,255},{255,200,0,255},{0,255,0,255},
    {0,0,255,255},{255,255,255,255},{64,64,64,255},{255,175,175,255},{192,192,192,255}};
  if(counter<10)return fixed[counter];
  const unsigned h=(counter*137)%360,sector=h/60,f=h%60;
  const unsigned v=240,lo=40,up=lo+200*f/60,down=v-200*f/60;
  const std::array<unsigned,3> rgb=sector==0?std::array<unsigned,3>{v,up,lo}:
    sector==1?std::array<unsigned,3>{down,v,lo}:sector==2?std::array<unsigned,3>{lo,v,up}:
    sector==3?std::array<unsigned,3>{lo,down,v}:sector==4?std::array<unsigned,3>{up,lo,v}:std::array<unsigned,3>{v,lo,down};
  return {static_cast<unsigned char>(rgb[0]),static_cast<unsigned char>(rgb[1]),static_cast<unsigned char>(rgb[2]),255};
}
PointStyle imagej_point_style(const ImageJRoi& wire) {
  point_selection(wire.roi.selection);PointStyle out;
  out.marker=PointMarker(wire.point_type);out.size=wire.point_size;out.labels=(wire.options&1024)!=0;
  validate(out);return out;
}
bool PointGlyph::visible(std::optional<PlanePosition> current,Plane plane,bool show_all) const {
  if(unsigned(plane)>2 || (current && (current->t<0 || current->c<0 || current->z<0)))
    invalid("Invalid point visibility view");
  return show_all || !position || !current || (position->t==current->t && position->c==current->c &&
      (plane!=Plane::XY || position->z==current->z));
}
ImageJRoi with_imagej_point_style(ImageJRoi wire,const PointStyle& style) {
  point_selection(wire.roi.selection);validate(style);
  wire.point_type=static_cast<unsigned char>(style.marker);wire.point_size=static_cast<unsigned char>(style.size);
  wire.options=std::uint16_t((wire.options&~1024u)|(style.labels?1024u:0u));return wire;
}
PointPresentation detail::point_presentation_admitted(const Selection& selection,const PointPresentationOptions& view,
    const PointPresentationLimits& limits,const std::function<void(std::size_t)>& admit) {
  point_selection(selection);
  if(!limits.max_points || limits.max_points>1000000 || selection.points.size()>limits.max_points)
    throw std::length_error("Point presentation vertex limit");
  if(!std::isfinite(selection.width) || selection.width<=0)invalid("Invalid point raster width");
  for(unsigned i=0;i<2;++i)if(!std::isfinite(view.origin[i]) || !std::isfinite(view.scale[i]) || view.scale[i]<=0)
    invalid("Point presentation requires finite positive scales");
  if(!std::isfinite(view.marker_scale) || view.marker_scale<=0 || unsigned(view.plane)>2)invalid("Invalid point presentation view");
  if(view.position && (view.position->t<0 || view.position->c<0 || view.position->z<0))invalid("Invalid point presentation position");
  if(view.clip) {
    for(double v:*view.clip)if(!std::isfinite(v))invalid("Invalid point presentation clip");
    if((*view.clip)[0]>(*view.clip)[2] || (*view.clip)[1]>(*view.clip)[3])invalid("Invalid point presentation clip");
  }
  std::size_t used=0;
  // Bounded palette/style parsing plus the small stack command buffer.
  charge(used,1,16384,limits.max_working_bytes,admit);
  const auto style=point_style(selection).value_or(PointStyle{});
  const bool tagged=selection.attributes.count(point_annotations_attribute)!=0;
  if(tagged)charge(used,selection.points.size(),sizeof(PointAnnotation),limits.max_working_bytes,admit);
  const auto entries=point_annotations(selection,limits.max_points);
  const bool categories=std::any_of(entries.begin(),entries.end(),[](const PointAnnotation& e){return e.counter!=0;});
  const bool labels=style.labels && view.labels && selection.points.size()>1;
  auto commands=[&](std::size_t i,std::array<double,2>& center)->Commands {
    const auto p=selection.points[i];
    if(!std::isfinite(p[0]) || !std::isfinite(p[1]))invalid("Invalid point coordinates");
    PointGlyph candidate;candidate.position=entries.empty()?std::nullopt:entries[i].position;
    if(!candidate.visible(view.position,view.plane,style.show_all || view.show_all))return {};
    for(unsigned d=0;d<2;++d) {
      center[d]=std::fma(p[d],view.scale[d],view.origin[d]);
      if(!std::isfinite(center[d]))invalid("Point device coordinates exceed finite range");
      if(view.round_centers)center[d]=std::floor(center[d]+.5);
    }
    return glyph(style,view,view.relative_primitives?std::array<double,2>{0,0}:center,
        i,entries.empty()?0:entries[i].counter,categories,labels);
  };
  std::size_t glyph_count=0,primitive_count=0,label_chars=0;
  for(std::size_t i=0;i<selection.points.size();++i) {
    std::array<double,2> center{};const auto g=commands(i,center);
    if(!g.count || !visible(g,view,center))continue;
    if(g.count>limits.max_primitives-std::min(primitive_count,limits.max_primitives))throw std::length_error("Point primitive limit");
    primitive_count+=g.count;++glyph_count;
    for(std::size_t j=0;j<g.count;++j) {
      const auto n=g.values[j].text.size();
      if(n>limits.max_label_chars-std::min(label_chars,limits.max_label_chars))throw std::length_error("Point label limit");
      label_chars+=n;
    }
  }
  charge(used,glyph_count,sizeof(PointGlyph),limits.max_working_bytes,admit);
  charge(used,primitive_count,sizeof(PointPrimitive),limits.max_working_bytes,admit);
  charge(used,label_chars,1,limits.max_working_bytes,admit);
  PointPresentation out;out.working_bytes=used;out.glyphs.reserve(glyph_count);out.primitives.reserve(primitive_count);
  for(std::size_t i=0;i<selection.points.size();++i) {
    std::array<double,2> center{};auto g=commands(i,center);
    if(!g.count || !visible(g,view,center))continue;
    out.glyphs.push_back({i,out.primitives.size(),g.count,entries.empty()?0:entries[i].counter,center,
        entries.empty()?std::nullopt:entries[i].position});
    for(std::size_t j=0;j<g.count;++j)out.primitives.push_back(std::move(g.values[j]));
  }
  return out;
}
PointPresentation point_presentation(const Selection& selection,const PointPresentationOptions& view,const PointPresentationLimits& limits) {
  return detail::point_presentation_admitted(selection,view,limits,{});
}

}
