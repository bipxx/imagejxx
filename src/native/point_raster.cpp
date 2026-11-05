#include "point_raster_internal.hpp"
#include "kernel_control.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace imagejxx {
namespace {
void invalid(const char* message) { throw std::invalid_argument(message); }
void charge(std::size_t& used,std::size_t count,std::size_t width,std::size_t cap) {
  if(used>cap || (count && width>(cap-used)/count))
    throw std::length_error("Point raster resource limit");
  used+=count*width;
}
struct Box { int left=0,top=0,right=0,bottom=0; };
struct Draw {
  const PointPrimitive& primitive;
  std::array<double,4> xy;
  const PointRasterGlyph* glyph=nullptr;
};
// Visit decimal glyph bitmaps and geometric primitives in original draw order.
// Bounds/work are computed with this same iterator before copying any pixels.
template<class F> void visit(const detail::PreparedPointRaster& prepared,
                            const PointRasterFont* font,F function) {
  for(const auto& glyph:prepared.layout.glyphs) {
    kernel_checkpoint_periodic();
    for(std::size_t k=0;k<glyph.primitive_count;++k) {
      const auto& p=prepared.layout.primitives[glyph.first_primitive+k];
      auto xy=p.coordinates;
      if(prepared.view.relative_primitives) {
        xy[0]+=glyph.center[0];xy[1]+=glyph.center[1];
        if(p.kind!=PointPrimitiveKind::Text) {xy[2]+=glyph.center[0];xy[3]+=glyph.center[1];}
      }
      if(p.kind==PointPrimitiveKind::Text) {
        if(!font || font->font_size!=p.font_size)
          invalid("Point labels require coverage at the exact device font size");
        double cursor=xy[0];
        for(char code:p.text) {
          if(code<'0' || code>'9')invalid("Point raster labels must be decimal");
          const auto& digit=font->digits[unsigned(code-'0')];
          const double x=std::floor(cursor)+digit.offset_x;
          const double y=std::floor(xy[1])+digit.offset_y;
          if(!std::isfinite(x) || !std::isfinite(y))invalid("Point label coordinates overflow");
          function(Draw{p,{x,y,x+digit.width,y+digit.height},&digit});
          cursor+=digit.advance;
          if(!std::isfinite(cursor))invalid("Point label advance overflow");
        }
      } else function(Draw{p,xy,nullptr});
    }
  }
}
Box bounds(const Draw& draw,const detail::PreparedPointRaster& prepared) {
  const auto& p=draw.primitive;const auto& xy=draw.xy;
  double x0=std::min(xy[0],xy[2]),x1=std::max(xy[0],xy[2]);
  double y0=std::min(xy[1],xy[3]),y1=std::max(xy[1],xy[3]);
  if(!draw.glyph && (p.kind==PointPrimitiveKind::Line || p.kind==PointPrimitiveKind::StrokeEllipse)) {
    x0-=p.stroke_width/2;x1+=p.stroke_width/2;y0-=p.stroke_width/2;y1+=p.stroke_width/2;
  }
  for(double n:{x0,x1,y0,y1})if(!std::isfinite(n))invalid("Point raster bounds overflow");
  if(prepared.view.clip) {
    const auto& clip=*prepared.view.clip;
    x0=std::max(x0,clip[0]);y0=std::max(y0,clip[1]);
    x1=std::min(x1,clip[2]);y1=std::min(y1,clip[3]);
  }
  if(x0>=x1 || y0>=y1)return {};
  // Clamp in floating point BEFORE narrowing, including huge offscreen inputs.
  return {int(std::floor(std::clamp(x0,0.,double(prepared.width)))),
      int(std::floor(std::clamp(y0,0.,double(prepared.height)))),
      int(std::ceil(std::clamp(x1,0.,double(prepared.width)))),
      int(std::ceil(std::clamp(y1,0.,double(prepared.height))))};
}
bool in_clip(double x,double y,const PointPresentationOptions& view) {
  return !view.clip || (x>=(*view.clip)[0] && y>=(*view.clip)[1] &&
      x<(*view.clip)[2] && y<(*view.clip)[3]);
}
bool inside(const Draw& draw,double x,double y) {
  const auto& p=draw.primitive;const auto& q=draw.xy;
  if(p.kind==PointPrimitiveKind::Line) {
    // PointPresentation emits only horizontal/vertical square-cap crosses.
    const double half=p.stroke_width/2;
    return x>=std::min(q[0],q[2])-half && x<std::max(q[0],q[2])+half &&
        y>=std::min(q[1],q[3])-half && y<std::max(q[1],q[3])+half;
  }
  if(p.kind==PointPrimitiveKind::FillRectangle)
    return x>=q[0] && x<q[2] && y>=q[1] && y<q[3];
  const double cx=q[0]/2+q[2]/2,cy=q[1]/2+q[3]/2;
  const double rx=(q[2]-q[0])/2,ry=(q[3]-q[1])/2;
  const auto ellipse=[&](double a,double b) {
    if(a<=0 || b<=0)return false;
    const double u=(x-cx)/a,v=(y-cy)/b;
    return u*u+v*v<=1;
  };
  if(p.kind==PointPrimitiveKind::FillEllipse)return ellipse(rx,ry);
  const double half=p.stroke_width/2;
  return ellipse(rx+half,ry+half) && !ellipse(rx-half,ry-half);
}
double coverage(const Draw& draw,int x,int y,const PointPresentationOptions& view) {
  if(draw.glyph) {
    const auto& q=draw.xy;const auto& g=*draw.glyph;
    const int gx=int(double(x)-q[0]),gy=int(double(y)-q[1]);
    if(gx<0 || gy<0 || gx>=g.width || gy>=g.height)return 0;
    double fraction=1;
    if(view.clip) {
      const auto& c=*view.clip;
      fraction=std::max(0.,std::min(double(x)+1,c[2])-std::max(double(x),c[0]))*
          std::max(0.,std::min(double(y)+1,c[3])-std::max(double(y),c[1]));
    }
    return g.coverage[std::size_t(gy)*g.width+gx]/255.*fraction;
  }
  unsigned hits=0;
  for(unsigned sy=0;sy<4;++sy)for(unsigned sx=0;sx<4;++sx) {
    const double px=x+(sx+.5)/4,py=y+(sy+.5)/4;
    hits+=in_clip(px,py,view) && inside(draw,px,py);
  }
  return hits/16.;
}
}
namespace detail {
PreparedPointRaster prepare_point_raster(int width,int height,const Selection& selection,
    const PointPresentationOptions& options,const PointRasterFont* font,const PointRasterLimits& limits,
    PointPresentation* captured_layout) {
  kernel_checkpoint();
  if(width<=0 || height<=0 || width>std::numeric_limits<int>::max()-8194 ||
      height>std::numeric_limits<int>::max()-8194)invalid("Invalid point raster dimensions");
  std::size_t pixels=0;charge(pixels,std::size_t(width),std::size_t(height),std::numeric_limits<std::size_t>::max()/3);
  const std::size_t components=pixels*3;
  std::size_t work=0;
  charge(work,components,2,limits.max_pixel_samples);
  charge(work,selection.points.size(),2,limits.max_pixel_samples);
  std::size_t bytes=0;charge(bytes,components,2*sizeof(double),limits.max_working_bytes);
  charge(bytes,1,sizeof(PreparedPointRaster),limits.max_working_bytes);
  if(font) {
    if(!std::isfinite(font->font_size) || font->font_size<=0 || font->font_size>4096)
      invalid("Invalid point raster font size");
    charge(bytes,1,sizeof(PointRasterFont),limits.max_working_bytes);
    for(const auto& g:font->digits) {
      if(g.width<0 || g.height<0 || g.width>4096 || g.height>4096 ||
          g.offset_x < -4096 || g.offset_x>4096 || g.offset_y < -4096 || g.offset_y>4096 ||
          !std::isfinite(g.advance) || g.advance<=0 || g.advance>4096 ||
          g.coverage.size()!=std::size_t(g.width)*std::size_t(g.height))
        invalid("Invalid decimal glyph coverage or metrics");
      charge(bytes,g.coverage.size(),1,limits.max_working_bytes);
      charge(work,g.coverage.size(),1,limits.max_pixel_samples);
    }
  }
  auto layout_limits=limits.presentation;
  layout_limits.max_working_bytes=std::min(layout_limits.max_working_bytes,limits.max_working_bytes-bytes);
  auto layout_options=options;
  // Font bearings are explicit inputs; layout's host-font bounds are only
  // conservative estimates. Raster clips actual primitives/glyph bitmaps.
  layout_options.clip.reset();
  PreparedPointRaster out;out.width=width;out.height=height;out.view=options;
  // Validate clip through the layout API without its host-font culling.
  if(options.clip) {
    for(double n:*options.clip)if(!std::isfinite(n))invalid("Invalid point raster clip");
    if((*options.clip)[0]>(*options.clip)[2] || (*options.clip)[1]>(*options.clip)[3])invalid("Invalid point raster clip");
  }
  // Private desktop capture was admitted with these same presentation limits.
  out.layout=captured_layout?std::move(*captured_layout):point_presentation(selection,layout_options,layout_limits);
  charge(bytes,out.layout.working_bytes,1,limits.max_working_bytes);
  out.working_bytes=bytes;
  // Work budget includes source validation/copy, the layout's two vertex walks,
  // all admitted glyph storage and every candidate coverage sample.
  out.pixel_samples=work;
  visit(out,font,[&](const Draw& draw) {
    const auto box=bounds(draw,out);
    const auto count=std::size_t(box.right-box.left)*std::size_t(box.bottom-box.top);
    charge(out.pixel_samples,1,1,limits.max_pixel_samples);
    charge(out.pixel_samples,count,draw.glyph?1:16,limits.max_pixel_samples);
  });
  kernel_checkpoint();return out;
}
Image raster_point_style(const Image& rgb,const PreparedPointRaster& prepared,const PointRasterFont* font) {
  if(rgb.type!=PixelType::UInt8 || rgb.channels!=3 || rgb.width!=prepared.width || rgb.height!=prepared.height ||
      rgb.pixels.size()!=std::size_t(prepared.width)*prepared.height*3)
    invalid("Point raster requires a complete uint8 RGB display image");
  for(double v:rgb.pixels) {
    kernel_checkpoint_periodic();
    if(!std::isfinite(v) || v<0 || v>255)invalid("Invalid point raster display component");
  }
  kernel_checkpoint();auto result=rgb;
  visit(prepared,font,[&](const Draw& draw) {
    const auto box=bounds(draw,prepared);
    for(int y=box.top;y<box.bottom;++y)for(int x=box.left;x<box.right;++x) {
      kernel_checkpoint_periodic();
      const double alpha=coverage(draw,x,y,prepared.view)*draw.primitive.color[3]/255.;
      if(alpha==0)continue;
      for(int c=0;c<3;++c) {
        auto& v=result.pixels[(std::size_t(y)*result.width+x)*3+c];
        v=std::floor(v*(1-alpha)+draw.primitive.color[c]*alpha+.5);
      }
    }
  });
  kernel_checkpoint();
  // A final checkpoint callback can itself cancel. Recheck inherited tokens
  // without dispatching another callback before publishing the detached result.
  detail::without_execution_callbacks([] { kernel_checkpoint(); });
  return result;
}
}
Image render_point_style(const Image& rgb,const Selection& selection,const PointPresentationOptions& options,
    const PointRasterFont* font,const PointRasterLimits& limits,const ExecutionControl& control) {
  ExecutionScope scope(control);
  if(rgb.type!=PixelType::UInt8 || rgb.channels!=3)invalid("Point raster requires uint8 RGB display pixels");
  const auto prepared=detail::prepare_point_raster(rgb.width,rgb.height,selection,options,font,limits);
  return detail::raster_point_style(rgb,prepared,font);
}
}
