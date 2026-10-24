#include <imagejxx/selection_edit.hpp>
#include <imagejxx/composite_path.hpp>
#include <cmath>
#include <iostream>
using namespace imagejxx;
namespace {
int checks=0;
void check(bool b,const char *s){if(!b)throw std::runtime_error(s);++checks;}
template<class E=std::invalid_argument,class F>void refuses(F f){try{f();}catch(const E&){++checks;return;}throw std::runtime_error("Expected resize refusal");}
Selection path(){auto p=Selection::from_path({0,0,0,1,10,0,2,15,5,10,10,3,8,12,2,12,0,10,4,0,2,2,1,3,2,1,3,3,4});p.width=2.5;p.attributes["opaque"]=metadata_array({std::string("kept"),std::uint64_t(-1)});return p;}
void affine(const std::vector<double>& source,const std::vector<double>& actual,std::array<double,2> a,std::array<double,2>s){
  check(source.size()==actual.size(),"Changed command count");
  for(std::size_t i=0;i<source.size();){const int op=int(source[i]);check(actual[i]==op,"Changed opcode");++i;const int n=op==4?0:op==3?6:op==2?4:2;
    for(int j=0;j<n;++j){const double expected=a[j%2]+s[j%2]*(source[i+j]-a[j%2]);check(std::abs(actual[i+j]-expected)<1e-12,"Affine operand mismatch");}i+=n;
  }
}
}
int main(){try{
  // Two nonzero axis scales remain invertible even when their determinant
  // product underflows binary64. Every expected operand is independently
  // representable; this is not a zero-area target or coincident-point case.
  const Selection unit=Selection::from_path({0,0,0,1,1,0,1,1,1,1,0,1,4});
  const auto tiny=SelectionEditor(unit,32,24).resized(SelectionHandleMode::Bounds,4,{1e-200,1e-200},false,false,{});
  check(tiny.path_commands()==std::vector<double>({0,0,0,1,1e-200,0,1,1e-200,1e-200,1,0,1e-200,4}),
        "Nonzero tiny resize did not preserve literal representable coordinates");
  refuses([&]{unit.path_geometry().transformed({0,0,0,0,1,0});});
  refuses([&]{unit.path_geometry().transformed({1,0,0,0,0,0});});
  // The bounded diagonal repair does not change general-affine admission.
  refuses([&]{unit.path_geometry().transformed({1e-200,1e-200,0,1e-200,1e-200,0});});
  const auto near=std::nextafter(1.,2.);
  const auto near_collinear=unit.path_geometry().transformed({1,1,0,1,near,0});
  check(near_collinear.commands()==std::vector<double>({0,0,0,1,1,1,1,2,2,1,1,near,4}),
        "Near-collinear general-affine admission changed");
  const auto p=path();SelectionEditor editor(p,32,24);const auto original=p.path_commands();
  check(editor.bounds()==std::array<double,4>{0,0,12.5,11.5},"Used control hull instead of analytic bounds");
  // Opt-in Bounds must not change the historical no-mode/ControlPoints API.
  int legacy_calls=0;ExecutionControl legacy_control;
  legacy_control.checkpoint=[&]{++legacy_calls;};
  legacy_control.progress=[&](auto,auto){++legacy_calls;};
  refuses([&]{editor.resized(0,{1,1});});
  refuses([&]{editor.resized(0,{1,1},false,false,{},legacy_control);});
  refuses([&]{editor.resized(SelectionHandleMode::ControlPoints,0,{1,1});});
  refuses([&]{editor.resized(SelectionHandleMode::ControlPoints,0,{1,1},false,false,{},legacy_control);});
  const auto invalid_mode=static_cast<SelectionHandleMode>(-1);
  refuses([&]{editor.resized(invalid_mode,4,{25,23});});
  refuses([&]{editor.resized(invalid_mode,4,{25,23},false,false,{},legacy_control);});
  for(const auto *kind:{"rectangle","ellipse"}) {
    SelectionEditor ordinary(Selection(kind,{{2,3},{6,5}},3),32,24);
    const std::vector<std::array<double,2>> expected{{2,3},{8,7}};
    check(ordinary.resized(4,{8,7}).points==expected,"Legacy ordinary resize changed");
    check(ordinary.resized(SelectionHandleMode::ControlPoints,4,{8,7}).points==expected,
          "Explicit ControlPoints changed ordinary resize");
    check(ordinary.resized(SelectionHandleMode::ControlPoints,4,{8,7},false,false,{}).points==expected,
          "Controlled ControlPoints changed ordinary resize");
    refuses([&]{ordinary.resized(SelectionHandleMode::Bounds,4,{8,7});});
    refuses([&]{ordinary.resized(SelectionHandleMode::Bounds,4,{8,7},false,false,{},legacy_control);});
  }
  check(legacy_calls==0,"Mode or legacy refusal called callbacks");
  const auto handles=editor.handles(SelectionHandleMode::Bounds);check(handles.size()==8,"Missing resize handles");
  for(const auto &h:editor.handles())check(h.kind==SelectionHandleKind::Vertex,"Changed old handle mode");
  const std::array<int,8> xs{-1,0,1,1,1,0,-1,-1},ys{-1,-1,-1,0,1,1,1,0};
  for(std::size_t i=0;i<8;++i)for(bool center:{false,true})for(bool aspect:{false,true})for(double sign:{-1.,1.}){
    std::array<double,2>a{center||!xs[i]?6.25:xs[i]<0?12.5:0,center||!ys[i]?5.75:ys[i]<0?11.5:0};
    const auto h=handles[i].position;std::array<double,2>s{xs[i]?sign*2:1,ys[i]?sign*3:1},point{a[0]+s[0]*(h[0]-a[0]),a[1]+s[1]*(h[1]-a[1])};
    if(aspect){const double m=xs[i]&&ys[i]?3:xs[i]?2:3;s={xs[i]?sign*m:m,ys[i]?sign*m:m};}
    const auto out=editor.resized(SelectionHandleMode::Bounds,i,point,aspect,center,{});affine(original,out.path_commands(),a,s);
    check(out.attributes==p.attributes && out.width==p.width,"Lost style");
  }
  check(editor.selection().path_commands()==original,"Mutated source");
  check(editor.resized(SelectionHandleMode::Bounds,4,handles[4].position,false,false,{})==p,"Identity differs");
  const auto hit=editor.hit_test(handles[0].position,{.1,.1},{},SelectionHandleMode::Bounds);
  check(hit&&hit->kind==SelectionHandleKind::Resize&&hit->index==0,"Bounds hit test wrong");
  check(!editor.hit_test({5,5},{.1,.1},{},SelectionHandleMode::Bounds),"Bounds mode moved whole path");
  SelectionEditor line(Selection::from_path({0,1,-0.,1,9,-0.}),32,24);
  const auto lh=line.handles(SelectionHandleMode::Bounds);check(lh.size()==2&&lh[0].index==3&&lh[1].index==7,"Degenerate handles wrong");
  const auto wider=line.resized(SelectionHandleMode::Bounds,3,{17,0},false,false,{});check(std::signbit(wider.path_commands()[2])&&std::signbit(wider.path_commands()[5]),"Uncontrolled signed zero changed");
  refuses([&]{line.resized(SelectionHandleMode::Bounds,0,{3,3},false,false,{});});refuses([&]{line.resized(SelectionHandleMode::Bounds,3,{17,0},true,false,{});});
  SelectionEditor point(Selection::from_path({0,3,3}),32,24);check(point.handles(SelectionHandleMode::Bounds).empty(),"Point has resize handles");
  int calls=0;ExecutionControl control;control.progress=[&](auto,auto){++calls;};control.checkpoint=[&]{++calls;};
  refuses<CollapsedSelectionResize>([&]{editor.resized(SelectionHandleMode::Bounds,4,{0,18},true,false,{},control);});check(calls==0,"Collapse called callbacks");
  auto limits=SelectionEditLimits{};limits.max_working_bytes=1;
  refuses<std::length_error>([&]{editor.resized(SelectionHandleMode::Bounds,4,{20,20},false,false,limits,control);});check(calls==0,"Resource refusal called callbacks");
  refuses([&]{editor.resized(SelectionHandleMode::Bounds,4,{INFINITY,20},false,false,{},control);});
  refuses([&]{editor.resized(SelectionHandleMode::Bounds,4,{1e30,20},false,false,{},control);});check(calls==0,"Invalid input called callbacks");
  affine(original,editor.resized(SelectionHandleMode::Bounds,4,{-12.5,-11.5},false,false,{}).path_commands(),{0,0},{-1,-1});
  auto token=std::make_shared<CancellationToken>();control={};control.cancellation=token;control.progress=[&](auto done,auto){if(done==1)token->cancel();};
  refuses<OperationCancelled>([&]{editor.resized(SelectionHandleMode::Bounds,4,{25,23},false,false,{},control);});check(editor.selection()==p,"Cancel changed editor");
  control={};auto mutable_editor=editor;limits={};control.progress=[&](auto,auto){mutable_editor=point;limits.max_working_bytes=1;control={};};
  affine(original,mutable_editor.resized(SelectionHandleMode::Bounds,4,{25,23},false,false,limits,control).path_commands(),{0,0},{2,2});
  std::cout<<"PASS "<<checks<<" path resize checks\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
