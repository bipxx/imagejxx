#include <imagejxx/rgb_contrast_session_editor.hpp>
#include <algorithm>
#include <iostream>

using namespace imagejxx;
int main(){try{
  Image image(2,1,3,PixelType::UInt8);std::fill(image.pixels.begin(),image.pixels.end(),30);
  Document owner(Dataset::from_stack({image}));const auto before=owner.snapshot();
  RgbContrastSessionEditor editor(before);editor.set_range(10,40);editor.promote();
  editor.set_range(0,170);editor.promote();editor.set_saved_stage(0,0,{10,70,7});
  editor.move_saved_stage(0,0,1);
  auto mapped=execute_rgb_contrast_session_command(editor.command()).preview();
  if(mapped.data().dataset.plane()->at(0,0,0)!=149)throw std::runtime_error("Saved order differs");
  editor.remove_saved_stage(0,1);
  auto edit=execute_rgb_contrast_session_command(editor.command());
  if(edit.preview().data().dataset.plane()->at(0,0,0)!=45)throw std::runtime_error("Saved removal differs");
  owner.commit(edit);
  if(owner.undo_steps()!=1)throw std::runtime_error("Expected one paired edit");
  owner.undo();if(!owner.snapshot().shares_data(before))throw std::runtime_error("Undo differs");
  RgbContrastSessionEditor area(before);area.set_range(10,40);area.promote();
  area.set_saved_selection(0,0,Selection("rectangle",{{1,0},{2,1}}));
  auto selected=execute_rgb_contrast_session_command(area.command()).preview().data().dataset.plane();
  if(selected->at(0,0,0)!=30 || selected->at(1,0,0)!=170)throw std::runtime_error("Saved selection differs");
  area.set_saved_selection(0,0,area.plan().visits[0].stages[0].selection,true);
  selected=execute_rgb_contrast_session_command(area.command()).preview().data().dataset.plane();
  if(selected->at(0,0,0)!=170 || selected->at(1,0,0)!=170)throw std::runtime_error("Whole-plane area differs");
  std::cout<<"PASS installed RGB saved-stage update/order/removal and paired history\n";
  return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
