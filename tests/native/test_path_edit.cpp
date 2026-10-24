#include <imagejxx/path_edit.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
int checks=0;
void check(bool v) { if(!v)throw std::runtime_error("Path edit assertion "+std::to_string(checks+1));++checks; }
template<class F> void refuses(F f) { try{f();}catch(const std::invalid_argument &){++checks;return;}throw std::runtime_error("Expected path edit refusal"); }
std::array<double,2> evaluate(std::vector<std::array<double,2>> points,double t) {
  while(points.size()>1) {
    for(std::size_t i=0;i+1<points.size();++i)
      for(int a=0;a<2;++a)points[i][a]=points[i][a]*(1-t)+points[i+1][a]*t;
    points.pop_back();
  }
  return points[0];
}
}
int main() {try {
  const ImageJCompositePath p({0,1,2,3,3,-4,8,15,11,3,4,0,20,20,1,30,30});
  const auto original=p.commands();
  auto changed=edit_composite_path(p,PathSegmentEdit::Replace,1,{2,7,8,11,3});
  check(changed.commands()==std::vector<double>({0,1,2,2,7,8,11,3,4,0,20,20,1,30,30}));
  check(edit_composite_path(p,PathSegmentEdit::InsertBefore,5,{4}).commands().back()==4);
  check(edit_composite_path(p,PathSegmentEdit::Remove,2).segment_count()==4);
  check(p.commands()==original);
  refuses([&]{edit_composite_path(p,PathSegmentEdit::Remove,0);});
  refuses([&]{edit_composite_path(p,PathSegmentEdit::Replace,5,{4});});
  refuses([&]{edit_composite_path(p,PathSegmentEdit::Replace,1,{1,0,0,4});});
  refuses([&]{edit_composite_path(p,PathSegmentEdit::Remove,1,{4});});
  refuses([&]{edit_composite_path(p,PathSegmentEdit(-1),1,{});});
  refuses([&]{edit_composite_path(ImageJCompositePath({0,1,2}),PathSegmentEdit::Remove,0);});
  for(const auto op:{1,2,3}) {
    const std::vector<double> commands=op==1?std::vector<double>{0,1,2,1,11,3}:op==2?std::vector<double>{0,1,2,2,7,8,11,3}:std::vector<double>{0,1,2,3,3,-4,8,15,11,3};
    const ImageJCompositePath curve(commands);
    std::vector<std::array<double,2>> controls{{1,2}};
    for(std::size_t i=4;i<commands.size();i+=2)controls.push_back({commands[i],commands[i+1]});
    for(const double t:{.125,.5,.875}) {
      const auto divided=split_composite_path(curve,1,t);
      const auto &c=divided.commands();const auto n=2u*op;
      std::vector<std::array<double,2>> left{{1,2}},right{{c[3+n-1],c[3+n]}};
      for(std::size_t i=4;i<4+n;i+=2)left.push_back({c[i],c[i+1]});
      for(std::size_t i=5+n;i<5+2*n;i+=2)right.push_back({c[i],c[i+1]});
      bool accurate=true;
      for(int j=0;j<=100;++j) {
        const double u=j/100.;
        const auto expected=evaluate(controls,u);
        const auto actual=u<=t?evaluate(left,u/t):evaluate(right,(u-t)/(1-t));
        for(int a=0;a<2;++a)accurate &= std::abs(expected[a]-actual[a])<1e-12;
      }
      check(accurate && divided.segment_count()==3);
    }
  }
  const auto close=split_composite_path(p,2,.25).commands();
  check(close==std::vector<double>({0,1,2,3,3,-4,8,15,11,3,1,8.5,2.75,4,0,20,20,1,30,30}));
  refuses([&]{split_composite_path(p,0);});
  refuses([&]{split_composite_path(p,5);});
  refuses([&]{split_composite_path(p,1,0);});
  refuses([&]{split_composite_path(p,1,1);});
  refuses([&]{split_composite_path(p,1,NAN);});
  int calls=0;ExecutionControl control;control.progress=[&](auto,auto){++calls;};
  PathEditLimits limits;limits.max_segments=p.segment_count();
  refuses([&]{split_composite_path(p,1,.5,limits,control);});check(calls==0);
  limits={};limits.max_working_bytes=1;
  refuses([&]{edit_composite_path(p,PathSegmentEdit::Remove,2,{},limits,control);});check(calls==0);
  limits={};limits.max_command_scalars=p.commands().size();
  refuses([&]{split_composite_path(p,1,.5,limits,control);});check(calls==0);
  auto token=std::make_shared<CancellationToken>();token->cancel();control.cancellation=token;
  try{split_composite_path(p,1,.5,{},control);throw std::runtime_error("Expected cancel");}catch(const OperationCancelled &){++checks;}
  check(calls==0);
  control={};auto mutable_source=p;std::vector<std::array<std::size_t,2>> progress;
  control.progress=[&](auto done,auto total){progress.push_back({done,total});mutable_source=ImageJCompositePath({0,0,0});limits.max_segments=1;control={};};
  limits={};const auto frozen=split_composite_path(mutable_source,1,.5,limits,control);
  check(frozen.commands()==split_composite_path(p,1).commands() && progress==std::vector<std::array<std::size_t,2>>{{0,1},{1,1}});
  token=std::make_shared<CancellationToken>();control={};control.cancellation=token;
  control.progress=[&](auto done,auto){if(done==1)token->cancel();};
  try{split_composite_path(p,1,.5,{},control);throw std::runtime_error("Expected finish cancel");}catch(const OperationCancelled &){++checks;}
  check(p.commands()==original);
  std::cout<<"PASS "<<checks<<" path edit checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
