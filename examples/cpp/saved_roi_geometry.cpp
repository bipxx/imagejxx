// Installed core-only SDK consumer for the owned saved-area geometry contract.
// Expected coordinates/memberships are literal, independent of SelectionEditor.
// No GUI, Python, JSON library or private imagejxx header is used here.
#include <imagejxx/application.hpp>
#include <imagejxx/saved_roi_geometry.hpp>
#include <imagejxx/selection_edit.hpp>
#include <imagejxx/session.hpp>
#include "saved_roi_geometry_observations.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
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
std::string current;
saved_roi_geometry_observations::Collector observations;
template<class A,class B> std::int64_t difference(A after,B before) {
  return static_cast<std::int64_t>(after)-static_cast<std::int64_t>(before);
}
std::string point_text(const Selection &selection) {
  std::string value;
  for (const auto &point : selection.points) {
    if (!value.empty()) value+=';';
    value+=saved_roi_geometry_observations::decimal(point[0])+','+
        saved_roi_geometry_observations::decimal(point[1]);
  }
  return value;
}
std::size_t members(const Selection &selection) {
  std::size_t count=0;
  for (const auto byte : selection.packed_mask())
    for (unsigned bit=0;bit<8;++bit) count+=(byte>>bit)&1u;
  return count;
}
void require(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template<class Error = std::invalid_argument, class F>
std::string refuses(F &&work, const char *message) {
  try { work(); }
  catch (const Error &error) { ++checks; return error.what(); }
  throw std::runtime_error(message);
}
const MetadataObject &object(const MetadataValue &value) {
  const auto tree = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  require(tree && *tree, "Expected an immutable metadata object");
  const auto result = std::get_if<MetadataObject>(&(*tree)->value);
  require(result != nullptr, "Expected object, not metadata array");
  return *result;
}
template<class F>
MetadataObject edit_object(MetadataObject record, const char *key, F edit) {
  auto child = object(record.at(key));
  edit(child);
  record[key] = metadata_object(std::move(child));
  return record;
}
MetadataObject attributes() {
  return {{"label", std::string("SDK retained")},
      {"future", metadata_object({{"version", std::int64_t(9)},
          {"values", metadata_array({true, std::string("opaque")})},
          {"unsigned", std::numeric_limits<std::uint64_t>::max()}})}};
}
Selection styled(Selection value) {
  value.width = 2.5;
  value.attributes = attributes();
  return value;
}
Selection rectangle() { return styled(Selection("rectangle", {{2,2},{8,7}})); }
Selection moved_rectangle() { return styled(Selection("rectangle", {{3,1},{9,6}})); }
Selection ellipse() { return styled(Selection("ellipse", {{2,2},{8,8}})); }
Selection moved_ellipse() { return styled(Selection("ellipse", {{1,3},{7,9}})); }
template<class Predicate>
std::vector<unsigned char> packed(Predicate inside) {
  // Independent literal fixture packing: 12x10 image cells, MSB first.
  std::vector<unsigned char> bytes(15, 0);
  for (int y = 0; y < 10; ++y) for (int x = 0; x < 12; ++x)
    if (inside(x,y)) {
      const auto index = std::size_t(y * 12 + x);
      bytes[index / 8] |= static_cast<unsigned char>(0x80u >> (index % 8));
    }
  return bytes;
}
Selection mask() {
  return styled(Selection::from_packed_mask(12,10,packed([](int x,int y) {
    return x>=2 && x<10 && y>=2 && y<8 && !(x>=4 && x<8 && y>=4 && y<6);
  })));
}
Selection moved_mask() {
  return styled(Selection::from_packed_mask(12,10,packed([](int x,int y) {
    return x>=3 && x<11 && y>=1 && y<7 && !(x>=5 && x<9 && y>=3 && y<5);
  })));
}
void geometry(const Selection &actual, const Selection &expected) {
  require(actual.kind == expected.kind && actual.width == 2.5 &&
              metadata_equal(actual.attributes, attributes()),
          "Area kind, width or opaque attributes changed");
  require(actual.points.size() == expected.points.size(), "Point cardinality differs");
  for (std::size_t i=0; i<expected.points.size(); ++i)
    for (int axis=0; axis<2; ++axis)
      require(std::isfinite(actual.points[i][axis]) &&
                  std::abs(actual.points[i][axis]-expected.points[i][axis]) <= 1e-11,
              "Literal geometry coordinates differ");
  require(actual.packed_mask() == expected.packed_mask(), "Literal packed mask bits differ");
}
SpatialCalibration calibration() {
  SpatialCalibration c;
  c.spacing={.5,1.25,2.5}; c.origin={11,-7,19}; c.unit="um";
  c.frame_interval=.75; c.time_origin=2.25; c.time_unit="s";
  return c;
}
bool same_calibration(const SpatialCalibration &a, const SpatialCalibration &b) {
  return a.spacing==b.spacing && a.origin==b.origin && a.direction==b.direction &&
      a.unit==b.unit && a.frame_interval==b.frame_interval &&
      a.time_origin==b.time_origin && a.time_unit==b.time_unit;
}
bool same_display(const DisplayState &a, const DisplayState &b, bool same_revision=true) {
  return a.t==b.t && a.c==b.c && a.crosshair==b.crosshair &&
      a.orthogonal==b.orthogonal && a.linkedZoom==b.linkedZoom &&
      a.zoom==b.zoom && a.planeZoom==b.planeZoom && a.pan==b.pan &&
      (!same_revision || a.revision==b.revision) && metadata_equal(a.properties,b.properties);
}
class UnreadSource final : public PlaneSource {
public:
  explicit UnreadSource(PixelType type=PixelType::UInt16, int samples=1)
      : type_(type), samples_(samples) {}
  DatasetShape shape() const override { return {2,3,3,10,12,samples_}; }
  PixelType pixel_type() const override { return type_; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++plane_reads;
    throw std::runtime_error("Metadata-only SDK action acquired an image plane");
  }
  Image read_region(PlanePosition,int,int,int,int) const override {
    ++region_reads;
    throw std::runtime_error("Metadata-only SDK action acquired an image region");
  }
  mutable std::size_t plane_reads=0, region_reads=0;
private:
  PixelType type_;
  int samples_;
};
Dataset eager(PixelType type=PixelType::UInt16, bool quiet=false) {
  Stack planes;
  for (int t=0;t<2;++t) for (int c=0;c<3;++c) for (int z=0;z<3;++z) {
    Image image(12,10,1,type);
    for (int y=0;y<10;++y) for (int x=0;x<12;++x) {
      const auto value=1000*t+100*c+10*z+2*y+x;
      image.at(x,y)=type==PixelType::UInt8 ? value%256 :
          type==PixelType::Float32 ? value/4.0 : value;
    }
    if (quiet && t==0 && c==0 && z==0)
      image.pixels[0]=static_cast<double>(std::numeric_limits<float>::quiet_NaN());
    planes.push_back(std::move(image));
  }
  return Dataset::from_planes({2,3,3,10,12,1},std::move(planes),calibration(),
      quiet ? FloatingSamplePolicy::PreserveQuietNaNFloat32 : FloatingSamplePolicy::FiniteOnly);
}
DocumentData data(Dataset dataset, Selection target, bool spectrum) {
  DocumentData d{std::move(dataset)};
  d.metadata.title="Installed saved-row SDK";
  d.metadata.properties["unrelated"]=metadata_object({{"value",std::int64_t(7)}});
  d.metadata.extensions["future"]=metadata_array({std::string("retained"),false});
  d.selection=Selection("rectangle",{{0,0},{1,1}},1);
  d.previous_selection=Selection("line",{{0,9},{11,0}},3);
  d.rois={{"target",std::move(target),{},{},{},{{"row",std::string("first")}}},
      {"explicit zero",ellipse(),0,0,0,{{"row",std::int64_t(2)}}},
      {"mixed",styled(Selection("polygon",{{2,2},{8,2},{8,7},{2,7}})),
          1,{},2,{{"row",std::int64_t(3)}}}};
  d.overlays={{{"opaque",metadata_array({std::int64_t(11),std::string("overlay")})}}};
  if (spectrum) d.spectrum=FourierState(Spectrum{1,1,{{3,4}}},std::array<int,2>{1,1});
  return d;
}
struct Fixture {
  Application app;
  std::shared_ptr<UnreadSource> provider;
  std::shared_ptr<Document> owner;
  explicit Fixture(Selection target=rectangle(), bool lazy=true,
                   PixelType type=PixelType::UInt16, int samples=1,
                   bool spectrum=true, bool quiet=false) {
    Dataset dataset=[&] {
      if (!lazy) return eager(type,quiet);
      provider=std::make_shared<UnreadSource>(type,samples);
      return Dataset(provider,calibration());
    }();
    DisplayState display;
    display.t=1; display.c=2; display.crosshair={6,5,2};
    display.orthogonal=true; display.zoom=1.5; display.pan[0]={2,3};
    display.properties["unrelated"]=std::string("display");
    owner=std::make_shared<Document>(data(std::move(dataset),std::move(target),spectrum),display);
    app.adopt(owner);
    auto setup=app.begin_edit(); setup.set_extension("temporary",true); app.commit(setup);
    app.undo(); app.mark_saved();
    require(owner->undo_steps()==0 && owner->redo_steps()==1 && !app.snapshot().dirty(),
            "Fixture must have a genuine clean baseline and populated redo");
  }
  void zero_reads() const {
    if (provider) require(provider->plane_reads==0 && provider->region_reads==0,
                          "SDK metadata path read provider pixels");
  }
};
struct Before {
  DocumentSnapshot snapshot;
  HistoryInfo history;
  std::vector<std::uint64_t> membership;
  std::uint64_t active;
  explicit Before(const Fixture &f) : snapshot(f.owner->snapshot()), history(f.owner->history()),
      membership(f.app.document_ids()), active(f.app.active_document_id()) {}
};
bool native_unchanged(const Fixture &f,const Before &before) {
  const auto now=f.owner->snapshot();const auto history=f.owner->history();
  return now.same_document(before.snapshot)&&now.shares_data(before.snapshot)&&
      now.revisions().matches(before.snapshot.revisions(),Change::All)&&
      now.dirty()==before.snapshot.dirty()&&now.data_identity()==before.snapshot.data_identity()&&
      same_display(now.display(),before.snapshot.display())&&
      history.undo_steps==before.history.undo_steps&&history.redo_steps==before.history.redo_steps&&
      history.step_limit==before.history.step_limit&&history.pixel_budget==before.history.pixel_budget&&
      f.app.document_ids()==before.membership&&f.app.active_document_id()==before.active;
}
void unchanged(const Fixture &f,const Before &before) {
  const auto s=f.owner->snapshot(); const auto h=f.owner->history();
  require(s.same_document(before.snapshot) && s.shares_data(before.snapshot) &&
      s.revisions().matches(before.snapshot.revisions(),Change::All) &&
      s.dirty()==before.snapshot.dirty() && s.data_identity()==before.snapshot.data_identity() &&
      same_display(s.display(),before.snapshot.display()), "Refusal/preparation/noop mutated source");
  require(h.undo_steps==before.history.undo_steps && h.redo_steps==before.history.redo_steps &&
      h.step_limit==before.history.step_limit && h.pixel_budget==before.history.pixel_budget,
      "Refusal/preparation/noop mutated history or policy");
  require(f.app.document_ids()==before.membership && f.app.active_document_id()==before.active,
          "Operation mutated application membership/activation");
  f.zero_reads();
}
void changed(const Fixture &f,const Before &before,std::size_t index,
             const Selection &replacement,const DocumentSnapshot &display_expected) {
  const auto s=f.owner->snapshot(); const auto &a=s.data(); const auto &b=before.snapshot.data();
  auto rows=b.rois; rows[index].selection=replacement;
  require(a.rois==rows && a.selection==b.selection && a.previous_selection==b.previous_selection,
          "Changed edit altered row identity, other rows or global selections");
  require(a.metadata==b.metadata && a.spectrum==b.spectrum && a.overlays.size()==b.overlays.size(),
          "Changed edit altered unrelated metadata/annotations/spectrum");
  for (std::size_t i=0;i<a.overlays.size();++i)
    require(metadata_equal(a.overlays[i],b.overlays[i]),"Overlay metadata changed");
  require(a.dataset.shares_pixels(b.dataset) &&
      a.dataset.shape().dimensions()==b.dataset.shape().dimensions() &&
      a.dataset.pixel_type()==b.dataset.pixel_type() &&
      a.dataset.sample_policy()==b.dataset.sample_policy() &&
      same_calibration(a.dataset.calibration(),b.dataset.calibration()),
      "Metadata edit changed pixel ownership, shape/type/policy or calibration");
  const auto r=s.revisions(), old=before.snapshot.revisions();
  require(r.pixels==old.pixels && r.metadata==old.metadata+1 && r.selection==old.selection+1 &&
      r.display==display_expected.revisions().display &&
      same_display(s.display(),display_expected.display()), "Changed revision/Display policy differs");
  require(s.dirty() && s.data_identity()!=before.snapshot.data_identity() &&
      f.owner->undo_steps()==before.history.undo_steps+1 && f.owner->redo_steps()==0,
      "Changed edit did not publish one dirty history item and clear redo");
  require(f.app.document_ids()==before.membership,"Changed edit altered membership");
  f.zero_reads();
}
SavedRoiGeometryPublication apply(Fixture &f,const Selection &replacement,std::size_t index=0) {
  const auto capture=f.app.capture_saved_roi_geometry();
  const auto command=resolve_saved_roi_geometry(capture.source(),SavedRoiGeometryRequest(index,replacement));
  auto prepared=f.app.prepare_saved_roi_geometry(capture,command);
  require(prepared.active() && prepared.retained_working_bytes()>0,
          "Valid preparation has no active logical reservation");
  const auto result=f.app.publish_saved_roi_geometry(prepared);
  require(!prepared.active() && prepared.retained_working_bytes()==0,
          "Terminal publication retained prepared storage");
  return result;
}

void literal_geometry_and_zero_read() {
  const auto test=[](Selection original,Selection expected,std::array<double,2> delta) {
    Fixture f(original); const Before before(f);
    SelectionEditLimits bounds;
    const SelectionEditor editor(original,12,10,bounds);
    const auto actual=editor.moved(delta,false,bounds);
    geometry(actual,expected);
    const auto command=resolve_saved_roi_geometry(before.snapshot,SavedRoiGeometryRequest(0,actual));
    require(command.row_index()==0 && !command.is_replay() &&
        command.source().shares_data(before.snapshot) && command.original_roi()==before.snapshot.data().rois[0],
        "Resolved request did not own the exact source/row");
    geometry(command.replacement(),expected);
    validate_saved_roi_geometry_record(command.record());
    unchanged(f,before);
    if (original.kind=="rectangle") {
      observations.observe("saved_roi.rectangle.moved_points",[&]{return point_text(actual);});
      observations.observe("saved_roi.geometry.preserved_width",[&]{return saved_roi_geometry_observations::decimal(actual.width);});
      observations.observe("saved_roi.geometry.attributes_preserved",[&]{return metadata_equal(actual.attributes,original.attributes);});
      observations.observe("saved_roi.position.wildcard_constraint_count",[&]{const auto &row=command.original_roi();return std::size_t(row.t.has_value())+row.c.has_value()+row.z.has_value();});
      observations.observe("saved_roi.entry.global_selection_changes",[&]{const auto now=f.app.snapshot();return std::size_t(now.data().selection!=before.snapshot.data().selection)+
          std::size_t(now.data().previous_selection!=before.snapshot.data().previous_selection);});
    } else if (original.kind=="mask")
      observations.observe("saved_roi.mask.translated_members",[&]{return members(actual);});
    require(apply(f,actual).changed,"Literal changed geometry became noop");
    changed(f,before,0,expected,before.snapshot);
    if (original.kind=="rectangle") {
      observations.observe("saved_roi.target.row_attributes_preserved",[&]{return metadata_equal(f.app.snapshot().data().rois[0].attributes,before.snapshot.data().rois[0].attributes);});
      observations.observe("saved_roi.target.provider_reads",[&]{return f.provider->plane_reads+f.provider->region_reads;});
      observations.observe("saved_roi.provider.read_plane_calls",[&]{return f.provider->plane_reads;});
      observations.observe("saved_roi.provider.read_region_calls",[&]{return f.provider->region_reads;});
      observations.observe("saved_roi.changed.metadata_revision_delta",[&]{return difference(f.app.snapshot().revisions().metadata,before.snapshot.revisions().metadata);});
      observations.observe("saved_roi.changed.selection_revision_delta",[&]{return difference(f.app.snapshot().revisions().selection,before.snapshot.revisions().selection);});
      observations.observe("saved_roi.changed.pixels_revision_delta",[&]{return difference(f.app.snapshot().revisions().pixels,before.snapshot.revisions().pixels);});
      observations.observe("saved_roi.changed.undo_steps_delta",[&]{return difference(f.owner->undo_steps(),before.history.undo_steps);});
      observations.observe("saved_roi.changed.redo_steps_after",[&]{return f.owner->redo_steps();});
    }
  };
  test(rectangle(),moved_rectangle(),{1,-1});
  test(ellipse(),moved_ellipse(),{-1,1});
  for (const auto kind : {"rotated_rectangle","rotated_ellipse"})
    test(styled(Selection(kind,{{6,5},{9,5},{6,7}})),
         styled(Selection(kind,{{7,4},{10,4},{7,6}})),{1,-1});
  for (const auto kind : {"polygon","freehand"})
    test(styled(Selection(kind,{{2,2},{8,2},{8,7},{2,7}})),
         styled(Selection(kind,{{3,1},{9,1},{9,6},{3,6}})),{1,-1});
  test(mask(),moved_mask(),{1,-1});
  const SelectionEditLimits bounds;
  const SelectionEditor mask_editor(mask(),12,10,bounds);
  const auto check_mask=[](const Selection &actual,const Selection &expected,const char *observation_id) {
    geometry(actual,expected);
    observations.observe(observation_id,[&]{return members(actual);});
    Fixture f(mask());const Before before(f);
    require(apply(f,actual).changed,"Literal mask transform became noop");
    changed(f,before,0,expected,before.snapshot);
  };
  check_mask(mask_editor.rotated(90,std::array<double,2>{6,5},bounds),
      styled(Selection::from_packed_mask(12,10,packed([](int x,int y) {
        return x>=3 && x<9 && y>=1 && y<9 && !(x>=5 && x<7 && y>=3 && y<7);
      }))),"saved_roi.mask.rotated_members");
  check_mask(mask_editor.moved({5,0},false,bounds),
      styled(Selection::from_packed_mask(12,10,packed([](int x,int y) {
        return x>=7 && x<12 && y>=2 && y<8 && !(x>=9 && x<12 && y>=4 && y<6);
      }))),"saved_roi.mask.clipped_members");
  {
    const auto empty=styled(Selection::from_packed_mask(12,10,std::vector<unsigned char>(15,0)));
    const SelectionEditor editor(empty,12,10,bounds);
    require(editor.handles(bounds).empty(),"Empty packed area exposes geometry handles");
    observations.observe("saved_roi.mask.empty_handle_count",[&]{return editor.handles(bounds).size();});
    geometry(editor.moved({0,0},false,bounds),empty);
    Fixture f(empty);const Before before(f);
    require(!apply(f,empty).changed,"Empty unchanged area became a changed edit");
    unchanged(f,before);
  }
  geometry(SelectionEditor(rectangle(),12,10,bounds).resized(3,{10,4.5},false,false,bounds),
           styled(Selection("rectangle",{{2,2},{10,7}})));
  const auto numeric=SelectionEditor(rectangle(),12,10,bounds).box_geometry({6,5},{4,4},0,bounds);
  geometry(numeric,styled(Selection("rectangle",{{4,3},{8,7}})));
  observations.observe("saved_roi.rectangle.numeric_points",[&]{return point_text(numeric);});
  const auto rotated=SelectionEditor(styled(Selection("rotated_rectangle",{{6,5},{9,5},{6,7}})),
      12,10,bounds).box_geometry({6,5},{6,4},90,bounds);
  geometry(rotated,styled(Selection("rotated_rectangle",{{6,5},{6,8},{4,5}})));
  const auto vertices=styled(Selection("polygon",{{2,2},{8,2},{8,7},{2,7}}));
  const auto inserted=SelectionEditor(vertices,12,10,bounds).inserted_vertex(2,{9,5},bounds);
  geometry(inserted,styled(Selection("polygon",{{2,2},{8,2},{9,5},{8,7},{2,7}})));
  geometry(SelectionEditor(inserted,12,10,bounds).removed_vertex(2,bounds),vertices);
  for (const auto type : {PixelType::UInt8,PixelType::UInt16,PixelType::Float32,PixelType::Float64})
    for (const auto samples : {1,3,4}) {
      Fixture f(rectangle(),true,type,samples); const Before before(f);
      require(apply(f,moved_rectangle()).changed,"Valid native layout refused metadata edit");
      changed(f,before,0,moved_rectangle(),before.snapshot);
    }
  Fixture quiet(rectangle(),false,PixelType::Float32,1,false,true);
  const Before before(quiet);
  require(apply(quiet,moved_rectangle()).changed,"Valid quiet-NaN source refused metadata edit");
  changed(quiet,before,0,moved_rectangle(),before.snapshot);
}

void prepared_publication_atomic_guards() {
  {
    Fixture f; const Before before(f);
    const auto capture=f.app.capture_saved_roi_geometry();
    const auto command=resolve_saved_roi_geometry(capture.source(),SavedRoiGeometryRequest(0,moved_rectangle()));
    auto p=f.app.prepare_saved_roi_geometry(capture,command);
    unchanged(f,before);
    auto display=f.app.snapshot().display(); display.zoom=4; display.pan[0]={-3,7};
    f.app.set_display(display); const auto fresh=f.app.snapshot();
    const auto result=f.app.publish_saved_roi_geometry(p);
    require(result.changed && result.document_id==f.owner->id && !p.active() && p.working_bytes()==0,
            "Numeric fresh-camera publication failed or retained its reservation");
    changed(f,before,0,moved_rectangle(),fresh);
    observations.observe("saved_roi.numeric.fresh_camera_preserved",[&]{return same_display(f.app.snapshot().display(),fresh.display());});
    f.app.undo();
    require(f.app.snapshot().data().rois==before.snapshot.data().rois && !f.app.snapshot().dirty(),
            "Undo did not restore exact saved geometry/clean identity");
    f.app.redo();
    require(f.app.snapshot().data().rois[0].selection==moved_rectangle(),"Redo lost replacement");
    observations.observe("saved_roi.undo_redo.pixels_revision_delta",[&]{return difference(f.app.snapshot().revisions().pixels,before.snapshot.revisions().pixels);});
  }
  const auto rejected=[](const std::function<void(Fixture&)> &mutate,
                         SavedRoiGeometryInteraction interaction=SavedRoiGeometryInteraction::Numeric,
                         bool owner_roundtrip=false) {
    Fixture f;
    const auto capture=f.app.capture_saved_roi_geometry();
    const auto command=resolve_saved_roi_geometry(capture.source(),SavedRoiGeometryRequest(0,moved_rectangle()));
    auto p=f.app.prepare_saved_roi_geometry(capture,command,interaction);
    mutate(f); const Before after_mutation(f);
    refuses<RevisionConflict>([&]{f.app.publish_saved_roi_geometry(p);},"Stale preparation published");
    require(!p.active() && p.retained_working_bytes()==0,"Stale publication retained resources");
    unchanged(f,after_mutation);
    if (owner_roundtrip) observations.observe("saved_roi.entry.owner_roundtrip_publication_delta",[&]{return difference(f.owner->snapshot().revisions().metadata,after_mutation.snapshot.revisions().metadata);});
  };
  rejected([](Fixture &f){auto d=f.app.snapshot().display();d.zoom=3;f.app.set_display(d);},
           SavedRoiGeometryInteraction::Pointer);
  rejected([](Fixture &f){auto d=f.app.snapshot().display();d.crosshair[2]=1;f.app.set_display(d);});
  rejected([](Fixture &f){f.app.select(Selection("ellipse",{{1,1},{3,4}}));});
  rejected([](Fixture &f){auto tx=f.app.begin_edit();tx.set_extension("interference",true);f.app.commit(tx);});
  rejected([](Fixture &f){auto tx=f.app.begin_edit();tx.replace_plane({1,2,2},Image(12,10,1,PixelType::UInt16));f.app.commit(tx);});
  rejected([](Fixture &f){auto other=std::make_shared<Document>(Dataset::from_stack({Image(12,10)}));
      f.app.adopt(other);f.app.activate(f.owner->id);},SavedRoiGeometryInteraction::Numeric,true);
  {
    Fixture f; const Before before(f);
    const auto capture=f.app.capture_saved_roi_geometry();
    const auto command=resolve_saved_roi_geometry(capture.source(),SavedRoiGeometryRequest(0,moved_rectangle()));
    auto p=f.app.prepare_saved_roi_geometry(capture,command);
    auto display=f.app.snapshot().display();display.crosshair[2]=1;f.app.set_display(display);
    display.crosshair[2]=2;f.app.set_display(display);const auto fresh=f.app.snapshot();
    require(f.app.publish_saved_roi_geometry(p).changed,"Headless equal-TCZ roundtrip invented an epoch");
    changed(f,before,0,moved_rectangle(),fresh);
  }
  {
    Fixture f; const auto capture=f.app.capture_saved_roi_geometry();
    const auto command=resolve_saved_roi_geometry(capture.source(),SavedRoiGeometryRequest(0,moved_rectangle()));
    Application foreign; foreign.adopt(f.owner); const Before before(f);
    refuses<DocumentUnavailable>([&]{foreign.prepare_saved_roi_geometry(capture,command);},
                                "Foreign Application accepted another controller's capture");
    unchanged(f,before);
    auto p=f.app.prepare_saved_roi_geometry(capture,command);
    f.app.close(f.owner->id,true);const Before closed(f);
    refuses<DocumentUnavailable>([&]{f.app.publish_saved_roi_geometry(p);},"Closed owner published");
    require(!p.active() && p.retained_working_bytes()==0,"Closed refusal retained resources");
    unchanged(f,closed);
  }
  {
    struct ConstraintRefusal : std::runtime_error {ConstraintRefusal():std::runtime_error("SDK constraint") {}};
    Fixture f; bool reject=false;
    const auto lease=f.owner->constrain([&](const DocumentData&,const DisplayState&){if(reject)throw ConstraintRefusal();});
    const auto capture=f.app.capture_saved_roi_geometry();
    const auto command=resolve_saved_roi_geometry(capture.source(),SavedRoiGeometryRequest(0,moved_rectangle()));
    auto p=f.app.prepare_saved_roi_geometry(capture,command);const Before before(f);reject=true;
    refuses<ConstraintRefusal>([&]{f.app.publish_saved_roi_geometry(p);},"Existing retained constraint was bypassed");
    require(!p.active() && p.retained_working_bytes()==0,"Constraint refusal retained resources");
    unchanged(f,before);
    observations.observe("saved_roi.failure.native_state_unchanged",[&]{return native_unchanged(f,before);});
    observations.observe("saved_roi.constraint_rejection.publication_delta",[&]{return difference(f.owner->snapshot().revisions().metadata,before.snapshot.revisions().metadata);});
  }
}

void strict_record_replay_then_stale_repeat() {
  Fixture f;
  const Before hidden_before(f);bool hidden_refused=false;
  try { (void)resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(1,moved_ellipse())); }
  catch (const std::invalid_argument &) { hidden_refused=true; }
  require(hidden_refused,"Fresh hidden explicit-zero row was admitted without navigation");
  unchanged(f,hidden_before);
  observations.observe("saved_roi.entry.hidden_fresh_refused",[&]{return hidden_refused;});
  auto at_zero=f.app.snapshot().display();at_zero.t=0;at_zero.c=0;at_zero.crosshair[2]=0;
  f.app.set_display(at_zero);
  const auto original=f.app.snapshot();
  const auto command=resolve_saved_roi_geometry(original,SavedRoiGeometryRequest(1,moved_ellipse()));
  const auto record=command.record(); validate_saved_roi_geometry_record(record);
  require(std::get<std::string>(record.at("schema"))=="imagejxx.saved-roi-geometry-command" &&
      std::get<std::string>(record.at("operation"))=="replace_saved_roi_geometry",
      "Normalized record identity changed");
  observations.observe("saved_roi.record.operation",[&]{return std::get<std::string>(record.at("operation"));});
  observations.observe("saved_roi.position.explicit_zero_tcz",[&]{const auto &row=command.original_roi();
    if(!row.t||!row.c||!row.z)throw std::runtime_error("Observed explicit-zero row has a wildcard constraint");
    return std::to_string(*row.t)+','+std::to_string(*row.c)+','+std::to_string(*row.z);});
  std::vector<MetadataObject> malformed;
  auto bad=record;bad["version"]=true;malformed.push_back(bad);
  bad=record;bad["unknown"]=0;malformed.push_back(bad);
  malformed.push_back(edit_object(record,"source",[](MetadataObject &o){o["roi_count"]=0;}));
  malformed.push_back(edit_object(record,"source",[](MetadataObject &o){o["pixels_revision"]=1.0;}));
  malformed.push_back(edit_object(record,"source",[](MetadataObject &o){o["shape"]=metadata_array({2,3,3,10,true,1});}));
  malformed.push_back(edit_object(record,"position",[](MetadataObject &o){o["t"]=false;}));
  malformed.push_back(edit_object(record,"original",[](MetadataObject &o){o["index"]=std::numeric_limits<std::uint64_t>::max();}));
  malformed.push_back(edit_object(record,"replacement",[](MetadataObject &o){o["width"]=3.0;}));
  malformed.push_back(edit_object(record,"replacement",[](MetadataObject &o){o["unknown"]=true;}));
  const Before unchanged_source(f);
  for (const auto &value : malformed)
    refuses([&]{validate_saved_roi_geometry_record(value);},"Malformed complete record was accepted");
  unchanged(f,unchanged_source);
  f.app.select(Selection("rectangle",{{1,1},{4,3}}));
  auto fresh=f.app.snapshot().display();fresh.t=1;fresh.c=2;fresh.crosshair[2]=2;
  f.app.set_display(fresh);const Before before(f);
  const auto replay=saved_roi_geometry_from_record(f.app.snapshot(),record);
  require(replay.is_replay() && metadata_equal(replay.record(),record) &&
      replay.source().revisions().selection==before.snapshot.revisions().selection &&
      replay.position().t==1 && replay.position().c==2 && replay.position().z==2,
      "Replay rewrote provenance or failed to capture fresh Selection/navigation");
  const auto &historical_position=object(replay.record().at("position"));
  require(std::get<std::int64_t>(historical_position.at("t"))==0,
          "Replay replaced original position provenance");
  auto p=f.app.prepare_saved_roi_geometry(f.app.capture_saved_roi_geometry(),replay);
  unchanged(f,before);
  require(f.app.publish_saved_roi_geometry(p).changed,"Strict replay reimposed hidden-row visibility");
  changed(f,before,1,moved_ellipse(),before.snapshot);
  observations.observe("saved_roi.strict_replay.metadata_revision_delta",[&]{return difference(f.app.snapshot().revisions().metadata,before.snapshot.revisions().metadata);});
  observations.observe("saved_roi.strict_replay.fresh_navigation_preserved",[&]{return same_display(f.app.snapshot().display(),before.snapshot.display());});
  const Before after(f);
  refuses<RevisionConflict>([&]{saved_roi_geometry_from_record(f.app.snapshot(),record);},
                            "Same record replayed after Metadata advanced");
  unchanged(f,after);
  observations.observe("saved_roi.strict_repeat.metadata_revision_delta",[&]{return difference(f.app.snapshot().revisions().metadata,after.snapshot.revisions().metadata);});
  Fixture foreign;
  auto wrong=record;wrong["operation"]=std::string("not_saved_roi_geometry");
  const auto expected=refuses([&]{validate_saved_roi_geometry_record(wrong);},"Invalid original validated");
  const auto actual=refuses([&]{saved_roi_geometry_from_record(foreign.app.snapshot(),wrong);},
                           "Invalid original was bound to another owner");
  require(actual==expected,"Owner binding preceded complete invalid-original validation");
}

class FreshSessionDirectory {
public:
  FreshSessionDirectory() {
    const auto base=std::filesystem::temp_directory_path();
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt=0;attempt<8;++attempt) {
      directory=base/("imagejxx-saved-roi-sdk-"+std::to_string(stamp)+"-"+std::to_string(attempt));
      if (std::filesystem::create_directory(directory)) { file=directory/"roundtrip.ijx"; return; }
    }
    throw std::runtime_error("Could not create fresh SDK session directory");
  }
  ~FreshSessionDirectory() {
    if (!cleaned) std::cerr << "RETAINED_SESSION_DIRECTORY " << directory.string() << '\n';
  }
  void clean() {
    require(std::filesystem::remove(file),"Owned session file cleanup failed");
    require(std::filesystem::remove(directory),"Owned SDK directory is not empty or cleanup failed");
    cleaned=true;
  }
  std::filesystem::path directory,file;
private:
  bool cleaned=false;
};
void explicit_rebind_and_v3_reopen() {
  {
    // Preserve the existing NativeV3 scientific-Fourier refusal explicitly;
    // never clear an already-live source to make the persistence fixture pass.
    Fixture unsupported(mask(),false);const Before before(unsupported);
    SessionCapture capture;capture.documents.push_back(unsupported.app.snapshot());
    std::size_t calls=0;ExecutionControl control;control.checkpoint=[&]{++calls;};
    refuses([&]{encode_session(capture,{},control);},"Unpositioned scientific Fourier was silently admitted");
    require(calls==0 && unsupported.app.snapshot().data().spectrum.has_value(),
            "Unsupported session invoked callbacks or discarded Fourier state");
    unchanged(unsupported,before);
  }
  Fixture f(mask(),false,PixelType::UInt16,1,false);
  const auto record=resolve_saved_roi_geometry(f.app.snapshot(),SavedRoiGeometryRequest(0,moved_mask())).record();
  const Before before(f);require(apply(f,moved_mask()).changed,"Persistence mask fixture became noop");
  changed(f,before,0,moved_mask(),before.snapshot);
  SessionCapture capture;capture.documents.push_back(f.app.snapshot());
  capture.results.push_back(metadata_object({{"result",std::int64_t(13)}}));
  capture.workflow.push_back(metadata_object(record));
  capture.extensions["future"]=metadata_array({false,std::numeric_limits<std::uint64_t>::max()});
  const Before before_save(f);
  FreshSessionDirectory scratch;
  std::cout << "SESSION_FILE " << scratch.file.string() << '\n';
  save_session(scratch.file.string(),capture);
  const auto reopened=load_session(scratch.file.string());
  unchanged(f,before_save);
  require(reopened.source_version==3 && reopened.documents.size()==1,"NativeV3 owner/version count differs");
  const auto owner=reopened.documents[0];const auto saved=capture.documents[0];const auto fresh=owner->snapshot();
  require(fresh.id()!=saved.id() && !fresh.dirty() && owner->undo_steps()==0 && owner->redo_steps()==0,
          "Reopen did not create a fresh clean history-free owner");
  require(fresh.data().rois==saved.data().rois &&
      fresh.data().rois[0].selection.packed_mask()==moved_mask().packed_mask() &&
      !fresh.data().rois[0].t && !fresh.data().rois[0].c && !fresh.data().rois[0].z &&
      fresh.data().rois[1].t==0 && fresh.data().rois[1].c==0 && fresh.data().rois[1].z==0 &&
      fresh.data().rois[2].t==1 && !fresh.data().rois[2].c && fresh.data().rois[2].z==2,
      "NativeV3 changed holes/attributes or collapsed zero and wildcard positions");
  observations.observe("saved_roi.reopen.saved_row_count_delta",[&]{return difference(fresh.data().rois.size(),saved.data().rois.size());});
  observations.observe("saved_roi.reopen.undo_steps",[&]{return owner->undo_steps();});
  observations.observe("saved_roi.reopen.redo_steps",[&]{return owner->redo_steps();});
  observations.observe("saved_roi.reopen.native_geometry_preserved",[&]{return fresh.data().rois==saved.data().rois;});
  observations.observe("saved_roi.reopen.explicit_zero_and_null_distinct",[&]{const auto &rows=fresh.data().rois;
    return !rows[0].t&&!rows[0].c&&!rows[0].z&&rows[1].t==0&&rows[1].c==0&&rows[1].z==0&&rows[2].t==1&&!rows[2].c&&rows[2].z==2;});
  require(fresh.data().selection==saved.data().selection &&
      fresh.data().previous_selection==saved.data().previous_selection &&
      fresh.data().metadata==saved.data().metadata && same_display(fresh.display(),saved.display(),false),
      "NativeV3 changed global selections, unrelated metadata or Display");
  require(metadata_equal(reopened.workflow,capture.workflow) &&
      metadata_equal(reopened.results,capture.results) && metadata_equal(reopened.extensions,capture.extensions),
      "NativeV3 changed or executed retained records/results/extensions");
  require(fresh.data().dataset.shape().dimensions()==std::array<int,6>{2,3,3,10,12,1} &&
      fresh.data().dataset.pixel_type()==PixelType::UInt16 &&
      same_calibration(fresh.data().dataset.calibration(),calibration()),
      "NativeV3 changed declared axes/type or complete calibration");
  for (int t=0;t<2;++t) for (int c=0;c<3;++c) for (int z=0;z<3;++z) {
    const auto plane=fresh.data().dataset.plane({t,c,z});
    require(plane->width==12 && plane->height==10 && plane->channels==1,
            "Reopened plane shape differs");
    for (int y=0;y<10;++y) for (int x=0;x<12;++x)
      require(plane->at(x,y)==1000*t+100*c+10*z+2*y+x,
              "NativeV3 changed independent literal source samples");
  }
  refuses<RevisionConflict>([&]{saved_roi_geometry_from_record(fresh,record);},
                            "Reopened owner implicitly rebound the old record");
  observations.observe("saved_roi.reopened_owner.strict_replay_publication_delta",[&]{return difference(owner->snapshot().revisions().metadata,fresh.revisions().metadata);});
  Application app;app.adopt(owner);
  auto position=app.snapshot().display();position.t=0;position.c=0;position.crosshair[2]=0;
  app.set_display(position);const auto rebind_source=app.snapshot();
  const SavedRoiGeometryRequest explicit_request(1,moved_ellipse());
  const auto rebound=rebind_saved_roi_geometry(rebind_source,record,explicit_request);
  require(!rebound.is_replay() && rebound.row_index()==1 && rebound.source().id()==owner->id &&
      rebound.replacement()==moved_ellipse(),"Complete rebind copied old row/owner/result defaults");
  const auto row0=rebind_source.data().rois[0];
  auto p=app.prepare_saved_roi_geometry(app.capture_saved_roi_geometry(),rebound);
  require(app.publish_saved_roi_geometry(p).changed && app.snapshot().data().rois[0]==row0 &&
      app.snapshot().data().rois[1].selection==moved_ellipse(),"Rebind did not publish only the explicit new row");
  observations.observe("saved_roi.full_rebind.metadata_revision_delta",[&]{return difference(app.snapshot().revisions().metadata,rebind_source.revisions().metadata);});
  observations.observe("saved_roi.rebind.explicit_target_preserved",[&]{return rebound.source().same_document(rebind_source)&&rebound.row_index()==explicit_request.row_index&&
      rebound.replacement()==explicit_request.replacement&&app.snapshot().data().rois[explicit_request.row_index].selection==rebound.replacement()&&app.snapshot().data().rois[0]==row0;});
  const auto malformed_before=app.snapshot();
  auto invalid=record;invalid["operation"]=std::string("invalid_original");
  const auto expected=refuses([&]{validate_saved_roi_geometry_record(invalid);},"Invalid original validated");
  const auto actual=refuses([&]{rebind_saved_roi_geometry(app.snapshot(),invalid,
      SavedRoiGeometryRequest(std::numeric_limits<std::size_t>::max(),Selection("line",{{0,0},{1,1}})));},
      "Rebind inspected/replaced malformed original");
  require(actual==expected,"Rebind parsed replacement before complete original validation");
  observations.observe("saved_roi.malformed_original.publication_delta",[&]{return difference(app.snapshot().revisions().metadata,malformed_before.revisions().metadata);});
  scratch.clean();
}

void caps_cancel_noop_and_source_preservation() {
  Fixture f;const Before before(f);
  const SavedRoiGeometryRequest request(0,moved_rectangle());
  const auto estimate=estimate_saved_roi_geometry(before.snapshot,request);
  require(estimate.xy_pixels==120 && estimate.vertices==4 &&
      estimate.roi_bytes>=3*sizeof(Roi) && estimate.record_metadata_bytes>0 &&
      estimate.record_wire_bytes>0 && estimate.working_bytes>estimate.fixed_bytes,
      "Public resource estimate omitted basic geometry/row/record descriptors");
  SavedRoiGeometryLimits defaults;
  require(defaults.max_xy_pixels==16777216 && defaults.max_vertices==65536 &&
      defaults.max_working_bytes==268435456 && defaults.max_record_bytes==16777216 &&
      defaults.metadata_depth==128,"Adopted SDK limits/defaults changed");
  std::size_t callbacks=0;ExecutionControl counted;counted.checkpoint=[&]{++callbacks;};
  std::vector<SavedRoiGeometryLimits> invalid;
  auto limit=defaults;limit.max_xy_pixels=119;invalid.push_back(limit);
  limit=defaults;limit.max_vertices=1;invalid.push_back(limit);
  limit=defaults;limit.max_record_bytes=1;invalid.push_back(limit);
  limit=defaults;limit.max_working_bytes=1;invalid.push_back(limit);
  limit=defaults;limit.metadata_depth=1;invalid.push_back(limit);
  for (const auto &bounds : invalid)
    refuses([&]{resolve_saved_roi_geometry(before.snapshot,request,bounds,counted);},
            "Invalid/insufficient cap was admitted");
  refuses([&]{resolve_saved_roi_geometry(before.snapshot,
      SavedRoiGeometryRequest(std::numeric_limits<std::size_t>::max(),moved_rectangle()),defaults,counted);},
      "Overflowing row index was admitted");
  require(callbacks==0,"Malformed or resource refusal invoked callbacks");
  unchanged(f,before);
  observations.observe("saved_roi.malformed.callback_count",[&]{return callbacks;});
  observations.observe("saved_roi.invalid_request.publication_delta",[&]{return difference(f.app.snapshot().revisions().metadata,before.snapshot.revisions().metadata);});
  auto token=std::make_shared<CancellationToken>();token->cancel();
  counted.cancellation=token;
  refuses<OperationCancelled>([&]{resolve_saved_roi_geometry(before.snapshot,request,defaults,counted);},
                              "Valid precancel resolved a command");
  require(callbacks==0,"Valid precancel invoked a callback");unchanged(f,before);
  observations.observe("saved_roi.pre_cancel.callback_count",[&]{return callbacks;});
  {
    const auto command=resolve_saved_roi_geometry(before.snapshot,request);
    const auto capture=f.app.capture_saved_roi_geometry();
    refuses<OperationCancelled>([&]{f.app.prepare_saved_roi_geometry(capture,command,
        SavedRoiGeometryInteraction::Numeric,defaults,counted);},"Precanceled preparation succeeded");
    unchanged(f,before);
    ExecutionControl control;control.cancellation=std::make_shared<CancellationToken>();
    auto p=f.app.prepare_saved_roi_geometry(capture,command,SavedRoiGeometryInteraction::Numeric,{},control);
    require(p.retained_working_bytes()>=estimate.working_bytes,"Prepared reservation is smaller than command bound");
    control.cancellation->cancel();
    refuses<OperationCancelled>([&]{f.app.publish_saved_roi_geometry(p);},"Canceled prepared edit published");
    require(!p.active() && p.retained_working_bytes()==0,"Canceled publication retained resources");
    unchanged(f,before);
    observations.observe("saved_roi.terminal.retained_working_bytes",[&]{return p.retained_working_bytes();});
    observations.observe("saved_roi.failure.metadata_revision_delta",[&]{return difference(f.app.snapshot().revisions().metadata,before.snapshot.revisions().metadata);});
    observations.observe("saved_roi.cancel.document_dirty_changes",[&]{return std::size_t(f.app.snapshot().dirty()!=before.snapshot.dirty());});
  }
  {
    Fixture other;auto source=before.snapshot;auto changed_request=request;
    auto limits=defaults;ExecutionControl control;bool entered=false;
    control.checkpoint=[&]{if(!entered){entered=true;source=other.app.snapshot();
      changed_request.row_index=1;changed_request.replacement=moved_ellipse();limits.max_working_bytes=1;
      control.checkpoint=[] {throw std::runtime_error("Mutated original control was used");};}};
    const auto command=resolve_saved_roi_geometry(source,changed_request,limits,control);
    require(entered && command.source().same_document(before.snapshot) && command.row_index()==0 &&
        command.replacement()==moved_rectangle() && command.limits().max_working_bytes==defaults.max_working_bytes,
        "Callback mutation changed captured source/request/limits/control");
    observations.observe("saved_roi.control.captured_values_preserved",[&]{return entered&&command.source().same_document(before.snapshot)&&command.row_index()==request.row_index&&
        command.replacement()==request.replacement&&command.limits().max_working_bytes==defaults.max_working_bytes;});
    unchanged(f,before);
  }
  {
    struct CallbackRefusal : std::runtime_error {CallbackRefusal():std::runtime_error("SDK callback") {}};
    ExecutionControl control;control.checkpoint=[] {throw CallbackRefusal();};
    refuses<CallbackRefusal>([&]{resolve_saved_roi_geometry(before.snapshot,request,{},control);},
                            "Preparation replaced callback exception identity");
    unchanged(f,before);
  }
  {
    const SelectionEditLimits limits;const auto input=mask();
    const SelectionEditor editor(input,12,10,limits);
    auto cancellation=std::make_shared<CancellationToken>();std::size_t visits=0;
    ExecutionControl control;control.cancellation=cancellation;
    control.checkpoint=[&]{if(++visits==2)cancellation->cancel();};
    refuses<OperationCancelled>([&]{editor.moved({1,-1},false,limits,control);},
                                "Controlled mask geometry ignored cancellation");
    require(visits==2 && editor.selection()==input,"Canceled geometry mutated input or lost control capture");
    unchanged(f,before);
  }
  const auto result=apply(f,before.snapshot.data().rois[0].selection);
  require(!result.changed && result.document_id==f.owner->id,"Exact geometry noop became a changed transaction");
  unchanged(f,before);
  observations.observe("saved_roi.noop.undo_steps_delta",[&]{return difference(f.owner->undo_steps(),before.history.undo_steps);});
}
} // namespace

int main(int argc,char **argv) {
  const std::array<std::pair<const char *,void(*)()>,5> cases{{
      {"S01.literal_row_geometry_and_zero_read",literal_geometry_and_zero_read},
      {"S02.prepared_publication_atomic_guards",prepared_publication_atomic_guards},
      {"S03.strict_record_replay_then_stale_repeat",strict_record_replay_then_stale_repeat},
      {"S04.explicit_rebind_and_v3_reopen",explicit_rebind_and_v3_reopen},
      {"S05.caps_cancel_noop_and_source_preservation",caps_cancel_noop_and_source_preservation}}};
  std::size_t completed=0;
  try {
    if (argc!=1) {
      if (argc!=3 || std::string(argv[1])!="--observations")
        throw std::invalid_argument("Use no arguments or --observations FRESH_JSON_FILE");
      observations.configure(argv[2],"p06g.saved-roi.sdk.raw.001");
    }
    for (const auto &[id,work] : cases) {
      current=id;const auto start=checks;observations.begin_case(id);work();observations.finish_case();++completed;
      std::cout << "PASS " << id << " checks=" << checks-start << '\n';
    }
    std::cout << "Installed SDK saved ROI geometry passed cases=" << completed
              << " checks=" << checks << '\n';
    observations.write();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << current << ": " << error.what()
              << " completed=" << completed << " checks=" << checks << '\n';
    return 1;
  }
}
