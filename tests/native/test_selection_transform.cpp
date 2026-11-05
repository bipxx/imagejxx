#include <imagejxx/selection_transform.hpp>
#include <imagejxx/point_annotations.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F>void refuses(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("Expected hull refusal");}
int main(){try{
  Selection input("multipoint",{{2,2},{12,2},{8,8},{12,12},{2,12},{2,2}});
  const std::vector<std::array<double,2>> expected{{2,2},{2,12},{12,12},{12,2}};
  check(convex_hull_selection(input,16,16).points==expected,"Hull ordering/concavity/duplicates differ");
  auto limits=SelectionEditLimits{};limits.max_working_bytes=estimate_convex_hull_selection(input,16,16);
  check(convex_hull_selection(input,16,16,false,limits).points==expected,"Exact hull cap refused");
  --limits.max_working_bytes;refuses([&]{convex_hull_selection(input,16,16,false,limits);});
  auto annotated=with_point_annotations(input,std::vector<PointAnnotation>(input.points.size(),PointAnnotation{2,{}}));
  refuses([&]{convex_hull_selection(annotated,16,16);});
  check(convex_hull_selection(annotated,16,16,true).attributes.count(point_annotations_attribute)==0,"Hull retained incompatible point annotations");
  refuses([&]{convex_hull_selection(Selection("line",{{0,0},{4,4}}),16,16);});
  refuses([&]{convex_hull_selection(Selection("multipoint",{{0,0},{1,1},{2,2}}),16,16);});
  ExecutionControl control;control.checkpoint=[&]{input.points={{1,1},{3,1},{2,3}};};
  const Selection original("multipoint",{{2,2},{12,2},{8,8},{12,12},{2,12}});input=original;
  check(convex_hull_selection(input,16,16,false,{},control).points==expected,"Callback redirected captured hull input");
  control.checkpoint=[] {throw std::runtime_error("cancel");};
  refuses([&]{convex_hull_selection(original,16,16,false,{},control);});
  std::cout<<"PASS Convex Hull geometry, exact/short limits, point metadata policy and frozen callback input\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
