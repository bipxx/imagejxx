#pragma once
#include <imagejxx/targets.hpp>
#include <string>

namespace imagejxx::desktop {
inline std::string plot_position_caption(PlanePosition position) {
  return "T "+std::to_string(position.t+1)+", C "+std::to_string(position.c+1)+", Z "+std::to_string(position.z+1);
}
inline std::string plot_plane_caption(const DocumentSnapshot& source,const std::string& role) {
  const auto& view=source.display();
  return role+" | "+plot_position_caption({view.t,view.c,view.crosshair[2]});
}
inline std::string plot_selection_caption(const DocumentSnapshot& source) {
  return source.data().selection?"active "+source.data().selection->kind+" selection":"whole plane";
}
inline std::string plot_target_caption(const ResolvedTarget& target) {
  const auto count=target.position_count();
  std::string text=std::to_string(count)+(count==1?" XY plane | ":" XY planes | ");
  if(count==1)text+=plot_position_caption(target.position(0));
  else if(target.scope()==TargetScope::Positions)text+="explicit order; first "+plot_position_caption(target.position(0));
  else {
    const auto& ranges=target.ranges();constexpr const char* names[]={"T","C","Z"};
    for(int i=0;i<3;++i) {
      const auto& r=ranges[std::size_t(i)];const auto last=r.start+(*r.stop-r.start-1)/r.step*r.step;
      if(i)text+=", ";
      text+=std::string(names[i])+" "+std::to_string(r.start+1)+".."+std::to_string(last+1)+" step "+std::to_string(r.step);
    }
  }
  text+=" | ";
  text+=target.roi_policy()==RoiPolicy::PositionedRois?"positioned ROI unions":
      target.roi_policy()==RoiPolicy::ActiveSelection && target.source().data().selection?"active area ROI":"whole plane";
  return text;
}
} // namespace imagejxx::desktop
