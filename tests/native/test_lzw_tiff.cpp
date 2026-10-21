#include <imagejxx/io.hpp>
#include <nlohmann/json.hpp>
#include "../../src/io/internal.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace imagejxx;
namespace {
std::size_t checks=0;
void check(bool ok,const char *message){++checks;if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception &){failed=true;}check(failed,"Expected Lzw refusal");}
ImageFileFormat format(const std::string &dtype,bool zip=true){
 if(dtype=="u8")return zip?ImageFileFormat::ScientificTiffU8LzwV1:ImageFileFormat::ScientificTiffU8V1;
 if(dtype=="u16")return zip?ImageFileFormat::ScientificTiffU16LzwV1:ImageFileFormat::ScientificTiffU16V1;
 if(dtype=="rgb8")return zip?ImageFileFormat::ScientificTiffRgb8LzwV1:ImageFileFormat::ScientificTiffRgb8V1;
 return zip?ImageFileFormat::ScientificTiffFloat32LzwV1:ImageFileFormat::ScientificTiffFloat32V1;
}
void expect(const Dataset &data,const std::string &hex){
 const auto shape=data.shape();std::size_t at=0;const auto size=data.pixel_type()==PixelType::Float32?4:data.pixel_type()==PixelType::UInt16?2:1;
 for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z){
  auto p=data.plane({t,c,z});for(auto v:p->pixels){
   const auto word=data.pixel_type()==PixelType::Float32?float32_source_sample_bits(v,data.sample_policy()):std::uint32_t(v);
   for(int b=0;b<size;++b){check(((word>>(b*8))&255)==std::stoul(hex.substr(at,2),nullptr,16),"Lzw changed sample word");at+=2;}
  }
 }
 check(at==hex.size(),"Lzw sample count changed");
}
}
int main(int argc,char **argv){try{
 check(argc==3,"Supply fixtures/output");std::filesystem::path root=argv[1],out=argv[2];std::filesystem::create_directories(out.parent_path());check(std::filesystem::create_directory(out),"Fresh output required");
 std::ifstream f(root/"SOURCE.json");auto manifest=nlohmann::json::parse(f);std::size_t valid=0,bad=0;
 ImageReadRequest read;read.sample_policy=FloatingSamplePolicy::PreserveIEEEFloat32;
 for(const auto &row:manifest.at("files")){
  const auto path=(root/row.at("file").get<std::string>()).u8string();
  if(!row.at("valid").get<bool>()){
   ++bad;
   if(row.at("file")=="predictor.tif") {
     check(inspect_image_file(path).format==ImageFileFormat::ScientificTiffU16LzwHorizontalV1,"Promoted predictor descriptor");
     const auto original=row.at("hex").get<std::string>();std::string restored;unsigned sum=0;constexpr char digits[]="0123456789abcdef";
     for(std::size_t i=0;i<original.size();i+=4){if((i/4)%row.at("shape")[4].get<std::size_t>()==0)sum=0;
       const auto v=unsigned(std::stoul(original.substr(i,2),nullptr,16))|(unsigned(std::stoul(original.substr(i+2,2),nullptr,16))<<8);sum=(sum+v)&65535;
       for(unsigned shift:{0u,8u}){const auto byte=(sum>>shift)&255;restored+=digits[byte>>4];restored+=digits[byte&15];}}
     expect(read_image_series(path).dataset,restored);continue;
   }
   if(row.at("descriptor_valid").get<bool>())check(inspect_image_file(path).series.size()==1,"Payload inspection decoded samples");else rejects([&]{inspect_image_file(path);});
   rejects([&]{read_image_series(path);});continue;
  }
  ++valid;const auto dtype=row.at("dtype").get<std::string>();auto request=read;if(dtype!="f32")request.sample_policy=FloatingSamplePolicy::FiniteOnly;
  auto info=inspect_image_file(path);check(info.format==format(dtype),"Wrong Lzw format");
  auto result=read_image_series(path,request);const auto hex=row.at("hex").get<std::string>();expect(result.dataset,hex);
  check(result.metadata==info.series[0].metadata,"Lzw metadata changed");
  check(result.dataset.calibration().spacing==std::array<double,3>{.5,1.25,2.75},"Lzw calibration changed");
  if(dtype=="f32")rejects([&]{read_image_series(path);});
  DatasetReadObserver observer;auto observed=result.dataset.observe_reads(observer);ImageWriteRequest write;write.format=format(dtype);
  auto estimate=estimate_image_write(observed,result.metadata,write);check(observer.counts().plane_reads==0,"Lzw planning read samples");
  const auto target=(out/row.at("file").get<std::string>()).u8string();write_image_series(target,observed,result.metadata,write);
  check(std::filesystem::file_size(target)<=estimate.encoded_upper_bound,"Lzw exceeded estimate");expect(read_image_series(target,request).dataset,hex);
  rejects([&]{write_image_series(target,observed,{},write);});write.replace=FileReplacePolicy::ReplaceExisting;write_image_series(target,observed,{},write);expect(read_image_series(target,request).dataset,hex);
  write.format=format(dtype,false);write_image_series((out/(row.at("file").get<std::string>()+"-raw.tif")).u8string(),observed,result.metadata,write);
 }
 check(valid==44 && bad==8,"Lzw corpus changed");
 { using namespace io_detail;
   std::vector<unsigned char> a(1);lzw_decode({0x80,0x10,0x60,0x20},a,{});check(a[0]==65,"Known MSB vector changed");
   rejects([&]{lzw_decode({0x80,0x10,0x60,0x21},a,{});});
   for(std::size_t size:{1u,2u,3u,253u,254u,255u,256u,511u,1023u,2047u,4095u,131072u})for(unsigned pattern=0;pattern<3;++pattern) {
     std::vector<unsigned char> raw(size);std::uint32_t seed=927;
     for(std::size_t i=0;i<size;++i){seed=1664525*seed+1013904223;raw[i]=static_cast<unsigned char>(pattern==0?7:pattern==1?i:seed>>24);}
     auto packed=lzw_encode(raw,{});std::vector<unsigned char> restored(size);lzw_decode(packed,restored,{});
     check(restored==raw && packed.size()<=lzw_bound(size),"LZW width/reset/bound changed");
   }
   ExecutionControl control;control.checkpoint=[] {throw OperationCancelled();};rejects([&]{lzw_encode(std::vector<unsigned char>(100000,0),control);});
   std::vector<unsigned char> long_input(262144);std::uint32_t seed=148;
   for(auto &v:long_input){seed=1664525*seed+1013904223;v=static_cast<unsigned char>(seed>>24);}
   auto packed=lzw_encode(long_input,{});unsigned calls=0;ExecutionControl mid;
   mid.checkpoint=[&]{if(++calls==2)throw OperationCancelled();};rejects([&]{lzw_encode(long_input,mid);});check(calls==2,"Encoder did not cancel during bounded work");
   calls=0;std::vector<unsigned char> destination(long_input.size());rejects([&]{lzw_decode(packed,destination,mid);});check(calls==2,"Decoder did not cancel during bounded work");

 }
 Image zeros(128,128,1,PixelType::UInt16);auto data=Dataset::from_planes({1,1,1,128,128,1},{zeros});ImageWriteRequest zip;zip.format=ImageFileFormat::ScientificTiffU16LzwV1;
 write_image_series((out/"small.tif").u8string(),data,{},zip);write_image_series((out/"raw.tif").u8string(),data);
 check(std::filesystem::file_size(out/"small.tif")*4<std::filesystem::file_size(out/"raw.tif"),"Padding negated compression");
 std::cout<<checks<<" Lzw TIFF checks passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
