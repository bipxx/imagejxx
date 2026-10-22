#include <imagejxx/virtual_io.hpp>
#include "internal.hpp"
#include "../native/kernel_control.hpp"
#include "../native/controlled_source_internal.hpp"
#include <algorithm>
namespace imagejxx {
namespace {
using namespace io_detail;
class VirtualTiff final:public PlaneSource,public detail::ControlledPlaneSource {
  std::shared_ptr<Source> source_;
  Layout layout_;
  ImageFileLimits limits_;
  std::size_t retained_;
  Image read(PlanePosition p,int x,int y,int width,int height) const {
    const auto s=shape();
    require(p.t>=0 && p.t<s.t && p.c>=0 && p.c<s.c && p.z>=0 && p.z<s.z,
            "Virtual TIFF plane is outside source");
    require(x>=0 && y>=0 && width>0 && height>0 && x<s.x && y<s.y &&
            width<=s.x-x && height<=s.y-y,"Virtual TIFF region is outside source");
    ExecutionControl control;control.cancellation=execution_cancellation();
    control.checkpoint=[] {kernel_checkpoint();};
    control.stage_progress=[](std::string_view name,std::size_t n,std::size_t total){execution_stage_progress(name,n,total);};
    checkpoint(control);source_->unchanged();
    const auto page_index=(std::size_t(p.t)*std::size_t(s.z)+std::size_t(p.z))*std::size_t(s.c)+std::size_t(p.c);
    const auto &page=layout_.pages.at(page_index);
    const auto stride=mul(layout_.sample_bytes,std::size_t(s.s));
    const auto block_width=layout_.tiled?std::size_t(page.tile_width):std::size_t(s.x);
    const auto block_height=layout_.tiled?std::size_t(page.tile_length):std::size_t(page.rows);
    const auto row_bytes=mul(block_width,stride);
    const auto output=mul(mul(mul(std::size_t(width),std::size_t(height)),std::size_t(s.s)),sizeof(double));
    auto working=add(retained_,output);
    working=add(working,mul(std::size_t(width),stride));
    if(layout_.compression!=1){working=add(working,layout_.max_encoded_strip_bytes);working=add(working,mul(layout_.sample_bytes,layout_.max_strip_samples));}
    if(layout_.predictor==3)working=add(working,row_bytes);
    cap(working,limits_.max_working_bytes,"Virtual TIFF read working-byte limit");
    checkpoint(control);
    Image image(width,height,s.s,pixel_type());
    std::vector<unsigned char> encoded,raw,prediction(layout_.predictor==3?row_bytes:0);
    if(layout_.compression!=1){encoded.reserve(layout_.max_encoded_strip_bytes);raw.reserve(mul(layout_.sample_bytes,layout_.max_strip_samples));}
    std::vector<unsigned char> row(mul(std::size_t(width),stride));
    const auto across=layout_.tiled?(std::size_t(s.x)-1)/block_width+1:1;
    const auto first_x=std::size_t(x)/block_width,last_x=(std::size_t(x)+width-1)/block_width;
    const auto first_y=std::size_t(y)/block_height,last_y=(std::size_t(y)+height-1)/block_height;
    const auto total=mul(last_x-first_x+1,last_y-first_y+1);std::size_t done=0;
    for(auto by=first_y;by<=last_y;++by)for(auto bx=first_x;bx<=last_x;++bx){
      checkpoint(control);const auto top=mul(by,block_height),left=mul(bx,block_width);
      const auto &block=page.strips.at(add(mul(by,across),bx));
      const auto begin_y=std::max(top,std::size_t(y)),end_y=std::min(top+block_height,std::size_t(y)+height);
      const auto begin_x=std::max(left,std::size_t(x)),end_x=std::min(left+block_width,std::size_t(x)+width);
      if(layout_.compression!=1){
        encoded.resize(block.bytes);
        const auto rows=layout_.tiled?block_height:std::min(block_height,std::size_t(s.y)-top);
        raw.resize(mul(rows,row_bytes));source_->read(block.offset,encoded.data(),encoded.size(),control);
        decode_compressed_block(encoded,raw,layout_.compression,limits_.max_codec_bytes,control);
      }
      for(auto yy=begin_y;yy<end_y;++yy){
        checkpoint(control);const auto offset=mul(yy-top,row_bytes);
        if(layout_.predictor==3)floating_predict(raw.data()+offset,prediction.data(),row_bytes,layout_.little,true,control);
        if(layout_.predictor==2)horizontal_predict(raw.data()+offset,row_bytes,layout_.sample_bytes,std::size_t(s.s),layout_.little,true,control);
        const auto count=mul(end_x-begin_x,stride),start=mul(begin_x-left,stride);
        const unsigned char *samples;
        if(layout_.compression==1){source_->read(add(block.offset,add(offset,start)),row.data(),count,control);samples=row.data();}
        else samples=raw.data()+offset+start;
        for(std::size_t i=0;i<mul(end_x-begin_x,std::size_t(s.s));++i){
          if(!(i&65535))checkpoint(control);
          std::uint32_t word=0;
          for(std::size_t b=0;b<layout_.sample_bytes;++b)word|=std::uint32_t(samples[i*layout_.sample_bytes+b])<<(8*(layout_.little?b:layout_.sample_bytes-1-b));
          image.pixels[((yy-std::size_t(y))*std::size_t(width)+begin_x-std::size_t(x))*std::size_t(s.s)+i]=
            pixel_type()==PixelType::Float32?float32_source_sample(word,FloatingSamplePolicy::FiniteOnly):double(word);
        }
      }
      rows_progress(control,++done,total,"Virtual TIFF blocks");
    }
    source_->unchanged();checkpoint(control);source_->unchanged();return image;
  }
public:
  VirtualTiff(std::shared_ptr<Source> source,Layout layout,ImageFileLimits limits)
    :source_(std::move(source)),layout_(std::move(layout)),limits_(limits),retained_(add(inspection_working(layout_,limits_),4096)){
    cap(retained_,limits_.max_working_bytes,"Virtual TIFF retained descriptor limit");
  }
  std::shared_ptr<const Image> controlled_plane(PlanePosition p,const detail::SourceReadControl &control) const override {detail::SourceReadScope scope(control);return read_plane(p);}
  Image controlled_region(PlanePosition p,int x,int y,int width,int height,const detail::SourceReadControl &control) const override {detail::SourceReadScope scope(control);return read_region(p,x,y,width,height);}
  DatasetShape shape()const override{return layout_.info.shape;}
  PixelType pixel_type()const override{return layout_.info.pixel_type;}
  std::shared_ptr<const Image> read_plane(PlanePosition p)const override{return std::make_shared<const Image>(read(p,0,0,shape().x,shape().y));}
  Image read_region(PlanePosition p,int x,int y,int width,int height)const override{return read(p,x,y,width,height);}
};
}
ImageReadResult open_virtual_image_series(const std::string &path_arg,
    const ImageReadRequest &request,const ImageFileLimits &limits,const ExecutionControl &control){
  using namespace io_detail;valid_limits(limits);checkpoint(control);
  require(request.series_index==0,"Scientific TIFF profile contains only series index zero");
  require(request.sample_policy==FloatingSamplePolicy::FiniteOnly,"Virtual TIFF currently requires finite-only samples");
  const auto path=native_path(path_arg,limits.max_metadata_bytes);
  auto source=std::make_shared<Source>(path,limits.max_file_bytes,control);
  auto layout=inspect(*source,path.u8string().size(),limits,control);
  auto metadata=layout.info.metadata;const auto calibration=layout.info.calibration;
  auto provider=std::make_shared<VirtualTiff>(source,std::move(layout),limits);
  progress(control,1);source->unchanged();checkpoint(control);source->unchanged();
  return {Dataset(std::move(provider),calibration),std::move(metadata)};
}
}
