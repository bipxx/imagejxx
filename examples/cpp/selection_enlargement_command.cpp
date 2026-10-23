#include <imagejxx/selection_enlargement.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace imagejxx;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
class Unread final : public PlaneSource {
public:
 DatasetShape shape() const override{return {2,2,2,9,9,1};}
 PixelType pixel_type() const override{return PixelType::UInt16;}
 std::shared_ptr<const Image> read_plane(PlanePosition) const override{throw std::runtime_error("Forbidden sample read");}
};
}
int main(){try{
 SpatialCalibration calibration;calibration.spacing={.5,1.5,2};calibration.unit="um";
 Document owner{Dataset(std::make_shared<Unread>(),calibration)};
 owner.select(Selection("rectangle",{{2,2},{7,7}}));
 auto display=owner.snapshot().display();display.t=display.c=display.crosshair[2]=1;owner.set_display(display);
 const auto before=owner.snapshot();
 const SelectionEnlargementRequest request{1,SelectionEnlargementUnits::Calibrated};
 const auto command=resolve_selection_enlargement_command(before,request);
 check(command.estimate().pixel_distance==2,"Calibrated distance did not use X spacing");
 check(command.result().points==std::vector<std::array<double,2>>{{0,0},{9,9}},"Analytic result differs");
 validate_selection_enlargement_command_record(command.record());
 const auto replay=selection_enlargement_command_from_record(before,command.record());
 auto edit=prepare_selection_enlargement(replay);owner.commit(edit);
 check(owner.snapshot().data().previous_selection==before.data().selection,"Previous geometry lost");
 check(owner.snapshot().data().dataset.shares_pixels(before.data().dataset),"Pixels changed");
 check(owner.undo_steps()==0 && !owner.snapshot().dirty(),"Transient edit changed history");
 auto malformed=command.record();malformed["distance"]=2.;bool refused=false;
 try{validate_selection_enlargement_command_record(malformed);}catch(const std::invalid_argument &){refused=true;}
 check(refused,"Forged original equation accepted");
 SelectionEnlargementCommandLimits cap;cap.max_working_bytes=command.estimate().working_bytes-1;
 refused=false;try{resolve_selection_enlargement_command(before,request,cap);}catch(const std::invalid_argument &){refused=true;}
 check(refused,"Below-estimate command admitted");
 auto stale=prepare_selection_enlargement(command);owner.select(Selection("point",{{1,1}}));
 refused=false;try{owner.commit(stale);}catch(const RevisionConflict &){refused=true;}
 check(refused,"Stale selection publication admitted");
 const auto current=owner.snapshot();
 const auto original_again=selection_enlargement_command_from_record(current,command.record());
 check(original_again.result()==command.result(),"Replay silently used live geometry");
 Document destination{Dataset(std::make_shared<Unread>(),calibration)};
 destination.select(Selection("rectangle",{{3,3},{6,6}}));
 refused=false;try{selection_enlargement_command_from_record(destination.snapshot(),command.record());}catch(const std::invalid_argument &){refused=true;}
 check(refused,"Different owner silently rebound");
 const auto rebound=rebind_selection_enlargement_command(destination.snapshot(),command.record(),{.5,SelectionEnlargementUnits::Calibrated});
 check(rebound.result().points==std::vector<std::array<double,2>>{{2,2},{7,7}},"Explicit rebind ignored destination selection");
 std::cout<<"PASS enlargement command SDK calibrated/records/rebind/guards/no-IO\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
