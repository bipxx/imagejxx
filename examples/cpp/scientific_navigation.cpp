// Public installed-SDK scientific read/navigation consumer; no GUI dependency.
#include <imagejxx/session.hpp>

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace imagejxx;
std::size_t checks=0;
void require(bool value,const char *message) {
  if(!value)throw std::runtime_error(message);
  ++checks;
}
double literal(int t,int c,int z,int y,int x,int s,int samples) {
  return samples==1 ? 10000*t+1000*c+100*z+10*y+x : 73*t+41*c+17*z+7*y+3*x+s;
}
SpatialCalibration calibration() {
  SpatialCalibration result;
  result.spacing={.25,1.5,3};result.origin={-7,11,-13};
  result.direction={-1,.5,0,0,1,.25,0,0,1};
  result.unit="um";result.time_unit="ms";
  result.frame_interval=.75;result.time_origin=2.5;
  return result;
}
Dataset dataset(DatasetShape shape,PixelType type) {
  Stack planes;
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z) {
    Image image(shape.x,shape.y,shape.s,type);
    for(int y=0;y<shape.y;++y)for(int x=0;x<shape.x;++x)for(int s=0;s<shape.s;++s)
      image.at(x,y,s)=literal(t,c,z,y,x,s,shape.s);
    planes.push_back(std::move(image));
  }
  return Dataset::from_planes(shape,std::move(planes),calibration());
}
void read(const Dataset &source,Plane plane,int fixed,int t,int c) {
  const auto shape=source.shape();
  const auto actual=source.extract_plane(plane,fixed,t,c);
  const int width=plane==Plane::YZ?shape.y:shape.x;
  const int height=plane==Plane::XY?shape.y:shape.z;
  require(actual.width==width && actual.height==height && actual.channels==shape.s &&
          actual.type==source.pixel_type(),"SDK plane layout/type");
  for(int v=0;v<height;++v)for(int u=0;u<width;++u)for(int s=0;s<shape.s;++s) {
    const int x=plane==Plane::YZ?fixed:u;
    const int y=plane==Plane::XY?v:plane==Plane::XZ?fixed:u;
    const int z=plane==Plane::XY?fixed:v;
    require(actual.at(u,v,s)==literal(t,c,z,y,x,s,shape.s),"SDK literal plane sample");
  }
}
void inspect(DatasetShape shape,PixelType type,bool session) {
  Document owner(dataset(shape,type),"scientific navigation SDK");
  const auto before=owner.snapshot();
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c) {
    auto display=owner.snapshot().display();
    display.t=t;display.c=c;display.crosshair={shape.x-1,shape.y-1,shape.z-1};
    display.orthogonal=true;display.linkedZoom=false;
    display.zoom=2.5;display.planeZoom={1.25,0,4};display.pan={{{3,-4},{-.5,.25},{0,-2}}};
    owner.set_display(display);
    const auto snapshot=owner.snapshot();
    require(snapshot.shares_data(before) && snapshot.data_identity()==before.data_identity() &&
            snapshot.revisions().pixels==0 && snapshot.revisions().metadata==0 &&
            snapshot.revisions().selection==0 && !snapshot.dirty() && owner.undo_steps()==0 &&
            owner.redo_steps()==0,"SDK navigation changed data/history");
    require(snapshot.data().dataset.plane({t,c,shape.z-1})==before.data().dataset.plane({t,c,shape.z-1}),
            "SDK navigation copied immutable XY backing");
    read(snapshot.data().dataset,Plane::XY,shape.z-1,t,c);
    read(snapshot.data().dataset,Plane::XZ,shape.y-1,t,c);
    read(snapshot.data().dataset,Plane::YZ,shape.x-1,t,c);
  }
  require(owner.snapshot().data().dataset.calibration().time(shape.t-1)==2.5+.75*(shape.t-1),
          "SDK calibrated time position");
  if(shape.x==7 && shape.y==5 && shape.z==4)
    require(owner.snapshot().data().dataset.calibration().voxel_to_world({6,4,3})==
            std::array<double,3>{-5.5,19.25,-4},"SDK calibrated world position");
  if(session) {
    SessionLimits limits;limits.max_documents=4;
    limits.max_json_bytes=limits.max_encoded_bytes=32*1024*1024;
    limits.max_working_bytes=128*1024*1024;
    const auto captured=owner.snapshot();
    const auto restored=decode_session(encode_session({{captured},{},{},{}},limits),limits);
    const auto loaded=restored.documents.at(0)->snapshot();
    require(loaded.data().dataset.shape().dimensions()==shape.dimensions() &&
            loaded.display().t==shape.t-1 && loaded.display().c==shape.c-1 &&
            loaded.display().crosshair==captured.display().crosshair &&
            loaded.display().planeZoom==captured.display().planeZoom && loaded.display().pan==captured.display().pan &&
            loaded.display().orthogonal && !loaded.display().linkedZoom,"SDK session position/cameras");
    require(loaded.id()!=captured.id() && !loaded.dirty() && restored.documents[0]->undo_steps()==0,
            "SDK reopen fresh clean owner");
    read(loaded.data().dataset,Plane::XY,shape.z-1,shape.t-1,shape.c-1);
  }
}
} // namespace

int main() {
  try {
    for(const auto type:{PixelType::UInt16,PixelType::Float32,PixelType::Float64})
      inspect({2,3,4,5,7,1},type,true);
    for(int samples:{3,4})inspect({2,3,4,5,7,samples},PixelType::UInt8,true);
    for(const auto shape:{DatasetShape{1,3,1,1,7,1},DatasetShape{2,1,4,5,1,1},DatasetShape{1,1,1,1,1,1}})
      inspect(shape,PixelType::UInt16,false);
    std::cout<<"PASS scientific navigation SDK checks="<<checks<<'\n';return 0;
  }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
