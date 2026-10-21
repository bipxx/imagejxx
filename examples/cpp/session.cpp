// Installed-SDK consumer: public headers only; no JSON, zlib, Python or GUI.
#include <imagejxx/session.hpp>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char *message) {
  if(!value)throw std::runtime_error(message);
}
std::uint64_t bits(double value) {
  std::uint64_t result;std::memcpy(&result,&value,sizeof(result));return result;
}
const imagejxx::MetadataObject &object(const imagejxx::MetadataValue &value) {
  return std::get<imagejxx::MetadataObject>(
      std::get<std::shared_ptr<const imagejxx::MetadataTree>>(value)->value);
}
}
int main(int argc,char **argv) {
  try {
    using namespace imagejxx;
    const DatasetShape shape{2,3,4,5,7,1};
    Stack planes;
    for(int t=0;t<2;++t)for(int c=0;c<3;++c)for(int z=0;z<4;++z) {
      Image image(7,5,1,PixelType::UInt16);
      for(int y=0;y<5;++y)for(int x=0;x<7;++x)
        image.at(x,y)=10000*t+1000*c+100*z+10*y+x;
      planes.push_back(std::move(image));
    }
    SpatialCalibration calibration;
    calibration.spacing={.25,1.5,3};calibration.origin={-7,11,-13};
    calibration.direction={-1,.5,-0.0,0,1,.25,0,0,1};
    calibration.unit="um";calibration.frame_interval=.75;calibration.time_origin=2.5;
    Document owner(Dataset::from_planes(shape,std::move(planes),calibration),"scientific SDK");
    auto edit=owner.begin_edit();
    const Selection rectangle("rectangle",{{1,1},{4,3}});
    edit.set_rois({Roi{"unpositioned",rectangle,{},{},{},{}},Roi{"last",rectangle,1,2,3,{}}});
    edit.set_property("negative_zero",-0.0);owner.commit(edit);
    auto display=owner.snapshot().display();display.t=1;display.c=2;display.crosshair={6,4,3};
    display.properties["lut"]=std::string("Fire");
    const auto channel_display=metadata_object({{"version",std::int64_t(1)},{"negative_zero",-0.0},
        {"opaque_payload",metadata_array({std::monostate{},true,std::string("retained")})}});
    display.properties["channel_display"]=channel_display;owner.set_display(display);
    const auto captured=owner.snapshot();
    SessionCapture capture{{captured},{metadata_object({{"value",std::int64_t(7)}})}, {},
                            {{"opaque_future_state",metadata_object({{"version",std::int64_t(1)}})}}};
    SessionLimits limits;limits.max_documents=16;limits.max_json_bytes=32*1024*1024;
    limits.max_encoded_bytes=32*1024*1024;limits.max_working_bytes=128*1024*1024;
    const auto estimate=estimate_session(capture,limits);
    require(estimate.documents==1 && estimate.planes==24 && estimate.samples==840,"SDK estimate axes");
    const auto bytes=encode_session(capture,limits);
    const auto restored=decode_session(bytes,limits);
    require(restored.source_version==3 && restored.documents.size()==1,"SDK decoded cardinality");
    const auto snapshot=restored.documents[0]->snapshot();const auto &data=snapshot.data();
    require(data.dataset.shape().dimensions()==shape.dimensions() && data.dataset.pixel_type()==PixelType::UInt16,
            "SDK scientific layout");
    for(int t=0;t<2;++t)for(int c=0;c<3;++c)for(int z=0;z<4;++z)
      for(int y=0;y<5;++y)for(int x=0;x<7;++x)
        require(data.dataset.plane({t,c,z})->at(x,y)==10000*t+1000*c+100*z+10*y+x,"SDK exact axis sample");
    require(data.dataset.plane({1,2,3})->at(6,4)==12346,"SDK literal final sample");
    require(data.dataset.calibration().voxel_to_world({6,4,3})==std::array<double,3>{-5.5,19.25,-4} &&
            data.dataset.calibration().time(1)==3.25,"SDK scientific calibration");
    require(data.rois.size()==2 && !data.rois[0].t && !data.rois[0].c && !data.rois[0].z &&
            data.rois[1].t==1 && data.rois[1].c==2 && data.rois[1].z==3,"SDK positioned ROIs");
    require(snapshot.display().t==1 && snapshot.display().c==2 && snapshot.display().crosshair[2]==3,
            "SDK display T/C/Z");
    const auto &loaded_channel=snapshot.display().properties.at("channel_display");
    require(metadata_equal(loaded_channel,channel_display) &&
            bits(std::get<double>(object(loaded_channel).at("negative_zero")))==0x8000000000000000ULL,
            "SDK opaque version1 document channel-display state");
    require(bits(std::get<double>(data.metadata.properties.at("negative_zero")))==0x8000000000000000ULL,
            "SDK typed metadata signed zero");
    require(snapshot.id()!=captured.id() && !snapshot.dirty() && restored.documents[0]->undo_steps()==0 &&
            restored.documents[0]->redo_steps()==0 && snapshot.revisions().pixels==0,"SDK fresh clean identity");
    // Finite Float32-tagged native backing may be more precise than float32.
    Image exact(3,1,1,PixelType::Float32);exact.pixels={1+std::ldexp(1.0,-40),-0.0,.125};
    Document precise(Dataset::from_stack({exact}));
    auto future_display=precise.snapshot().display();
    const auto future_channel=metadata_object({{"version",std::int64_t(37)},{"negative_zero",-0.0},
        {"opaque_payload",metadata_object({{"vendor",std::string("unknown version")},{"enabled",false}})}});
    future_display.properties["channel_display"]=future_channel;precise.set_display(future_display);
    const auto backing=decode_session(encode_session({{precise.snapshot()}, {}, {}, {}},limits),limits);
    const auto plane=backing.documents[0]->snapshot().data().dataset.plane();
    require(bits(plane->pixels[0])==bits(1+std::ldexp(1.0,-40)) &&
            bits(plane->pixels[1])==0x8000000000000000ULL,"SDK noncanonical backing bits");
    const auto future_snapshot=backing.documents[0]->snapshot();
    const auto &future_loaded=future_snapshot.display().properties.at("channel_display");
    require(metadata_equal(future_loaded,future_channel) &&
            bits(std::get<double>(object(future_loaded).at("negative_zero")))==0x8000000000000000ULL,
            "SDK opaque future-version document channel-display state");
    if(argc==2) {
      save_session(argv[1],capture,limits);
      const auto file=load_session(argv[1],limits);
      require(file.documents[0]->snapshot().data().dataset.plane({1,2,3})->at(6,4)==12346,"SDK file publication");
    }
    require(owner.snapshot().shares_data(captured) && owner.snapshot().dirty(),"SDK serialization source isolation");
    std::cout<<"PASS session SDK: TCZYXS, 24 planes, 840 exact samples, calibration, ROIs, native bits\n";
    return 0;
  }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
