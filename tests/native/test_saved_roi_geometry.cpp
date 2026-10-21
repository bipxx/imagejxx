// Independent assertions for the adopted positioned saved-area XY contract.
// Expected geometry is literal/test-owned; no product geometry constructs it.
#include <imagejxx/application.hpp>
#include <imagejxx/saved_roi_geometry.hpp>
#include <imagejxx/selection_edit.hpp>
#include <imagejxx/session.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
namespace {
std::size_t checks = 0;
std::vector<std::string> completed;
std::string current;
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(current + ": " + message);
}
template<class Error = std::invalid_argument, class F>
void rejects(F &&call, const char *message) {
  bool refused = false;
  try { call(); } catch (const Error &) { refused = true; }
  check(refused, message);
}
void near(double a, double b, const char *message) {
  check(std::isfinite(a) && std::abs(a - b) <= 1e-11, message);
}
MetadataObject object(const MetadataValue &v) {
  const auto p = std::get_if<std::shared_ptr<const MetadataTree>>(&v);
  check(p && *p, "Expected an owned metadata tree");
  const auto q = std::get_if<MetadataObject>(&(*p)->value);
  check(q != nullptr, "Expected a metadata object");
  return *q;
}
template<class F>
MetadataObject editObject(MetadataObject r, const char *key, F &&edit) {
  auto o = object(r.at(key)); edit(o); r[key] = metadata_object(std::move(o));
  return r;
}
MetadataObject attributes() {
  return {{"label", std::string("kept")},
          {"future", metadata_object({{"version", std::int64_t(9)},
                                      {"values", metadata_array({true, std::string("opaque")})},
                                      {"unsigned",std::numeric_limits<std::uint64_t>::max()}})}};
}
Selection styled(Selection s) { s.width = 2.5; s.attributes = attributes(); return s; }
Selection rectangle() { return styled(Selection("rectangle", {{2,2},{8,7}})); }
Selection replacement() { return styled(Selection("rectangle", {{3,1},{9,6}})); }
Selection ellipse() { return styled(Selection("ellipse", {{2,2},{8,8}})); }
Selection polygon(const char *kind = "polygon") {
  return styled(Selection(kind, {{2,2},{8,2},{8,7},{2,7}}));
}
template<class P> std::vector<unsigned char> packed(int w, int h, P inside) {
  std::vector<unsigned char> result((std::size_t(w) * h + 7) / 8, 0);
  for (int y=0; y<h; ++y) for (int x=0; x<w; ++x)
    if (inside(x,y)) {
      const auto i=std::size_t(y)*w+x;
      result[i/8] |= static_cast<unsigned char>(0x80u >> (i%8));
    }
  return result;
}
Selection holedMask() {
  return styled(Selection::from_packed_mask(12,10,packed(12,10,[](int x,int y) {
    return x>=2 && x<10 && y>=2 && y<8 && !(x>=4 && x<8 && y>=4 && y<6);
  })));
}
template<class P> void membership(const Selection &actual, P inside, std::size_t count) {
  const auto raster=actual.mask(12,10);
  std::size_t n=0;
  for (int y=0;y<10;++y) for(int x=0;x<12;++x) {
    const bool expected=inside(x,y);
    check((raster.at(x,y)!=0)==expected,"Literal cell membership differs");
    n += expected;
  }
  check(n==count,"Independent fixture cell count differs");
}
void stylePreserved(const Selection &s) {
  check(s.width==2.5 && metadata_equal(s.attributes,attributes()),
        "Geometry changed width or opaque/style attributes");
}
SpatialCalibration calibration() {
  SpatialCalibration c;
  c.spacing={.5,1.25,2.5}; c.origin={11,-7,19}; c.unit="um";
  c.frame_interval=.75; c.time_origin=2.25; c.time_unit="s"; return c;
}
class UnreadSource final : public PlaneSource {
public:
  explicit UnreadSource(PixelType type=PixelType::UInt16,
                       DatasetShape shape={2,3,3,10,12,1}): type_(type),shape_(shape) {}
  DatasetShape shape() const override { return shape_; }
  PixelType pixel_type() const override { return type_; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++planes; throw std::runtime_error("Saved ROI action read source pixels");
  }
  Image read_region(PlanePosition,int,int,int,int) const override {
    ++regions; throw std::runtime_error("Saved ROI action read source region");
  }
  mutable std::size_t planes=0,regions=0;
private:
  PixelType type_; DatasetShape shape_;
};
Dataset eager(PixelType type=PixelType::UInt16,int samples=1) {
  Stack planes;
  for(int t=0;t<2;++t) for(int c=0;c<3;++c) for(int z=0;z<3;++z) {
    Image p(12,10,samples,type);
    for(int y=0;y<10;++y) for(int x=0;x<12;++x) for(int s=0;s<samples;++s) {
      const int v=1000*t+100*c+10*z+2*y+x;
      p.at(x,y,s)=type==PixelType::UInt8 ? v%256 : type==PixelType::Float32 ? v/4. : v;
    }
    planes.push_back(std::move(p));
  }
  return Dataset::from_planes({2,3,3,10,12,samples},std::move(planes),calibration());
}
DocumentData initialData(Dataset dataset,Selection target=rectangle()) {
  DocumentData d{std::move(dataset)};
  d.metadata.title="Scientific saved ROI";
  d.metadata.properties["unrelated"]=metadata_object({{"v",std::int64_t(7)}});
  d.metadata.extensions["future"]=metadata_array({std::string("retained"),false});
  d.selection=Selection("rectangle",{{0,0},{1,1}},1);
  d.previous_selection=Selection("line",{{0,9},{11,0}},3);
  d.rois={{"target",std::move(target),{},{},{},{{"row",std::string("first")}}},
          {"explicit zero",ellipse(),0,0,0,{{"row",std::int64_t(2)}}},
          {"mixed",polygon(),1,{},2,{{"row",std::int64_t(3)}}}};
  d.overlays={{{"opaque",metadata_array({std::int64_t(11),std::string("overlay")})}}};
  d.spectrum=FourierState(Spectrum{1,1,{{3,4}}},std::array<int,2>{1,1});
  return d;
}
struct Fixture {
  Application app;
  std::shared_ptr<Document> owner;
  std::shared_ptr<UnreadSource> provider;
  explicit Fixture(Selection target=rectangle(),bool lazy=true,
                   PixelType type=PixelType::UInt16,int samples=1,
                   bool withSpectrum=true) {
    Dataset dataset= [&] {
      if(!lazy) return eager(type,samples);
      provider=std::make_shared<UnreadSource>(type,DatasetShape{2,3,3,10,12,samples});
      return Dataset(provider,calibration());
    }();
    DisplayState display; display.t=1; display.c=2; display.crosshair={6,5,2};
    display.orthogonal=true; display.zoom=1.5; display.pan[0]={2,3};
    display.properties["unrelated"]=std::string("display");
    auto data=initialData(std::move(dataset),std::move(target));
    if(!withSpectrum)data.spectrum.reset(); // admitted nativeV3 scientific profile
    owner=std::make_shared<Document>(std::move(data),display);
    app.adopt(owner);
    // Genuine history/redo baseline; no revision manufacture or reset.
    auto tx=owner->begin_edit(); tx.set_extension("temporary",true); owner->commit(tx);
    owner->undo(); owner->mark_saved();
  }
  void zeroReads() const {
    if(provider) check(provider->planes==0 && provider->regions==0,"Metadata path read provider");
  }
};
struct Before {
  DocumentSnapshot snapshot;
  HistoryInfo history;
  std::vector<std::uint64_t> ids;
  std::uint64_t active;
  explicit Before(const Fixture &f):snapshot(f.owner->snapshot()),history(f.owner->history()),
    ids(f.app.document_ids()),active(f.app.active_document_id()) {}
};
void unchanged(const Fixture &f,const Before &b) {
  const auto s=f.owner->snapshot(); const auto h=f.owner->history();
  check(s.shares_data(b.snapshot) && s.data_identity()==b.snapshot.data_identity() &&
        s.revisions().matches(b.snapshot.revisions(),Change::All) && s.dirty()==b.snapshot.dirty(),
        "Rejected/prepared action changed source identity/revisions/dirty");
  check(h.undo_steps==b.history.undo_steps && h.redo_steps==b.history.redo_steps &&
        h.step_limit==b.history.step_limit && h.pixel_budget==b.history.pixel_budget,
        "Rejected/prepared action changed history/redo/policy");
  check(f.app.document_ids()==b.ids && f.app.active_document_id()==b.active,
        "Rejected/prepared action changed membership or activation");
  f.zeroReads();
}
void preserved(const DocumentSnapshot &a,const DocumentSnapshot &b) {
  check(a.data().dataset.shares_pixels(b.data().dataset),"Geometry copied/replaced source pixels");
  check(a.data().selection==b.data().selection && a.data().previous_selection==b.data().previous_selection,
        "Saved-row action changed current/previous global selection");
  check(a.data().metadata==b.data().metadata && a.data().overlays.size()==b.data().overlays.size() &&
        a.data().spectrum==b.data().spectrum,"Saved-row action changed unrelated data");
  for(std::size_t i=0;i<a.data().overlays.size();++i)
    check(metadata_equal(a.data().overlays[i],b.data().overlays[i]),"Saved-row action changed overlays");
  check(a.data().dataset.calibration().spacing==b.data().dataset.calibration().spacing &&
        a.data().dataset.calibration().origin==b.data().dataset.calibration().origin &&
        a.data().dataset.calibration().direction==b.data().dataset.calibration().direction &&
        a.data().dataset.calibration().unit==b.data().dataset.calibration().unit &&
        a.data().dataset.calibration().time_unit==b.data().dataset.calibration().time_unit &&
        a.data().dataset.calibration().frame_interval==b.data().dataset.calibration().frame_interval &&
        a.data().dataset.calibration().time_origin==b.data().dataset.calibration().time_origin,
        "Saved-row action changed calibration");
}
SavedRoiGeometryPublication apply(Fixture &,const Selection &,std::size_t row=0);

void areaKinds() {
  const auto rect=rectangle(); const SelectionEditor r(rect,12,10);
  auto moved=r.moved({1,-1});
  check(moved.points==std::vector<std::array<double,2>>{{3,1},{9,6}},"Rectangle literal move");
  auto resized=r.resized(3,{10,4.5});
  check(resized.points==std::vector<std::array<double,2>>{{2,2},{10,7}},"East handle index3 resize");
  auto numeric=r.box_geometry({6,5},{4,4});
  check(numeric.points==std::vector<std::array<double,2>>{{4,3},{8,7}},"Numeric literal box");
  stylePreserved(moved);stylePreserved(resized);stylePreserved(numeric);
  {Fixture f;apply(f,moved);check(f.app.snapshot().data().rois[0].selection==replacement(),"Rectangle service literal output");f.zeroReads();}
  auto em=SelectionEditor(ellipse(),12,10).moved({-1,1});
  check(em.points==std::vector<std::array<double,2>>{{1,3},{7,9}},"Ellipse literal move");
  stylePreserved(em);
  {Fixture f(ellipse());apply(f,em);check(f.app.snapshot().data().rois[0].selection==styled(Selection("ellipse",{{1,3},{7,9}})),
                                       "Ellipse service literal output");f.zeroReads();}
  for(const auto kind:{"polygon","freehand"}) {
    const SelectionEditor v(polygon(kind),12,10);
    auto p=v.moved_vertex(1,{9,3});
    check(p.points==std::vector<std::array<double,2>>{{2,2},{9,3},{8,7},{2,7}},"Literal moved vertex");
    auto inserted=SelectionEditor(p,12,10).inserted_vertex(2,{9,5});
    check(inserted.points==std::vector<std::array<double,2>>{{2,2},{9,3},{9,5},{8,7},{2,7}},
          "Literal inserted vertex order");
    check(SelectionEditor(inserted,12,10).removed_vertex(2)==p,"Delete exact inverse");
    rejects([&]{SelectionEditor(Selection(kind,{{1,1},{5,1},{2,4}}),12,10).removed_vertex(1);},
            "Three-vertex deletion was admitted");
    stylePreserved(p);stylePreserved(inserted);
    Fixture f(polygon(kind));apply(f,p);
    check(f.app.snapshot().data().rois[0].selection==styled(Selection(kind,{{2,2},{9,3},{8,7},{2,7}})),
          "Vertex service literal output");f.zeroReads();
  }
  // Literal sin/cos(30deg) axes; no product frame constructs the expected box.
  const double q=std::sqrt(3.0);
  for(const auto kind:{"rotated_rectangle","rotated_ellipse"}) {
    auto s=styled(Selection(kind,{{6,5},{6+1.5*q,6.5},{5,5+q}}));
    auto frame=s.box_frame();check(bool(frame),"Rotated box frame missing");
    near(frame->center[0],6,"Literal rotated center X");near(frame->center[1],5,"Literal rotated center Y");
    near(std::hypot(frame->u[0],frame->u[1])*2,6,"Literal rotated width");
    near(std::hypot(frame->v[0],frame->v[1])*2,4,"Literal rotated height");
    near(std::atan2(frame->u[1],frame->u[0])*180/std::acos(-1.0),30,"Literal angle30");
    const auto actual=s.mask(12,10);
    for(int y=0;y<10;++y)for(int x=0;x<12;++x){
      const double dx=x+.5-6,dy=y+.5-5;
      const double u=(q*.5*dx+.5*dy)/3,v=(-.5*dx+q*.5*dy)/2;
      const bool expected=std::string(kind)=="rotated_ellipse" ? u*u+v*v<=1 : std::abs(u)<=1&&std::abs(v)<=1;
      check((actual.at(x,y)!=0)==expected,"Independent inverse-rotation membership");
    }
    for(double angle:{0.,90.}) {
      const auto changed=SelectionEditor(s,12,10).box_geometry({6,5},{6,4},angle);
      auto f=changed.box_frame();check(bool(f),"Numeric box frame missing");
      near(f->center[0],6,"Numeric center X");near(f->center[1],5,"Numeric center Y");
      near(f->u[0],angle==0?3:0,"Numeric literal Ux");near(f->u[1],angle==0?0:3,"Numeric literal Uy");
      near(f->v[0],angle==0?0:-2,"Numeric literal Vx");near(f->v[1],angle==0?2:0,"Numeric literal Vy");
      stylePreserved(changed);
    }
    const auto local=SelectionEditor(s,12,10).resized(3,{6+3*q,8});
    const auto lf=local.box_frame();check(bool(lf),"Local-axis resized frame missing");
    near(lf->center[0],6+.75*q,"Local resize literal center X");
    near(lf->center[1],5.75,"Local resize literal center Y");
    near(lf->u[0],2.25*q,"Local resize literal Ux");near(lf->u[1],2.25,"Local resize literal Uy");
    near(lf->v[0],-1,"Local resize literal Vx");near(lf->v[1],q,"Local resize literal Vy");stylePreserved(local);
    Fixture f(s);const auto literal=styled(Selection(kind,{{6,5},{6,8},{4,5}}));
    apply(f,SelectionEditor(s,12,10).box_geometry({6,5},{6,4},90));
    check(f.app.snapshot().data().rois[0].selection==literal,"Rotated service literal angle90 output");f.zeroReads();
  }
}
void masks() {
  const auto source=holedMask();
  const SelectionEditor editor(source,12,10);
  membership(source,[](int x,int y){return x>=2&&x<10&&y>=2&&y<8&&!(x>=4&&x<8&&y>=4&&y<6);},40);
  auto moved=editor.moved({1,-1});
  const auto expectedMove=packed(12,10,[](int x,int y){return x>=3&&x<11&&y>=1&&y<7&&!(x>=5&&x<9&&y>=3&&y<5);});
  check(moved.packed_mask()==expectedMove,"Literal holed packed translation");
  auto rotated=editor.rotated(90,std::array<double,2>{6,5});
  check(rotated.packed_mask()==packed(12,10,[](int x,int y){return x>=3&&x<9&&y>=1&&y<9&&!(x>=5&&x<7&&y>=3&&y<7);}),
        "Literal holed packed quarter turn");
  auto clipped=editor.moved({5,0},false);
  membership(clipped,[](int x,int y){return x>=7&&x<12&&y>=2&&y<8&&!(x>=9&&x<12&&y>=4&&y<6);},24);
  stylePreserved(moved);stylePreserved(rotated);stylePreserved(clipped);
  auto empty=styled(Selection::from_packed_mask(12,10,std::vector<unsigned char>(15,0)));
  SelectionEditor e(empty,12,10);
  check(e.handles().empty(),"Empty mask has interactive handles");
  check(e.moved({0,0})==empty,"Unchanged empty mask move differs");
  check(e.rotated(0)==empty,"Unchanged empty mask rotation differs");
  check(!e.hit_test({6,5},{.5,.5}),"Empty mask acquired an interior handle");
  for(const auto &candidate:{moved,rotated,clipped}) {
    Fixture f(source);const auto result=apply(f,candidate);
    check(result.changed&&f.app.snapshot().data().rois[0].selection==candidate,"Mask metadata publication differs");f.zeroReads();
  }
  Fixture f(empty);const Before before(f);const auto result=apply(f,empty);
  check(!result.changed,"Empty unchanged mask claimed a transaction");unchanged(f,before);
}

SavedRoiGeometryPublication apply(Fixture &f,const Selection &s,std::size_t row) {
  auto capture=f.app.capture_saved_roi_geometry();
  const auto command=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(row,s));
  auto prepared=f.app.prepare_saved_roi_geometry(capture,command);
  check(prepared.active(),"Fresh prepared action is inactive");
  check(prepared.working_bytes()>0,"Prepared action has no declared working storage");
  check(metadata_equal(prepared.record(),command.record()),"Prepared record differs from resolved intent");
  const auto result=f.app.publish_saved_roi_geometry(prepared);
  check(!prepared.active(),"Terminal publication retained an active prepared action");
  check(prepared.retained_working_bytes()==0,"Terminal publication retained logical working storage");
  return result;
}
template<class F> std::string invalidMessage(F &&call) {
  try { call(); } catch(const std::invalid_argument &e) { return e.what(); }
  throw std::runtime_error(current+": Expected invalid_argument");
}
void navigate(Fixture &f,int t,int c,int z) {
  auto d=f.app.snapshot().display(); d.t=t;d.c=c;d.crosshair[2]=z;
  f.app.set_display(std::move(d));
}
void rowPositions() {
  Fixture f; const Before before(f);
  auto wildcard=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,replacement()));
  check(wildcard.position().t==1&&wildcard.position().c==2&&wildcard.position().z==2,
        "Wildcard row did not capture actual TCZ");
  auto original=object(wildcard.record().at("original"));
  auto roi=object(original.at("roi"));
  for(const char *axis:{"t","c","z"})
    check(std::holds_alternative<std::monostate>(roi.at(axis)),"Wildcard was encoded as explicit zero");
  const auto e=styled(Selection("ellipse",{{1,3},{7,9}}));
  rejects([&]{resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(1,e));},
          "Constrained explicit-zero row resolved while invisible");
  unchanged(f,before);
  navigate(f,0,0,0);
  const auto zero=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(1,e));
  const auto zr=object(object(zero.record().at("original")).at("roi"));
  for(const char *axis:{"t","c","z"})
    check(std::get<std::int64_t>(zr.at(axis))==0,"Explicit zero lost in canonical record");
  navigate(f,1,0,2);
  auto mixed=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(2,polygon()));
  check(mixed.row_index()==2 && mixed.position().c==0,"Mixed wildcard C did not retain actual plane");
  navigate(f,0,0,2);
  rejects([&]{resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(2,polygon()));},
          "Mixed positioned row ignored T constraint");
  rejects([&]{resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(3,replacement()));},
          "Out-of-range saved-row index was admitted");
  rejects([&]{resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,Selection("line",{{2,2},{8,7}},2.5)));},
          "Non-area replacement was admitted");
  auto changedStyle=replacement();changedStyle.width=4;
  rejects([&]{resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,changedStyle));},
          "Geometry-only action changed line width");
  changedStyle=replacement();changedStyle.attributes["future"]=true;
  rejects([&]{resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,changedStyle));},
          "Geometry-only action changed opaque attributes");
  Fixture line(styled(Selection("line",{{2,2},{8,7}})));
  rejects([&]{resolve_saved_roi_geometry(line.app.snapshot(),SavedRoiGeometryRequest(0,replacement()));},
          "Non-area original row was admitted");
  f.zeroReads();line.zeroReads();
}
void rowPreservation() {
  Fixture f; const auto before=f.app.snapshot();
  const auto result=apply(f,replacement());
  const auto after=f.app.snapshot();
  check(result.changed&&result.document_id==f.owner->id,"Changed publication returned wrong owner/outcome");
  check(after.data().rois.size()==3&&after.data().rois[0].selection==replacement(),"Wrong row replacement");
  check(after.data().rois[0].name==before.data().rois[0].name&&
        after.data().rois[0].t==before.data().rois[0].t&&after.data().rois[0].c==before.data().rois[0].c&&
        after.data().rois[0].z==before.data().rois[0].z&&
        metadata_equal(after.data().rois[0].attributes,before.data().rois[0].attributes),
        "Geometry changed target-row identity/positions/attributes");
  check(after.data().rois[1]==before.data().rois[1]&&after.data().rois[2]==before.data().rois[2],
        "Geometry changed unrelated rows");
  preserved(after,before);f.zeroReads();
}
void historyNoop() {
  Fixture f; const auto before=f.app.snapshot(); const auto h=f.owner->history();
  const Before originalState(f);
  check(!before.dirty()&&h.undo_steps==0&&h.redo_steps==1,"History fixture lacks clean populated redo");
  auto capture=f.app.capture_saved_roi_geometry();
  const auto command=resolve_saved_roi_geometry(before,SavedRoiGeometryRequest(0,replacement()));
  auto prepared=f.app.prepare_saved_roi_geometry(capture,command);
  unchanged(f,originalState);
  const auto result=f.app.publish_saved_roi_geometry(prepared);const auto after=f.app.snapshot();
  check(result.changed&&after.dirty()&&after.data_identity()!=before.data_identity(),"Changed action has no dirty data identity");
  check(after.revisions().metadata==before.revisions().metadata+1&&
        after.revisions().selection==before.revisions().selection+1&&
        after.revisions().pixels==before.revisions().pixels&&after.revisions().display==before.revisions().display,
        "Changed action did not advance exactly Metadata+Selection");
  check(f.owner->undo_steps()==1&&f.owner->redo_steps()==0,"Changed action history/redo not atomic");
  f.app.undo();const auto undone=f.app.snapshot();
  check(undone.data().rois==before.data().rois&&!undone.dirty()&&f.owner->redo_steps()==1,"Undo did not restore clean original row");
  preserved(undone,before);
  f.app.redo();check(f.app.snapshot().data().rois==after.data().rois,"Redo did not restore exact accepted rows");
  Fixture noop; const Before b(noop);
  auto out=apply(noop,rectangle());
  check(!out.changed&&out.document_id==noop.owner->id,"Exact no-op claimed changed publication");
  unchanged(noop,b);
  auto nc=noop.app.capture_saved_roi_geometry();
  const auto nochange=resolve_saved_roi_geometry(noop.app.snapshot(),SavedRoiGeometryRequest(0,rectangle()));
  auto np=noop.app.prepare_saved_roi_geometry(nc,nochange);navigate(noop,0,0,0);const Before navigated(noop);
  rejects<RevisionConflict>([&]{noop.app.publish_saved_roi_geometry(np);},"No-op bypassed current TCZ guard");
  check(!np.active()&&np.retained_working_bytes()==0,"Stale no-op retained prepared resources");
  unchanged(noop,navigated);f.zeroReads();

  // A nonzero history-byte policy remains active on metadata-only lazy owners.
  Fixture lazyBudget;
  lazyBudget.app.set_history_policy(2,960);
  const auto lazyBefore=lazyBudget.app.snapshot();const auto lazyHistory=lazyBudget.owner->history();
  check(lazyHistory.pixel_budget==960&&lazyHistory.step_limit==2,"Lazy history policy setup failed");
  apply(lazyBudget,replacement());const auto lazyAfter=lazyBudget.owner->history();
  check(lazyAfter.pixel_budget==960&&lazyAfter.step_limit==2&&lazyAfter.undo_steps==1&&lazyAfter.redo_steps==0,
        "Metadata-only lazy edit changed native byte/step policy or redo outcome");
  preserved(lazyBudget.app.snapshot(),lazyBefore);lazyBudget.zeroReads();
  lazyBudget.app.undo();check(lazyBudget.app.snapshot().data().rois==lazyBefore.data().rois,
                             "Budgeted lazy edit undo lost original row");lazyBudget.zeroReads();

  // One distinct 12x10 double-backed plane is exactly960 obsolete bytes. Keep
  // it at the byte boundary while a metadata-only redo is discarded. The saved
  // row edit must not invent an extra pixel buffer or prune that valid step.
  Fixture byteBudget(rectangle(),false);
  byteBudget.app.set_history_policy(2,960);
  auto pixels=byteBudget.app.begin_edit();Image changed(12,10,1,PixelType::UInt16);changed.at(0,0)=17;
  pixels.replace_plane({0,0,0},std::move(changed));byteBudget.app.commit(pixels);
  auto metadata=byteBudget.app.begin_edit();metadata.set_extension("history_marker",true);byteBudget.app.commit(metadata);
  byteBudget.app.undo();const auto byteBefore=byteBudget.app.snapshot();const auto hb=byteBudget.owner->history();
  check(hb.undo_steps==1&&hb.redo_steps==1&&hb.pixels.exact&&hb.pixels.obsolete_bytes==960&&hb.pixel_budget==960,
        "Pixel-budget fixture lacks exact one-plane obsolete history and populated redo");
  apply(byteBudget,replacement());const auto ha=byteBudget.owner->history();const auto byteAfter=byteBudget.app.snapshot();
  check(ha.undo_steps==2&&ha.redo_steps==0&&ha.pixels.exact&&ha.pixels.obsolete_bytes==960&&
        ha.pixel_budget==960&&ha.step_limit==2,"Metadata-only edit changed exact obsolete-pixel pruning at budget boundary");
  check(byteAfter.revisions().pixels==byteBefore.revisions().pixels,"Budgeted row edit advanced Pixels");
  preserved(byteAfter,byteBefore);
  byteBudget.app.undo();check(byteBudget.app.snapshot().data().rois==byteBefore.data().rois,
                             "First budgeted undo did not restore original saved row");
  byteBudget.app.undo();check(byteBudget.app.snapshot().data().dataset.plane({0,0,0})->at(0,0)==0,
                             "Metadata-only publication pruned the independently retained original pixel step");
}

void recordGrammar() {
  Fixture f;
  const auto record=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,replacement())).record();
  check(record.size()==7&&std::get<std::string>(record.at("schema"))=="imagejxx.saved-roi-geometry-command"&&
        std::get<std::int64_t>(record.at("version"))==1&&
        std::get<std::string>(record.at("operation"))=="replace_saved_roi_geometry","Exact record discriminator/keys");
  validate_saved_roi_geometry_record(record);
  const auto bad=[&](MetadataObject value) {
    rejects([&]{validate_saved_roi_geometry_record(value);},"Malformed record was admitted");
  };
  auto largest=record;
  largest["version"]=std::uint64_t(1);
  largest=editObject(largest,"source",[](auto &s){
    for(const char *k:{"id","pixels_revision","metadata_revision","selection_revision"})
      s[k]=std::numeric_limits<std::uint64_t>::max();
  });
  validate_saved_roi_geometry_record(largest);
  check(std::get<std::uint64_t>(object(largest.at("source")).at("id"))==std::numeric_limits<std::uint64_t>::max(),
        "Unsigned identity fixture lost exact bits");
  auto q=record;q["extra"]=false;bad(q);
  q=record;q.erase("replacement");bad(q);
  for(const MetadataValue &v:MetadataArray{true,1.0,std::int64_t(0),std::int64_t(2),std::string("1")}) {
    q=record;q["version"]=v;bad(q);
  }
  q=record;q["schema"]=std::string("unknown");bad(q);
  q=record;q["operation"]=std::string("select_roi");bad(q);
  for(const char *field:{"id","pixels_revision","metadata_revision","selection_revision","roi_count"}) {
    for(const MetadataValue &v:MetadataArray{std::monostate{},true,1.0,std::int64_t(-1),std::string("1")})
      bad(editObject(record,"source",[&](auto &s){s[field]=v;}));
  }
  for(const char *field:{"id","pixels_revision","metadata_revision","selection_revision","shape","type","sample_policy","roi_count"})
    bad(editObject(record,"source",[&](auto &s){s.erase(field);}));
  bad(editObject(record,"source",[](auto &s){s["id"]=std::uint64_t(0);}));
  bad(editObject(record,"source",[](auto &s){s["roi_count"]=std::uint64_t(0);}));
  bad(editObject(record,"source",[](auto &s){s["unknown"]=false;}));
  bad(editObject(record,"source",[](auto &s){s["type"]=std::string("uint32");}));
  bad(editObject(record,"source",[](auto &s){s["sample_policy"]=std::string("anything");}));
  for(const auto &shape:std::vector<MetadataArray>{{2,3,3,10,12},{2,3,3,10,12,0},{2,3,3,10,12,true},
                                                {2,3,3,10,12,1.0},{2,3,3,10,12,std::uint64_t(-1)}})
    bad(editObject(record,"source",[&](auto &s){s["shape"]=metadata_array(shape);}));
  for(const char *axis:{"t","c","z"}) {
    for(const MetadataValue &v:MetadataArray{true,0.0,std::int64_t(-1),std::uint64_t(-1)})
      bad(editObject(record,"position",[&](auto &p){p[axis]=v;}));
    bad(editObject(record,"position",[&](auto &p){p.erase(axis);}));
  }
  bad(editObject(record,"position",[](auto &p){p["extra"]=false;}));
  bad(editObject(record,"position",[](auto &p){p["z"]=std::int64_t(3);}));
  for(const MetadataValue &v:MetadataArray{true,0.0,std::int64_t(-1),std::uint64_t(3)})
    bad(editObject(record,"original",[&](auto &o){o["index"]=v;}));
  bad(editObject(record,"original",[](auto &o){o["extra"]=true;}));
  const auto mutateRoi=[&](auto change) {
    return editObject(record,"original",[&](auto &o){auto r=object(o.at("roi"));change(r);o["roi"]=metadata_object(std::move(r));});
  };
  bad(mutateRoi([](auto &r){r["extra"]=false;}));
  bad(mutateRoi([](auto &r){r.erase("t");}));
  bad(mutateRoi([](auto &r){r["t"]=true;}));
  bad(mutateRoi([](auto &r){r["z"]=std::int64_t(3);}));
  bad(mutateRoi([](auto &r){r["name"]=false;}));
  bad(mutateRoi([](auto &r){r["attributes"]=true;}));
  for(const char *field:{"kind","points","width","attributes"}) {
    bad(editObject(record,"replacement",[&](auto &s){s.erase(field);}));
  }
  bad(editObject(record,"replacement",[](auto &s){s["extra"]=false;}));
  bad(editObject(record,"replacement",[](auto &s){s["kind"]=std::string("line");}));
  bad(editObject(record,"replacement",[](auto &s){s["width"]=true;}));
  bad(editObject(record,"replacement",[](auto &s){s["width"]=4.0;}));
  bad(editObject(record,"replacement",[](auto &s){s["points"]=metadata_array({metadata_array({3.0,1.0,9.0})});}));
  bad(editObject(record,"replacement",[](auto &s){s["points"]=metadata_array({metadata_array({true,1.0}),metadata_array({9.0,6.0})});}));
  // Native MetadataTree rejects nested nonfinite values during construction.
  // Keep construction inside the refusal boundary; never forge a mutable node.
  rejects([&]{
    auto nonfinite=editObject(record,"replacement",[](auto &s){
      s["points"]=metadata_array({metadata_array({std::numeric_limits<double>::infinity(),1.0}),
                                  metadata_array({9.0,6.0})});
    });
    validate_saved_roi_geometry_record(nonfinite);
  },"Nonfinite coordinate passed native metadata/record admission");
  auto nonfiniteTop=record;nonfiniteTop["version"]=std::numeric_limits<double>::infinity();
  bad(nonfiniteTop);
  bad(editObject(record,"replacement",[](auto &s){s["attributes"]=metadata_object({{"replacement",true}});}));
  Fixture mask(holedMask());
  const auto mr=resolve_saved_roi_geometry(mask.app.snapshot(),SavedRoiGeometryRequest(0,holedMask())).record();
  bad(editObject(mr,"replacement",[](auto &s){s["mask"]=std::string("");}));
  bad(editObject(mr,"replacement",[](auto &s){s["shape"]=metadata_array({10,11});}));
  bad(editObject(mr,"replacement",[](auto &s){s["shape"]=metadata_array({true,12});}));
  // Independent 3x3 wire fixture: two bytes, seven unused tail bits.
  auto tr=editObject(mr,"source",[](auto &s){s["shape"]=metadata_array({2,3,3,3,3,1});});
  tr=editObject(tr,"original",[](auto &o){
    auto r=object(o.at("roi"));auto s=object(r.at("selection"));
    s["shape"]=metadata_array({3,3});s["mask"]=std::string("gIA=");
    r["selection"]=metadata_object(std::move(s));o["roi"]=metadata_object(std::move(r));
  });
  tr=editObject(tr,"replacement",[](auto &s){s["shape"]=metadata_array({3,3});s["mask"]=std::string("gIA=");});
  validate_saved_roi_geometry_record(tr);
  bad(editObject(tr,"replacement",[](auto &s){s["mask"]=std::string("gIE=");}));
  f.zeroReads();mask.zeroReads();
}

void strictReplay() {
  Fixture f;const Before initial(f);
  const auto original=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,replacement())).record();
  unchanged(f,initial);
  // Historical Selection and Display provenance must not overwrite fresh state.
  f.app.select(Selection("rectangle",{{9,0},{11,2}}));navigate(f,0,1,1);
  const auto fresh=f.app.snapshot();
  auto replay=saved_roi_geometry_from_record(fresh,original);
  check(replay.is_replay()&&metadata_equal(replay.record(),original),"Replay rewrote historical provenance");
  check(replay.source().revisions().matches(fresh.revisions(),Change::All)&&
        replay.position().t==0&&replay.position().c==1&&replay.position().z==1,"Replay did not capture fresh guards");
  auto capture=f.app.capture_saved_roi_geometry();
  const Before before(f);
  auto prepared=f.app.prepare_saved_roi_geometry(capture,replay);
  unchanged(f,before);
  const auto result=f.app.publish_saved_roi_geometry(prepared);
  check(result.changed&&f.app.snapshot().data().rois[0].selection==replacement(),"Replay did not publish stored geometry");
  const auto after=f.app.snapshot();preserved(after,fresh);
  check(after.display().t==0&&after.display().c==1&&after.display().crosshair[2]==1,"Replay restored historical navigation");
  const Before done(f);
  rejects<RevisionConflict>([&]{saved_roi_geometry_from_record(f.app.snapshot(),original);},"Same record replayed after Metadata advanced");
  unchanged(f,done);
}

void rebindReopened() {
  // SESSION_FORMAT rejects scientific Fourier state without position binding.
  // Source-preservation cases retain it; persistence uses the admitted profile.
  Fixture f(rectangle(),false,PixelType::UInt16,1,false);
  const auto original=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,replacement())).record();
  SessionCapture input{{f.app.snapshot()},{},{metadata_object(original)},{}};
  const auto encoded=encode_session(input);auto decoded=decode_session(encoded);
  check(decoded.documents.size()==1&&decoded.documents[0]->id!=f.owner->id,"NativeV3 did not create a fresh owner");
  check(metadata_equal(object(decoded.workflow.at(0)),original),"NativeV3 changed workflow provenance");
  Application reopened;reopened.adopt(decoded.documents[0]);const auto before=reopened.snapshot();
  rejects<RevisionConflict>([&]{saved_roi_geometry_from_record(before,original);},"Fresh owner accepted strict old identity");
  auto fresh=styled(Selection("rectangle",{{4,3},{8,7}}));
  auto rebound=rebind_saved_roi_geometry(before,original,SavedRoiGeometryRequest(0,fresh));
  check(!rebound.is_replay()&&rebound.source().id()==before.id()&&rebound.replacement()==fresh,
        "Complete rebind borrowed old owner/result");
  auto capture=reopened.capture_saved_roi_geometry();auto prepared=reopened.prepare_saved_roi_geometry(capture,rebound);
  const auto outcome=reopened.publish_saved_roi_geometry(prepared);
  check(outcome.changed&&reopened.snapshot().data().rois[0].selection==fresh,"Rebound explicit replacement not published");
  auto malformed=original;malformed["unknown"]=true;
  const auto expected=invalidMessage([&]{validate_saved_roi_geometry_record(malformed);});
  const auto actual=invalidMessage([&]{rebind_saved_roi_geometry(reopened.snapshot(),malformed,
                               SavedRoiGeometryRequest(std::numeric_limits<std::size_t>::max(),Selection("line",{{0,0},{1,1}})));});
  check(actual==expected,"Rebind inspected new invalid request before complete old record");
  const auto r=editObject(original,"source",[](auto &s){s["roi_count"]=std::uint64_t(2);});
  rejects<RevisionConflict>([&]{saved_roi_geometry_from_record(f.app.snapshot(),r);},"Replay trusted false source row count");
  f.zeroReads();
}

void activationAndMetadata() {
  {
    Fixture f;auto capture=f.app.capture_saved_roi_geometry();
    const auto command=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,replacement()));
    auto prepared=f.app.prepare_saved_roi_geometry(capture,command);
    auto other=std::make_shared<Document>(Dataset(std::make_shared<UnreadSource>()));f.app.adopt(other);
    const Before before(f);
    rejects<RevisionConflict>([&]{f.app.publish_saved_roi_geometry(prepared);},"Inactive captured owner published");
    check(!prepared.active()&&prepared.retained_working_bytes()==0,"Stale publication retained prepared storage");
    unchanged(f,before);
  }
  {
    Fixture f;auto capture=f.app.capture_saved_roi_geometry();
    const auto command=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,replacement()));
    Application foreign;foreign.adopt(f.owner);
    const Before before(f);
    rejects<DocumentUnavailable>([&]{foreign.prepare_saved_roi_geometry(capture,command);},"Foreign Application consumed capture");
    unchanged(f,before);
  }
  {
    Fixture f;auto capture=f.app.capture_saved_roi_geometry();
    const auto command=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,replacement()));
    auto prepared=f.app.prepare_saved_roi_geometry(capture,command);
    f.app.close(f.owner->id,true);const Before before(f);
    rejects<DocumentUnavailable>([&]{f.app.publish_saved_roi_geometry(prepared);},"Closed owner published saved geometry");
    check(!prepared.active()&&prepared.retained_working_bytes()==0,"Closed publication retained prepared storage");
    unchanged(f,before);
  }
  for(int kind=0;kind<3;++kind) {
    Fixture f;auto capture=f.app.capture_saved_roi_geometry();
    const auto command=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,replacement()));
    auto prepared=f.app.prepare_saved_roi_geometry(capture,command);
    auto tx=f.app.begin_edit();
    if(kind==0)tx.set_extension("intervening",true);
    else {auto rows=f.app.snapshot().data().rois;
      if(kind==1)std::swap(rows[0],rows[1]);else rows.erase(rows.begin());tx.set_rois(std::move(rows));}
    f.app.commit(tx);const Before before(f);
    rejects<RevisionConflict>([&]{f.app.publish_saved_roi_geometry(prepared);},"Metadata/reordered/deleted row published stale edit");
    check(!prepared.active()&&prepared.retained_working_bytes()==0,"Metadata conflict retained prepared storage");
    unchanged(f,before);
  }
}
void dataAndPositionGuards() {
  for(int kind=0;kind<3;++kind) {
    Fixture f(rectangle(),kind!=0);auto capture=f.app.capture_saved_roi_geometry();
    const auto command=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,replacement()));
    auto prepared=f.app.prepare_saved_roi_geometry(capture,command);
    if(kind==0) {
      auto tx=f.app.begin_edit();Image p(12,10,1,PixelType::UInt16);p.at(0,0)=17;
      tx.replace_plane({1,2,2},std::move(p));f.app.commit(tx);
    } else if(kind==1)f.app.select(Selection("rectangle",{{0,1},{2,3}}));
    else navigate(f,1,2,1);
    const Before before(f);
    rejects<RevisionConflict>([&]{f.app.publish_saved_roi_geometry(prepared);},"Pixels/Selection/TCZ guard was bypassed");
    check(!prepared.active()&&prepared.retained_working_bytes()==0,"Guard refusal retained prepared lease");
    unchanged(f,before);
  }
  // Capture is not reminted at preparation after a conflicting Data edit.
  Fixture f;auto capture=f.app.capture_saved_roi_geometry();
  auto tx=f.app.begin_edit();tx.set_property("later",true);f.app.commit(tx);
  const auto fresh=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,replacement()));
  const Before before(f);
  rejects<RevisionConflict>([&]{f.app.prepare_saved_roi_geometry(capture,fresh);},"Preparation refreshed stale capture Data");
  unchanged(f,before);
}
void displayPolicy() {
  for(const auto interaction:{SavedRoiGeometryInteraction::Pointer,SavedRoiGeometryInteraction::Numeric}) {
    Fixture f;auto capture=f.app.capture_saved_roi_geometry();
    const auto command=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,replacement()));
    auto prepared=f.app.prepare_saved_roi_geometry(capture,command,interaction);
    auto display=f.app.snapshot().display();display.zoom=3;display.pan[0]={8,-2};display.crosshair[0]=4;
    f.app.set_display(display);const Before before(f);
    if(interaction==SavedRoiGeometryInteraction::Pointer) {
      rejects<RevisionConflict>([&]{f.app.publish_saved_roi_geometry(prepared);},"Pointer accepted changed mouse-down Display");
      unchanged(f,before);
    } else {
      const auto result=f.app.publish_saved_roi_geometry(prepared);const auto after=f.app.snapshot();
      check(result.changed&&after.display().zoom==3&&after.display().pan[0]==std::array<double,2>{8,-2}&&
            after.display().crosshair[0]==4&&after.revisions().display==before.snapshot.revisions().display,
            "Numeric edit overwrote fresh camera/crosshair Display");
      preserved(after,before.snapshot);f.zeroReads();
    }
    check(!prepared.active()&&prepared.retained_working_bytes()==0,"Terminal Display outcome retained storage");
  }
  // A newly minted capture must not authenticate an older pointer transform.
  Fixture f;
  const auto old=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,replacement()));
  auto display=f.app.snapshot().display();display.zoom=4;display.pan[0]={-3,7};f.app.set_display(display);
  auto fresh=f.app.capture_saved_roi_geometry();const Before before(f);
  rejects<RevisionConflict>([&]{f.app.prepare_saved_roi_geometry(fresh,old,SavedRoiGeometryInteraction::Pointer);},
                            "Fresh capture authenticated old pointer Display source");
  unchanged(f,before);
  auto numeric=f.app.prepare_saved_roi_geometry(fresh,old,SavedRoiGeometryInteraction::Numeric);
  const auto result=f.app.publish_saved_roi_geometry(numeric);const auto after=f.app.snapshot();
  check(result.changed&&after.display().zoom==4&&after.display().pan[0]==std::array<double,2>{-3,7}&&
        after.revisions().display==before.snapshot.revisions().display,"Numeric refresh lost fresh camera Display");
  check(!numeric.active()&&numeric.retained_working_bytes()==0,"Numeric refreshed publication retained storage");
  preserved(after,before.snapshot);f.zeroReads();
}
void roundtrips() {
  {
    Fixture f;auto capture=f.app.capture_saved_roi_geometry();
    const auto command=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,replacement()));
    auto prepared=f.app.prepare_saved_roi_geometry(capture,command);
    auto other=std::make_shared<Document>(Dataset(std::make_shared<UnreadSource>()));
    f.app.adopt(other);f.app.activate(f.owner->id);const Before before(f);
    rejects<RevisionConflict>([&]{f.app.publish_saved_roi_geometry(prepared);},"Owner away/back restored stale activation");
    unchanged(f,before);
  }
  {
    Fixture f;auto capture=f.app.capture_saved_roi_geometry();
    const auto command=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,replacement()));
    auto other=std::make_shared<Document>(Dataset(std::make_shared<UnreadSource>()));
    f.app.adopt(other);f.app.activate(f.owner->id);const Before before(f);
    rejects<RevisionConflict>([&]{f.app.prepare_saved_roi_geometry(capture,command);},"Prepare reminted away/back activation");
    unchanged(f,before);
  }
  // Direct headless TCZ roundtrip is explicitly an end-state guard, not an epoch.
  Fixture f;auto capture=f.app.capture_saved_roi_geometry();
  const auto command=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,replacement()));
  auto prepared=f.app.prepare_saved_roi_geometry(capture,command,SavedRoiGeometryInteraction::Numeric);
  navigate(f,0,0,0);navigate(f,1,2,2);const auto fresh=f.app.snapshot();
  const auto result=f.app.publish_saved_roi_geometry(prepared);
  check(result.changed&&f.app.snapshot().display().t==1&&f.app.snapshot().display().c==2&&
        f.app.snapshot().display().crosshair[2]==2,"Numeric TCZ roundtrip end-state was rejected or overwritten");
  check(f.app.snapshot().revisions().display==fresh.revisions().display,"TCZ publication invented Display advance");
  f.zeroReads();
}

void limitsAndPrecancel() {
  Fixture f;const Before before(f);SavedRoiGeometryRequest request(0,replacement());
  const auto estimate=estimate_saved_roi_geometry(f.app.snapshot(),request);
  check(estimate.xy_pixels==120&&estimate.vertices==4,"Estimate fixture XY/vertex count differs");
  check(estimate.source_data_bytes>0&&estimate.roi_bytes>0&&estimate.original_selection_bytes>0&&
        estimate.replacement_selection_bytes>0&&estimate.record_metadata_bytes>0&&
        estimate.record_wire_bytes>0&&estimate.fixed_bytes>0&&estimate.working_bytes>estimate.roi_bytes,
        "Logical estimate omitted owned data/rows/record/fixed holders");
  for(int kind=0;kind<5;++kind) {
    SavedRoiGeometryLimits limits;
    if(kind==0)limits.max_xy_pixels=119;
    if(kind==1)limits.max_vertices=1;
    if(kind==2)limits.max_working_bytes=estimate.working_bytes-1;
    if(kind==3)limits.max_record_bytes=1;
    if(kind==4)limits.metadata_depth=1;
    std::size_t callbacks=0;ExecutionControl control;
    control.checkpoint=[&]{++callbacks;};control.progress=[&](auto,auto){++callbacks;};
    control.stage_progress=[&](auto,auto,auto){++callbacks;};
    rejects([&]{resolve_saved_roi_geometry(f.app.snapshot(),request,limits,control);},"Preflight cap was ignored");
    check(callbacks==0,"Invalid cap dispatched callbacks before rejection");
    unchanged(f,before);
  }
  auto exact=SavedRoiGeometryLimits{};exact.max_working_bytes=estimate.working_bytes;
  check(resolve_saved_roi_geometry(f.app.snapshot(),request,exact).estimate().working_bytes==estimate.working_bytes,
        "Exact declared working budget did not admit source");
  auto token=std::make_shared<CancellationToken>();token->cancel();
  ExecutionControl canceled;canceled.cancellation=token;std::size_t callbacks=0;
  canceled.checkpoint=[&]{++callbacks;};canceled.progress=[&](auto,auto){++callbacks;};
  canceled.stage_progress=[&](auto,auto,auto){++callbacks;};
  rejects<OperationCancelled>([&]{resolve_saved_roi_geometry(f.app.snapshot(),request,{},canceled);},"Valid precancel did not abort");
  rejects([&]{resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(99,replacement()),{},canceled);},
          "Malformed request did not precede precancellation");
  check(callbacks==0,"Precancel invoked a caller callback");
  auto malformed=resolve_saved_roi_geometry(f.app.snapshot(),request).record();
  malformed=editObject(malformed,"source",[](auto &s){s["roi_count"]=std::numeric_limits<std::uint64_t>::max();});
  rejects([&]{validate_saved_roi_geometry_record(malformed);},"Untrusted row-count storage overflow admitted");
  auto huge=editObject(resolve_saved_roi_geometry(f.app.snapshot(),request).record(),"source",[](auto &s){
    const auto n=std::int64_t(std::numeric_limits<int>::max());s["shape"]=metadata_array({n,n,n,n,n,n});
  });
  rejects([&]{validate_saved_roi_geometry_record(huge);},"Untrusted shape arithmetic overflow admitted");
  SelectionEditLimits geometry;geometry.max_xy_pixels=119;
  rejects<std::length_error>([&]{SelectionEditor(rectangle(),12,10,geometry,canceled);},"Controlled geometry cap ignored");
  rejects<OperationCancelled>([&]{SelectionEditor(rectangle(),12,10,SelectionEditLimits{},canceled);},
                              "Valid controlled geometry precancel did not abort");
  check(callbacks==0,"Geometry preflight/precancel called callback");unchanged(f,before);

  // Application preparation additionally owns the actual history-holder vector;
  // the pure command bound alone does not reserve those private owner entries.
  Fixture holders;
  for(int i=0;i<3;++i) {
    auto tx=holders.app.begin_edit();tx.set_extension("holder"+std::to_string(i),true);holders.app.commit(tx);
  }
  check(holders.owner->undo_steps()==3,"History-holder fixture lacks three real retained steps");
  const Before holderBefore(holders);auto holderCapture=holders.app.capture_saved_roi_geometry();
  const auto holderCommand=resolve_saved_roi_geometry(holders.app.snapshot(),SavedRoiGeometryRequest(0,replacement()));
  const auto pureWorking=holderCommand.estimate().working_bytes;
  SavedRoiGeometryLimits pureCap;pureCap.max_working_bytes=pureWorking;
  std::size_t holderCallbacks=0;ExecutionControl holderControl;
  holderControl.checkpoint=[&]{++holderCallbacks;};
  holderControl.progress=[&](auto,auto){++holderCallbacks;};
  holderControl.stage_progress=[&](auto,auto,auto){++holderCallbacks;};
  rejects([&]{holders.app.prepare_saved_roi_geometry(holderCapture,holderCommand,SavedRoiGeometryInteraction::Numeric,pureCap,holderControl);},
          "Application omitted actual history-holder staging from its preparation budget");
  check(holderCallbacks==0,"History-holder cap refusal dispatched callbacks");unchanged(holders,holderBefore);
  {
    auto roomy=holders.app.prepare_saved_roi_geometry(holderCapture,holderCommand);
    check(roomy.active()&&roomy.working_bytes()>pureWorking&&roomy.retained_working_bytes()==roomy.working_bytes(),
          "Roomy Application preparation did not retain its additional history-holder cost");
    unchanged(holders,holderBefore);
  }
  unchanged(holders,holderBefore);

  // Numeric refresh retains the old command source too. Removing its large
  // Display payload cannot make that still-owned source disappear from D.
  Fixture displayOwner;
  auto oldDisplay=displayOwner.app.snapshot().display();
  oldDisplay.properties["old_opaque_payload"]=std::string(65536,'d');displayOwner.app.set_display(oldDisplay);
  const auto oldCommand=resolve_saved_roi_geometry(displayOwner.app.snapshot(),SavedRoiGeometryRequest(0,replacement()));
  auto smallDisplay=displayOwner.app.snapshot().display();smallDisplay.properties.erase("old_opaque_payload");
  displayOwner.app.set_display(smallDisplay);auto smallCapture=displayOwner.app.capture_saved_roi_geometry();
  const auto smallCommand=resolve_saved_roi_geometry(displayOwner.app.snapshot(),SavedRoiGeometryRequest(0,replacement()));
  check(oldCommand.estimate().source_data_bytes>smallCommand.estimate().source_data_bytes,
        "Display fixture does not distinguish retained old versus fresh D");
  std::size_t smallWorking=0;
  {
    auto admitted=displayOwner.app.prepare_saved_roi_geometry(smallCapture,smallCommand);
    smallWorking=admitted.working_bytes();
  }
  check(oldCommand.estimate().working_bytes>smallWorking+2,"Retained Display fixture lacks a discriminating budget interval");
  SavedRoiGeometryLimits displayCap;
  displayCap.max_working_bytes=smallWorking+(oldCommand.estimate().working_bytes-smallWorking)/2;
  const Before displayBefore(displayOwner);
  {
    auto freshOnly=displayOwner.app.prepare_saved_roi_geometry(smallCapture,smallCommand,SavedRoiGeometryInteraction::Numeric,displayCap);
    check(freshOnly.active()&&freshOnly.working_bytes()<=displayCap.max_working_bytes,
          "Discriminating cap does not admit the actual smaller fresh source");
  }
  std::size_t displayCallbacks=0;ExecutionControl displayControl;
  displayControl.checkpoint=[&]{++displayCallbacks;};
  displayControl.progress=[&](auto,auto){++displayCallbacks;};
  displayControl.stage_progress=[&](auto,auto,auto){++displayCallbacks;};
  rejects([&]{displayOwner.app.prepare_saved_roi_geometry(smallCapture,oldCommand,SavedRoiGeometryInteraction::Numeric,displayCap,displayControl);},
          "Numeric refresh omitted retained old command Display storage");
  check(displayCallbacks==0,"Retained Display cap refusal dispatched callbacks");unchanged(displayOwner,displayBefore);
  // Display can grow after Numeric preparation. Refuse only a working-budget
  // overflow, and admit the same fresh state with sufficient headroom.
  for (bool roomy : {false,true}) {
    Fixture f;
    const auto capture=f.app.capture_saved_roi_geometry();
    const auto command=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,replacement()));
    SavedRoiGeometryLimits limits;
    limits.max_working_bytes=command.estimate().working_bytes+(roomy ? 4*1024*1024 : 4096);
    auto prepared=f.app.prepare_saved_roi_geometry(capture,command,SavedRoiGeometryInteraction::Numeric,limits);
    auto display=f.app.snapshot().display();
    display.properties["added_after_prepare"]=std::string(65536,'q');
    display.pan[0]={9,7};
    f.app.set_display(display);
    const Before before(f);
    if(roomy) {
      const auto result=f.app.publish_saved_roi_geometry(prepared);
      const auto after=f.app.snapshot();
      check(result.changed && after.data().rois[0].selection==replacement(),
            "Roomy Numeric publication refused fresh opaque Display");
      check(after.display().pan==display.pan &&
            metadata_equal(after.display().properties,display.properties),
            "Numeric publication replaced fresh opaque Display");
    } else {
      rejects<std::invalid_argument>([&]{f.app.publish_saved_roi_geometry(prepared);},
                                    "Fresh Display copy escaped working budget");
      unchanged(f,before);
    }
    check(!prepared.active() && prepared.retained_working_bytes()==0,
          "Fresh Display terminal publication retained staging");
    f.zeroReads();
  }
}

class CallbackFailure final : public std::runtime_error {
public: CallbackFailure():std::runtime_error("independent preparation callback") {}
};
class ConstraintRefusal final : public std::runtime_error {
public: ConstraintRefusal():std::runtime_error("retained saved-row constraint") {}


};
void callbacksAndConstraint() {
  Fixture f;const Before original(f);
  SavedRoiGeometryRequest request(0,replacement());SavedRoiGeometryLimits limits;
  ExecutionControl control;auto token=std::make_shared<CancellationToken>();control.cancellation=token;
  std::size_t callbacks=0;bool mutated=false;
  control.checkpoint=[&] {
    ++callbacks;
    check(f.app.snapshot().id()==f.owner->id,"Preparation callback could not read controller");
    if(!mutated) {
      mutated=true;request.row_index=99;request.replacement=Selection("line",{{0,0},{1,1}});
      limits.max_working_bytes=1;
      auto later=std::make_shared<CancellationToken>();later->cancel();control.cancellation=later;
      control.progress=[](auto,auto){throw CallbackFailure();};
    }
  };
  const auto command=resolve_saved_roi_geometry(f.app.snapshot(),request,limits,control);
  check(callbacks>0&&mutated&&command.row_index()==0&&command.replacement()==replacement(),
        "Resolve did not own caller request/control/limits before callback");
  check(command.limits().max_working_bytes==268435456&&!token->canceled(),"Caller mutation changed frozen limits/control");
  unchanged(f,original);
  ExecutionControl failure;failure.checkpoint=[] {throw CallbackFailure();};
  rejects<CallbackFailure>([&]{resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,replacement()),{},failure);},
                           "Resolve swallowed checkpoint exception");
  auto capture=f.app.capture_saved_roi_geometry();
  rejects<CallbackFailure>([&]{f.app.prepare_saved_roi_geometry(capture,command,SavedRoiGeometryInteraction::Numeric,{},failure);},
                           "Prepare swallowed checkpoint exception");
  unchanged(f,original);

  // Bounded raster work must poll after positive progress, not only at entry.
  Fixture mask(holedMask());const Before maskBefore(mask);
  SelectionEditLimits geometry;const SelectionEditor editor(holedMask(),12,10,geometry);
  ExecutionControl during;during.cancellation=std::make_shared<CancellationToken>();
  bool positive=false;std::size_t reports=0;
  during.stage_progress=[&](std::string_view,std::size_t done,std::size_t total) {
    ++reports;check(done<=total,"Invalid controlled geometry stage bounds");
    check(mask.app.snapshot().id()==mask.owner->id,"Raster callback held publication lock");
    if(done>0) {positive=true;during.cancellation->cancel();}
  };
  rejects<OperationCancelled>([&]{editor.moved({1,-1},false,geometry,during);},
                              "Mid-raster cancellation returned a publishable geometry");
  check(reports>0&&positive,"Cancellation did not observe positive bounded raster progress");
  unchanged(mask,maskBefore);

  // Existing nonreentrant Document::constrain leases are the clarification003
  // exception: they run inside commit before mutation and cannot be bypassed.
  std::size_t rejected=0;
  const auto lease=f.owner->constrain([&](const DocumentData &data,const DisplayState &) {
    if(data.rois[0].selection==replacement()) {++rejected;throw ConstraintRefusal();}
  });
  auto retainedCapture=f.app.capture_saved_roi_geometry();
  ExecutionControl readback;std::size_t preparationCallbacks=0;
  readback.checkpoint=[&]{++preparationCallbacks;check(f.app.snapshot().id()==f.owner->id,"Application prepare callback held its lock");};
  auto prepared=f.app.prepare_saved_roi_geometry(retainedCapture,command,SavedRoiGeometryInteraction::Numeric,{},readback);
  check(preparationCallbacks>0,"Application preparation did not dispatch its checkpoint");
  const auto beforePublicationCallbacks=preparationCallbacks;
  check(prepared.active()&&prepared.retained_working_bytes()>0,"Prepared constrained action did not retain working storage");
  rejects<ConstraintRefusal>([&]{f.app.publish_saved_roi_geometry(prepared);},"Retained validator rejection was bypassed");
  check(rejected==1&&bool(lease),"Rejecting constraint was not invoked exactly once at publication");
  check(preparationCallbacks==beforePublicationCallbacks,"Publication redispatched preparation callbacks under owner locks");
  check(!prepared.active()&&prepared.retained_working_bytes()==0,"Constraint rejection retained prepared resources");
  unchanged(f,original);
  rejects([&]{f.app.publish_saved_roi_geometry(prepared);},"Consumed failed preparation was reusable");
  unchanged(f,original);
  bool armed=false;std::size_t noopRefusals=0;
  const auto noopLease=f.owner->constrain([&](const DocumentData &,const DisplayState &){
    if(armed){++noopRefusals;throw ConstraintRefusal();}
  });
  auto noopCapture=f.app.capture_saved_roi_geometry();
  const auto noop=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,rectangle()));
  auto noopPrepared=f.app.prepare_saved_roi_geometry(noopCapture,noop);armed=true;
  rejects<ConstraintRefusal>([&]{f.app.publish_saved_roi_geometry(noopPrepared);},"No-op bypassed retained constraint");
  check(noopRefusals==1&&bool(noopLease)&&!noopPrepared.active()&&noopPrepared.retained_working_bytes()==0,
        "No-op constraint refusal did not consume/release prepared action");
  unchanged(f,original);

  // Retiring the oldest history may release the last custom source. Its
  // deleter must observe the committed row after both owner locks are released.
  {
    Fixture f;
    f.owner->set_history_limit(1);
    std::size_t retired=0;
    {
      auto source=std::shared_ptr<const UnreadSource>(
          new UnreadSource(),[&](const UnreadSource *old) {
            check(old->planes==0 && old->regions==0,"Retired provider was read");
            const auto snapshot=f.app.snapshot();
            check(snapshot.data().rois[0].selection==replacement(),
                  "History source released before changed publication");
            ++retired;
            delete old;
          });
      auto tx=f.app.begin_edit();
      tx.replace_dataset(Dataset(std::move(source),calibration()));
      f.app.commit(tx);
    }
    {
      auto tx=f.app.begin_edit();
      tx.replace_dataset(Dataset(f.provider,calibration()));
      f.app.commit(tx);
    }
    check(retired==0,"History provider retired before saved-row edit");
    const auto command=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,replacement()));
    const auto capture=f.app.capture_saved_roi_geometry();
    auto prepared=f.app.prepare_saved_roi_geometry(capture,command);
    const auto result=f.app.publish_saved_roi_geometry(prepared);
    check(result.changed && retired==1,"Oldest history source was not retired after unlock");
    f.zeroReads();
  }
}

void allNativeLayouts() {
  for(const auto type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float32,PixelType::Float64})
    for(int samples:{1,3,4}) {
      Fixture f(rectangle(),true,type,samples);const auto before=f.app.snapshot();
      const auto result=apply(f,replacement());
      check(result.changed,"Native type/sample layout refused metadata edit");
      const auto after=f.app.snapshot();
      check(after.data().dataset.pixel_type()==type&&after.data().dataset.shape().s==samples,
            "Metadata edit changed declared type/sample layout");
      preserved(after,before);f.zeroReads();
      f.app.undo();check(f.app.snapshot().data().rois==before.data().rois,"Metadata-only native layout undo failed");
      f.app.redo();check(f.app.snapshot().data().rois[0].selection==replacement(),"Metadata-only native layout redo failed");
      f.zeroReads();
    }
  // Quiet policy is valid only on reviewed owned Float32 storage. Do not fake
  // opt-in on a custom provider, which Dataset deliberately refuses.
  Stack planes;
  std::uint32_t word=0x7fc00001u;float quiet;std::memcpy(&quiet,&word,sizeof quiet);
  for(int i=0;i<18;++i) {Image p(12,10,1,PixelType::Float32);p.at(1,1)=quiet;p.at(2,2)=-0.0;planes.push_back(std::move(p));}
  Dataset dataset=Dataset::from_planes({2,3,3,10,12,1},std::move(planes),calibration(),FloatingSamplePolicy::PreserveQuietNaNFloat32);
  auto owner=std::make_shared<Document>(initialData(dataset));Application app;app.adopt(owner);
  const auto before=app.snapshot();const auto tableBefore=before.data().dataset.shared_planes();
  check(bool(tableBefore),"Quiet fixture is not reviewed eager storage");
  auto capture=app.capture_saved_roi_geometry();
  const auto command=resolve_saved_roi_geometry(before,SavedRoiGeometryRequest(0,replacement()));
  auto prepared=app.prepare_saved_roi_geometry(capture,command);const auto out=app.publish_saved_roi_geometry(prepared);
  const auto after=app.snapshot();check(out.changed,"Quiet-policy metadata edit failed");
  check(after.data().dataset.sample_policy()==FloatingSamplePolicy::PreserveQuietNaNFloat32,
        "Metadata edit downgraded explicit quiet policy");
  const auto tableAfter=after.data().dataset.shared_planes();check(bool(tableAfter),"Quiet storage ceased to be eager");
  check(tableAfter->size()==18,"Quiet metadata edit changed plane count");
  for(std::size_t i=0;i<18;++i) {
    check(tableBefore->shared(i)==tableAfter->shared(i),"Quiet metadata edit replaced immutable pixel owner");
    check(std::memcmp((*tableBefore)[i].pixels.data(),(*tableAfter)[i].pixels.data(),
                      (*tableBefore)[i].pixels.size()*sizeof(double))==0,"Quiet/signed-zero source bits changed");
  }
  preserved(after,before);
}

void nativeV3Roundtrip() {
  // Preserve the existing codec boundary explicitly, without clearing a live
  // owner's spectrum to make Save/Open succeed or inventing Fourier positions.
  {
    Fixture unsupported(holedMask(),false);
    const Before unchangedSource(unsupported);
    SessionCapture invalid{{unsupported.app.snapshot()},{},{},{}};
    std::size_t callbacks=0;ExecutionControl control;
    control.checkpoint=[&]{++callbacks;};
    rejects([&]{encode_session(invalid,{},control);},
            "NativeV3 silently accepted unpositioned scientific Fourier state");
    check(callbacks==0,"Unsupported Fourier session ran callbacks before refusal");
    unchanged(unsupported,unchangedSource);
    check(unsupported.app.snapshot().data().spectrum.has_value(),
          "Session admission silently discarded unrelated spectrum");
  }
  Fixture f(holedMask(),false,PixelType::UInt16,1,false);const auto before=f.app.snapshot();
  auto expected=styled(Selection::from_packed_mask(12,10,packed(12,10,[](int x,int y){
    return x>=3&&x<11&&y>=1&&y<7&&!(x>=5&&x<9&&y>=3&&y<5);
  })));
  const auto record=resolve_saved_roi_geometry(before,SavedRoiGeometryRequest(0,expected)).record();
  const auto result=apply(f,expected);check(result.changed,"Mask fixture was a no-op");
  const auto source=f.app.snapshot();
  SessionCapture capture{{source},{metadata_object({{"result",std::int64_t(13)}})},
                         {metadata_object(record)},{{"future",metadata_array({false,std::uint64_t(-1)})}}};
  const auto bytes=encode_session(capture);const auto decoded=decode_session(bytes);
  check(decoded.source_version==3&&decoded.documents.size()==1,"NativeV3 framing/version/owner count");
  const auto reopened=decoded.documents[0]->snapshot();
  check(reopened.id()!=source.id()&&!reopened.dirty()&&decoded.documents[0]->undo_steps()==0&&
        decoded.documents[0]->redo_steps()==0,"Reopen did not create a fresh clean history-free owner");
  check(reopened.data().rois==source.data().rois&&reopened.data().rois[0].selection.packed_mask()==expected.packed_mask(),
        "V3 row geometry/packed holes/opaque attributes changed");
  check(!reopened.data().rois[0].t&&!reopened.data().rois[0].c&&!reopened.data().rois[0].z&&
        reopened.data().rois[1].t==0&&reopened.data().rois[1].c==0&&reopened.data().rois[1].z==0&&
        reopened.data().rois[2].t==1&&!reopened.data().rois[2].c&&reopened.data().rois[2].z==2,
        "V3 collapsed explicit zero, null wildcard or mixed positions");
  check(reopened.data().selection==source.data().selection&&reopened.data().previous_selection==source.data().previous_selection&&
        reopened.data().metadata==source.data().metadata,"V3 changed global selections or opaque metadata");
  check(metadata_equal(decoded.workflow,capture.workflow)&&metadata_equal(decoded.results,capture.results)&&
        metadata_equal(decoded.extensions,capture.extensions),"V3 changed/ran workflow, results or future extension");
  const auto a=source.data().dataset.shared_planes(),b=reopened.data().dataset.shared_planes();
  check(a&&b&&a->size()==b->size(),"V3 eager plane table changed");
  for(std::size_t i=0;i<a->size();++i)
    check((*a)[i].pixels==(*b)[i].pixels,"V3 changed calibrated scientific fixture sample bytes");
  check(reopened.data().dataset.shape().dimensions()==std::array<int,6>{2,3,3,10,12,1}&&
        reopened.data().dataset.pixel_type()==PixelType::UInt16&&
        reopened.data().dataset.calibration().spacing==std::array<double,3>{.5,1.25,2.5}&&
        reopened.data().dataset.calibration().origin==std::array<double,3>{11,-7,19}&&
        reopened.data().dataset.calibration().direction==std::array<double,9>{1,0,0,0,1,0,0,0,1}&&
        reopened.data().dataset.calibration().unit=="um"&&reopened.data().dataset.calibration().time_unit=="s"&&
        reopened.data().dataset.calibration().frame_interval==.75&&reopened.data().dataset.calibration().time_origin==2.25,
        "V3 lost scientific axes/calibration/time metadata");
  rejects<RevisionConflict>([&]{saved_roi_geometry_from_record(reopened,record);},"V3 reopen silently rebound old workflow identity");
  check(source.data().rois[0].selection==expected&&before.data().rois[0].selection==holedMask(),
        "Source snapshot/accepted geometry lifetime changed during codec");
}

struct Case {const char *id;void (*run)();};
const std::array<Case,16> cases{{
 {"N01.area_kinds_and_literal_geometry",areaKinds},
 {"N02.mask_translate_rotate_clip_empty",masks},
 {"N03.row_positions_zero_null_and_original_validation",rowPositions},
 {"N04.current_previous_and_other_rows_preserved",rowPreservation},
 {"N05.changed_history_dirty_and_noop",historyNoop},
 {"N06.strict_record_integer_field_variant_grammar",recordGrammar},
 {"N07.strict_replay_before_publication_and_stale_repeat",strictReplay},
 {"N08.full_rebind_original_validation_and_reopened_owner",rebindReopened},
 {"N09.activation_foreign_closed_and_metadata_stale",activationAndMetadata},
 {"N10.pixels_selection_and_tcz_atomic_guards",dataAndPositionGuards},
 {"N11.pointer_display_stale_numeric_camera_fresh",displayPolicy},
 {"N12.owner_roundtrip_stale_tcz_roundtrip_end_guard",roundtrips},
 {"N13.preflight_limits_overflow_precancel_zero_read",limitsAndPrecancel},
 {"N14.callback_owned_capture_exception_midcancel",callbacksAndConstraint},
 {"N15.all_native_types_quiet_policy_zero_provider_reads",allNativeLayouts},
 {"N16.native_v3_positions_attributes_masks_workflow_roundtrip",nativeV3Roundtrip}
}};
} // namespace

int main() {
  try {
    for(const auto &entry:cases) {
      current=entry.id;entry.run();completed.push_back(current);
      std::cout<<"PASS "<<current<<'\n';
    }
    check(completed.size()==16,"Frozen native case membership incomplete");
    std::cout<<"Saved ROI geometry: "<<completed.size()<<" cases / "<<checks<<" checks completed\n";
    return 0;
  } catch(const std::exception &e) {
    std::cerr<<e.what()<<" ("<<completed.size()<<" completed / "<<checks<<" checks)\n";
    return 1;
  }
}
