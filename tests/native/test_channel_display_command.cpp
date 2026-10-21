#include <imagejxx/channel_display_command.hpp>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace imagejxx;
namespace {
std::size_t checks = 0;
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
template<class Error = std::invalid_argument, class F> void refuses(F f) {
  bool rejected = false;
  try { f(); } catch (const Error &) { rejected = true; }
  check(rejected, "Expected command refusal");
}
MetadataObject object(const MetadataValue &v) {
  return std::get<MetadataObject>(std::get<std::shared_ptr<const MetadataTree>>(v)->value);
}
MetadataArray array(const MetadataValue &v) {
  return std::get<MetadataArray>(std::get<std::shared_ptr<const MetadataTree>>(v)->value);
}
MetadataObject changed(MetadataObject value, const char *field,
                       std::function<void(MetadataObject &)> edit) {
  auto child = object(value.at(field)); edit(child);
  value[field] = metadata_object(std::move(child)); return value;
}
Dataset formula(PixelType type = PixelType::UInt8, int channels = 3, int offset = 0) {
  Stack planes;
  for (int t=0;t<2;++t) for(int c=0;c<channels;++c) for(int z=0;z<4;++z) {
    Image image(5,3,1,type);
    for(int y=0;y<3;++y) for(int x=0;x<5;++x)
      image.at(x,y) = offset+40*t+30*c+5*z+2*y+x;
    planes.push_back(std::move(image));
  }
  return Dataset::from_planes({2,channels,4,3,5,1},std::move(planes));
}
ChannelDisplayState display(int count = 3) {
  ChannelDisplayState state;
  state.mode=ChannelDisplayMode::Color;
  for(int c=0;c<count;++c) {
    ChannelDisplayChannel channel;
    channel.lut.name=c%3==0?"Red":c%3==1?"Green":"Blue";
    state.channels.push_back(channel);
  }
  return state;
}
void position(Document &doc, int t, int c, int z) {
  auto view=doc.snapshot().display(); view.t=t;view.c=c;view.crosshair[2]=z;
  doc.set_display(std::move(view));
}
ChannelDisplayRequest setting(int count=3) {
  return {ChannelDisplayAction::Set,display(count),{}};
}
void records() {
  Document doc(formula());
  const auto command=resolve_channel_display_command(doc.snapshot(),setting());
  const auto record=command.record();
  validate_channel_display_command_record(record);
  check(record.size()==7,"Exact command envelope");
  for(const auto &[key,value]:record) {
    (void)value; auto missing=record; missing.erase(key);
    refuses([&]{validate_channel_display_command_record(missing);});
  }
  auto extra=record;extra["extra"]=std::int64_t(1);
  refuses([&]{validate_channel_display_command_record(extra);});
  for(MetadataValue invalid: {MetadataValue(true),MetadataValue(1.0),MetadataValue(std::int64_t(2))}) {
    auto copy=record;copy["version"]=invalid;
    refuses([&]{validate_channel_display_command_record(copy);});
  }
  for(const auto *field:{"id","pixels_revision","metadata_revision"}) {
    for(MetadataValue invalid:{MetadataValue(true),MetadataValue(0.0),MetadataValue(std::int64_t(-1))}) {
      auto copy=changed(record,"source",[&](auto &source){source[field]=invalid;});
      refuses([&]{validate_channel_display_command_record(copy);});
    }
  }
  auto zero=changed(record,"source",[](auto &s){s["id"]=std::uint64_t(0);});
  refuses([&]{validate_channel_display_command_record(zero);});
  for(int axis=0;axis<6;++axis) {
    auto copy=changed(record,"source",[&](auto &s){auto v=array(s.at("shape"));v[axis]=true;s["shape"]=metadata_array(v);});
    refuses([&]{validate_channel_display_command_record(copy);});
  }
  for(const char *field:{"t","c","z"}) {
    auto copy=changed(record,"position",[&](auto &p){p[field]=true;});
    refuses([&]{validate_channel_display_command_record(copy);});
    copy=changed(record,"position",[&](auto &p){p[field]=std::int64_t(99);});
    refuses([&]{validate_channel_display_command_record(copy);});
  }
  auto no_state=record;no_state["state"]=std::monostate{};
  refuses([&]{validate_channel_display_command_record(no_state);});
  auto reset=changed(record,"reset",[](auto &p){p["channels"]=metadata_array({std::int64_t(0)});});
  refuses([&]{validate_channel_display_command_record(reset);});
  auto disable=record;disable["action"]=std::string("disable");
  refuses([&]{validate_channel_display_command_record(disable);});
  auto malformed=changed(record,"state",[](auto &s){s["mode"]=std::string("multiply");});
  refuses([&]{validate_channel_display_command_record(malformed);});
  Document many(formula(PixelType::UInt8,9));auto requested=setting(9);
  requested.state->mode=ChannelDisplayMode::Composite;
  auto normalized=resolve_channel_display_command(many.snapshot(),requested);
  check(normalized.state()->mode==ChannelDisplayMode::Color,"Explicit >8 fallback");
  auto invalid=changed(normalized.record(),"state",[](auto &s){s["mode"]=std::string("composite");});
  refuses([&]{validate_channel_display_command_record(invalid);});
}
void replay() {
  Document doc(formula());position(doc,1,2,1);
  auto before=doc.snapshot();auto command=resolve_channel_display_command(before,setting());
  position(doc,0,0,3);doc.select(Selection("rectangle",{{0,0},{2,2}}));
  auto now=doc.snapshot();auto strict=channel_display_command_from_record(now,command.record());
  auto result=command_display_state(strict,now);
  check(result.t==0&&result.c==0&&result.crosshair[2]==3,"Replay preserves navigation");
  const auto history=doc.history(); const bool dirty=now.dirty();
  doc.set_display(result);
  check(doc.snapshot().shares_data(now),"Display shares immutable data");
  check(doc.snapshot().dirty()==dirty&&doc.history().undo_steps==history.undo_steps,"No pixel history/dirty change");
  check(doc.snapshot().revisions().pixels==now.revisions().pixels&&doc.snapshot().revisions().metadata==now.revisions().metadata,"No data revision");
  Document other(formula());
  refuses([&]{channel_display_command_from_record(other.snapshot(),command.record());});
  refuses([&]{command_display_state(command,other.snapshot());});
  auto rebound=rebind_channel_display_command(other.snapshot(),command.record(),setting());
  check(rebound.source().same_document(other.snapshot()),"Explicit rebind new owner");
  auto malformed=command.record();malformed.erase("source");
  refuses([&]{rebind_channel_display_command(other.snapshot(),malformed,setting());});
  auto edit=doc.begin_edit();edit.set_title("changed");doc.commit(edit);
  refuses<RevisionConflict>([&]{channel_display_command_from_record(doc.snapshot(),command.record());});
  refuses<RevisionConflict>([&]{command_display_state(command,doc.snapshot());});
}
void mutations() {
  Document doc(formula());
  auto before=doc.snapshot(); auto command=resolve_channel_display_command(before,setting());
  auto view=command_display_state(command,before);view.properties["unrelated"]=std::string("retained");
  doc.set_display(view);
  auto edit=doc.begin_edit();edit.replace_dataset(formula(PixelType::UInt8,2));
  refuses([&]{doc.commit(edit);});
  check(doc.snapshot().shares_data(before)&&edit.active(),"C-count edit refusal is atomic");
  edit.rollback();
  auto disable=resolve_channel_display_command(doc.snapshot(),{ChannelDisplayAction::Disable,{},{}});
  auto disabled=command_display_state(disable,doc.snapshot());
  check(!disabled.properties.count("channel_display")&&disabled.properties.count("unrelated"),"Disable changes only owned field");
  int callbacks=0;ExecutionControl control;control.checkpoint=[&]{++callbacks;};
  (void)resolve_channel_display_command(doc.snapshot(),setting(),{},control);
  (void)resolve_channel_display_command(doc.snapshot(),{ChannelDisplayAction::Disable,{},{}},{},control);
  check(callbacks==0,"Metadata-only commands invoke no callbacks");
  Document f64(formula(PixelType::Float64));
  auto opaque=f64.snapshot().display();
  opaque.properties["channel_display"]=metadata_object({{"schema",std::string("future")},{"version",std::int64_t(37)}});
  f64.set_display(opaque);
  auto clearing=resolve_channel_display_command(f64.snapshot(),{ChannelDisplayAction::Disable,{},{}});
  check(!command_display_state(clearing,f64.snapshot()).properties.count("channel_display"),"Disable opaque state on legacy type");
  Document future(formula(PixelType::Float32));
  auto future_view=future.snapshot().display();
  future_view.properties["channel_display"]=metadata_object({{"schema",std::string("future")},{"version",std::int64_t(37)}});
  future.set_display(future_view);
  auto retained=future.snapshot();auto remap=future.begin_edit();
  remap.replace_dataset(formula(PixelType::Float32,2));
  refuses([&]{future.commit(remap);});
  check(future.snapshot().shares_data(retained)&&remap.active(),"Opaque C remap refuses atomically");
  auto explicitly_cleared=retained.display();explicitly_cleared.properties.erase("channel_display");
  future.commit(remap,explicitly_cleared);
  check(future.snapshot().data().dataset.shape().c==2&&!future.snapshot().display().properties.count("channel_display"),"Explicit opaque removal permits C edit");
  refuses([&]{resolve_channel_display_command(doc.snapshot(),{ChannelDisplayAction::Disable,display(),{}});});
  refuses([&]{resolve_channel_display_command(doc.snapshot(),{ChannelDisplayAction::Set,display(),{0}});});
  auto stale=doc.begin_edit();auto plane=*doc.snapshot().data().dataset.plane();plane.pixels[0]+=1;
  stale.replace_plane({},plane);doc.commit(stale);
  refuses<RevisionConflict>([&]{command_display_state(command,doc.snapshot());});
}
void reset() {
  Document doc(formula(PixelType::Float32));position(doc,1,2,2);
  auto request=setting();request.action=ChannelDisplayAction::ResetRange;request.reset_channels={2,0};
  auto command=resolve_channel_display_command(doc.snapshot(),request);
  check(command.state()->channels[2].range==std::array<double,2>{110,118},"Captured T/Z C2 reset");
  check(command.state()->channels[0].range==std::array<double,2>{50,58},"Ordered C0 reset");
  check(command.state()->channels[1].range==std::array<double,2>{0,255},"Untouched C1 range");
  auto observations=object(command.record().at("reset"));
  check(array(observations.at("channels"))==MetadataArray{std::int64_t(2),std::int64_t(0)},"Reset order recorded");
  position(doc,0,1,0);
  auto strict=channel_display_command_from_record(doc.snapshot(),command.record());
  check(strict.state()->channels[2].range[0]==110,"Strict reset replay does not recompute");
  Document other(formula(PixelType::Float32,3,100));position(other,1,2,2);
  auto rebound=rebind_channel_display_command(other.snapshot(),command.record(),request);
  check(rebound.state()->channels[2].range==std::array<double,2>{210,218},"Explicit rebind recomputes only requested reset");
  auto invalid=changed(command.record(),"reset",[](auto &r){r["empty_finite"]=metadata_array({true,false});});
  refuses([&]{validate_channel_display_command_record(invalid);});
  int callbacks=0;ExecutionControl control;control.checkpoint=[&]{++callbacks;};
  ChannelDisplayLimits cap;cap.max_working_bytes=1;
  refuses([&]{resolve_channel_display_command(doc.snapshot(),request,cap,control);});
  check(callbacks==0,"Reset cap refuses before callbacks");
  request.reset_channels={2,2};
  refuses([&]{resolve_channel_display_command(doc.snapshot(),request,{},control);});
  check(callbacks==0,"Duplicate reset channels refuse before callbacks");
}
} // namespace
int main(int argc,char **argv) {
  try {
    const std::string group=argc>1?argv[1]:"all";
    if(group!="all"&&group!="records"&&group!="replay"&&group!="mutations"&&group!="reset")
      throw std::invalid_argument("Unknown channel display command group");
    if(group=="records"||group=="all") records();
    if(group=="replay"||group=="all") replay();
    if(group=="mutations"||group=="all") mutations();
    if(group=="reset"||group=="all") reset();
    std::cout<<"channel_display_command "<<group<<": "<<checks<<" checks\n";
    return 0;
  } catch(const std::exception &e) {
    std::cerr<<"channel_display_command after "<<checks<<" checks: "<<e.what()<<'\n';return 1;
  }
}
