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
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception &){failed=true;}check(failed,"Expected PackBits refusal");}
ImageFileFormat format(const std::string &dtype,bool zip=true){
 if(dtype=="u8")return zip?ImageFileFormat::ScientificTiffU8PackBitsV1:ImageFileFormat::ScientificTiffU8V1;
 if(dtype=="u16")return zip?ImageFileFormat::ScientificTiffU16PackBitsV1:ImageFileFormat::ScientificTiffU16V1;
 if(dtype=="rgb8")return zip?ImageFileFormat::ScientificTiffRgb8PackBitsV1:ImageFileFormat::ScientificTiffRgb8V1;
 return zip?ImageFileFormat::ScientificTiffFloat32PackBitsV1:ImageFileFormat::ScientificTiffFloat32V1;
}
void expect(const Dataset &data,const std::string &hex){
 const auto shape=data.shape();std::size_t at=0;const auto size=data.pixel_type()==PixelType::Float32?4:data.pixel_type()==PixelType::UInt16?2:1;
 for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z){
  auto p=data.plane({t,c,z});for(auto v:p->pixels){
   const auto word=data.pixel_type()==PixelType::Float32?float32_source_sample_bits(v,data.sample_policy()):std::uint32_t(v);
   for(int b=0;b<size;++b){check(((word>>(b*8))&255)==std::stoul(hex.substr(at,2),nullptr,16),"PackBits changed sample word");at+=2;}
  }
 }
 check(at==hex.size(),"PackBits sample count changed");
}
}
int main(int argc,char **argv){try{
 check(argc==3,"Supply fixtures/output");std::filesystem::path root=argv[1],out=argv[2];std::filesystem::create_directories(out.parent_path());check(std::filesystem::create_directory(out),"Fresh output required");
 std::ifstream f(root/"SOURCE.json");auto manifest=nlohmann::json::parse(f);std::size_t valid=0,bad=0;
 ImageReadRequest read;read.sample_policy=FloatingSamplePolicy::PreserveIEEEFloat32;
 for(const auto &row:manifest.at("files")){
  const auto path=(root/row.at("file").get<std::string>()).u8string();
  if(!row.at("valid").get<bool>()){
   ++bad;if(row.at("descriptor_valid").get<bool>())check(inspect_image_file(path).series.size()==1,"Payload inspection decoded samples");else rejects([&]{inspect_image_file(path);});
   rejects([&]{read_image_series(path);});continue;
  }
  ++valid;const auto dtype=row.at("dtype").get<std::string>();auto request=read;if(dtype!="f32")request.sample_policy=FloatingSamplePolicy::FiniteOnly;
  auto info=inspect_image_file(path);check(info.format==format(dtype),"Wrong PackBits format");
  auto result=read_image_series(path,request);const auto hex=row.at("hex").get<std::string>();expect(result.dataset,hex);
  check(result.metadata==info.series[0].metadata,"PackBits metadata changed");
  check(result.dataset.calibration().spacing==std::array<double,3>{.5,1.25,2.75},"PackBits calibration changed");
  if(dtype=="f32")rejects([&]{read_image_series(path);});
  DatasetReadObserver observer;auto observed=result.dataset.observe_reads(observer);ImageWriteRequest write;write.format=format(dtype);
  auto estimate=estimate_image_write(observed,result.metadata,write);check(observer.counts().plane_reads==0,"PackBits planning read samples");
  const auto target=(out/row.at("file").get<std::string>()).u8string();write_image_series(target,observed,result.metadata,write);
  check(std::filesystem::file_size(target)<=estimate.encoded_upper_bound,"PackBits exceeded estimate");expect(read_image_series(target,request).dataset,hex);
  rejects([&]{write_image_series(target,observed,{},write);});write.replace=FileReplacePolicy::ReplaceExisting;write_image_series(target,observed,{},write);expect(read_image_series(target,request).dataset,hex);
  write.format=format(dtype,false);write_image_series((out/(row.at("file").get<std::string>()+"-raw.tif")).u8string(),observed,result.metadata,write);
 }
 check(valid==24 && bad==8,"PackBits corpus changed");
 { using namespace io_detail;
   std::vector<unsigned char> output(131),encoded{0,42,129,7,128,1,1,2};packbits_decode(encoded,output,{});
   check(output[0]==42 && output[129]==1 && output[130]==2,"Packet boundary vector changed");
   for(std::size_t i=1;i<129;++i)check(output[i]==7,"Repeat packet changed");
   for(std::size_t width:{1u,2u,3u,127u,128u,129u,255u,256u,257u})for(unsigned pattern=0;pattern<4;++pattern) {
     std::vector<unsigned char> raw(width*3);
     for(std::size_t i=0;i<raw.size();++i)raw[i]=static_cast<unsigned char>(pattern==0?7:pattern==1?i:pattern==2?i/2:i/3);
     auto packed=packbits_encode(raw,width,{});std::vector<unsigned char> restored(raw.size());packbits_decode(packed,restored,{});
     check(restored==raw && packed.size()<=packbits_bound(width,3),"Codec boundary roundtrip/bound changed");
   }
   std::vector<unsigned char> row(256,99);check(packbits_encode(row,128,{})==std::vector<unsigned char>({129,99,129,99}),"Encoder crossed row boundary");
   for(auto bytes:std::vector<std::vector<unsigned char>>{{0},{129},{0,1},{128}})rejects([&]{std::vector<unsigned char> dst(256);packbits_decode(bytes,dst,{});});
   ExecutionControl control;control.checkpoint=[] {throw OperationCancelled();};rejects([&]{packbits_encode(row,128,control);});
 }

 Image zeros(128,128,1,PixelType::UInt16);auto data=Dataset::from_planes({1,1,1,128,128,1},{zeros});ImageWriteRequest zip;zip.format=ImageFileFormat::ScientificTiffU16PackBitsV1;
 write_image_series((out/"small.tif").u8string(),data,{},zip);write_image_series((out/"raw.tif").u8string(),data);
 check(std::filesystem::file_size(out/"small.tif")*4<std::filesystem::file_size(out/"raw.tif"),"Padding negated compression");
 std::cout<<checks<<" PackBits TIFF checks passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
