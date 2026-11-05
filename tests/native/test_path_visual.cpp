#include "path_visual.hpp"
#include "planar_annotation_visual.hpp"
#include "roi_import_preview.hpp"
#include "point_visual.hpp"
#include "rendering.hpp"
#include "point_raster_export.hpp"
#include "point_raster_font.hpp"
#include "../../src/native/kernel_control.hpp"
#include <cstdlib>
#include <fstream>
#include <imagejxx/point_raster.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <imagejxx/composite_path.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
using namespace imagejxx::desktop;
namespace {
int checks = 0;
void check(bool value) {
  ++checks;
  if (!value) throw std::runtime_error("Path visual check " + std::to_string(checks));
}
template <class F> void refuses(F f) {
  bool caught = false;
  try { f(); } catch (const std::invalid_argument &) { caught = true; }
  check(caught);
}
} // namespace
namespace {
struct FontEnvironment {
  const char* initial=std::getenv("IMAGEJXX_FONT");
  bool present=initial!=nullptr;
  std::string previous=initial?initial:"";
  void replace(const char* value) {
#ifdef _WIN32
    if(_putenv_s("IMAGEJXX_FONT",value))throw std::runtime_error("Cannot stage font environment");
#else
    if(setenv("IMAGEJXX_FONT",value,1))throw std::runtime_error("Cannot stage font environment");
#endif
  }
  ~FontEnvironment() {
#ifdef _WIN32
    _putenv_s("IMAGEJXX_FONT",present?previous.c_str():"");
#else
    if(present)setenv("IMAGEJXX_FONT",previous.c_str(),1);else unsetenv("IMAGEJXX_FONT");
#endif
  }
};
}
int main() {
  try {
    auto path = Selection::from_path(std::vector<double>{
        0, 1, 1, 2, 4, -2, 7, 1, 3, 9, 3, 6, 7, 4, 6, 1, 1, 6, 4,
        0, 2, 3, 1, 4, 3, 1, 4, 5, 1, 2, 5, 4});
    path = with_roi_style(path, {{"stroke_color", std::string("#80112233")},
                                {"fill_color", std::string("#40224466")},
                                {"stroke_width", 2.5}});
    const auto original = path.path_commands();
    const auto small = pathVisual(path, 10, 8, {1, 1}, 8 * 1048576);
    const auto large = pathVisual(path, 10, 8, {100, 100}, 8 * 1048576);
    check(small.area && !small.markers && !small.edges.empty());
    check(large.edges.size() > small.edges.size());
    check(small.fill.size() == large.fill.size());
    check(path.path_commands() == original);
    check(small.style.stroke_width == 2.5);
    check(planarSelectionJson(path).at("path_version") == 1);
    check(planarSelectionJson(path).at("path").get<std::vector<double>>() == original);
    const auto controls = planarControlPoints(path);
    check(controls.size() == 11 && controls[1] == std::array<double, 2>{4, -2});
    const auto mask = path.mask(10, 8);
    std::vector<bool> covered(80);
    for (const auto &span : small.fill)
      for (int x = span.x; x < span.x + span.width; ++x)
        covered[std::size_t(span.y) * 10 + x] = true;
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 10; ++x)
        if (covered[std::size_t(y) * 10 + x] != (mask.at(x, y) != 0))
          throw std::runtime_error("Path fill differs from explicit native mask");
    check(true);
    refuses([&] { pathVisual(path, 10, 8, {0, 1}, 1048576); });
    refuses([&] { pathVisual(path, 10, 8, {1, 1}, 1); });
    refuses([&] { pathVisual(Selection("rectangle", {{0, 0}, {1, 1}}), 10, 8, {1, 1}, 1048576); });
    auto line = Selection::from_path(std::vector<double>{0, -1e9, -1e9, 1, 1e9, 1e9});
    const auto distant = pathVisual(line, 10, 8, {1, 1}, 1048576);
    check(distant.edges.size() == 2 && distant.fill.empty());
    auto cleared = with_roi_style(path, {{"stroke_color", std::monostate{}},
                                      {"fill_color", std::monostate{}}});
    const auto empty = pathVisual(cleared, 10, 8, {1, 1}, 1048576);
    check(!empty.edges.empty() && empty.fill.empty());
    check(pathVisualBytes(large) > pathVisualBytes(small));
    auto tiny=Selection::from_path(std::vector<double>{0,2,2,3,2.1,2.1,1.9,2.1,2,2,4});
    const auto dot=pathVisual(tiny,10,8,{1,1},1048576);
    check(!dot.edges.empty() && dot.edges[0][0]==dot.edges[0][2] && dot.edges[0][1]==dot.edges[0][3]);
    auto distant_curve=Selection::from_path(std::vector<double>{0,1e8,1e8,3,1e8+10000,1e8-10000,1e8+20000,1e8+10000,1e8+30000,1e8});
    check(pathVisual(distant_curve,10,8,{100,100},1024).edges.empty());
    auto accuracy=[&](const std::vector<double> &commands,bool cubic) {
      auto p=Selection::from_path(commands);
      const auto visual=pathVisual(p,12,12,{8,32},1048576);
      for(int sample=0;sample<=1000;++sample) {
        const double t=sample/1000.,u=1-t;
        const double x=(cubic?30*t*u*u+10*t*t*t:10*t)*8;
        const double y=(cubic?30*t*t*u+10*t*t*t:20*t*u)*32;
        double nearest=std::numeric_limits<double>::infinity();
        for(const auto &edge:visual.edges) {
          const double ax=edge[0]*8,ay=edge[1]*32,dx=(edge[2]-edge[0])*8,dy=(edge[3]-edge[1])*32;
          const double q=std::clamp(((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy),0.,1.);
          nearest=std::min(nearest,std::hypot(x-ax-q*dx,y-ay-q*dy));
        }
        if(nearest>.251)throw std::runtime_error("Curve display deviation exceeds a quarter pixel");
      }
      check(true);
    };
    accuracy({0,0,0,2,5,10,10,0},false);
    accuracy({0,0,0,3,10,0,0,10,10,10},true);
    const DatasetShape huge{1,1,1,100000,100000,1};
    PlanarAnnotation stroke_only{"line",line,Plane::XY,0,{},{},{}};
    admit_planar_annotation_visuals(huge,{stroke_only},1048576);
    check(true);
    stroke_only.selection=with_roi_style(line,{{"fill_color",std::string("blue")}});
    refuses([&]{admit_planar_annotation_visuals(huge,{stroke_only},1048576);});
    const DatasetShape shape{1,1,1,8,10,1};
    imagejxx::Document owner(DocumentData{Dataset::from_planes(shape,{Image(10,8,1,PixelType::UInt8)})});
    ImageJRoi source;source.roi={"curve",path,{},{},0,{}};
    ImageJRoiPlaneMapping mapping;mapping.extent=shape;
    const auto mapped=map_imagej_roi(source,mapping);
    const auto bytes=encode_imagej_roi_native_envelope(mapped,source);
    auto imported=resolve_imagej_roi_import(owner.snapshot(),{{"curve.roi",bytes,{},{}}});
    RoiImportPreview preview(imported,8*1048576);
    check(preview.matches(owner.snapshot(),8*1048576));
    {
      Selection points("multipoint",{{5,5},{12,12}});
      points=with_point_style(points,PointStyle{});
      PlanarAnnotation row{"points",points,Plane::XY,0,{},{},{}};
      // Point-only metadata on a large plane must not admit a full raster mask.
      check(planar_annotation_visual_estimate({1,1,1,100000,100000,1},{row},1048576)<1048576);
      auto v=roiVisual(selectionJson(points),100000,100000,{255,255,0,255},1048576);
      check(v.pointVisual && v.points.empty() && v.fill.empty() && v.pointVisual->layout.glyphs.size()==2);
      check(pathVisualBytes(v)>=v.pointVisual->layout.working_bytes);
      points.attributes[point_style_attribute]=metadata_object({{"version",std::int64_t(2)}});
      auto unknown=roiVisual(selectionJson(points),100000,100000,{255,255,0,255},1048576);
      check(!unknown.pointVisual->error.empty() && unknown.pointVisual->layout.glyphs.empty());
    }
    {
      Selection points("multipoint",{{4,4},{9,9}});PointStyle style;
      style.marker=PointMarker::Cross;style.size=0;style.labels=false;
      points=with_point_style(points,style);
      DatasetReadObserver reads;
      auto dataset=Dataset::from_stack(Stack{Image(16,16,1,PixelType::UInt8)}).observe_reads(reads);
      DocumentData data{dataset};data.overlays={metadataObjectFromJson(selectionJson(points))};
      imagejxx::Document owner(std::move(data));const auto before=owner.snapshot();
      const auto count=reads.counts().plane_reads;
      PreparedExportImage prepared(before,false,1048576);
      check(prepared.working_bytes()<=1048576 && reads.counts().plane_reads==count);
      ExecutionControl cancelled;cancelled.cancellation=std::make_shared<CancellationToken>();cancelled.cancellation->cancel();
      bool stopped=false;try{(void)prepared.render(cancelled);}catch(const OperationCancelled&){stopped=true;}
      check(stopped && reads.counts().plane_reads==count);
      prepared.validate(owner.snapshot(),false,1048576);
      auto output=prepared.render();check(reads.counts().plane_reads==count+1);
      PointPresentationOptions view;view.color={255,204,0,255};
      check(output.pixels==render_point_style(Image(16,16,3,PixelType::UInt8),points,view).pixels);
      auto display=owner.snapshot().display();display.pan={2,3};owner.set_display(display);
      bool stale=false;try{prepared.validate(owner.snapshot(),false,1048576);}catch(const RevisionConflict&){stale=true;}
      check(stale);
      bool denied=false;try{PreparedExportImage low(before,false,1);}catch(const std::length_error&){denied=true;}
      check(denied && reads.counts().plane_reads==count+1);
    }
    {
      const auto path=annotationFontPath();std::ifstream available(std::filesystem::u8path(path),std::ios::binary);
      if(available) {
        available.close();Selection points("multipoint",{{4,4},{9,9}});PointStyle style;
        style.marker=PointMarker::Cross;style.size=0;points=with_point_style(points,style);
        DatasetReadObserver reads;DocumentData data{Dataset::from_stack(Stack{Image(16,16,1,PixelType::UInt8)}).observe_reads(reads)};
        data.overlays={metadataObjectFromJson(selectionJson(points))};imagejxx::Document owner(std::move(data));
        const auto n=reads.counts().plane_reads;PreparedExportImage prepared(owner.snapshot(),false,16*1048576);
        check(reads.counts().plane_reads==n);
        auto font=loadPointRasterFont(path,8);PointPresentationOptions options;options.color={255,204,0,255};
        auto expected=render_point_style(Image(16,16,3,PixelType::UInt8),points,options,&font);
        check(!std::filesystem::exists("imagejxx-missing-font-capture-proof.ttf"));
        FontEnvironment environment;environment.replace("imagejxx-missing-font-capture-proof.ttf");
        auto actual=prepared.render();check(actual.pixels==expected.pixels && reads.counts().plane_reads==n+1);
      } else std::cout<<"Unavailable platform font capture input: "<<path<<"\n";
    }
    {
      const auto path=annotationFontPath();std::ifstream available(std::filesystem::u8path(path),std::ios::binary);
      if(available) {
        available.close();
        const auto small_budget=std::size_t(std::filesystem::file_size(std::filesystem::u8path(path)))+65536+path.size()*3+512;
        PointRasterFontCache limited(small_budget);limited.open(path);
        bool staging=false;try{(void)limited.textGlyph(512,'W');}catch(const std::length_error&){staging=true;}
        check(staging && limited.working_bytes()<=small_budget);
        PointRasterFontCache recovery;recovery.open(path);check(!recovery.textGlyph(16,'W')->coverage.empty());
        const std::vector<std::string> samples{"A12 A12","A\nB\n12","\xce\xb2\xce\xa9 e\xcc\x81","\xe6\xb1\x89\xe5\xad\x97", "", "\n\n", "   "};
        for(int size:{1,9,32,512})for(const auto& content:samples) {
          Image rgb(64,48,3,PixelType::UInt8);
          for(std::size_t i=0;i<rgb.pixels.size();++i)rgb.pixels[i]=31+(i%3)*39;
          Json overlays=Json::array({{{"kind","text"},{"points",{{-3.25,2.75}}},{"text",content},{"font_size",size},{"color","#40d08080"}}});
          auto expected=rgb;renderDisplayOverlays(expected,{{"overlays",overlays},{"slice_index",0}},PlanePosition{0,0,0});
          DatasetReadObserver reads;DocumentData data{Dataset::from_stack(Stack{rgb}).observe_reads(reads)};
          data.overlays={metadataObjectFromJson(overlays[0])};imagejxx::Document owner(std::move(data));
          const auto n=reads.counts().plane_reads;PreparedExportImage prepared(owner.snapshot(),false,32*1048576);
          check(reads.counts().plane_reads==n && prepared.working_bytes()<=32*1048576);
          check(prepared.render().pixels==expected.pixels && reads.counts().plane_reads==n+1);
        }
        // File and process policy disappear after capture; the immutable glyphs
        // must still render, with no second font IO or provider pre-read.
        Image rgb(64,48,3,PixelType::UInt8);
        Json row{{"kind","text"},{"points",{{1.75,2.25}}},{"text","A12\n\xce\xb2 A12"},{"font_size",16},{"color","#80c04080"}};
        auto expected=rgb;renderDisplayOverlays(expected,{{"overlays",Json::array({row})},{"slice_index",0}},PlanePosition{0,0,0});
        const auto directory=std::filesystem::temp_directory_path()/("imagejxx-text-font-"+std::to_string(next_document_id()));
        check(std::filesystem::create_directory(directory));
        const auto copy=directory/"capture.ttf";
        std::filesystem::copy_file(std::filesystem::u8path(path),copy);
        row["font_path"]=copy.u8string();
        DatasetReadObserver reads;DocumentData data{Dataset::from_stack(Stack{rgb}).observe_reads(reads)};
        data.overlays={metadataObjectFromJson(row)};imagejxx::Document owner(std::move(data));
        const auto n=reads.counts().plane_reads;
        try {
          PreparedExportImage prepared(owner.snapshot(),false,8*1048576);
          check(reads.counts().plane_reads==n && prepared.working_bytes()<8*1048576);
          check(std::filesystem::remove(copy));check(std::filesystem::remove(directory));
          FontEnvironment environment;environment.replace("imagejxx-missing-text-font-capture-proof.ttf");
          ExecutionControl cancel;cancel.cancellation=std::make_shared<CancellationToken>();cancel.cancellation->cancel();
          bool stopped=false;try{(void)prepared.render(cancel);}catch(const OperationCancelled&){stopped=true;}
          check(stopped && reads.counts().plane_reads==n);
          check(prepared.render().pixels==expected.pixels && reads.counts().plane_reads==n+1);
          bool cancelled_capture=false;
          try {ExecutionScope scope(cancel);PreparedExportImage missing(owner.snapshot(),false,8*1048576);}
          catch(const OperationCancelled&){cancelled_capture=true;}
          check(cancelled_capture && reads.counts().plane_reads==n+1);
        } catch(...) {
          // Exact fresh owned targets only, never recursive removal.
          std::error_code cleanup;std::filesystem::remove(copy,cleanup);std::filesystem::remove(directory,cleanup);throw;
        }
      }
    }
    std::cout << "PASS " << checks << " path/point visual checks\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
