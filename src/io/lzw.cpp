// Original TIFF MSB/early-change LZW codec. Fixed dictionaries; no plugin code.
#include "internal.hpp"
#include <array>
namespace imagejxx::io_detail {
namespace {
struct Writer {
  std::vector<unsigned char> out;
  std::uint32_t value=0;unsigned bits=0;
  void put(unsigned code,unsigned width) {
    value=(value<<width)|code;bits+=width;
    while(bits>=8){bits-=8;out.push_back(static_cast<unsigned char>(value>>bits));}
  }
  void finish(){if(bits)out.push_back(static_cast<unsigned char>(value<<(8-bits)));}
};
struct Reader {
  const std::vector<unsigned char> &input;
  std::size_t at=0;std::uint32_t value=0;unsigned bits=0;
  unsigned get(unsigned width) {
    while(bits<width){require(at<input.size(),"Truncated LZW code stream");value=(value<<8)|input[at++];bits+=8;}
    bits-=width;return (value>>bits)&((1u<<width)-1);
  }
  void finish() {
    require(at==input.size() && (value&((1u<<bits)-1))==0,"Trailing bytes or nonzero LZW padding");
  }
};
}
std::size_t lzw_bound(std::size_t n) {
  // At most one data code per byte, plus initial/reset CLEAR and EOI. The
  // encoder resets after 3837 inserted entries; every code is at most 12 bits.
  const auto bits=mul(add(add(n,n/3837),3),12);
  return add(bits/8,bits%8?1:0);
}
std::vector<unsigned char> lzw_encode(const std::vector<unsigned char> &input,
                                     const ExecutionControl &control) {
  struct Slot {std::uint32_t key;unsigned code;};
  std::array<Slot,8192> table;
  auto reset=[&]{for(auto &s:table)s.key=UINT32_MAX;};reset();
  Writer writer;writer.out.reserve(lzw_bound(input.size()));
  unsigned width=9,next=258;std::size_t probes=0;writer.put(256,width);checkpoint(control);
  if(!input.empty()) {
    unsigned prefix=input[0];
    for(std::size_t i=1;i<input.size();++i) {
      if((i&65535)==0)checkpoint(control);
      const auto key=(std::uint32_t(prefix)<<8)|input[i];
      auto slot=std::size_t((key*2654435761u)>>19);
      while(table[slot].key!=UINT32_MAX && table[slot].key!=key) {
        if((++probes&65535)==0)checkpoint(control);
        slot=(slot+1)&8191;
      }
      if(table[slot].key==key)prefix=table[slot].code;
      else {
        writer.put(prefix,width);table[slot]={key,next++};
        if(width<12 && next==(1u<<width))++width;
        if(next==4095){writer.put(256,width);reset();next=258;width=9;}
        prefix=input[i];
      }
    }
    writer.put(prefix,width);
    // The decoder adds an entry after the final data code too; EOI must use
    // that decoder width when the stream ends exactly at a width boundary.
    if(width<12 && next==(1u<<width)-1)++width;
  }
  writer.put(257,width);writer.finish();
  require(writer.out.size()<=lzw_bound(input.size()),"LZW exceeded encoded bound");
  checkpoint(control);return std::move(writer.out);
}
void lzw_decode(const std::vector<unsigned char> &input,std::vector<unsigned char> &output,
                const ExecutionControl &control) {
  std::array<std::uint16_t,4096> prefix{};
  std::array<unsigned char,4096> suffix{},stack{};
  Reader reader{input};unsigned width=9,next=258;int previous=-1;
  std::size_t done=0,tick_in=0,tick_out=0;checkpoint(control);
  require(reader.get(9)==256,"LZW strip requires initial CLEAR");
  for(;;) {
    if(reader.at-tick_in>=65536 || done-tick_out>=65536){checkpoint(control);tick_in=reader.at;tick_out=done;}
    const auto code=reader.get(width);
    if(code==256){width=9;next=258;previous=-1;continue;}
    if(code==257){require(done==output.size(),"LZW decoded byte count mismatch");reader.finish();checkpoint(control);return;}
    require(code<next || (code==next && previous>=0 && next<4096),"Invalid LZW dictionary code");
    if(previous<0)require(code<256,"LZW first data code must be literal");
    const bool special=code==next;auto current=special?unsigned(previous):code;
    std::size_t n=0;
    while(current>=256) {
      require(current>=258 && current<next && n<stack.size(),"Invalid LZW prefix chain");
      stack[n++]=suffix[current];const auto parent=prefix[current];
      require(parent<current,"Cyclic LZW dictionary");current=parent;
    }
    require(n<stack.size(),"LZW dictionary string limit");stack[n++]=static_cast<unsigned char>(current);
    const auto first=stack[n-1];require(n+std::size_t(special)<=output.size()-done,"LZW strip exceeds decoded byte count");
    while(n)output[done++]=stack[--n];if(special)output[done++]=first;
    if(previous>=0 && next<4096) {
      prefix[next]=std::uint16_t(previous);suffix[next]=first;++next;
      if(width<12 && next==(1u<<width)-1)++width;
    }
    previous=int(code);
  }
}
}
