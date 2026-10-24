#include <imagejxx/composite_path.hpp>
#include <imagejxx/imagej_annotation_import.hpp>
#include <imagejxx/imagej_roi_pairing.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
class UnreadSource final : public PlaneSource {
public:
  DatasetShape shape() const override { return {1,1,3,10,12,1}; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;throw std::runtime_error("Unexpected sample read");
  }
  Image read_region(PlanePosition,int,int,int,int) const override {
    ++reads;throw std::runtime_error("Unexpected sample read");
  }
  mutable int reads=0;
};
}
int main() {
  try {
    int checks=0;
    auto check=[&](bool value) {++checks;if(!value)throw std::runtime_error("Native bare import check "+std::to_string(checks));};
    const std::vector<double> commands{0,-1.25,1,2,3,7,7,1,3,9,2,9,5,7,5,4};
    const auto provider=std::make_shared<UnreadSource>();
    const auto shape=provider->shape();
    Document document{Dataset(provider)};
    ImageJRoi source;source.roi={"curve",Selection::from_path(commands),{},{},{},{}};
    const auto wire=encode_imagej_roi(source);
    ImageJRoiPlaneMapping mapping;mapping.extent=shape;mapping.coordinate=0;
    ImageJRoiImportLimits limits;limits.roi.max_mask_pixels=limits.roi.max_path_work=0;
    ImageJRoiImportItem input{"curve.roi",wire,mapping,{},true};
    auto before=document.snapshot();
    auto result=resolve_imagej_roi_import(before,{input},ImageJRoiImportMode::Append,limits);
    check(result.annotations().size()==1);
    check(result.annotations()[0].selection.path_commands()==commands);
    check(result.records()[0].source_record().roi.selection.is_path());
    validate_imagej_roi_import_record(result.record());check(true);
    auto request=decode_imagej_roi_import_request(result.record());check(request.items[0].native_path && request.items[0].data==wire);
    auto transaction=prepare_imagej_roi_import(result);
    document.commit(transaction);check(document.undo_steps()==1);
    auto catalog=inspect_imagej_roi_sources(document.snapshot(),limits);
    auto pair=resolve_imagej_roi_pairing(catalog,0,0);check(pair.source_item().native_path);
    validate_imagej_roi_pairing_record(pair.record());check(true);
    ImageJAnnotationImportLimits mixed; mixed.batch=limits;
    mixed.batch.roi.max_mask_pixels=mixed.batch.roi.max_path_work=1;
    auto imported=resolve_imagej_annotation_import(document.snapshot(),{input},ImageJRoiImportMode::Append,mixed);
    check(imported.annotations().size()==2);
    validate_imagej_annotation_import_record(imported.record());check(true);
    check(decode_imagej_annotation_import_request(imported.record()).items.size()==1);
    document.undo();check(document.snapshot().shares_data(before));
    document.redo();check(planar_annotations(document.snapshot().data())[0].selection.path_commands()==commands);
    bool refused=false;input.mapping.reset();
    try {resolve_imagej_roi_import(document.snapshot(),{input});}catch(const std::invalid_argument &){refused=true;}
    check(refused);
    check(provider->reads==0);
    std::cout<<"PASS "<<checks<<" native bare import checks\n";
  }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
