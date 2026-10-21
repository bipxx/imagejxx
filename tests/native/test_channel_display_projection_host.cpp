// Integration assertions for native projection publication and actual ImGui
// input. Literal RGB expectations are independent of the product compositor.
#include "window.hpp"
#include "headless_session_internal.hpp"
#include <imagejxx/session.hpp>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace imagejxx;
namespace desktop = imagejxx::desktop;
using desktop::Window;
using desktop::Json;
using desktop::HeadlessInputSession;
namespace fs = std::filesystem;
namespace {
std::size_t checks = 0, frames = 0;
void check(bool value, const char *message) {
  ++checks; if (!value) throw std::runtime_error(message);
}
std::shared_ptr<Document> fixture() {
  Stack planes;
  for (int t=0;t<2;++t) for (int c=0;c<3;++c) for (int z=0;z<2;++z) {
    Image image(3,2,1,PixelType::UInt8);
    for (auto &v:image.pixels) v=40*t+30*c+10*z;
    planes.push_back(std::move(image));
  }
  auto owner=std::make_shared<Document>(Dataset::from_planes({2,3,2,2,3,1},std::move(planes)));
  auto view=owner->snapshot().display();view.t=1;view.c=2;view.crosshair={1,1,1};
  owner->set_display(view);owner->mark_saved();return owner;
}
ChannelDisplayState constant_state() {
  ChannelDisplayState state;
  for (const auto &rgb:std::array<std::array<std::uint8_t,3>,3>{{{200,30,90},{80,150,60},{120,70,220}}}) {
    ChannelDisplayChannel channel;channel.lut.name.clear();channel.lut.table.emplace();
    channel.lut.table->fill(rgb);state.channels.push_back(channel);
  }
  return state;
}
ChannelDisplayState current(const std::shared_ptr<Document> &source) {
  return *recognized_channel_display(source->snapshot().display(),source->snapshot().data().dataset);
}
Json frame(HeadlessInputSession &session,const Json &events=Json::array()) {
  check(++frames<=180,"Host input exceeded180frames");return session.frame(events);
}
Json settle(HeadlessInputSession &session) {Json out;for(int i=0;i<3;++i)out=frame(session);return out;}
const Json &plane(const Json &report,const char *name) {
  for(const auto &p:report.at("orthogonal_planes"))if(p.at("plane")==name)return p;
  throw std::runtime_error("Missing linked plane");
}
void rgb(const Json &pixels,std::array<int,3> wanted,const char *projection) {
  const auto &read=pixels.at("readback");check(read.at("available")==true,"Missing actual renderer readback");
  const auto &values=read.at("rendered_rgb").at("pixels");check(!values.empty()&&values.size()%3==0,"RGB output cardinality");
  for(std::size_t i=0;i<values.size();++i)check(values.at(i)==wanted[i%3],"Actual RGB differs from literal components");
  const auto &state=pixels.at("channel_display").at("state");
  check(state.at("version")==2&&state.at("projection")==projection,"Cache/diagnostics lost V2 projection");
}
void unchanged(const std::shared_ptr<Document> &owner,const DocumentSnapshot &before) {
  const auto after=owner->snapshot();check(after.shares_data(before)&&!after.dirty(),"Projection changed source data/dirty state");
  check(after.revisions().matches(before.revisions(),Change::Data),"Projection changed pixel/metadata revisions");
  check(owner->history().undo_steps==0&&owner->history().redo_steps==0,"Projection changed pixel history");
}
void rendering(const fs::path &path) {
  Window window((path/"preferences.json").u8string());auto owner=fixture();window.addNativeDocument(owner);
  const auto before=owner->snapshot();HeadlessInputSession session(window);
  for(const auto &row:std::array<std::pair<ChannelDisplayProjection,std::array<int,3>>,3>{{
      {ChannelDisplayProjection::Sum,{255,250,255}},
      {ChannelDisplayProjection::Max,{200,150,220}},
      {ChannelDisplayProjection::Min,{80,30,60}}}}) {
    const char *name=row.first==ChannelDisplayProjection::Sum?"sum":row.first==ChannelDisplayProjection::Max?"max":"min";
    auto state=constant_state();state.version=ChannelDisplayVersion::V2;state.projection=row.first;
    auto view=owner->snapshot().display();view.orthogonal=false;window.setNativeDisplay(view);
    window.applyChannelDisplay(owner->snapshot(),{ChannelDisplayAction::Set,state,{}});
    auto ordinary=settle(session);rgb(ordinary.at("ordinary_pixels"),row.second,name);
    view=owner->snapshot().display();view.orthogonal=true;window.setNativeDisplay(view);
    auto linked=settle(session);for(const char *axis:{"XY","XZ","YZ"})rgb(plane(linked,axis),row.second,name);
    const auto stable=frame(session);
    check(plane(stable,"XZ").at("builds")==plane(linked,"XZ").at("builds"),"Unchanged projection rebuilt cache");
  }
  check(window.error().empty()&&window.workflow.empty(),"Rendering changed workflow or reported an error");unchanged(owner,before);
}
Json click(HeadlessInputSession &session,const Json &report,const char *selector) {
  const auto &panel=report.at("channel_display_panel");
  const auto &observed=panel.at("control_states").at(selector);
  check(observed.at("id").get<std::uint64_t>()>0&&observed.at("enabled")==true&&observed.at("admitted")==true,
        "Physical control has no actual available ImGui widget");
  const auto &item=panel.at("controls").at(selector);
  // Coordinates are from actual current widget observations, never hardcoded.
  const auto &point=item; // controls maps selectors to widget center points.
  const double x=std::floor(point.at(0).get<double>()),y=std::floor(point.at(1).get<double>());
  const auto &rect=observed.at("rect");
  check(std::isfinite(x)&&std::isfinite(y)&&x>=rect[0].get<double>()&&x<rect[2].get<double>()&&
        y>=rect[1].get<double>()&&y<rect[3].get<double>(),"Physical pointer outside observed widget");
  frame(session,Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
  frame(session,Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
  return frame(session,Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
}
void panel(const fs::path &path) {
  Window window((path/"preferences.json").u8string());auto owner=fixture();window.addNativeDocument(owner);
  window.applyChannelDisplay(owner->snapshot(),{ChannelDisplayAction::Set,constant_state(),{}});
  const auto before=owner->snapshot();window.recording=true;window.action("show_channel_display");
  HeadlessInputSession session(window);auto report=settle(session);
  report=click(session,report,"channel_display.mode");report=frame(session);
  report=click(session,report,"channel_display.mode.composite_max");report=settle(session);
  const auto state=current(owner);
  check(state.version==ChannelDisplayVersion::V2&&state.projection==ChannelDisplayProjection::Min,"Physical Max did not preserve observed constant-LUT Min outcome");
  check(owner->snapshot().display().c==0&&owner->snapshot().display().t==1&&owner->snapshot().display().crosshair[2]==1,"Physical transform lost deliberateC0 or T/Z");
  check(window.workflow.size()==1&&window.workflow[0].at("action")=="select_max"&&window.workflow[0].at("state").at("projection")=="min","Physical action did not record resolved Min");
  rgb(report.at("ordinary_pixels"),{85,35,105},"min");
  check(report.at("channel_display_panel").at("mode_label")=="Composite Min","Actual combo preview did not label resolved Min");
  // Selecting ordinary Composite chooses SUM but retains the V2 wire identity.
  report=click(session,report,"channel_display.mode");report=frame(session);
  report=click(session,report,"channel_display.mode.composite");report=settle(session);
  check(current(owner).version==ChannelDisplayVersion::V2&&current(owner).projection==ChannelDisplayProjection::Sum,"Physical Composite lost V2SUM identity");
  check(report.at("channel_display_panel").at("mode_label")=="Composite","Actual combo preview did not label SUM");
  check(window.workflow.size()==2&&window.error().empty(),"Panel published multiple actions or reported an error");unchanged(owner,before);
}
void records(const fs::path &path) {
  Window window((path/"preferences.json").u8string());auto owner=fixture();window.addNativeDocument(owner);
  window.applyChannelDisplay(owner->snapshot(),{ChannelDisplayAction::Set,constant_state(),{}});
  const auto before=owner->snapshot();window.recording=true;
  window.action("channel_display",{{"request",{{"action","select_max"},{"state",nullptr},{"channels",Json::array()}}}});
  check(window.workflow.size()==1,"JSON intent dispatch lost action");const auto record=window.workflow[0];
  auto view=owner->snapshot().display();view.t=0;view.c=2;view.crosshair[2]=0;window.setNativeDisplay(view);
  window.replayChannelDisplay(desktop::metadataObjectFromJson(record));
  check(owner->snapshot().display().c==0&&owner->snapshot().display().t==0&&owner->snapshot().display().crosshair[2]==0,"Strict replay reran capture navigation or lost C effect");
  check(current(owner).projection==ChannelDisplayProjection::Min&&window.workflow.size()==2&&window.workflow[1]==record,"Replay did not retain exact resolved record");
  save_session((path/"projection.ijx").u8string(),{{owner->snapshot()},{},{},{}});
  const auto loaded=load_session((path/"projection.ijx").u8string());
  const auto restored=loaded.documents[0]->snapshot();
  check(metadata_equal(restored.display().properties.at("channel_display"),owner->snapshot().display().properties.at("channel_display")),"Native Save/Open changed V2Min state");
  auto bad=record;bad["navigation"]["c"]=2;const auto snapshot=owner->snapshot();const auto rows=window.workflow;
  bool refused=false;try{window.replayChannelDisplay(desktop::metadataObjectFromJson(bad));}catch(const std::invalid_argument&){refused=true;}
  check(refused&&window.workflow==rows&&metadata_equal(owner->snapshot().display().properties,snapshot.display().properties),"Malformed resolved navigation partially published");
  unchanged(owner,before);
}
}
int main(int argc,char **argv) {
  try {
    check(argc==2||argc==3,"Supply fresh output directory and optional group");const auto path=fs::absolute(fs::u8path(argv[1]));
    check(!fs::exists(path),"Host output must be fresh");fs::create_directories(path);const std::string group=argc==3?argv[2]:"all";bool matched=false;
    const auto run=[&](const char *name,auto test){if(group!="all"&&group!=name)return;matched=true;frames=0;const auto dir=path/name;fs::create_directory(dir);test(dir);std::cout<<"PASS projection-host."<<name<<" frames="<<frames<<'\n';};
    run("rendering",rendering);run("panel",panel);run("records",records);check(matched,"Unknown host group");std::cout<<"projection-host checks="<<checks<<'\n';return 0;
  } catch(const std::exception &error){std::cerr<<"FAIL projection-host after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
