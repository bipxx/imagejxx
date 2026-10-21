// Installed SDK consumer: only public headers and imagejxx::core are used.
#include <imagejxx/channel_display_command.hpp>
#include <imagejxx/session.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace imagejxx;
namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class Error=std::invalid_argument,class F>void rejects(F f){bool caught=false;try{f();}catch(const Error&){caught=true;}require(caught,"Expected SDK refusal");}
ChannelDisplayState palette(){
  ChannelDisplayState state;
  for(const auto &rgb:std::array<std::array<std::uint8_t,3>,3>{{{200,30,90},{80,150,60},{120,70,220}}}){
    ChannelDisplayChannel channel;channel.lut.name.clear();channel.lut.table.emplace();channel.lut.table->fill(rgb);state.channels.push_back(channel);
  }
  return state;
}
Dataset source(PixelType type){Stack planes;for(int t=0;t<2;++t)for(int c=0;c<3;++c)for(int z=0;z<2;++z){Image image(3,2,1,type);for(auto &v:image.pixels)v=40*t+30*c+10*z;planes.push_back(std::move(image));}return Dataset::from_planes({2,3,2,2,3,1},std::move(planes));}
void pixels(const Image &image,std::array<int,3> wanted){for(std::size_t i=0;i<image.pixels.size();++i)require(image.pixels[i]==wanted[i%3],"SDK literal mapped components differ");}
ChannelDisplayTable minTable(int name){
  ChannelDisplayTable table{};
  for(int i=0;i<256;++i){
    std::array<int,3> rgb{};
    switch(name){
    case 0:rgb={i,0,0};break;
    case 1:rgb={i,i,0};break;
    case 2:rgb={0,i,i};break;
    case 3:rgb={255-i,255,255};break;
    case 4:rgb={255-i,255-i,255};break;
    case 5:rgb={255,255-i,255-i};break;
    case 6:rgb={(i*i+41)%256,(17*i+5)%256,255-i};break;
    case 7:rgb={37,91,143};break;
    case 8:rgb={(11*i+23)%256,(2*i+101)%256,(13*i+3)%256};break;
    case 9:rgb=i==255?std::array<int,3>{250,240,230}:std::array<int,3>{200-i/2,160-i/3,120-i/4};break;
    case 10:rgb={(3*i+7)%256,(5*i+11)%256,(7*i+13)%256};break;
    default:throw std::invalid_argument("Unknown SDK literal table");
    }
    for(int j=0;j<3;++j)table[i][j]=static_cast<std::uint8_t>(rgb[j]);
  }
  return table;
}
ChannelDisplayState minInput(int row){
  const std::array<std::array<int,3>,8> names{{{0,1,2},{3,4,5},{3,1,6},{0,7,8},{0,9,10},{0,1,2},{0,4,6},{0,4,6}}};
  ChannelDisplayState state;
  state.mode=(row==5||row==7)?ChannelDisplayMode::Color:row==6?ChannelDisplayMode::Grayscale:ChannelDisplayMode::Composite;
  if(row==1||row==2||row>=5){state.version=ChannelDisplayVersion::V2;state.projection=ChannelDisplayProjection::Max;}
  for(int c=0;c<3;++c){ChannelDisplayChannel ch;ch.range=std::array<std::array<double,2>,3>{{{1,201},{7,219},{11,241}}}[c];ch.visible=state.mode==ChannelDisplayMode::Color?c==1:c!=1;ch.lut.name.clear();ch.lut.table=minTable(names[row][c]);state.channels.push_back(ch);}
  return state;
}
void translateExpected(ChannelDisplayState &state){
  // Independently authored integer brightness inversion, not a product helper.
  for(auto &ch:state.channels)for(auto &rgb:*ch.lut.table){
    const int shift=255-std::min({int(rgb[0]),int(rgb[1]),int(rgb[2])})-std::max({int(rgb[0]),int(rgb[1]),int(rgb[2])});
    for(auto &value:rgb)value=static_cast<std::uint8_t>(int(value)+shift);
  }
}
ChannelDisplayState minExpected(int row){
  auto state=minInput(row);state.version=ChannelDisplayVersion::V2;state.mode=ChannelDisplayMode::Composite;
  state.projection=row==2?ChannelDisplayProjection::Max:ChannelDisplayProjection::Min;
  if(row==0||row==2||row==5||row==6)translateExpected(state);
  if(row>=5)for(auto &ch:state.channels)ch.visible=true;
  return state;
}
bool sameState(const ChannelDisplayState &a,const ChannelDisplayState &b){
  if(a.mode!=b.mode||a.projection!=b.projection||a.version!=b.version||a.channels.size()!=b.channels.size())return false;
  for(std::size_t c=0;c<a.channels.size();++c){const auto &x=a.channels[c],&y=b.channels[c];if(x.visible!=y.visible||x.range!=y.range||x.lut.name!=y.lut.name||x.lut.table!=y.lut.table)return false;}
  return true;
}
void minSetup(Document &doc,const ChannelDisplayState &state,int c=1){auto view=doc.snapshot().display();view.t=1;view.c=c;view.crosshair={1,1,1};view.zoom=2.5;view.properties["retained"]=std::string("SDK Min");doc.set_display(with_channel_display(view,doc.snapshot().data().dataset,state));}
class UnreadMinSource final:public PlaneSource{
public:
  mutable int reads=0,regions=0;
  DatasetShape shape()const override{return {2,3,2,2,3,1};}
  PixelType pixel_type()const override{return PixelType::UInt16;}
  std::shared_ptr<const Image> read_plane(PlanePosition)const override{++reads;throw std::runtime_error("SDK Min read plane");}
  Image read_region(PlanePosition,int,int,int,int)const override{++regions;throw std::runtime_error("SDK Min read region");}
};
}
int main(){try{
  for(const auto type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float32}){
    const auto data=source(type);auto state=palette();
    const auto v1=encode_channel_display(state,data.shape(),type,data.sample_policy());require(v1.size()==4&&!v1.count("projection"),"SDK V1 wire changed");
    state.version=ChannelDisplayVersion::V2;
    for(const auto &row:std::array<std::pair<ChannelDisplayProjection,std::array<int,3>>,3>{{
        {ChannelDisplayProjection::Sum,{255,250,255}},
        {ChannelDisplayProjection::Max,{200,150,220}},
        {ChannelDisplayProjection::Min,{80,30,60}}}}){
      state.projection=row.first;
      for(const auto plane:{Plane::XY,Plane::XZ,Plane::YZ})pixels(compose_channel_display(data,state,plane,1,1,2),row.second);
      const auto encoded=encode_channel_display(state,data.shape(),type,data.sample_policy());require(encoded.size()==5,"SDK V2 exact fields");
      require(parse_channel_display(encoded,data.shape(),type,data.sample_policy()).projection==row.first,"SDK V2 projection retention");
    }
  }
  std::cout<<"PASS S01.literal27views-and-version\n";
  const auto data=source(PixelType::UInt8);auto state=palette();state.version=ChannelDisplayVersion::V2;state.projection=ChannelDisplayProjection::Min;
  int reads=0;for(auto &channel:state.channels)channel.visible=false;
  pixels(compose_channel_display(data,state,Plane::XY,1,1,2,[&](Plane,int,int,int){++reads;return data.plane({0,0,0});}),{0,0,0});require(reads==0,"SDK empty display read source");
  ChannelDisplayLimits tiny;tiny.max_working_bytes=1;rejects([&]{compose_channel_display(data,state,Plane::XY,1,1,2,{},tiny);});
  Image quiet0(2,1,1,PixelType::Float32),quiet1(2,1,1,PixelType::Float32);
  quiet0.pixels={std::numeric_limits<double>::quiet_NaN(),255};quiet1.pixels={128,1};
  const auto quiet=Dataset::from_planes({1,2,1,1,2,1},{quiet0,quiet1},{},FloatingSamplePolicy::PreserveQuietNaNFloat32);
  auto quietState=default_channel_display(quiet,0,0);quietState.version=ChannelDisplayVersion::V2;
  quietState.projection=ChannelDisplayProjection::Max;
  for(auto &channel:quietState.channels)channel.range={0,255};
  auto &custom=quietState.channels[0].lut;custom.name.clear();custom.table.emplace();
  for(int i=0;i<256;++i)(*custom.table)[i]={static_cast<std::uint8_t>(i),0,0};
  (*custom.table)[0]={7,11,13};quietState.channels[1].lut.name="Blue";
  int quietReads=0;
  const auto quietRgb=compose_channel_display(quiet,quietState,Plane::XY,0,0,0,
      [&](Plane plane,int coordinate,int t,int c){
        require(plane==Plane::XY&&coordinate==0&&t==0,"SDK provider coordinates differ");
        ++quietReads;quietState.projection=ChannelDisplayProjection::Min;
        return quiet.plane({t,c,coordinate});
      });
  require(quietReads==2&&quietRgb.pixels==std::vector<double>({7,11,128,255,0,1}),"SDK quiet-NaN/custom-zero/provider capture differs");
  require(std::isnan(quiet.plane()->pixels[0])&&quiet.plane()->pixels[1]==255,"SDK quiet source changed");
  std::cout<<"PASS S02.empty-quietnan-provider-and-preflight\n";
  Document doc(data);auto view=doc.snapshot().display();view.t=1;view.c=2;view.crosshair[2]=1;
  doc.set_display(with_channel_display(view,data,palette()));const auto before=doc.snapshot();
  auto command=resolve_channel_display_command(before,{ChannelDisplayAction::SelectMax,{}, {}});
  require(command.version()==ChannelDisplayVersion::V2&&command.state()->projection==ChannelDisplayProjection::Min&&command.navigation_channel()==0&&command.requires_captured_display(),"SDK intent lost Min/effect/dependency");
  auto changed=doc.snapshot().display();changed.t=0;changed.c=2;changed.crosshair[2]=0;doc.set_display(changed);
  rejects<RevisionConflict>([&]{command_display_state(command,doc.snapshot());});
  auto replay=channel_display_command_from_record(doc.snapshot(),command.record());require(!replay.requires_captured_display(),"SDK replay retained execution-time display dependency");
  doc.set_display(command_display_state(replay,doc.snapshot()));require(doc.snapshot().display().c==0&&doc.snapshot().display().t==0&&doc.snapshot().display().crosshair[2]==0,"SDK replay replaced freshT/Z");
  require(doc.snapshot().shares_data(before)&&doc.history().undo_steps==0,"SDK projection changed data/history");
  std::cout<<"PASS S03.intent-and-strict-replay\n";
  auto record=command.record();record["version"]=true;rejects([&]{rebind_channel_display_command(doc.snapshot(),record,{ChannelDisplayAction::Disable,{}, {}});});
  auto token=std::make_shared<CancellationToken>();token->cancel();ExecutionControl control;control.cancellation=token;int callbacks=0;control.checkpoint=[&]{++callbacks;};
  rejects<OperationCancelled>([&]{resolve_channel_display_command(doc.snapshot(),{ChannelDisplayAction::SelectMax,{}, {}},{},control);});require(callbacks==0,"SDK pre-cancel invoked checkpoint");
  std::cout<<"PASS S04.original-preflight-and-cancel\n";
  const auto decoded=decode_session(encode_session({{doc.snapshot()},{},{},{}}));const auto reopened=decoded.documents[0]->snapshot();
  require(metadata_equal(reopened.display().properties.at("channel_display"),doc.snapshot().display().properties.at("channel_display")),"SDK nativeV3 lost V2Min");
  std::cout<<"PASS S05.session-roundtrip\n";
  for(int row=0;row<8;++row){
    Document owner(data);minSetup(owner,minInput(row),row==1?2:1);const auto captured=owner.snapshot();
    const auto result=resolve_channel_display_command(captured,{ChannelDisplayAction::SelectMin,{},{}});
    const auto expected=minExpected(row);const bool transformed=row==0||row==2||row==5||row==6;
    require(result.action()==ChannelDisplayAction::SelectMin&&result.version()==ChannelDisplayVersion::V2&&sameState(*result.state(),expected),"SDK N01-N08 Min state/table/range/flags differ");
    require(result.navigation_channel()==(transformed?std::optional<int>(0):std::nullopt)&&result.requires_captured_display()&&result.display_dependency_revision()==captured.revisions().display,"SDK N01-N08 navigation/dependency differs");
    const auto published=command_display_state(result,captured);require(published.t==1&&published.crosshair[2]==1&&published.c==(transformed?0:row==1?2:1),"SDK Min captured navigation differs");
    require(owner.snapshot().shares_data(captured)&&owner.history().undo_steps==0,"SDK Min preparation changed source");
    if(row==0||row==2||row==6){owner.set_display(published);auto repeat=resolve_channel_display_command(owner.snapshot(),{ChannelDisplayAction::SelectMin,{},{}});auto repeatExpected=expected;repeatExpected.projection=ChannelDisplayProjection::Min;if(row==2)translateExpected(repeatExpected);require(sameState(*repeat.state(),repeatExpected)&&repeat.navigation_channel()==(row==2?std::optional<int>(0):std::nullopt),"SDK Min literal repeat differs (Gray is owned Min/Min)");}
  }
  std::cout<<"PASS S06.min-eight-inputs-and-three-repeats\n";
  auto minQuiet=quietState;minQuiet.projection=ChannelDisplayProjection::Min;minQuiet.channels[0].lut.table->at(0)={7,11,13};
  int minReads=0;const auto minQuietBefore0=quiet.plane({0,0,0})->pixels,minQuietBefore1=quiet.plane({0,1,0})->pixels;
  const auto minRgb=compose_channel_display(quiet,minQuiet,Plane::XY,0,0,1,[&](Plane plane,int coordinate,int t,int c){require(plane==Plane::XY&&coordinate==0&&t==0,"SDK Min provider position");++minReads;minQuiet.projection=ChannelDisplayProjection::Max;minQuiet.channels.clear();return quiet.plane({t,c,coordinate});});
  require(minReads==2&&minRgb.pixels==std::vector<double>({0,0,13,0,0,0}),"SDK Min quiet entry0/first-contributor/provider capture differs");
  require(std::memcmp(quiet.plane({0,0,0})->pixels.data(),minQuietBefore0.data(),minQuietBefore0.size()*sizeof(double))==0&&std::memcmp(quiet.plane({0,1,0})->pixels.data(),minQuietBefore1.data(),minQuietBefore1.size()*sizeof(double))==0,"SDK Min provider mutated quiet source bits");
  std::cout<<"PASS S07.min-quietnan-provider-capture\n";
  auto unread=std::make_shared<UnreadMinSource>();Document metadataOnly{Dataset(unread)};minSetup(metadataOnly,minInput(0));
  const auto unreadBefore=metadataOnly.snapshot();int minCallbacks=0;ExecutionControl minControl;minControl.checkpoint=[&]{++minCallbacks;};ChannelDisplayLimits onePixel;onePixel.max_pixels=1;
  const auto noRead=resolve_channel_display_command(unreadBefore,{ChannelDisplayAction::SelectMin,{},{}},onePixel,minControl);
  require(minCallbacks>0&&unread->reads==0&&unread->regions==0&&sameState(*noRead.state(),minExpected(0)),"SDK Min metadata path read source or applied area cap");
  minCallbacks=0;ChannelDisplayLimits minTiny;minTiny.max_working_bytes=1;rejects([&]{resolve_channel_display_command(unreadBefore,{ChannelDisplayAction::SelectMin,{},{}},minTiny,minControl);});require(minCallbacks==0,"SDK Min cap invoked callback");
  auto minToken=std::make_shared<CancellationToken>();minToken->cancel();minControl.cancellation=minToken;rejects<OperationCancelled>([&]{resolve_channel_display_command(unreadBefore,{ChannelDisplayAction::SelectMin,{},{}},{},minControl);});require(minCallbacks==0&&unread->reads==0&&unread->regions==0&&metadataOnly.snapshot().shares_data(unreadBefore),"SDK Min refusal performed read/callback/publication");
  std::cout<<"PASS S08.min-metadata-no-read-and-preflight\n";
  Document minOwner(data);minSetup(minOwner,minInput(2));const auto minBefore=minOwner.snapshot();auto minCommand=resolve_channel_display_command(minBefore,{ChannelDisplayAction::SelectMin,{},{}});
  require(minCommand.state()->projection==ChannelDisplayProjection::Max&&minCommand.navigation_channel()==0,"SDK SelectMin faithful Max/C0 effect lost");
  auto malformed=minCommand.record();malformed["navigation"]=std::monostate{};rejects([&]{validate_channel_display_command_record(malformed);});rejects([&]{rebind_channel_display_command(minBefore,malformed,{ChannelDisplayAction::Disable,{},{}});});
  auto minView=minOwner.snapshot().display();minView.t=0;minView.c=2;minView.crosshair[2]=0;minView.zoom=4.;minOwner.set_display(minView);rejects<RevisionConflict>([&]{command_display_state(minCommand,minOwner.snapshot());});
  auto minReplay=channel_display_command_from_record(minOwner.snapshot(),minCommand.record());auto replayView=command_display_state(minReplay,minOwner.snapshot());require(!minReplay.requires_captured_display()&&replayView.t==0&&replayView.crosshair[2]==0&&replayView.c==0&&replayView.zoom==4.&&sameState(*recognized_channel_display(replayView,data),minExpected(2)),"SDK Min replay reclassified or changed fresh navigation");
  Document minDestination(data);minSetup(minDestination,minInput(7));auto minRebound=rebind_channel_display_command(minDestination.snapshot(),minCommand.record(),{ChannelDisplayAction::SelectMin,{},{}});require(sameState(*minRebound.state(),minExpected(7))&&!minRebound.navigation_channel()&&minRebound.source().same_document(minDestination.snapshot()),"SDK complete Min rebind inherited tables/C effect");
  require(minOwner.snapshot().shares_data(minBefore)&&minOwner.history().undo_steps==0,"SDK Min replay/rebind changed history/data");
  std::cout<<"PASS S09.min-record-replay-and-complete-rebind\n";
  Document gray(data);minSetup(gray,minInput(6));auto grayFirst=resolve_channel_display_command(gray.snapshot(),{ChannelDisplayAction::SelectMin,{},{}});gray.set_display(command_display_state(grayFirst,gray.snapshot()));
  const auto graySession=decode_session(encode_session({{gray.snapshot()},{},{metadata_object(grayFirst.record())},{}}));auto grayReopened=graySession.documents[0];
  auto grayRepeat=resolve_channel_display_command(gray.snapshot(),{ChannelDisplayAction::SelectMin,{},{}}),grayOpenRepeat=resolve_channel_display_command(grayReopened->snapshot(),{ChannelDisplayAction::SelectMin,{},{}});
  Document grayFresh(data);minSetup(grayFresh,minExpected(6),0);auto grayFreshRepeat=resolve_channel_display_command(grayFresh.snapshot(),{ChannelDisplayAction::SelectMin,{},{}});
  require(sameState(*grayRepeat.state(),minExpected(6))&&sameState(*grayOpenRepeat.state(),minExpected(6))&&sameState(*grayFreshRepeat.state(),minExpected(6))&&!grayRepeat.navigation_channel()&&!grayOpenRepeat.navigation_channel()&&!grayFreshRepeat.navigation_channel(),"SDK native Gray logical-state/SaveOpen equivalence differs");
  require(metadata_equal(graySession.workflow[0],metadata_object(grayFirst.record()))&&!grayReopened->snapshot().same_document(gray.snapshot()),"SDK Min session historical binding changed");rejects([&]{channel_display_command_from_record(grayReopened->snapshot(),grayFirst.record());});
  auto grayBound=rebind_channel_display_command(grayReopened->snapshot(),grayFirst.record(),{ChannelDisplayAction::SelectMin,{},{}});require(sameState(*grayBound.state(),minExpected(6))&&!grayBound.navigation_channel(),"SDK Gray Min full rebind differs");
  std::cout<<"PASS S10.min-gray-saveopen-determinism\n";return 0;
}catch(const std::exception &error){std::cerr<<"FAIL projection-SDK: "<<error.what()<<'\n';return 1;}}
