// Integer horizontal prediction on one row, with explicit file byte order.
#include "internal.hpp"
namespace imagejxx::io_detail {
// Predictor 3 stores byte planes in significance order, independent of the
// TIFF directory's endian flag. Differencing spans the entire shuffled row.
// All operations transport bytes; no Float32 arithmetic or casts occur.
void floating_predict(unsigned char *data,unsigned char *scratch,std::size_t bytes,
                      bool little,bool decode,const ExecutionControl &control) {
  require(data && scratch && data!=scratch && bytes && bytes%4==0,"Invalid floating predictor row");
  const auto width=bytes/4;
  checkpoint(control);
  if(decode) {
    for(std::size_t i=1;i<bytes;++i){if((i&65535)==0)checkpoint(control);data[i]=static_cast<unsigned char>(unsigned(data[i])+data[i-1]);}
    for(std::size_t i=0;i<bytes;++i){if((i&65535)==0)checkpoint(control);const auto sample=i/4,byte=i%4;scratch[i]=data[(little?3-byte:byte)*width+sample];}
    for(std::size_t i=0;i<bytes;++i){if((i&65535)==0)checkpoint(control);data[i]=scratch[i];}
  } else {
    for(std::size_t i=0;i<bytes;++i){if((i&65535)==0)checkpoint(control);const auto plane=i/width,sample=i%width;scratch[i]=data[4*sample+(little?3-plane:plane)];}
    data[0]=scratch[0];
    for(std::size_t i=1;i<bytes;++i){if((i&65535)==0)checkpoint(control);data[i]=static_cast<unsigned char>(unsigned(scratch[i])-scratch[i-1]);}
  }
  checkpoint(control);
}
void horizontal_predict(unsigned char *data,std::size_t bytes,std::size_t size,
                        std::size_t components,bool little,bool decode,
                        const ExecutionControl &control) {
  require(data && bytes && (size==1 || size==2) && (components==1 || components==3) &&
          bytes%size==0 && (bytes/size)%components==0,"Invalid horizontal predictor row");
  const auto count=bytes/size;
  const unsigned mask=size==1?255u:65535u;
  auto read=[&](std::size_t i){const auto *p=data+i*size;return size==1?unsigned(*p):little?unsigned(p[0])|(unsigned(p[1])<<8):unsigned(p[1])|(unsigned(p[0])<<8);};
  auto write=[&](std::size_t i,unsigned v){auto *p=data+i*size;if(size==1)*p=static_cast<unsigned char>(v);else {p[little?0:1]=static_cast<unsigned char>(v);p[little?1:0]=static_cast<unsigned char>(v>>8);}};
  checkpoint(control);
  if(decode) {
    for(std::size_t i=components;i<count;++i){if((i&65535)==0)checkpoint(control);write(i,(read(i)+read(i-components))&mask);}
  } else {
    for(std::size_t i=count;i>components;){--i;if((i&65535)==0)checkpoint(control);write(i,(read(i)-read(i-components))&mask);}
  }
  checkpoint(control);
}
}
