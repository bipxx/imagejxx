// Original bounded TIFF PackBits implementation. No ImageJ/Bio-Formats code.
// TIFF 6.0 section 10: row-wise output, at most 128 bytes per packet.
#include "internal.hpp"
#include <algorithm>
namespace imagejxx::io_detail {
std::size_t packbits_bound(std::size_t row_bytes,std::size_t rows) {
  return mul(add(row_bytes,add(row_bytes/128,row_bytes%128?1:0)),rows);
}
std::vector<unsigned char> packbits_encode(const std::vector<unsigned char> &input,
                                         std::size_t width,const ExecutionControl &control) {
  require(width && input.size()%width==0,"PackBits requires complete rows");
  std::vector<unsigned char> out;out.reserve(packbits_bound(width,input.size()/width));
  std::size_t tick=0;
  for(std::size_t row=0;row<input.size();row+=width) {
    checkpoint(control);const auto end=row+width;std::size_t at=row;
    auto run=[&](std::size_t first){std::size_t n=1;while(n<128 && first+n<end && input[first+n]==input[first])++n;return n;};
    while(at<end) {
      if(at-tick>=65536){checkpoint(control);tick=at;}
      const auto repeated=run(at);
      if(repeated>=3) {out.push_back(static_cast<unsigned char>(257-repeated));out.push_back(input[at]);at+=repeated;}
      else {
        const auto first=at++;
        while(at<end && at-first<128 && run(at)<3)++at;
        const auto n=at-first;out.push_back(static_cast<unsigned char>(n-1));
        out.insert(out.end(),input.begin()+first,input.begin()+at);
      }
    }
  }
  require(out.size()<=packbits_bound(width,input.size()/width),"PackBits exceeded encoded bound");
  checkpoint(control);return out;
}
void packbits_decode(const std::vector<unsigned char> &input,std::vector<unsigned char> &out,
                     const ExecutionControl &control) {
  std::size_t at=0,done=0,tick_in=0,tick_out=0;
  while(at<input.size()) {
    if(at-tick_in>=65536 || done-tick_out>=65536){checkpoint(control);tick_in=at;tick_out=done;}
    const auto code=input[at++];
    if(code==128)continue;
    const auto n=code<128?std::size_t(code)+1:std::size_t(257-code);
    require(n<=out.size()-done,"PackBits strip exceeds decoded byte count");
    if(code<128) {
      require(n<=input.size()-at,"Truncated PackBits literal");
      std::copy_n(input.data()+at,n,out.data()+done);at+=n;
    } else {
      require(at<input.size(),"Truncated PackBits repeat");
      std::fill_n(out.data()+done,n,input[at++]);
    }
    done+=n;
  }
  require(done==out.size(),"PackBits decoded byte count mismatch");
  checkpoint(control);
}
}
