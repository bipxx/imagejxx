#include <imagejxx/application.hpp>
#include <imagejxx/selection_region.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}}
int main(){try{
 Image pixels(9,9,1,PixelType::UInt16),mask(9,9,1,PixelType::UInt8);
 mask.at(0,0)=255;
 const auto region=SelectionRegion(Selection::from_mask(mask),9,9).enlarged(2).as_selection();
 const auto moved=SelectionEditor(region,9,9).moved({-5,3});
 DatasetReadObserver reads;Application app;
 auto owner=std::make_shared<Document>(Dataset::from_stack({pixels}).observe_reads(reads));app.adopt(owner);
 auto edit=owner->begin_edit();edit.set_rois({Roi{"full",region}});owner->commit(edit);
 owner->select(region);const auto before=owner->snapshot();const auto history=owner->undo_steps();
 const auto command=resolve_saved_roi_geometry(before,SavedRoiGeometryRequest(0,moved));
 check(std::get<std::int64_t>(command.record().at("version"))==3,"Regional command did not use V3");
 validate_saved_roi_geometry_record(command.record());
 const auto replay=saved_roi_geometry_from_record(before,command.record());
 check(replay.original_roi().selection==region && replay.replacement()==moved,"Replay lost full geometry");
 auto prepared=app.prepare_saved_roi_geometry(app.capture_saved_roi_geometry(),replay);
 check(owner->snapshot().shares_data(before),"Preparation changed owner");
 const auto result=app.publish_saved_roi_geometry(prepared);
 check(result.changed && owner->undo_steps()==history+1,"Publication lost history");
 check(owner->snapshot().data().rois[0].selection==moved,"Publication clipped regional row");
 check(owner->snapshot().data().selection==region,"Publication changed active geometry");
 check(reads.counts().plane_reads==0 && reads.counts().region_reads==0,"Geometry command read samples");
 owner->undo();check(owner->snapshot().shares_data(before),"Undo lost original row");
 std::cout<<"PASS V3 saved regional geometry capture/replay/publication/undo/no reads\n";
 return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
