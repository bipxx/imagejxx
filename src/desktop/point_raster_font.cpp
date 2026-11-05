#include "point_raster_font.hpp"
#include "desktop.hpp"
#include <stb_truetype.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include "../native/kernel_control.hpp"

namespace imagejxx::desktop {
namespace point_font_detail {
struct alignas(std::max_align_t) Allocation {std::size_t bytes;};
void Budget::retain(std::size_t bytes) {
  if(allocated>limit || held>limit-allocated || bytes>limit-allocated-held)
    throw std::length_error("Point font resource limit");
  held+=bytes;
}
void* allocate(std::size_t bytes,void* user) {
  if(!user)return std::malloc(bytes);
  auto& budget=*static_cast<Budget*>(user);
  if(budget.allocated>budget.limit || budget.held>budget.limit-budget.allocated ||
      sizeof(Allocation)>budget.limit-budget.allocated-budget.held ||
      bytes>budget.limit-budget.allocated-budget.held-sizeof(Allocation)) {
    budget.denied=true;return nullptr;
  }
  const auto amount=bytes+sizeof(Allocation);
  auto* block=static_cast<Allocation*>(std::malloc(amount));
  if(!block){budget.denied=true;return nullptr;}
  block->bytes=amount;budget.allocated+=amount;return block+1;
}
void release(void* memory,void* user) {
  if(!memory)return;
  if(!user){std::free(memory);return;}
  auto& budget=*static_cast<Budget*>(user);
  auto* block=static_cast<Allocation*>(memory)-1;
  budget.allocated-=block->bytes;std::free(block);
}
}
std::string annotationFontPath(std::string path) {
  if(path.empty())if(const auto* configured=std::getenv("IMAGEJXX_FONT"))path=configured;
  if(path.empty()) {
#ifdef _WIN32
    const auto* windows=std::getenv("WINDIR");
    path=std::string(windows?windows:"C:/Windows")+"/Fonts/segoeui.ttf";
#elif defined(__APPLE__)
    path="/System/Library/Fonts/Supplemental/Arial.ttf";
#else
    path="/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#endif
  }
  if(path.size()>131064 || path.find('\0')!=std::string::npos)
    throw std::invalid_argument("Point font path must be bounded text");
  return path;
}
struct PointRasterFontCache::State {
  point_font_detail::Budget budget;
  std::string path;
  std::vector<unsigned char> data;
  stbtt_fontinfo face{};
  std::map<double,PointRasterFont> fonts;
  explicit State(std::size_t limit):budget{limit} {
    if(limit<65536 || limit>64*1048576)throw std::invalid_argument("Point font budget must be64 KiB..64 MiB");
    budget.retain(65536); // Fixed owner, metrics and map/control allowance.
  }
  void open(const std::string& name) {
    fonts.clear();std::vector<unsigned char>().swap(data);path.clear();budget.held=65536;budget.denied=false;
    // Charge bounded path/control copies alongside file, coverage and stb work.
    budget.retain(name.size()*3);
    kernel_checkpoint();
    data=readBytes(name,budget.limit-budget.held);
    budget.retain(data.size());
    // stb's parser assumes a complete trusted font resource. Reject obviously
    // short files before its header accesses; configured fonts remain resources,
    // not a general untrusted-font upload/validation interface.
    if(data.size()<16)throw std::invalid_argument("Point font file is too short");
    const int offset=stbtt_GetFontOffsetForIndex(data.data(),0);
    if(offset<0 || std::size_t(offset)>data.size()-12)
      throw std::invalid_argument("Invalid point font face offset");
    face={};face.userdata=&budget;
    if(!stbtt_InitFont(&face,data.data(),offset))throw std::invalid_argument("Invalid point font");
    path=name;kernel_checkpoint();
  }
  PointRasterFont make(double size) {
    if(!std::isfinite(size) || size<=0 || size>512)throw std::invalid_argument("Point font size must be0..512");
    PointRasterFont result;result.font_size=size;
    const float scale=stbtt_ScaleForPixelHeight(&face,float(size));
    if(!std::isfinite(scale) || scale<=0)throw std::invalid_argument("Invalid point font scale");
    for(unsigned d=0;d<10;++d) {
      kernel_checkpoint();
      if(!stbtt_FindGlyphIndex(&face,int('0'+d)))throw std::invalid_argument("Point font lacks decimal digits");
      int left,top,right,bottom,advance,bearing;
      stbtt_GetCodepointBitmapBox(&face,int('0'+d),scale,scale,&left,&top,&right,&bottom);
      stbtt_GetCodepointHMetrics(&face,int('0'+d),&advance,&bearing);
      const auto width=std::int64_t(right)-left,height=std::int64_t(bottom)-top;
      if(width<0 || height<0 || width>4096 || height>4096 || left < -4096 || left>4096 || top < -4096 || top>4096)
        throw std::invalid_argument("Point font glyph bounds exceed supported extent");
      auto& glyph=result.digits[d];glyph.width=int(width);glyph.height=int(height);
      glyph.offset_x=left;glyph.offset_y=top;glyph.advance=advance*double(scale);
      if(!std::isfinite(glyph.advance) || glyph.advance<=0 || glyph.advance>4096)
        throw std::invalid_argument("Invalid point font advance");
      const auto bytes=std::size_t(width)*std::size_t(height);budget.retain(bytes);
      glyph.coverage.resize(bytes);
      if(bytes)stbtt_MakeCodepointBitmap(&face,glyph.coverage.data(),glyph.width,glyph.height,glyph.width,scale,scale,int('0'+d));
      if(budget.denied || budget.allocated)throw std::length_error("Point font raster staging limit");
    }
    kernel_checkpoint();return result;
  }
};
PointRasterFontCache::PointRasterFontCache(std::size_t bytes):max_bytes_(bytes){}
PointRasterFontCache::~PointRasterFontCache()=default;
const PointRasterFont& PointRasterFontCache::get(const std::string& configured,double size) {
  if(!std::isfinite(size) || size<=0 || size>512)throw std::invalid_argument("Point font size must be positive and at most512");
  if(!state_)state_=std::make_unique<State>(max_bytes_);
  const auto path=annotationFontPath(configured);
  if(state_->path!=path)state_->open(path);
  auto found=state_->fonts.find(size);
  if(found!=state_->fonts.end())return found->second;
  if(state_->fonts.size()==7)throw std::length_error("Point font cache size limit");
  return state_->fonts.emplace(size,state_->make(size)).first->second;
}
PointRasterFont loadPointRasterFont(const std::string& path,double size,std::size_t max_bytes) {
  if(!std::isfinite(size) || size<=0 || size>512)throw std::invalid_argument("Point font size must be positive and at most512");
  PointRasterFontCache::State resource(max_bytes);resource.open(annotationFontPath(path));
  return resource.make(size);
}
}
