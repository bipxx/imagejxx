#include <imagejxx/application.hpp>
#include <imagejxx/operations.hpp>
#include <imagejxx/rank_command.hpp>
#include <imagejxx/session.hpp>
#include <nlohmann/json.hpp>
#include "operation_internal.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace imagejxx;
using Json=nlohmann::json;
namespace {
constexpr auto ieee=FloatingSamplePolicy::PreserveIEEEFloat32;
constexpr auto quiet=FloatingSamplePolicy::PreserveQuietNaNFloat32;
std::size_t checks=0;
void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument&){caught=true;}check(caught,"Invalid IEEE source input admitted");}
std::uint64_t bits(double v){std::uint64_t w;std::memcpy(&w,&v,8);return w;}
double sample(std::uint64_t w){double v;std::memcpy(&v,&w,8);return v;}
std::uint64_t word(const std::string &raw,std::size_t i,int bytes){std::uint64_t v=0;for(int b=0;b<bytes;++b)v|=std::stoull(raw.substr((i*bytes+b)*2,2),nullptr,16)<<(8*b);return v;}
std::vector<double> values;
std::vector<std::uint32_t> expected;
Dataset fixture(){Stack planes;for(int p=0;p<8;++p){Image image(32,16,1,PixelType::Float32);image.pixels.assign(values.begin()+p*512,values.begin()+(p+1)*512);planes.push_back(std::move(image));}return Dataset::from_planes({2,2,2,16,32,1},std::move(planes),{},ieee);}
void exact(const Dataset &data){check(data.sample_policy()==ieee,"Source policy changed");for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<2;++z){auto image=data.plane({t,c,z});const auto start=((t*2+c)*2+z)*512;for(int i=0;i<512;++i)check(bits(image->pixels[i])==bits(values[start+i]),"Native source backing bits changed");}}
void load(const char*path){std::ifstream input(path);check(bool(input),"Missing independent source corpus");auto data=Json::parse(input);check(data.at("pattern_count")==4096,"Source corpus count changed");const std::string a=data.at("binary32_le"),b=data.at("binary64_le");for(std::size_t i=0;i<4096;++i){expected.push_back(std::uint32_t(word(a,i,4)));values.push_back(sample(word(b,i,8)));}}
void transport(){
 for(std::size_t i=0;i<values.size();++i){const auto w=expected[i];
  check(bits(float32_source_sample(w,ieee))==bits(values[i]),"Decoder differs from independent binary64 layout");
  check(float32_source_sample_bits(values[i],ieee)==w,"Encoder changed binary32 sign/payload");
  if((w&0x7f800000u)==0x7f800000u){
   rejects([&]{float32_source_sample(w);});rejects([&]{float32_source_sample_bits(values[i]);});
   if((w&0x00400000u)==0){rejects([&]{float32_source_sample(w,quiet);});rejects([&]{float32_source_sample_bits(values[i],quiet);});}
   else check(float32_source_sample_bits(float32_source_sample(w,quiet),quiet)==w,"Quiet policy changed");
  }
 }
 rejects([]{float32_source_sample(0,static_cast<FloatingSamplePolicy>(99));});
 rejects([]{float32_source_sample_bits(0.1,ieee);});
 rejects([]{float32_source_sample_bits(sample(0x7ff8000000000001ULL),ieee);});
 rejects([]{float32_source_sample_bits(sample(0x7ff0000000000001ULL),ieee);});
 rejects([]{float32_source_sample_bits(std::numeric_limits<double>::max(),ieee);});
}
void storage_and_geometry(){
 auto data=fixture();exact(data);check(data.shared_planes()->sample_policy()==ieee,"Shared table lost policy");
 const auto observed=data.observe_reads(DatasetReadObserver{});exact(observed);
 for(int t=0;t<2;++t)for(int c=0;c<2;++c){
  auto xz=data.extract_plane(Plane::XZ,3,t,c);auto yz=data.extract_plane(Plane::YZ,4,t,c);
  for(int z=0;z<2;++z){
   const auto base=((t*2+c)*2+z)*512;
   for(int x=0;x<32;++x)check(bits(xz.at(x,z))==bits(values[base+3*32+x]),"XZ extraction changed IEEE bits");
   for(int y=0;y<16;++y)check(bits(yz.at(y,z))==bits(values[base+y*32+4]),"YZ extraction changed IEEE bits");
  }
 }
 auto region=data.region({1,1,1},2,3,5,4);
 for(int y=0;y<4;++y)for(int x=0;x<5;++x)check(bits(region.at(x,y))==bits(values[7*512+(y+3)*32+x+2]),"Region changed IEEE bits");
 auto copied=Dataset::from_shared_planes(data.shape(),*data.shared_planes(),data.calibration());exact(copied);
 for(auto type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float64})rejects([&]{Dataset::from_stack({Image(2,2,1,type)},{},ieee);});
 rejects([]{Dataset::from_stack({Image(2,2,3,PixelType::Float32)},{},ieee);});
 auto malformed=*data.plane();malformed.pixels[0]=sample(0x7ff8000000000001ULL);rejects([&]{data.with_plane({},malformed);});
}
void history_and_admission(){
 auto data=fixture();Document owner(data);owner.mark_saved();const auto before=owner.snapshot();
 auto edit=owner.begin_edit();auto replacement=*data.plane();replacement.pixels[0]=float32_source_sample(0x7f800000u,ieee);
 edit.replace_plane({},replacement);edit.select(Selection("rectangle",{{1,1},{4,3}}));edit.set_property("ieee_marker",1);owner.commit(edit);
 check(owner.undo_steps()==1 && owner.snapshot().data().dataset.sample_policy()==ieee,"IEEE edit/history policy changed");
 check(bits(owner.snapshot().data().dataset.plane()->pixels[0])==0x7ff0000000000000ULL,"Edit quieted/changed infinity");
 owner.undo();exact(owner.snapshot().data().dataset);check(!owner.snapshot().dirty() && !owner.snapshot().data().selection,"Undo failed to restore IEEE source/selection");
 owner.redo();check(bits(owner.snapshot().data().dataset.plane()->pixels[0])==0x7ff0000000000000ULL,"Redo lost IEEE edit");
 DatasetReadObserver reads;Document unqualified(data.observe_reads(reads));auto target=resolve_target(unqualified.snapshot(),{},"invert");
 rejects([&]{validate_execution_parameters("invert",target,{},{});});
 rejects([&]{prepare_operation("invert",target);});
 check(std::string(resolve_remove_nans_command(unqualified.snapshot()).operation())=="remove_nans_imagej","Qualified IEEE Remove NaNs metadata admission failed");
 check(reads.counts().plane_reads==0,"Unqualified IEEE operation acquired pixels");
}
void sessions(const char*path){
 std::ifstream f(path,std::ios::binary);check(bool(f),"Missing independently framed V3 archive");std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),{});
 auto decoded=decode_session(bytes);check(decoded.source_version==3 && decoded.documents.size()==1,"Golden V3 IEEE source not decoded");exact(decoded.documents[0]->snapshot().data().dataset);
 check(decoded.documents[0]->undo_steps()==0 && !decoded.documents[0]->snapshot().dirty(),"Load manufactured a dirty edit");
 auto captured=SessionCapture{{decoded.documents[0]->snapshot()},{},{},{}};auto roundtrip=decode_session(encode_session(captured));exact(roundtrip.documents[0]->snapshot().data().dataset);
 Document owner(fixture());auto p=*owner.snapshot().data().dataset.plane();p.pixels[0]=float32_source_sample(0xff800001u,ieee);auto edit=owner.begin_edit();edit.replace_plane({},p);owner.commit(edit);
 auto loaded=decode_session(encode_session({{owner.snapshot()},{},{},{}}));check(bits(loaded.documents[0]->snapshot().data().dataset.plane()->pixels[0])==0xfff0000020000000ULL,"Edited signaling-NaN payload lost in session");
}
struct Descriptor:PlaneSource{mutable int reads=0;DatasetShape shape()const override{return {1,1,1,1,1,1};}PixelType pixel_type()const override{return PixelType::Float32;}std::shared_ptr<const Image>read_plane(PlanePosition)const override{++reads;throw std::runtime_error("Unexpected virtual source read");}};
void descriptor(){auto source=std::make_shared<Descriptor>();rejects([&]{Dataset(source,{},ieee);});check(source->reads==0,"Unreviewed IEEE source acquired pixels");}
}
int main(int argc,char**argv){try{check(argc==3,"Pass source corpus and golden session");load(argv[1]);transport();storage_and_geometry();history_and_admission();sessions(argv[2]);descriptor();std::cout<<checks<<" IEEE Float32 source checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
