// Public installed-SDK C++17 consumer. Pixel-grid compatibility and the owned
// nominal world affine are separate from ImageJ's copied scalar offsets.
#include <imagejxx/application.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using namespace imagejxx;
using Clock=std::chrono::steady_clock;
const auto started=Clock::now();
std::size_t row_count=0,output_bytes=0;
constexpr std::size_t expected_rows=16,maximum_output=1024*1024;
void require(bool value,const char *message) {
  if(!value)throw std::runtime_error(message);
}
void deadline() {
  require(Clock::now()-started<std::chrono::seconds(30),"SDK consumer exceeded 30 seconds");
}
bool exact(double a,double b){return std::memcmp(&a,&b,sizeof(double))==0;}
std::string quote(const std::string &value) {
  constexpr char digits[]="0123456789abcdef";std::string result="\"";
  for(unsigned char c:value) {
    if(c=='"'||c=='\\'){result+='\\';result+=char(c);}
    else if(c<32){result+="\\u00";result+=digits[c>>4];result+=digits[c&15];}
    else result+=char(c);
  }
  return result+'"';
}
std::string number(double value) {
  require(std::isfinite(value),"Nonfinite observation metadata");
  std::ostringstream stream;stream<<std::setprecision(17)<<value;return stream.str();
}
template<class Values>std::string array(const Values &values) {
  std::string result="[";bool first=true;
  for(const auto &value:values) {
    if(!first)result+=',';first=false;
    if constexpr(std::is_integral_v<std::decay_t<decltype(value)>>)result+=std::to_string(value);
    else result+=number(value);
  }
  return result+']';
}
std::string bits(double value) {
  static_assert(sizeof(double)==8&&std::numeric_limits<double>::is_iec559,"Requires IEEE binary64");
  std::uint64_t word=0;std::memcpy(&word,&value,sizeof(word));
  constexpr char digits[]="0123456789abcdef";std::string result;
  for(int b=0;b<8;++b){const auto byte=unsigned((word>>(8*b))&255u);result+=digits[byte>>4];result+=digits[byte&15];}
  return result;
}
template<class Values>std::string bit_array(const Values &values) {
  std::string result="[";bool first=true;
  for(double value:values){if(!first)result+=',';first=false;result+=quote(bits(value));}
  return result+']';
}
const char *type_name(PixelType type) {
  switch(type){case PixelType::UInt8:return "UInt8";case PixelType::UInt16:return "UInt16";
    case PixelType::Float32:return "Float32";case PixelType::Float64:return "Float64";}
  throw std::runtime_error("Unknown public pixel type");
}
const char *branch_name(AxisRangeSlicerResizeBranch branch) {
  switch(branch){case AxisRangeSlicerResizeBranch::NoResize:return "no_resize";
    case AxisRangeSlicerResizeBranch::EqualDimensionCrop:return "equal_dimension_crop";
    case AxisRangeSlicerResizeBranch::Bilinear:return "bilinear_resize";}
  throw std::runtime_error("Unknown public resize branch");
}
std::string stored_hex(const Dataset &data) {
  std::string result;const auto shape=data.shape();
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z) {
    deadline();for(double value:data.plane({t,c,z})->pixels)result+=bits(value);
  }
  return result;
}
std::string dataset_fields(const Dataset &data) {
  const auto &cal=data.calibration();
  return "\"shape_tczyxs\":"+array(data.shape().dimensions())+",\"pixel_type\":"+quote(type_name(data.pixel_type()))+
    ",\"stored_encoding\":\"Float64LE\",\"stored_raw_hex\":"+quote(stored_hex(data))+
    ",\"spacing\":"+array(cal.spacing)+",\"origin\":"+array(cal.origin)+",\"direction\":"+array(cal.direction)+
    ",\"spacing_bits_le\":"+bit_array(cal.spacing)+",\"origin_bits_le\":"+bit_array(cal.origin)+
    ",\"direction_bits_le\":"+bit_array(cal.direction)+",\"unit\":"+quote(cal.unit)+
    ",\"time_origin\":"+number(cal.time_origin)+",\"frame_interval\":"+number(cal.frame_interval);
}
void emit(const std::string &id,const std::string &fields) {
  deadline();const auto line="{\"case_id\":"+quote(id)+','+fields+"}\n";
  ++row_count;output_bytes+=line.size();
  require(row_count<=expected_rows&&output_bytes<=maximum_output,"SDK output inventory or byte budget exceeded");
  std::cout<<line;require(bool(std::cout),"Could not write SDK observations");
}
Dataset literal_source(PixelType type) {
  SpatialCalibration cal;cal.spacing={1,2.25,4};cal.unit="um";
  Stack stack;
  for(int z=0;z<2;++z) {
    Image image(2,2,1,type);const auto value=(type==PixelType::UInt16?32768.:0.)+2*z;
    for(auto &pixel:image.pixels)pixel=value;stack.push_back(std::move(image));
  }
  return Dataset::from_stack(std::move(stack),cal);
}
AxisRangeSlicerResizeRequest request(Plane plane=Plane::XZ) {
  AxisRangeSlicerResizeRequest value;value.orientation.range.plane=plane;return value;
}
void mapping_rows() {
  for(PixelType type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float32})
    for(Plane plane:{Plane::XZ,Plane::YZ})for(bool transpose:{false,true}) {
      const auto input=literal_source(type);auto req=request(plane);req.orientation.transpose=transpose;
      const auto estimate=estimate_axis_range_slicer_resize(input,req);
      const auto result=reslice_slicer_axis_range(input,req);const auto image=result.dataset.plane();
      const auto observed=transpose?image->at(2,0):image->at(0,2);
      const auto expected=type==PixelType::UInt16?32769.:type==PixelType::UInt8?1.:.5;
      require(exact(observed,expected),"Literal quarter sample or unsigned half-up rounding differs");
      require(result.resize_branch==AxisRangeSlicerResizeBranch::Bilinear&&result.vertical_count==8&&
              estimate.width==image->width&&estimate.height==image->height&&estimate.working_bytes==result.working_bytes,
              "Public estimate/result contract differs");
      require(exact(result.processor_vertical_origin,-.25)&&exact(result.processor_vertical_step,.25)&&
              exact(result.dataset.calibration().origin[2],-1.)&&result.dataset.pixel_type()==type,
              "Nominal unclamped origin, processor grid or type differs");
      const auto scalar=plane==Plane::XZ?std::array<double,3>{1,1,2.25}:std::array<double,3>{2.25,1,1};
      auto expected_scalar=scalar;if(transpose)std::swap(expected_scalar[0],expected_scalar[1]);
      require(result.scalar_spacing==expected_scalar,"Separate positive scalar metric differs");
      const auto id=std::string("sdk.literal.")+type_name(type)+(plane==Plane::XZ?".xz":".yz")+(transpose?".transpose1":".transpose0");
      emit(id,dataset_fields(result.dataset)+",\"branch\":"+quote(branch_name(result.resize_branch))+
        ",\"processor_vertical_origin\":"+number(result.processor_vertical_origin)+
        ",\"processor_vertical_step\":"+number(result.processor_vertical_step)+
        ",\"z_spacing_ratio\":"+number(result.z_spacing_ratio)+",\"scalar_spacing\":"+array(result.scalar_spacing)+
        ",\"working_bytes\":"+std::to_string(result.working_bytes)+",\"quarter_sample\":"+number(observed));
    }
}
bool retained(const Document &owner,const DocumentSnapshot &before,const HistoryInfo &history) {
  const auto after=owner.snapshot();return after.same_document(before)&&after.shares_data(before)&&
    after.data_identity()==before.data_identity()&&after.revisions().matches(before.revisions(),Change::All)&&
    after.dirty()==before.dirty()&&owner.history().undo_steps==history.undo_steps&&owner.history().redo_steps==history.redo_steps;
}
void publication_row() {
  DocumentData data{literal_source(PixelType::Float32),{"SDK source",{{"retained",true}}},{},{}};
  data.selection=Selection("point",{{1,0}});data.previous_selection=Selection("point",{{0,0}});
  data.rois={{"saved",Selection("point",{{0,0}}),0,0,0}};data.overlays={{{"marker",true}}};
  data.metadata.extensions["opaque"]=true;
  auto source=std::make_shared<Document>(std::move(data));
  auto edit=source->begin_edit();edit.set_property("history",true);source->commit(edit);source->undo();
  auto other=std::make_shared<Document>(literal_source(PixelType::UInt8),"Other");Application app;
  app.adopt(source);app.adopt(other);const auto before=source->snapshot(),other_before=other->snapshot();
  const auto history=source->history(),other_history=other->history();const auto before_ids=app.document_ids();
  auto req=request();req.orientation.range.last=1;req.orientation.reverse_order=req.orientation.flip_vertical=req.orientation.transpose=true;
  auto prepared=app.prepare_axis_range_slicer_resize(req,source->id);auto candidate=prepared.prepared_new_document();
  require(candidate&&!app.contains(candidate->id)&&app.document_ids()==before_ids&&app.active_document_id()==other->id,
          "Preparation changed application membership/focus");
  const auto fresh=candidate->snapshot();
  require(!fresh.dirty()&&fresh.data_identity()==0&&candidate->history().undo_steps==0&&candidate->history().redo_steps==0&&
          !fresh.data().selection&&!fresh.data().previous_selection&&fresh.data().rois.empty()&&fresh.data().overlays.empty()&&
          fresh.data().metadata.properties.empty()&&fresh.data().metadata.extensions.size()==1&&
          prepared.parameters().size()==35,"Fresh candidate retained old annotations/history");
  const auto output=app.publish(prepared);require(output.created&&output.document==candidate&&output.document_id==candidate->id&&
    app.active_document_id()==candidate->id&&retained(*source,before,history)&&retained(*other,other_before,other_history),
    "Exact fresh-owner publication or retained owners differ");
  const auto ids=app.document_ids();std::string repeat;
  try{app.publish(prepared);}catch(const std::invalid_argument &error){repeat=error.what();}
  require(!repeat.empty()&&app.document_ids()==ids&&app.active_document_id()==candidate->id,"Repeat publication changed state");
  emit("sdk.publication.fresh_owner",dataset_fields(output.document->snapshot().data().dataset)+
    ",\"source_document_id\":"+std::to_string(source->id)+",\"output_document_id\":"+std::to_string(output.document_id)+
    ",\"active_document_id\":"+std::to_string(app.active_document_id())+",\"document_ids\":"+array(ids)+
    ",\"source_revisions_before\":"+array(std::array<std::uint64_t,4>{before.revisions().pixels,before.revisions().metadata,before.revisions().selection,before.revisions().display})+
    ",\"source_revisions_after\":"+array(std::array<std::uint64_t,4>{source->snapshot().revisions().pixels,source->snapshot().revisions().metadata,source->snapshot().revisions().selection,source->snapshot().revisions().display})+
    ",\"output_dirty\":"+(output.document->snapshot().dirty()?"true":"false")+
    ",\"output_undo_steps\":"+std::to_string(output.document->history().undo_steps)+
    ",\"output_redo_steps\":"+std::to_string(output.document->history().redo_steps)+",\"repeat_exception\":"+quote(repeat));
}
class CountedSource final:public PlaneSource {
public:
  PixelType type;double value;mutable std::size_t reads=0;
  CountedSource(PixelType t,double v):type(t),value(v){}
  DatasetShape shape()const override{return {1,1,2,2,2,1};}
  PixelType pixel_type()const override{return type;}
  std::shared_ptr<const Image>read_plane(PlanePosition)const override{++reads;return std::make_shared<const Image>(image(2,2));}
  Image read_region(PlanePosition,int,int,int width,int height)const override{++reads;return image(width,height);}
private:
  Image image(int width,int height)const{Image result(width,height,1,type);for(auto &pixel:result.pixels)pixel=value;return result;}
};
void refusal_rows() {
  for(int scenario=0;scenario<3;++scenario) {
    const auto type=scenario==0?PixelType::Float64:scenario==1?PixelType::UInt8:PixelType::Float32;
    auto provider=std::make_shared<CountedSource>(type,scenario==1?.5:1.);Dataset input(provider);std::string message;
    const auto req=request();ExecutionControl control;auto token=std::make_shared<CancellationToken>();
    std::size_t callbacks=0;control.checkpoint=[&]{++callbacks;};
    if(scenario==2){token->cancel();control.cancellation=token;}
    if(scenario==0) {
      try{reslice_slicer_axis_range(input,req,control);}catch(const std::invalid_argument &error){message=error.what();}
      require(!message.empty()&&provider->reads==0&&callbacks==0,"Metadata admission failed to precede reads/callbacks");
    } else if(scenario==1) {
      const auto estimate=estimate_axis_range_slicer_resize(input,req);
      require(estimate.resize_branch==AxisRangeSlicerResizeBranch::NoResize&&provider->reads==0,
              "Metadata estimate read pixels or claimed source canonicality");
      try{reslice_slicer_axis_range(input,req,control);}catch(const std::invalid_argument &error){message=error.what();}
      require(!message.empty()&&provider->reads>0,"Noncanonical lazy values were not validated during processing");
    } else {
      try{reslice_slicer_axis_range(input,req,control);}catch(const OperationCancelled &error){message=error.what();}
      require(!message.empty()&&provider->reads==0,"Pre-canceled request read source values");
    }
    const auto id=scenario==0?"sdk.refusal.metadata_type":scenario==1?"sdk.refusal.lazy_canonicality":"sdk.refusal.precanceled";
    emit(id,"\"source_shape_tczyxs\":"+array(input.shape().dimensions())+",\"source_pixel_type\":"+quote(type_name(type))+
      ",\"source_literal_value\":"+number(provider->value)+",\"provider_reads\":"+std::to_string(provider->reads)+
      ",\"checkpoint_calls\":"+std::to_string(callbacks)+",\"exception_category\":"+quote(scenario==2?"OperationCancelled":"std::invalid_argument")+
      ",\"exception_message\":"+quote(message));
  }
}
}
int main(int argc,char **) {
  try {
    require(argc==1,"This SDK consumer has no arguments");mapping_rows();publication_row();refusal_rows();
    require(row_count==expected_rows,"Incomplete SDK consumer inventory");return 0;
  }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
