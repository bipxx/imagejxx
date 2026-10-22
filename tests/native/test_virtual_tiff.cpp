#include <imagejxx/virtual_io.hpp>
#include <filesystem>
#include <future>
#include <iostream>
using namespace imagejxx;
void check(bool b){if(!b)throw std::runtime_error("Virtual TIFF contract failed");}
int main(int argc,char **argv){try{
  check(argc==2);namespace fs=std::filesystem;auto root=fs::absolute(fs::u8path(argv[1]));check(!fs::exists(root));fs::create_directories(root);
  Stack images;for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<3;++z){Image image(5,4,1,PixelType::UInt16);for(int i=0;i<20;++i)image.pixels[i]=1000*t+100*c+10*z+i;images.push_back(std::move(image));}
  auto original=Dataset::from_planes({2,2,3,4,5,1},std::move(images));ImageWriteRequest request;request.format=ImageFileFormat::ScientificTiffU16LzwHorizontalTiledV1;
  const auto path=root/"virtual.tif";write_image_series(path.u8string(),original,{},request);
  auto opened=open_virtual_image_series(path.u8string());check(!opened.dataset.has_eager_planes() && opened.dataset.shape().t==2);
  std::vector<std::future<void>> reads;
  for(int i=0;i<16;++i)reads.push_back(std::async(std::launch::async,[&,i]{auto p=opened.dataset.region({i%2,(i/2)%2,(i/4)%3},1,1,3,2);check(p.at(2,1)==1000*(i%2)+100*((i/2)%2)+10*((i/4)%3)+13);}));
  for(auto &read:reads)read.get();
  auto xz=opened.dataset.extract_plane(Plane::XZ,2,1,1);check(xz.at(4,2)==1134);
  auto yz=opened.dataset.extract_plane(Plane::YZ,3,1,1);check(yz.at(3,1)==1128);
  auto held=opened.dataset.plane();fs::rename(path,root/"old.tif");fs::copy_file(root/"old.tif",path);
  bool failed=false;try{opened.dataset.plane();}catch(const std::exception &){failed=true;}check(failed && held->at(4,3)==19);
  std::cout<<"PASS guarded virtual TIFF concurrent regions and scientific axes\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
