#include "planar_annotation_rebind.hpp"
#include "geometry_number.hpp"
#include <imagejxx/roi_style.hpp>
#include <imagejxx/selection_edit.hpp>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <cmath>

namespace imagejxx::desktop {
struct PlanarAnnotationRebind::State {
  std::optional<DocumentSnapshot> source;
  MetadataObject original;
  std::optional<PlanarAnnotation> replacement;
  bool open=false,focus=false,all_t=false,all_c=false;
  int operation=0,target=0,plane=0,coordinate=0,t=1,c=1,vertex=1;
  std::string name,stroke,fill,stroke_width,error,cancel_reason;
  std::array<std::string,5> box;
  std::array<std::string,2> point;
  Json controls=Json::object();
  void seed(const PlanarAnnotation &value) {
    replacement=value;name=value.name;plane=int(value.plane);coordinate=value.coordinate;
    all_t=!value.t;all_c=!value.c;t=value.t.value_or(0)+1;c=value.c.value_or(0)+1;
    const auto style=resolve_roi_style(value.selection,{64,210,240,255});
    stroke=roi_color_string(style.stroke_color);fill=roi_color_string(style.fill_color);
    stroke_width=geometry_number_text(style.stroke_width);vertex=1;load_vertex();
    if(const auto frame=value.selection.box_frame()) {
      const std::array<double,5> values{frame->center[0],frame->center[1],
          2*std::hypot(frame->u[0],frame->u[1]),2*std::hypot(frame->v[0],frame->v[1]),
          std::atan2(frame->u[1],frame->u[0])*180/std::acos(-1.)};
      for(int i=0;i<5;++i)box[i]=geometry_number_text(values[i]);
    }
    error.clear();
  }
  void load_vertex() {
    if(replacement && vertex>0 && std::size_t(vertex)<=replacement->selection.points.size())
      for(int i=0;i<2;++i)point[i]=geometry_number_text(replacement->selection.points[vertex-1][i]);
  }
  Selection vertex_geometry() const {
    if(!replacement || vertex<1 || std::size_t(vertex)>replacement->selection.points.size())
      throw std::invalid_argument("Select a valid 1-based replacement vertex");
    auto geometry=replacement->selection;
    geometry.points[vertex-1]={parse_geometry_number(point[0]),parse_geometry_number(point[1])};
    return geometry;
  }
  PlanarAnnotationRequest request() const {
    const auto op=operation==0?PlanarAnnotationOperation::Store:operation==1?PlanarAnnotationOperation::Update:PlanarAnnotationOperation::Delete;
    std::optional<std::size_t> index;
    if(op!=PlanarAnnotationOperation::Store) {
      if(target<1)throw std::invalid_argument("Choose a destination annotation (1-based)");
      index=std::size_t(target-1);
    }
    if(op==PlanarAnnotationOperation::Delete)return {op,index,{}};
    if(!replacement)throw std::invalid_argument("Choose a complete replacement annotation");
    auto value=*replacement;value.name=name;value.plane=Plane(plane);value.coordinate=coordinate;
    if((!all_t && t<1) || (!all_c && c<1))throw std::invalid_argument("Time and channel must be positive 1-based indices");
    value.t=all_t?std::optional<int>{}:t-1;value.c=all_c?std::optional<int>{}:c-1;
    if(value.selection.box_frame()) {
      std::array<double,5> values;
      for(int i=0;i<5;++i)values[i]=parse_geometry_number(box[i]);
      const bool ellipse=value.selection.kind=="ellipse" || value.selection.kind=="rotated_ellipse";
      Selection unit(ellipse?"ellipse":"rectangle",{{0,0},{1,1}},value.selection.width);
      unit.attributes=value.selection.attributes;
      const auto dimensions=planar_annotation_geometry(source->data().dataset.shape(),value.plane);
      value.selection=SelectionEditor(unit,dimensions.width,dimensions.height,SelectionEditLimits{})
          .box_geometry({values[0],values[1]},{values[2],values[3]},values[4]);
    } else if(value.selection.kind!="mask" && !value.selection.points.empty())value.selection=vertex_geometry();
    value.selection=with_roi_style(value.selection,{{"stroke_color",stroke},{"fill_color",fill},{"stroke_width",parse_geometry_number(stroke_width)}});
    return {op,index,std::move(value)};
  }
  void observe(const char *key,bool enabled=true,float width=0,ImGuiID explicit_id=0) {
    const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
    controls[key]={{"id",explicit_id?explicit_id:ImGui::GetItemID()},{"enabled",enabled},
        {"rect",{a.x,a.y,b.x,b.y}},{"point",{width>0?a.x+width/2:(a.x+b.x)/2,(a.y+b.y)/2}}};
  }
};
PlanarAnnotationRebind::PlanarAnnotationRebind():state_(std::make_unique<State>()) {}
PlanarAnnotationRebind::~PlanarAnnotationRebind()=default;
bool PlanarAnnotationRebind::visible() const {return state_->open;}
void PlanarAnnotationRebind::cancel(const char *reason) {
  state_->open=false;state_->source.reset();state_->original.clear();state_->replacement.reset();
  state_->controls=Json::object();state_->cancel_reason=reason;
}
void PlanarAnnotationRebind::show(const DocumentSnapshot &source,const MetadataObject &original) {
  // Reject the full original equation before replacing any frontend draft.
  validate_planar_annotation_command_record(original);
  State next;next.source=source;next.original=original;next.open=next.focus=true;
  const auto record=metadataJson(original);const auto dimensions=record.at("source").at("shape").get<std::array<int,6>>();
  const DatasetShape old_shape{dimensions[0],dimensions[1],dimensions[2],dimensions[3],dimensions[4],dimensions[5]};
  const auto request=decode_planar_annotation_request(old_shape,metadataObjectFromJson({{"operation",record.at("operation")},{"index",record.at("index")},{"annotation",record.at("annotation")}}));
  next.operation=request.operation==PlanarAnnotationOperation::Store?0:request.operation==PlanarAnnotationOperation::Update?1:2;
  // Destination index intentionally starts unselected, even for a captured index.
  if(request.annotation)next.seed(*request.annotation);
  else next.seed({"Annotation",Selection("rectangle",{{0,0},{1,1}}),Plane::XY,source.display().crosshair[2],source.display().t,source.display().c,{}});
  *state_=std::move(next);
}
void PlanarAnnotationRebind::draw(const std::optional<DocumentSnapshot> &current,bool busy,const PlanarAnnotationPublisher &publish) {
  auto &s=*state_;s.controls=Json::object();if(!s.open)return;
  if(!current || !s.source || !s.source->same_document(*current) ||
     !s.source->revisions().matches(current->revisions(),Change::Pixels|Change::Metadata)) {cancel("source_changed");return;}
  if(busy) {cancel("busy");return;}
  if(s.focus)ImGui::SetNextWindowFocus();
  ImGui::SetNextWindowSize({600,630},ImGuiCond_FirstUseEver);
  ImGui::Begin("Rebind planar annotation",&s.open);s.focus=false;
  ImGui::TextWrapped("Choose the destination and complete replacement. Apply creates a new command for this image; the captured command stays unchanged.");
  ImGui::Text("Destination: %s",current->data().metadata.title.c_str());
  if(ImGui::BeginCombo("Operation",s.operation==0?"Store new":s.operation==1?"Update existing":"Delete existing")) {
    for(int i=0;i<3;++i) {
      ImGui::PushID(i);if(ImGui::Selectable(i==0?"Store new":i==1?"Update existing":"Delete existing",s.operation==i))s.operation=i;
      const auto key=std::string("operation.")+std::to_string(i);s.observe(key.c_str());ImGui::PopID();
    }ImGui::EndCombo();
  }s.observe("operation");
  const auto rows=planar_annotations(current->data());
  if(s.operation!=0) {
    ImGui::SetNextItemWidth(90);const auto id=ImGui::GetID("Destination row (1-based)");
    ImGui::InputInt("Destination row (1-based)",&s.target,0,0);s.observe("target",true,90,id);
    if(ImGui::BeginCombo("Destination annotation",s.target>0 && std::size_t(s.target)<=rows.size()?rows[s.target-1].name.c_str():"Choose an annotation")) {
      for(std::size_t i=0;i<rows.size();++i) {
        ImGui::PushID(int(i));if(ImGui::Selectable(rows[i].name.c_str(),s.target==int(i+1)))s.target=int(i+1);
        const auto key="target."+std::to_string(i);s.observe(key.c_str());ImGui::PopID();
      }ImGui::EndCombo();
    }s.observe("target_choice");
  }
  if(s.operation!=2) {
    if(ImGui::BeginCombo("Load replacement from image","Choose an annotation")) {
      for(std::size_t i=0;i<rows.size();++i) {
        ImGui::PushID(int(i));if(ImGui::Selectable(rows[i].name.c_str()))s.seed(rows[i]);
        const auto key="replacement."+std::to_string(i);s.observe(key.c_str());ImGui::PopID();
      }ImGui::EndCombo();
    }s.observe("replacement_choice");
    ImGui::SetNextItemWidth(220);ImGui::InputText("Name",&s.name);s.observe("name",true,220);
    ImGui::SetNextItemWidth(80);ImGui::Combo("Plane",&s.plane,"XY\0XZ\0YZ\0");s.observe("plane");ImGui::SameLine();
    ImGui::SetNextItemWidth(90);const auto fixed_id=ImGui::GetID("Fixed coordinate (0-based)");
    ImGui::InputInt("Fixed coordinate (0-based)",&s.coordinate,0,0);s.observe("coordinate",true,90,fixed_id);
    ImGui::Checkbox("All time points",&s.all_t);s.observe("all_t");ImGui::SameLine();
    ImGui::BeginDisabled(s.all_t);ImGui::SetNextItemWidth(90);const auto t_id=ImGui::GetID("Time (1-based)");
    ImGui::InputInt("Time (1-based)",&s.t,0,0);s.observe("t",!s.all_t,90,t_id);ImGui::EndDisabled();
    ImGui::Checkbox("All scientific channels",&s.all_c);s.observe("all_c");ImGui::SameLine();
    ImGui::BeginDisabled(s.all_c);ImGui::SetNextItemWidth(90);const auto c_id=ImGui::GetID("Channel (1-based)");
    ImGui::InputInt("Channel (1-based)",&s.c,0,0);s.observe("c",!s.all_c,90,c_id);ImGui::EndDisabled();
    auto number=[&](const char *label,std::string &text,const char *key) {
      ImGui::SetNextItemWidth(90);ImGui::InputText(label,&text);s.observe(key,true,90);
    };
    if(s.replacement->selection.box_frame()) {
      number("Center U",s.box[0],"center_u");ImGui::SameLine();number("Center V",s.box[1],"center_v");
      number("Width",s.box[2],"width");ImGui::SameLine();number("Height",s.box[3],"height");ImGui::SameLine();number("Angle",s.box[4],"angle");
    } else if(s.replacement->selection.kind!="mask" && !s.replacement->selection.points.empty()) {
      ImGui::Text("%s: %zu vertices",s.replacement->selection.kind.c_str(),s.replacement->selection.points.size());
      ImGui::SetNextItemWidth(90);const auto vertex_id=ImGui::GetID("Vertex (1-based)");
      if(ImGui::InputInt("Vertex (1-based)",&s.vertex,0,0))s.load_vertex();s.observe("vertex",true,90,vertex_id);
      number("Vertex U",s.point[0],"vertex_u");ImGui::SameLine();number("Vertex V",s.point[1],"vertex_v");
      if(ImGui::Button("Stage vertex"))try {s.replacement->selection=s.vertex_geometry();s.error.clear();}catch(const std::exception &e){s.error=e.what();}
      s.observe("stage_vertex");ImGui::TextUnformatted("Stage each edited vertex before choosing another. Apply includes the current vertex.");
    } else ImGui::TextUnformatted("Packed mask geometry is retained. Load a matching destination mask to change its extent.");
    ImGui::SetNextItemWidth(130);ImGui::InputText("Stroke (#AARRGGBB/none)",&s.stroke);s.observe("stroke",true,130);
    ImGui::SetNextItemWidth(130);ImGui::InputText("Fill (#AARRGGBB/none)",&s.fill);s.observe("fill",true,130);ImGui::SameLine();
    number("Stroke width",s.stroke_width,"stroke_width");
  }
  const bool apply=ImGui::Button("Apply rebound annotation");s.observe("apply");ImGui::SameLine();
  bool canceled=ImGui::Button("Cancel");s.observe("cancel");
  canceled|=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape);
  if(!s.error.empty())ImGui::TextWrapped("%s",s.error.c_str());
  ImGui::End();
  if(!s.open || canceled) {cancel("canceled");return;}
  if(apply)try {
    const auto command=rebind_planar_annotation_command(*s.source,s.original,s.request());
    publish(command);cancel("accepted");
  }catch(const std::exception &e) {s.error=e.what();}
}
Json PlanarAnnotationRebind::diagnostics() const {
  const auto &s=*state_;return {{"visible",s.open},{"controls",s.controls},{"error",s.error},{"cancel_reason",s.cancel_reason},
      {"operation",s.operation},{"destination_row",s.target},{"source_document_id",s.source?s.source->id():0}};
}
} // namespace imagejxx::desktop
