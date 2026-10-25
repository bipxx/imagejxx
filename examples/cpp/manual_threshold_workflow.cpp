// Installed C++17 consumer: public headers and independent literal pixels only.
#include <imagejxx/manual_threshold_command.hpp>
#include <imagejxx/native_workflow_program.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
std::size_t checks=0;
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);++checks;}
}
int main(){try{
  Image image(11,11,1,PixelType::UInt16);
  for(int y=0;y<11;++y)for(int x=0;x<11;++x)image.pixels[y*11+x]=7*y+3*x;
  Document capture(Dataset::from_stack({image}));
  const auto p=Selection::from_path({0,2,2,1,8,2,1,2,8,4});
  capture.select(p);
  const auto original=resolve_manual_threshold_command(capture.snapshot(),{45,95}).record();
  Document owner(Dataset::from_stack({image,image}));
  const auto q=Selection::from_path({0,1,1,1,7,1,1,1,7,4});
  owner.select(q);
  const auto before=owner.snapshot();const auto history=owner.undo_steps();
  NativeWorkflowOperation step;step.operation="threshold";step.original=original;
  step.parameters={{"lower",20.},{"upper",35.}};step.target.scope=TargetScope::AllZ;
  const auto program=make_native_workflow_program({step});
  check(std::get<std::int64_t>(program.record().at("version"))==9,"SDK typed manual version");
  const auto restored=native_workflow_program_from_json(program.json());
  check(metadata_equal(restored.record(),program.record()),"SDK program roundtrip");
  const auto prepared=prepare_native_workflow(before,restored.steps());
  check(owner.snapshot().shares_data(before),"SDK preparation published");
  check(prepared.preview().data().dataset.pixel_type()==PixelType::UInt16,
        "SDK partial threshold lost destination dtype");
  for(int z=0;z<2;++z){
    auto expected=image.pixels;
    for(int y=1;y<7;++y)for(int x=1;x<7;++x)if(x+y<8)
      expected[y*11+x]=(image.pixels[y*11+x]>=20&&image.pixels[y*11+x]<=35)?255:0;
    check(prepared.preview().data().dataset.plane({0,0,z})->pixels==expected,"SDK destination literal mask/pixels");
  }
  check(prepared.preview().data().selection==q,"SDK restored captured selection");
  check(metadata_equal(std::get<NativeWorkflowOperation>(restored.steps()[0]).original.value(),original),"SDK changed original");
  auto edit=prepared.begin_edit();owner.commit(edit);
  check(owner.undo_steps()==history+1,"SDK publication not atomic");
  owner.undo();check(owner.snapshot().data().dataset.plane({0,0,0})->pixels==image.pixels,"SDK Undo");
  owner.redo();check(owner.snapshot().data().selection==q,"SDK Redo selection");
  std::cout<<checks<<" installed manual workflow checks passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
