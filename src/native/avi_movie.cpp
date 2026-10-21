#include <imagejxx/avi_movie.hpp>
#include "atomic_file.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numeric>

namespace imagejxx {
namespace {
using Bytes=std::vector<unsigned char>;
void u16(Bytes &b,std::uint32_t n){b.push_back(n&255);b.push_back((n>>8)&255);}
void u32(Bytes &b,std::uint32_t n){u16(b,n&65535);u16(b,n>>16);}
void tag(Bytes &b,const char *s){b.insert(b.end(),s,s+4);}
void chunk(Bytes &b,const char *name,const Bytes &data){tag(b,name);u32(b,static_cast<std::uint32_t>(data.size()));b.insert(b.end(),data.begin(),data.end());}
std::size_t header_bytes(int channels){return channels==1?1248:224;}
Bytes header(const AviMovieSpec &s,const AviMovieEstimate &e){
  Bytes avih;
  u32(avih,static_cast<std::uint32_t>(std::llround(1e6*e.scale/e.rate)));
  u32(avih,static_cast<std::uint32_t>(std::min<double>(UINT32_MAX,std::ceil((e.frame_bytes+8)*s.fps))));
  u32(avih,0);u32(avih,0x10);u32(avih,s.frames);u32(avih,0);u32(avih,1);
  u32(avih,e.frame_bytes);u32(avih,s.width);u32(avih,s.height);for(int i=0;i<4;++i)u32(avih,0);
  Bytes strh;tag(strh,"vids");tag(strh,"DIB ");u32(strh,0);u16(strh,0);u16(strh,0);
  u32(strh,0);u32(strh,e.scale);u32(strh,e.rate);u32(strh,0);u32(strh,s.frames);
  u32(strh,e.frame_bytes);u32(strh,UINT32_MAX);u32(strh,0);
  u16(strh,0);u16(strh,0);u16(strh,s.width);u16(strh,s.height);
  Bytes strf;u32(strf,40);u32(strf,s.width);u32(strf,s.height);u16(strf,1);u16(strf,s.channels*8);
  u32(strf,0);u32(strf,e.frame_bytes);u32(strf,0);u32(strf,0);u32(strf,s.channels==1?256:0);u32(strf,0);
  if(s.channels==1)for(int i=0;i<256;++i){strf.push_back(i);strf.push_back(i);strf.push_back(i);strf.push_back(0);}
  Bytes strl;tag(strl,"strl");chunk(strl,"strh",strh);chunk(strl,"strf",strf);
  Bytes hdrl;tag(hdrl,"hdrl");chunk(hdrl,"avih",avih);chunk(hdrl,"LIST",strl);
  Bytes out;tag(out,"RIFF");u32(out,static_cast<std::uint32_t>(e.file_bytes-8));tag(out,"AVI ");chunk(out,"LIST",hdrl);
  tag(out,"LIST");u32(out,static_cast<std::uint32_t>(4+std::uint64_t(s.frames)*(8+e.frame_bytes)));tag(out,"movi");
  if(out.size()!=header_bytes(s.channels))throw std::logic_error("AVI header size mismatch");
  return out;
}
}
AviMovieEstimate estimate_avi_movie(const AviMovieSpec &s){
  if(s.width<1||s.height<1||s.width>32767||s.height>32767||s.frames<1||(s.channels!=1&&s.channels!=3))
    throw std::invalid_argument("AVI needs 1..32767 width/height, positive frames and 1 or 3 channels");
  if(!std::isfinite(s.fps)||s.fps<.001||s.fps>1000)throw std::invalid_argument("AVI FPS must be finite and within 0.001..1000");
  const auto rate=static_cast<std::uint32_t>(std::llround(s.fps*1000000));
  const auto divisor=std::gcd(rate,std::uint32_t(1000000));
  const std::uint64_t row=(std::uint64_t(s.width)*s.channels+3)&~std::uint64_t(3);
  const std::uint64_t frame=row*s.height;
  const std::uint64_t file=header_bytes(s.channels)+std::uint64_t(s.frames)*(frame+24)+8;
  const std::uint64_t working=std::uint64_t(s.width)*s.height*s.channels*sizeof(double)+row+16384;
  if(file>std::uint64_t(UINT32_MAX)+8||file>s.max_file_bytes||working>s.max_working_bytes||file>SIZE_MAX||working>SIZE_MAX)
    throw std::invalid_argument("AVI exceeds the RIFF file or working-byte limit");
  return {static_cast<std::size_t>(file),static_cast<std::size_t>(working),static_cast<std::size_t>(row),static_cast<std::size_t>(frame),rate/divisor,1000000/divisor};
}
struct StagedAviMovie::State{std::unique_ptr<detail::StagedAtomicStream> stream;};
StagedAviMovie::StagedAviMovie(std::unique_ptr<State> s):state_(std::move(s)){}
StagedAviMovie::~StagedAviMovie()=default;
void StagedAviMovie::publish(bool replace){if(!state_->stream)throw std::logic_error("AVI already published");state_->stream->publish(replace);state_->stream.reset();}
std::unique_ptr<StagedAviMovie> stage_avi_movie(const std::string &path,const AviMovieSpec &s,const AviFrameProvider &provider,const ExecutionControl &control){
  const auto e=estimate_avi_movie(s);
  if(!provider)throw std::invalid_argument("AVI frame provider is required");
  if(path.empty()||path.find('\0')!=std::string::npos)throw std::invalid_argument("AVI path must be nonempty UTF-8 text");
  (void)nlohmann::json(path).dump();
  auto state=std::make_unique<StagedAviMovie::State>();state->stream=std::make_unique<detail::StagedAtomicStream>(std::filesystem::u8path(path));
  auto &stream=*state->stream;stream.open(control);const auto h=header(s,e);stream.append(h.data(),h.size(),control);
  Bytes row(e.row_bytes,0),marker;tag(marker,"00db");u32(marker,e.frame_bytes);
  for(int f=0;f<s.frames;++f){
    detail::file_stage_progress(control,"AVI frames",f,s.frames);
    if(control.progress)control.progress(f,s.frames);
    detail::file_checkpoint(control);
    Image image=provider(static_cast<std::size_t>(f));
    if(image.width!=s.width||image.height!=s.height||image.channels!=s.channels||image.type!=PixelType::UInt8||image.pixels.size()!=std::size_t(s.width)*s.height*s.channels)
      throw std::invalid_argument("AVI provider returned incompatible image dimensions or type");
    stream.append(marker.data(),marker.size(),control);
    for(int y=s.height-1;y>=0;--y){
      for(int x=0;x<s.width;++x)for(int c=0;c<s.channels;++c){
        const double value=image.at(x,y,s.channels==3?2-c:c);
        if(!std::isfinite(value)||value<0||value>255||value!=std::floor(value))throw std::invalid_argument("AVI requires exact UInt8 samples");
        row[std::size_t(x)*s.channels+c]=static_cast<unsigned char>(value);
      }
      stream.append(row.data(),row.size(),control);
    }
  }
  Bytes index;tag(index,"idx1");u32(index,static_cast<std::uint32_t>(std::uint64_t(s.frames)*16));stream.append(index.data(),index.size(),control);
  for(int f=0;f<s.frames;++f){
    detail::file_stage_progress(control,"AVI index",f,s.frames);index.clear();tag(index,"00db");u32(index,0x10);
    u32(index,static_cast<std::uint32_t>(4+std::uint64_t(f)*(8+e.frame_bytes)));u32(index,e.frame_bytes);stream.append(index.data(),index.size(),control);
  }
  detail::file_stage_progress(control,"AVI frames",s.frames,s.frames);
  if(control.progress)control.progress(s.frames,s.frames);
  stream.close(control);
  return std::unique_ptr<StagedAviMovie>(new StagedAviMovie(std::move(state)));
}
void write_avi_movie(const std::string &p,const AviMovieSpec &s,const AviFrameProvider &f,bool replace,const ExecutionControl &c){auto stage=stage_avi_movie(p,s,f,c);detail::file_checkpoint(c);stage->publish(replace);}
void write_avi_dataset(const std::string &path,const Dataset &data,double fps,bool replace,std::size_t file_limit,std::size_t working_limit,const ExecutionControl &control){
  const auto shape=data.shape();if(shape.t!=1||shape.c!=1||data.pixel_type()!=PixelType::UInt8||(shape.s!=1&&shape.s!=3))throw std::invalid_argument("AVI dataset must be an UInt8 Z stack with 1 or 3 samples");
  AviMovieSpec spec{shape.x,shape.y,shape.z,shape.s,fps,file_limit,working_limit};
  write_avi_movie(path,spec,[&](std::size_t f){return *data.plane({0,0,static_cast<int>(f)});},replace,control);
}
} // namespace imagejxx
