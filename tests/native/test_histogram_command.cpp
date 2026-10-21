#include <imagejxx/histogram_command.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
void check(bool ok){if(!ok)throw std::runtime_error("Histogram command contract failed");}
template<class E=std::invalid_argument,class F> void rejects(F f){bool rejected=false;try{f();}catch(const E &){rejected=true;}check(rejected);}
struct Source final:PlaneSource {
  mutable int reads=0;
  DatasetShape shape() const override{return {1,1,3,2,2,1};}
  PixelType pixel_type() const override{return PixelType::UInt16;}
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    ++reads;Image plane(2,2,1,PixelType::UInt16);
    for(int i=0;i<4;++i)plane.pixels[i]=p.z*4+i;
    return std::make_shared<const Image>(std::move(plane));
  }
};
int main(){try {
  auto provider=std::make_shared<Source>();Document owner{Dataset(provider)};
  auto display=owner.snapshot().display();display.crosshair[2]=1;owner.set_display(display);
  auto command=resolve_histogram_command(owner.snapshot());auto record=command.record();
  check(provider->reads==0 && command.target().position(0).z==1);
  validate_histogram_command_record(record);
  auto estimate=estimate_histogram_command(command);check(estimate.record_bytes>=65536);
  HistogramCommandLimits cap;cap.max_record_bytes=1;
  rejects([&]{resolve_histogram_command(owner.snapshot(),{}, {},false,cap);});
  rejects([&]{execute_histogram_command(command,cap);});
  check(provider->reads==0);
  display.crosshair[2]=2;owner.set_display(display);
  const auto replay=histogram_command_from_record(owner.snapshot(),record);
  const auto result=execute_histogram_command(replay);
  check(result.finite_count==4 && result.mean==5.5 && provider->reads==2 && owner.undo_steps()==0);
  Document other{Dataset(provider)};
  rejects([&]{histogram_command_from_record(other.snapshot(),record);});
  const auto rebound=rebind_histogram_command(other.snapshot(),record,{});
  check(rebound.target().position(0).z==0 && execute_histogram_command(rebound).mean==1.5);
  auto wrong=record;wrong["log_counts"]=1;
  rejects([&]{rebind_histogram_command(other.snapshot(),wrong,{});});
  wrong=record;wrong["junk"]=std::string(9000000,'x');
  rejects([&]{validate_histogram_command_record(wrong);});
  wrong=record;wrong["options"]=metadata_object({{"binning",std::string("uniform_range")},{"bins",4},{"range",metadata_array({true,5.0})}});
  rejects([&]{validate_histogram_command_record(wrong);});
  auto edit=owner.begin_edit();edit.set_title("changed");owner.commit(edit);
  rejects<RevisionConflict>([&]{histogram_command_from_record(owner.snapshot(),record);});
  // Executing a captured immutable snapshot remains valid after owner changes.
  check(execute_histogram_command(command).mean==5.5);
  TargetHistogramOptions options;options.binning=HistogramBinning::UniformRange;
  options.bins=3;options.range=std::array<double,2>{0,12};
  TargetRequest all;all.scope=TargetScope::AllZ;all.roi=RoiPolicy::WholePlane;
  auto mutable_command=resolve_histogram_command(other.snapshot(),options,all,true);
  ExecutionControl control;control.progress=[&](std::size_t n,std::size_t){if(n==0)mutable_command=command;};
  const auto frozen=execute_histogram_command(mutable_command,{},control);
  check(frozen.histogram.counts==std::vector<std::uint64_t>({4,4,4}));
  auto token=std::make_shared<CancellationToken>();token->cancel();control={};control.cancellation=token;
  const int before=provider->reads;
  rejects<OperationCancelled>([&]{execute_histogram_command(command,{},control);});check(before==provider->reads);
  std::weak_ptr<Source> weak;
  TargetHistogramResult detached;
  {auto source=std::make_shared<Source>();weak=source;Document temporary{Dataset(source)};
    const auto captured=resolve_histogram_command(temporary.snapshot());detached=execute_histogram_command(captured);}
  check(weak.expired() && detached.mean==1.5);
  std::cout<<"histogram immutable intent, strict replay, rebind, budgets, callbacks and ownership passed\n";
  return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
