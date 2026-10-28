#include "calibration_plot_drawing.hpp"
#include <imgui_internal.h>
#include <stb_image_write.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace imagejxx::desktop {
std::size_t calibrationPlotImageBytes(const CalibrationPlotPayload& payload,int width,int height) {
  if(width<256 || width>4096 || height<192 || height>4096 || std::uint64_t(width)*height>4*1024*1024)
    throw std::invalid_argument("Plot image dimensions require width256-4096, height192-4096 and at most4 megapixels");
  auto bytes=RenderMemory::add(RenderMemory::mul(std::size_t(width)*height,32),16*1024*1024);
  bytes=RenderMemory::add(bytes,RenderMemory::mul(payload.curveSize(),512));
  bytes=RenderMemory::add(bytes,RenderMemory::mul(payload.calibration()?payload.data.measured.size():0,4096));
  return RenderMemory::add(bytes,RenderMemory::mul(payload.annotation.canvas_text.size()+16384,256));
}
namespace {
double edge(const ImVec2& a,const ImVec2& b,double x,double y) {
  return (double(b.x)-a.x)*(y-a.y)-(double(b.y)-a.y)*(x-a.x);
}
bool topLeft(const ImVec2& a,const ImVec2& b) {
  return b.y<a.y || (b.y==a.y && b.x>a.x);
}
std::string boundedCaption(const std::string& text) {
  auto end=std::min<std::size_t>(4096,text.size());
  while(end<text.size() && (static_cast<unsigned char>(text[end])&0xc0)==0x80)--end;
  return text.substr(0,end);
}
void rasterize(const ImDrawList& list,const unsigned char* atlas,int aw,int ah,
               int width,int height,std::vector<unsigned char>& rgb) {
  for(const auto& command:list.CmdBuffer) {
    if(command.UserCallback || (command.ElemCount && command.GetTexID()!=1))
      throw std::runtime_error("Unsupported plot draw command");
    const auto clip=command.ClipRect;
    const auto left=std::clamp(double(clip.x),0.,double(width)),right=std::clamp(double(clip.z),0.,double(width));
    const auto top=std::clamp(double(clip.y),0.,double(height)),bottom=std::clamp(double(clip.w),0.,double(height));
    if(right<=left || bottom<=top)continue;
    for(unsigned int i=0;i+2<command.ElemCount;i+=3) {
      const ImDrawVert* v[3];
      for(unsigned int k=0;k<3;++k)v[k]=&list.VtxBuffer[int(command.VtxOffset+list.IdxBuffer[int(command.IdxOffset+i+k)])];
      auto area=edge(v[0]->pos,v[1]->pos,v[2]->pos.x,v[2]->pos.y);
      if(!std::isfinite(area) || area==0)continue;
      if(area<0){std::swap(v[1],v[2]);area=-area;}
      const auto minX=std::max(left,double(std::min({v[0]->pos.x,v[1]->pos.x,v[2]->pos.x}))),maxX=std::min(right,double(std::max({v[0]->pos.x,v[1]->pos.x,v[2]->pos.x})));
      const auto minY=std::max(top,double(std::min({v[0]->pos.y,v[1]->pos.y,v[2]->pos.y}))),maxY=std::min(bottom,double(std::max({v[0]->pos.y,v[1]->pos.y,v[2]->pos.y})));
      if(maxX<=minX || maxY<=minY)continue;
      std::array<std::array<double,4>,3> color{};
      for(int k=0;k<3;++k)color[k]={double((v[k]->col>>IM_COL32_R_SHIFT)&255),double((v[k]->col>>IM_COL32_G_SHIFT)&255),double((v[k]->col>>IM_COL32_B_SHIFT)&255),double((v[k]->col>>IM_COL32_A_SHIFT)&255)};
      for(int y=int(std::floor(minY));y<int(std::ceil(maxY));++y)for(int x=int(std::floor(minX));x<int(std::ceil(maxX));++x) {
        const double px=x+.5,py=y+.5;
        if(px<left || px>=right || py<top || py>=bottom)continue;
        const double e0=edge(v[1]->pos,v[2]->pos,px,py),e1=edge(v[2]->pos,v[0]->pos,px,py),e2=edge(v[0]->pos,v[1]->pos,px,py);
        if(e0<0 || (e0==0 && !topLeft(v[1]->pos,v[2]->pos)) || e1<0 || (e1==0 && !topLeft(v[2]->pos,v[0]->pos)) || e2<0 || (e2==0 && !topLeft(v[0]->pos,v[1]->pos)))continue;
        const double w0=e0/area,w1=e1/area,w2=e2/area;
        const auto u=(w0*v[0]->uv.x+w1*v[1]->uv.x+w2*v[2]->uv.x)*aw-.5;
        const auto vv=(w0*v[0]->uv.y+w1*v[1]->uv.y+w2*v[2]->uv.y)*ah-.5;
        const auto ix=int(std::floor(u)),iy=int(std::floor(vv));const auto fx=u-ix,fy=vv-iy;
        const auto sample=[&](int sx,int sy){return double(atlas[std::size_t(std::clamp(sy,0,ah-1))*aw+std::clamp(sx,0,aw-1)]);};
        const auto coverage=((1-fx)*sample(ix,iy)+fx*sample(ix+1,iy))*(1-fy)+((1-fx)*sample(ix,iy+1)+fx*sample(ix+1,iy+1))*fy;
        const auto alpha=(w0*color[0][3]+w1*color[1][3]+w2*color[2][3])*coverage/(255*255);
        for(int c=0;c<3;++c) {
          auto& target=rgb[(std::size_t(y)*width+x)*3+c];
          const auto source=w0*color[0][c]+w1*color[1][c]+w2*color[2][c];
          target=static_cast<unsigned char>(std::clamp(std::lround(source*alpha+target*(1-alpha)),0L,255L));
        }
      }
    }
  }
}
}
Json exportCalibrationPlotPng(const std::string& path,const CalibrationPlotPayload& payload,
    const CalibrationPlotView& view,const CalibrationPlotStyle& style,int width,int height,RenderMemory& memory) {
  if(path.empty() || extension(path)!=".png")throw std::invalid_argument("Plot image export requires a PNG path");
  const auto bytes=calibrationPlotImageBytes(payload,width,height);
  auto reservation=memory.acquire(bytes);
  // Dedicated built-in atlas: no GL/context/system-font dependence or mutation.
  ImFontAtlas atlas;auto* font=atlas.AddFontDefault();atlas.SetTexID(1);
  unsigned char* alpha=nullptr;int aw=0,ah=0;atlas.GetTexDataAsAlpha8(&alpha,&aw,&ah);
  if(!alpha || aw<=0 || ah<=0 || std::uint64_t(aw)*ah>8*1024*1024)throw std::runtime_error("Plot export font atlas exceeds reservation");
  ImDrawListSharedData shared;shared.Font=font;shared.FontSize=13;shared.FontScale=1;
  shared.TexUvWhitePixel=atlas.TexUvWhitePixel;shared.CurveTessellationTol=1.25f;shared.SetCircleTessellationMaxError(.3f);
  shared.InitialFlags=ImDrawListFlags_AntiAliasedLines|ImDrawListFlags_AntiAliasedFill|ImDrawListFlags_AllowVtxOffset;
  shared.ClipRectFullscreen={0,0,float(width),float(height)};
  ImDrawList list(&shared);list._ResetForNewFrame();list.PushTextureID(1);list.PushClipRect({0,0},{float(width),float(height)});
  const ImU32 foreground=IM_COL32(25,25,25,255);
  const auto heading=boundedCaption(payload.caption());
  const auto source=std::string("Source: ")+boundedCaption(payload.source_title.empty()?"(untitled source)":payload.source_title);
  list.PushClipRect({12,8},{float(width-12),65},true);
  list.AddText(font,13,{12,12},foreground,heading.c_str());list.AddText(font,13,{12,30},foreground,source.c_str());
  if(!payload.context_caption.empty()){const auto context=boundedCaption(payload.context_caption);list.AddText(font,13,{12,48},foreground,context.c_str());}
  list.PopClipRect();
  const CalibrationPlotCanvas canvas{{80,70},{float(width-110),float(height-125)},{12,60},{float(width-24),float(height-90)},font,13,1,
    IM_COL32(80,80,80,255),IM_COL32(215,215,215,255),foreground};
  auto geometry=drawCalibrationPlotGeometry(&list,payload.data,&payload.annotation,view,style,canvas,true,&payload);
  auto axis=std::string("X: ")+boundedCaption(payload.x_axis)+" | Y: "+boundedCaption(payload.yLabel());
  list.AddText(font,13,{12,float(height-20)},foreground,axis.c_str());
  if(view.log_x || view.log_y)list.AddText(font,13,{12,float(height-38)},foreground,"Log axes: nonpositive values are omitted.");
  list.PopClipRect();list.PopTextureID();
  // Bound generated geometry as well as the estimate before allocating output.
  const auto actual=RenderMemory::add(RenderMemory::mul(std::size_t(list.VtxBuffer.Capacity),sizeof(ImDrawVert)),RenderMemory::mul(std::size_t(list.IdxBuffer.Capacity),sizeof(ImDrawIdx)));
  if(actual>bytes-RenderMemory::mul(std::size_t(width)*height,32)-8*1024*1024)throw std::invalid_argument("Plot export geometry exceeded reservation");
  std::vector<unsigned char> rgb(std::size_t(width)*height*3,255);rasterize(list,alpha,aw,ah,width,height,rgb);
  struct Encoded {std::vector<unsigned char> bytes;std::size_t limit;bool failed=false;};
  Encoded encoded{{},std::size_t(width)*height*6+65536};encoded.bytes.reserve(encoded.limit);
  const auto append=[](void* context,void* data,int size) {
    auto& out=*static_cast<Encoded*>(context);
    if(size<0 || std::size_t(size)>out.limit-out.bytes.size()){out.failed=true;return;}
    try{const auto* first=static_cast<const unsigned char*>(data);out.bytes.insert(out.bytes.end(),first,first+size);}catch(...){out.failed=true;}
  };
  if(!stbi_write_png_to_func(append,&encoded,width,height,3,rgb.data(),width*3) || encoded.failed)throw std::runtime_error("Plot PNG encoding failed");
  Json result={{"path",path},{"width",width},{"height",height},{"source_id",payload.source_id},{"payload_id",payload.payload_id},{"context_caption",payload.context_caption},
    {"canvas_origin",{canvas.origin.x,canvas.origin.y}},{"canvas_size",{canvas.size.x,canvas.size.y}},
    {"view_limits",view.limits(payload.data)},{"nice_ticks",view.nice_ticks},{"minor_ticks",view.minor_ticks},
    {"x_ticks",std::move(geometry.x_ticks)},{"y_ticks",std::move(geometry.y_ticks)},{"x_minor_ticks",std::move(geometry.x_minor_ticks)},{"y_minor_ticks",std::move(geometry.y_minor_ticks)},
    {"model_overlay",std::move(geometry.model_overlay)},
    {"kind",payload.kindName()},{"drawn_bars",geometry.drawn_bars},{"drawn_segments",geometry.drawn_segments},{"masked_segments",geometry.masked_segments},{"working_bytes",bytes},
    {"font","Dear ImGui built-in 13px; unsupported glyphs use fallback"},{"background","white"},{"encoded_bytes",encoded.bytes.size()}};
  writeBytes(path,encoded.bytes);return result;
}
} // namespace imagejxx::desktop
