// Use the core's already vendored, prefixed zlib. No external codec ABI.
#define Z_PREFIX
#include "../../third_party/zlib/zlib.h"
#include "internal.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <limits>
namespace imagejxx::io_detail {
namespace {
struct Budget { std::size_t limit, used=0; };
struct alignas(std::max_align_t) Allocation { std::size_t bytes; };
voidpf allocate(voidpf opaque, uInt count, uInt size) noexcept {
  auto &b=*static_cast<Budget *>(opaque);
  if(size && count>SIZE_MAX/size)return nullptr;
  const auto bytes=std::size_t(count)*size;
  if(bytes>SIZE_MAX-sizeof(Allocation))return nullptr;
  const auto total=bytes+sizeof(Allocation);
  if(total>b.limit-b.used)return nullptr;
  auto *p=static_cast<Allocation *>(std::calloc(1,total));
  if(!p)return nullptr;
  p->bytes=total;b.used+=total;return p+1;
}
void release(voidpf opaque, voidpf data) noexcept {
  if(!data)return;
  auto &b=*static_cast<Budget *>(opaque);
  auto *p=static_cast<Allocation *>(data)-1;b.used-=p->bytes;std::free(p);
}
struct Codec {
  Budget budget;
  z_stream stream{};
  bool encoding;
  Codec(std::size_t limit,bool encode):budget{limit},encoding(encode) {
    stream.zalloc=allocate;stream.zfree=release;stream.opaque=&budget;
    const auto status=encoding?deflateInit(&stream,Z_DEFAULT_COMPRESSION):inflateInit(&stream);
    if(status==Z_MEM_ERROR)throw std::length_error("Deflate codec allocation limit");
    require(status==Z_OK,"Deflate codec initialization failed");
  }
  ~Codec(){if(encoding)deflateEnd(&stream);else inflateEnd(&stream);}
};
void transform(const std::vector<unsigned char> &input,std::vector<unsigned char> &output,
               std::size_t limit,bool encode,const ExecutionControl &control,
               std::size_t &produced) {
  Codec codec(limit,encode);auto &s=codec.stream;
  std::size_t supplied=0,consumed=0;produced=0;
  // One overflow byte detects excess expansion even when expected output is
  // exactly full; no unbounded growth or truncated-stream acceptance.
  unsigned char overflow=0;
  for(;;) {
    checkpoint(control);
    if(!s.avail_in && supplied<input.size()) {
      const auto n=std::min(std::size_t(65536),input.size()-supplied);
      s.next_in=const_cast<Bytef *>(input.data()+supplied);s.avail_in=uInt(n);supplied+=n;
    }
    const auto available=std::min(std::size_t(65536),output.size()-produced);
    s.next_out=available?output.data()+produced:&overflow;s.avail_out=uInt(available?available:1);
    const auto before_in=s.avail_in,before_out=s.avail_out;
    const auto status=encode?deflate(&s,supplied==input.size()?Z_FINISH:Z_NO_FLUSH):inflate(&s,Z_NO_FLUSH);
    const auto took=before_in-s.avail_in,made=before_out-s.avail_out;
    consumed+=took;
    require(available || !made,"Deflate decoded/encoded strip exceeds admitted size");
    produced+=made;
    if(status==Z_MEM_ERROR)throw std::length_error("Deflate codec allocation limit");
    if(status==Z_STREAM_END) {
      require(consumed==input.size(),"Trailing bytes in Deflate strip");
      if(!encode)require(produced==output.size(),"Deflate strip decoded byte count mismatch");
      checkpoint(control);return;
    }
    require(status==Z_OK && (took || made),"Malformed or truncated Deflate strip");
  }
}
}
std::size_t deflate_bound(std::size_t n) {
  // zlib compressBound with checked arithmetic for default parameters.
  return add(add(add(add(n,n>>12),n>>14),n>>25),13);
}
std::vector<unsigned char> deflate_strip(const std::vector<unsigned char> &input,
                                       std::size_t limit,const ExecutionControl &control) {
  std::vector<unsigned char> output(deflate_bound(input.size()));std::size_t n;
  transform(input,output,limit,true,control,n);output.resize(n);return output;
}
void inflate_strip(const std::vector<unsigned char> &input,std::vector<unsigned char> &output,
                   std::size_t limit,const ExecutionControl &control) {
  std::size_t n;transform(input,output,limit,false,control,n);
}
}
