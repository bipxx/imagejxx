// Core-only installed SDK example: no Python, ImGui or private includes.
#include <imagejxx/roi_manager.hpp>
#include <imagejxx/roi_style.hpp>
#include <iostream>
#include <stdexcept>

int main() {
  using namespace imagejxx;
  try {
    Stack planes;
    for(int i=0;i<12;++i) planes.emplace_back(7,5,1,PixelType::UInt16);
    Document document(Dataset::from_planes({2,2,3,5,7,1},std::move(planes)),"ROI example");
    const auto publish=[&](PreparedRoiManagerEdit edit) {
      document.commit(edit.transaction,edit.display);
    };
    auto display=document.snapshot().display();
    display.t=1;display.c=1;display.crosshair[2]=2;document.set_display(display);
    document.select(Selection("rectangle",{{1,1},{5,4}}));
    publish(prepare_store_roi(document.snapshot(),"captured"));
    const auto stored=document.snapshot().data().rois.at(0);
    if(stored.t!=1 || stored.c!=1 || stored.z!=2) throw std::runtime_error("Captured T/C/Z mismatch");
    publish(prepare_roi_properties(document.snapshot(),0,"styled",
        {{"stroke_color",std::string("red")},{"stroke_width",2.5}},0,{},2));
    publish(prepare_select_roi(document.snapshot(),0));
    const auto result=document.snapshot();
    if(result.display().t!=0 || result.display().c!=1 || result.display().crosshair[2]!=2 ||
        result.data().rois[0].name!="styled" || resolve_roi_style(*result.data().selection).stroke_width!=2.5)
      throw std::runtime_error("Position/style/reselection mismatch");
    if(document.history().undo_steps!=2) throw std::runtime_error("Reselection created history");
    std::cout<<"PASS installed SDK ROI manager: captured TCZ, wildcard, style, atomic reselect, history\n";
    return 0;
  } catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
