#include <imagejxx/selection_region.hpp>
#include <imagejxx/dataset_histogram.hpp>
#include <imagejxx/auto_contrast.hpp>
#include <imagejxx/channel_display.hpp>
#include <imagejxx/contrast_apply_command.hpp>
#include <imagejxx/rgb_contrast_session_command.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {void check(bool ok,const char *s){if(!ok)throw std::runtime_error(s);}}
int main(){try{
 Image image(9,9,1,PixelType::UInt16),mask(9,9,1,PixelType::UInt8);
 for(int i=0;i<81;++i)image.pixels[i]=i;
 mask.at(0,0)=255;
 const auto area=SelectionRegion(Selection::from_mask(mask),9,9).enlarged(2).as_selection();
 DatasetReadObserver reads;Document owner{Dataset::from_stack({image}).observe_reads(reads)};owner.select(area);
 ChannelDisplayState state;state.channels.resize(1);
 owner.set_display(with_channel_display(owner.snapshot().display(),owner.snapshot().data().dataset,state));
 const auto before=owner.snapshot();
 DatasetHistogramRequest request;request.area_roi=area;
 const auto estimate=estimate_dataset_histogram(before.data().dataset,request);
 check(estimate.selection_bytes>=area.packed_mask().size() && reads.counts().plane_reads==0,"Admission read source or lost retained geometry");
 const auto histogram=accumulate_dataset_histogram(before.data().dataset,request);
 check(histogram.visited_count==8 && histogram.sum==70 && histogram.plane_finite_count==81,"Histogram used off-image cells");
 const auto automatic=resolve_auto_contrast_command(before);
 check(std::get<std::int64_t>(automatic.record().at("version"))==2,"Regional Auto did not capture V2");
 check(automatic.selection_provenance()==area && automatic.resolved_range()==std::array<double,2>{0,80},"Auto changed provenance or fallback");
 validate_auto_contrast_command_record(automatic.record());
 const auto replay=auto_contrast_command_from_record(before,automatic.record());
 check(replay.selection_provenance()==area,"Auto replay lost full frame");
 const auto apply=resolve_contrast_apply_command(before,{0,80,8});
 check(std::get<std::int64_t>(apply.record().at("schema_version"))==2,"Regional Apply did not capture V2");
 validate_contrast_apply_command_record(apply.record());
 const auto strict=contrast_apply_command_from_record(before,apply.record());
 auto edit=execute_contrast_apply_command(strict);owner.commit(edit);
 const auto output=owner.snapshot().data().dataset.plane({0,0,0});
 check(output->at(1,0)==3 && output->at(8,8)==80,"Apply changed outside pixels or used wrong mapping");
 check(owner.snapshot().data().selection==area && owner.undo_steps()==1,"Apply lost geometry/history");
 owner.undo();check(owner.snapshot().shares_data(before),"Undo did not restore source");
 Image rgb(9,9,3,PixelType::UInt8);
 for(int i=0;i<81;++i)for(int c=0;c<3;++c)rgb.pixels[i*3+c]=i;
 Document color{Dataset::from_stack({rgb})};color.select(area);
 RgbContrastSessionPlan plan;plan.visits.push_back({{0,0,0},{{{0,80,4},area,false}}});
 const auto session=resolve_rgb_contrast_session_command(color.snapshot(),plan);
 check(std::get<std::int64_t>(session.record().at("schema_version"))==2,"Regional history did not use V2");
 validate_rgb_contrast_session_command_record(session.record());
 auto rgb_edit=execute_rgb_contrast_session_command(session);color.commit(rgb_edit);
 const auto result=color.snapshot().data().dataset.plane({0,0,0});
 check(result->at(1,0,0)==3 && result->at(1,0,1)==1 && result->at(8,8,0)==80,"RGB history changed wrong members/components");
 check(color.snapshot().data().selection==area,"RGB history clipped canonical geometry");
 std::cout<<"PASS regional histogram/Auto/scalar Apply/RGB history SDK\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
