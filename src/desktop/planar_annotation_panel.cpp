#include "planar_annotation_panel.hpp"
#include "geometry_number.hpp"
#include "planar_annotation_visual.hpp"
#include "path_visual.hpp"
#include "path_segment_controls.hpp"
#include "path_authoring_controls.hpp"
#include <imagejxx/planar_annotations.hpp>
#include <imagejxx/planar_annotation_command.hpp>
#include <imagejxx/selection_creation.hpp>
#include <imagejxx/selection_edit.hpp>
#include <map>
#include <imagejxx/roi_style.hpp>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace imagejxx::desktop {
namespace {
const char *planeName(Plane p) {return p==Plane::XY?"XY":p==Plane::XZ?"XZ":"YZ";}
SelectionEditLimits limits() {return {};}
std::array<int,2> snap(std::array<double,2> p,const PlanarCanvas &c) {
  return {int(std::clamp(std::floor(p[0]+.5),0.,double(c.width))),
          int(std::clamp(std::floor(p[1]+.5),0.,double(c.height)))};
}

}
struct PlanarAnnotationPanel::State {
  void controls(Document &,bool enabled,std::size_t cap,const PlanarAnnotationPublisher &);
  bool pointer(Document &,const PlanarCanvas &,bool enabled,bool hovered,const PlanarAnnotationPublisher &);
  void draw(Document &,const PlanarCanvas &,ImDrawList *,bool show,bool editable);
  void cancel(const char *reason);
  bool active() const {return draft_.has_value() || pathDraft_.has_value() || pathResize_.active() || pathAuthoring_.active();}
  Json diagnostics() const;

  enum class Mode {Navigate,Rectangle,Ellipse,Freehand,Line,Point,Edit,Polygon,Multipoint,Path};
  struct Draft {
    DocumentSnapshot source;
    PlanarCanvas canvas;
    PlanarAnnotation original;
    std::optional<std::size_t> row;
    std::array<double,2> down;
    std::optional<SelectionHandle> handle;
    std::optional<SelectionCreationBoxState> box;
    std::optional<Selection> candidate;
    std::vector<std::array<double,2>> points;
    bool invalid = false;
  };
  Mode mode_=Mode::Navigate;
  std::uint64_t owner_=0,metadata_revision_=~std::uint64_t(0);
  std::vector<PlanarAnnotation> rows_;
  std::optional<std::size_t> selected_;
  std::optional<PlanarAnnotation> selected_value_;
  std::optional<Draft> draft_;
  struct PathDraft { DocumentSnapshot source; std::size_t row; PlanarAnnotation value; };
  std::optional<PathDraft> pathDraft_, pathResizeSource_;
  PathSegmentControls pathControls_;
  PathAuthoringControls pathAuthoring_;
  PathResizeControls pathResize_;
  bool pathTransform_ = false, pathAspect_ = false, pathCenter_ = false;
  bool controller_enabled_ = false, path_pointer_admitted_ = false;
  struct Visual {Selection geometry;RoiVisual visual;int width,height;std::array<float,2> scale;};
  std::map<std::size_t,Visual> visuals_;
  std::array<std::array<float,2>,3> scales_{{{1,1},{1,1},{1,1}}};
  std::size_t visual_builds_=0,cap_=256*1048576,admitted_cap_=0;
  Json controls_=Json::object(),input_=Json::object(),layers_=Json::array(),handles_=Json::array();
  std::string name_,stroke_="#FF40D2F0",fill_="none",last_cancel_,error_;
  double width_=1,move_x_=0,move_y_=0,rotation_=0;
  std::array<std::string,5> box_numbers_;
  std::array<std::string,2> vertex_numbers_;
  int vertex_index_=1;
  std::vector<std::array<double,2>> vertex_points_;

  int scope_plane_=0,scope_coordinate_=0,scope_t_=1,scope_c_=1;
  bool all_t_=false,all_c_=false,finish_requested_=false,vertices_requested_=false,resource_refused_=false;
  bool multi() const {return mode_==Mode::Polygon || mode_==Mode::Multipoint;}
  void update_vertices();
  void sync(Document &);
  void select(std::size_t);
  void commit(Document &,const DocumentSnapshot &,std::optional<std::size_t>,const PlanarAnnotation &,const PlanarAnnotationPublisher &,std::optional<std::size_t> control={});
  void observe(const std::string &,bool enabled=true,float input_width=0,ImGuiID item_id=0);
};
bool PlanarCanvas::operator==(const PlanarCanvas &v) const {
  return plane==v.plane && coordinate==v.coordinate && width==v.width && height==v.height &&
         id==v.id && origin==v.origin && scale==v.scale && extent==v.extent &&
      viewport_low==v.viewport_low && viewport_high==v.viewport_high && gui_scale==v.gui_scale;
}
void PlanarAnnotationPanel::State::observe(const std::string &name,bool enabled,float input_width,ImGuiID item_id) {
  const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
  const auto id=item_id?item_id:ImGui::GetItemID();
  const auto x=input_width>0?a.x+input_width/2:(a.x+b.x)/2;
  const auto *window=ImGui::GetCurrentWindowRead();
  const auto clip=window->ClipRect;
  controls_[name]={{"id",id},{"enabled",enabled},
      {"rect",{a.x,a.y,b.x,b.y}},{"point",{x,(a.y+b.y)/2}},
      {"visible",ImGui::IsItemVisible()},{"window_name",window->Name},
      {"clip_rect",{clip.Min.x,clip.Min.y,clip.Max.x,clip.Max.y}}};
}
void PlanarAnnotationPanel::State::cancel(const char *reason) {
  if(active() || pathResizeSource_)last_cancel_=reason;
  draft_.reset();
  pathDraft_.reset(); pathResizeSource_.reset(); pathControls_.reset(); pathResize_.reset(); pathAuthoring_.cancel(reason);
  finish_requested_=false;
  vertices_requested_=false;
  visuals_.erase(std::numeric_limits<std::size_t>::max());
}
void PlanarAnnotationPanel::State::sync(Document &d) {
  const auto source=d.nativeSnapshot();
  pathAuthoring_.sync(source, true);
  if(pathResizeSource_) {
    const auto &captured=*pathResizeSource_;
    if(!source.same_document(captured.source) ||
        !source.revisions().matches(captured.source.revisions(),Change::All) ||
        !selected_ || *selected_!=captured.row || !selected_value_ || !(*selected_value_==captured.value) ||
        source.display().t!=captured.source.display().t || source.display().c!=captured.source.display().c ||
        source.display().crosshair!=captured.source.display().crosshair)
      cancel("resize_source_changed");
  }
  if (pathDraft_ && (!source.same_document(pathDraft_->source) ||
      !source.revisions().matches(pathDraft_->source.revisions(), Change::All) ||
      source.display().t != pathDraft_->source.display().t || source.display().c != pathDraft_->source.display().c ||
      source.display().crosshair != pathDraft_->source.display().crosshair)) cancel("path_source_changed");
  if(owner_!=source.id()) {cancel("owner_changed");visuals_.clear();selected_.reset();selected_value_.reset();owner_=source.id();metadata_revision_=~std::uint64_t(0);}
  if(metadata_revision_!=source.revisions().metadata) {
    rows_=planar_annotations(source.data());metadata_revision_=source.revisions().metadata;admitted_cap_=0;
    if(selected_ && (*selected_>=rows_.size() || !selected_value_ || !(rows_[*selected_]==*selected_value_))) {
      selected_.reset();selected_value_.reset();
    }
  }
  if(admitted_cap_!=cap_) {
    try {admit_planar_annotation_visuals(source.data().dataset.shape(),rows_,cap_);admitted_cap_=cap_;
      if(resource_refused_) {error_.clear();resource_refused_=false;}
    }
    catch (...) {cancel("resource_refusal");throw;}
  }
  for(auto it=visuals_.begin();it!=visuals_.end();) {
    if(it->first!=std::numeric_limits<std::size_t>::max() &&
       (it->first>=rows_.size() || it->second.geometry!=rows_[it->first].selection))it=visuals_.erase(it);
    else ++it;
  }
}
void PlanarAnnotationPanel::State::select(std::size_t row) {
  pathDraft_.reset(); pathResizeSource_.reset(); pathControls_.reset(); pathResize_.reset();
  selected_=row;selected_value_=rows_.at(row);name_=selected_value_->name;
  const auto style=resolve_roi_style(selected_value_->selection,{64,210,240,255});
  stroke_=roi_color_string(style.stroke_color);fill_=roi_color_string(style.fill_color);width_=style.stroke_width;
  move_x_=move_y_=rotation_=0;
  scope_plane_=int(selected_value_->plane);scope_coordinate_=selected_value_->coordinate;
  all_t_=!selected_value_->t;all_c_=!selected_value_->c;
  scope_t_=selected_value_->t.value_or(0)+1;scope_c_=selected_value_->c.value_or(0)+1;
  vertex_index_=1;
  const auto &selection=selected_value_->selection;
  vertex_points_=planarControlPoints(selection);
  if(!vertex_points_.empty())
    for(int i=0;i<2;++i)vertex_numbers_[i]=geometry_number_text(vertex_points_[0][i]);
  if(const auto frame=selection.box_frame()) {
    const std::array<double,5> values{frame->center[0],frame->center[1],
        2*std::hypot(frame->u[0],frame->u[1]),2*std::hypot(frame->v[0],frame->v[1]),
        std::atan2(frame->u[1],frame->u[0])*180/std::acos(-1.)};
    for(int i=0;i<5;++i)box_numbers_[i]=geometry_number_text(values[i]);
  }

}
void PlanarAnnotationPanel::State::update_vertices() {
  auto &state=*draft_;
  if(state.points.size()>65536)throw std::invalid_argument("Planar annotation vertex cap exceeded");
  if(state.points.size()>=(mode_==Mode::Polygon?3u:1u))
    state.candidate=Selection(mode_==Mode::Polygon?"polygon":"multipoint",state.points);
  else state.candidate.reset();
}
void PlanarAnnotationPanel::State::commit(Document &d,const DocumentSnapshot &source,
                                  std::optional<std::size_t> row,const PlanarAnnotation &value,const PlanarAnnotationPublisher &publish,std::optional<std::size_t> control) {
  const int keep_vertex=control?int(*control)+1:(row && selected_==row?vertex_index_:1);
  const auto index=row.value_or(planar_annotations(source.data()).size());
  auto proposed=rows_;if(row)proposed.at(*row)=value;else proposed.push_back(value);
  const auto shape=source.data().dataset.shape();
  auto retained=planar_annotation_visual_estimate(shape,proposed,cap_);
  for(std::size_t n=0;n<proposed.size();++n)if(proposed[n].selection.is_path()) {
    const auto geometry=planar_annotation_geometry(shape,proposed[n].plane);
    const auto scale=scales_[std::size_t(proposed[n].plane)];
    const auto cached=visuals_.find(n);
    const auto bytes=cached!=visuals_.end() && cached->second.geometry==proposed[n].selection &&
        cached->second.scale==scale && cached->second.width==geometry.width && cached->second.height==geometry.height
        ? pathVisualBytes(cached->second.visual)
        : pathVisualBytes(pathVisual(proposed[n].selection,geometry.width,geometry.height,scale,cap_-retained));
    if(bytes>cap_-retained)throw std::invalid_argument("Planar path visual exceeds the processing memory cap");
    retained+=bytes;
  }
  const auto command=resolve_planar_annotation_command(source,{row?PlanarAnnotationOperation::Update:PlanarAnnotationOperation::Store,row,value});
  if(publish)publish(command);else {auto edit=prepare_planar_annotation(command);d.commitPlanarAnnotation(edit);}
  selected_=index;selected_value_=value;metadata_revision_=~std::uint64_t(0);sync(d);select(index);
  if(keep_vertex>=1 && std::size_t(keep_vertex)<=vertex_points_.size()) {
    vertex_index_=keep_vertex;
    for(int axis=0;axis<2;++axis)vertex_numbers_[axis]=geometry_number_text(vertex_points_[std::size_t(keep_vertex-1)][axis]);
  }
}
void PlanarAnnotationPanel::State::controls(Document &d,bool enabled,std::size_t cap,const PlanarAnnotationPublisher &publish) {
  controller_enabled_=enabled;
  cap_=cap;
  finish_requested_=false;
  vertices_requested_=false;
  controls_=Json::object();layers_=Json::array();handles_=Json::array();
  try {sync(d);}catch(const std::exception &error) {
    resource_refused_=true;error_=error.what();visuals_.clear();
    ImGui::TextWrapped("Annotation: %s",error_.c_str());return;
  }
  ImGui::PushID("planar_annotations");ImGui::BeginDisabled(!enabled);
  auto action=[&](auto call) {
    try {call();error_.clear();}catch(const std::exception &error) {cancel("action_refusal");error_=error.what();}
  };
  const std::array<const char *,10> names{"Navigate","Rectangle","Ellipse","Freehand","Line","Point","Edit","Polygon","Multipoint","Path"};
  const std::array<const char *,10> ids{"navigate","rectangle","ellipse","freehand","line","point","edit","polygon","multipoint","path"};
  ImGui::TextUnformatted("Plane annotations:");
  const auto &style=ImGui::GetStyle();
  const float right=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
  for(int i=0;i<10;++i) {
    const float width=ImGui::GetFrameHeight()+style.ItemInnerSpacing.x+
        ImGui::CalcTextSize(names[i]).x;
    if(ImGui::GetItemRectMax().x+style.ItemSpacing.x+width<=right)ImGui::SameLine();
    if(ImGui::RadioButton(names[i],mode_==Mode(i))) {cancel("tool_changed");mode_=Mode(i);}
    observe(ids[i],enabled);
  }
  if(mode_==Mode::Path) {
    pathAuthoring_.sync(d.nativeSnapshot(),enabled,cap_);
    pathAuthoring_.controls(enabled,[&](const DocumentSnapshot &source,const Selection &selection,const PathAuthoringCanvas &canvas) {
      PlanarAnnotation value{"Path "+std::to_string(rows_.size()+1),selection,Plane(canvas.plane),canvas.coordinate,
          source.display().t,source.display().c,{}};
      commit(d,source,{},value,publish);mode_=Mode::Edit;
    });
  }
  if(selected_ && rows_[*selected_].selection.is_path() && mode_==Mode::Edit) {
    ImGui::BeginDisabled(pathDraft_.has_value() || draft_.has_value() || pathResize_.active());
    if(ImGui::RadioButton("Edit path points",!pathTransform_)){pathTransform_=false;pathResize_.reset();pathResizeSource_.reset();}observe("path_points",enabled);
    ImGui::SameLine();if(ImGui::RadioButton("Transform path",pathTransform_))pathTransform_=true;observe("path_transform",enabled);
    ImGui::Checkbox("Preserve path aspect",&pathAspect_);observe("path_aspect",enabled);
    ImGui::SameLine();ImGui::Checkbox("Resize path from center",&pathCenter_);observe("path_center",enabled);
    ImGui::EndDisabled();
    ImGui::Text("Resize anchor: %s",pathCenter_ || ImGui::GetIO().KeyCtrl?"center":"opposite bound");
    ImGui::TextUnformatted(draft_ && draft_->invalid?"Invalid resize: release cancels":"Resize valid; Shift preserves aspect");
  }
  if(multi()) {
    const bool ready=draft_ && draft_->candidate.has_value();
    ImGui::BeginDisabled(!ready);
    if(ImGui::Button("Finish annotation"))finish_requested_=true;
    observe("finish",enabled && ready);ImGui::EndDisabled();ImGui::SameLine();
    ImGui::BeginDisabled(!draft_);
    if(ImGui::Button("Remove last vertex")) {
      draft_->points.pop_back();if(draft_->points.empty())cancel("last_vertex_removed");else update_vertices();
      vertices_requested_=draft_.has_value();
    }
    observe("remove_last",enabled && draft_.has_value());ImGui::SameLine();
    if(ImGui::Button("Cancel annotation"))cancel("cancel_button");
    observe("cancel_draft",enabled && draft_.has_value());ImGui::EndDisabled();
    ImGui::TextUnformatted("Click grid vertices; Finish or double-click completes. Polygon also closes at its first vertex.");
  }
  if(mode_!=Mode::Navigate || !rows_.empty()) {
    ImGui::SetNextItemWidth(220);
    const auto label=selected_?rows_[*selected_].name:std::string("Select an annotation");
    if(ImGui::BeginCombo("Stored",label.c_str())) {
      for(std::size_t n=0;n<rows_.size();++n) {
        ImGui::PushID(int(n));
        const auto row_label=rows_[n].name+" ("+planeName(rows_[n].plane)+"="+std::to_string(rows_[n].coordinate)+")";
        if(ImGui::Selectable(row_label.c_str(),selected_==n)) {cancel("row_changed");select(n);mode_=Mode::Edit;}
        observe("row."+std::to_string(n),enabled);ImGui::PopID();
      }
      ImGui::EndCombo();
    }
    observe("rows",enabled);
    if(selected_) {
      ImGui::SameLine();
      if(ImGui::Button("Go to plane")) {
        cancel("navigation");auto view=d.view();const auto &row=rows_[*selected_];
        const auto geometry=planar_annotation_geometry(d.nativeSnapshot().data().dataset.shape(),row.plane);
        view.crosshair[geometry.fixed_axis]=row.coordinate;
        if(row.t)view.t=*row.t;if(row.c)view.c=*row.c;
        d.setNativeDisplay(view);
      }
      observe("go_to",enabled);ImGui::SameLine();
      if(ImGui::Button("Delete annotation")) {
        cancel("deleted");const auto command=resolve_planar_annotation_command(d.nativeSnapshot(),{PlanarAnnotationOperation::Delete,selected_,{}});
        if(publish)publish(command);else {auto edit=prepare_planar_annotation(command);d.commitPlanarAnnotation(edit);}
        selected_.reset();selected_value_.reset();sync(d);
      }
      observe("delete",enabled);
    }
    if(selected_) {
      const auto row=rows_[*selected_];
      ImGui::Text("%s = %d | T %s | C %s",planeName(row.plane),row.coordinate,
          row.t?std::to_string(*row.t+1).c_str():"all",row.c?std::to_string(*row.c+1).c_str():"all");
      ImGui::BeginDisabled(pathDraft_.has_value() || pathResize_.active());
      const bool scope_open=ImGui::CollapsingHeader("Annotation scope");observe("scope",enabled);
      if(scope_open) {
      ImGui::SetNextItemWidth(100);
      ImGui::Combo("Annotation plane",&scope_plane_,"XY\0XZ\0YZ\0");observe("scope_plane",enabled);
      ImGui::SameLine();ImGui::SetNextItemWidth(80);const auto fixed_id=ImGui::GetID("Fixed coordinate (0-based)");
      ImGui::InputInt("Fixed coordinate (0-based)",&scope_coordinate_,0,0);observe("scope_coordinate",enabled,80,fixed_id);
      ImGui::Checkbox("All time points",&all_t_);observe("all_t",enabled);ImGui::SameLine();
      ImGui::SetNextItemWidth(80);const auto time_id=ImGui::GetID("Time (1-based)");
      ImGui::BeginDisabled(all_t_);ImGui::InputInt("Time (1-based)",&scope_t_,0,0);observe("scope_t",enabled && !all_t_,80,time_id);ImGui::EndDisabled();
      ImGui::SameLine();ImGui::Checkbox("All scientific channels",&all_c_);observe("all_c",enabled);
      ImGui::SameLine();ImGui::SetNextItemWidth(80);const auto channel_id=ImGui::GetID("Channel (1-based)");
      ImGui::BeginDisabled(all_c_);ImGui::InputInt("Channel (1-based)",&scope_c_,0,0);observe("scope_c",enabled && !all_c_,80,channel_id);ImGui::EndDisabled();
      if(ImGui::Button("Apply annotation scope")) action([&] {
        auto value=row;value.plane=Plane(scope_plane_);value.coordinate=scope_coordinate_;
        if((!all_t_ && scope_t_<1) || (!all_c_ && scope_c_<1))throw std::invalid_argument("Time and channel must be positive 1-based indices");
        value.t=all_t_?std::optional<int>{}:scope_t_-1;value.c=all_c_?std::optional<int>{}:scope_c_-1;
        commit(d,d.nativeSnapshot(),selected_,value,publish);
      });
      observe("scope_apply",enabled);
      }
      ImGui::EndDisabled();
      const bool geometry_open=ImGui::CollapsingHeader("Annotation geometry");observe("geometry_section",enabled);
      if(geometry_open) {
        ImGui::TextUnformatted("Plane-local image-cell boundary coordinates; Apply commits once.");
        auto number=[&](const char *label,std::string &value,const char *id) {
          ImGui::SetNextItemWidth(90);ImGui::InputText(label,&value);observe(id,enabled,90);
        };
        const auto shape=d.nativeSnapshot().data().dataset.shape();
        const auto geometry=planar_annotation_geometry(shape,row.plane);
        if(row.selection.box_frame()) {
          number("Center U",box_numbers_[0],"center_u");ImGui::SameLine();
          number("Center V",box_numbers_[1],"center_v");
          number("Box width",box_numbers_[2],"box_width");ImGui::SameLine();
          number("Box height",box_numbers_[3],"box_height");ImGui::SameLine();
          number("Box angle (degrees)",box_numbers_[4],"box_angle");
          if(ImGui::Button("Apply box geometry")) action([&] {
            std::array<double,5> values;
            for(int i=0;i<5;++i)values[i]=parse_geometry_number(box_numbers_[i]);
            auto value=row;
            value.selection=SelectionEditor(row.selection,geometry.width,geometry.height,limits())
                .box_geometry({values[0],values[1]},{values[2],values[3]},values[4]);
            commit(d,d.nativeSnapshot(),selected_,value,publish);
          });
          observe("box_apply",enabled);
        } else if(row.selection.is_path() || (row.selection.kind!="mask" && !row.selection.points.empty())) {
          ImGui::BeginDisabled(pathDraft_.has_value() || pathResize_.active());
          const auto &controls=vertex_points_;
          if(row.selection.is_path())ImGui::TextUnformatted("Vertices index path endpoints and control points; curve commands are preserved.");
          ImGui::SetNextItemWidth(90);const auto vertex_id=ImGui::GetID("Vertex (1-based)");
          if(ImGui::InputInt("Vertex (1-based)",&vertex_index_,0,0) && vertex_index_>=1 &&
             std::size_t(vertex_index_)<=controls.size())
            for(int i=0;i<2;++i)vertex_numbers_[i]=geometry_number_text(controls[vertex_index_-1][i]);
          observe("vertex_index",enabled,90,vertex_id);
          number("Vertex U",vertex_numbers_[0],"vertex_u");ImGui::SameLine();
          number("Vertex V",vertex_numbers_[1],"vertex_v");
          auto vertex_action=[&](int operation) {
            if(vertex_index_<1)throw std::invalid_argument("Vertex index must be positive and 1-based");
            const auto index=std::size_t(vertex_index_-1);
            auto value=row;SelectionEditor editor(row.selection,geometry.width,geometry.height,limits());
            if(operation==2)value.selection=editor.removed_vertex(index);
            else {
              const std::array<double,2> point{parse_geometry_number(vertex_numbers_[0]),parse_geometry_number(vertex_numbers_[1])};
              value.selection=operation==0?editor.moved_vertex(index,point):editor.inserted_vertex(index,point);
            }
            commit(d,d.nativeSnapshot(),selected_,value,publish);
          };
          if(ImGui::Button("Apply vertex"))action([&]{vertex_action(0);});observe("vertex_apply",enabled);
          ImGui::SameLine();const bool insertable=row.selection.kind!="angle" && !row.selection.is_path();
          ImGui::BeginDisabled(!insertable);
          if(ImGui::Button("Insert before vertex"))action([&]{vertex_action(1);});observe("vertex_insert",enabled && insertable);
          ImGui::EndDisabled();ImGui::SameLine();
          ImGui::BeginDisabled(row.selection.is_path());
          if(ImGui::Button("Remove vertex"))action([&]{vertex_action(2);});observe("vertex_remove",enabled && !row.selection.is_path());
          ImGui::EndDisabled();
          ImGui::EndDisabled();
          if(row.selection.is_path()) {
            // Staging changes only the private preview; the typed row command
            // owns publication, history, workflow recording and stale guards.
            const Selection visible = pathDraft_ ? pathDraft_->value.selection : row.selection;
            if(pathTransform_) {
              // Capture before the first text edit, including a refused first
              // Stage that has not produced a publishable PathDraft yet.
              if(!pathResizeSource_)pathResizeSource_=PathDraft{d.nativeSnapshot(),*selected_,row};
              pathResize_.draw(visible,geometry.width,geometry.height,int(row.plane),
                SelectionEditLimits{limits().max_xy_pixels,limits().max_vertices,cap_},enabled,
                [&](const Selection &replacement) {
                  auto candidate=pathDraft_?*pathDraft_:PathDraft{d.nativeSnapshot(),*selected_,row};
                  candidate.value.selection=replacement;
                  (void)resolve_planar_annotation_command(candidate.source,
                      {PlanarAnnotationOperation::Update,candidate.row,candidate.value});
                  auto proposed=rows_;proposed.at(candidate.row)=candidate.value;
                  admit_planar_annotation_visuals(candidate.source.data().dataset.shape(),proposed,cap_);
                  pathDraft_=std::move(candidate);
                  error_.clear();resource_refused_=false;
                },[&](const std::string &id,bool available,float width){observe(id,available,width);});
            } else pathControls_.draw(visible, enabled, [&](const Json &request) {
              if (draft_) cancel("path_segment_staging");
              PathEditLimits pathLimits; pathLimits.max_working_bytes = cap_;
              auto candidate = pathDraft_ ? *pathDraft_ : PathDraft{d.nativeSnapshot(), *selected_, row};
              candidate.value.selection = editPathSegments(candidate.value.selection, request, pathLimits);
              // Admit complete root/annotation command retention before keeping
              // the new private preview; this does not publish a transaction.
              (void)resolve_planar_annotation_command(candidate.source,
                  {PlanarAnnotationOperation::Update, candidate.row, candidate.value});
              auto proposed = rows_; proposed.at(candidate.row) = candidate.value;
              admit_planar_annotation_visuals(candidate.source.data().dataset.shape(), proposed, cap_);
              pathDraft_ = std::move(candidate);
              error_.clear();resource_refused_=false;
            }, [&](const std::string &id, bool available, float width) { observe(id, available, width); });
            ImGui::BeginDisabled(!pathDraft_ || pathControls_.invalid() || pathResize_.invalid());
            if (ImGui::Button("Apply path changes")) action([&] {
              const auto candidate = *pathDraft_;
              commit(d, candidate.source, candidate.row, candidate.value, publish);
              pathDraft_.reset();
            });
            observe("path_apply", enabled && pathDraft_.has_value() && !pathControls_.invalid() && !pathResize_.invalid());
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!pathDraft_ && !pathResize_.active());
            ImGui::SameLine();
            if (ImGui::Button("Cancel path changes")) cancel("path_canceled");
            observe("path_cancel", enabled && (pathDraft_.has_value() || pathResize_.active()));
            ImGui::EndDisabled();
          }
          else ImGui::TextUnformatted("Insertion at vertex count + 1 appends. Shape minimums are enforced.");
        } else ImGui::TextUnformatted("Masks retain their packed geometry; use move / rotation in properties.");
      }
      ImGui::BeginDisabled(pathDraft_.has_value() || pathResize_.active());
      const bool properties_open=ImGui::CollapsingHeader("Annotation properties");observe("properties_section",enabled);
      if(properties_open) {
      ImGui::SetNextItemWidth(160);ImGui::InputText("Name",&name_);observe("name",enabled,160);
      ImGui::SameLine();ImGui::SetNextItemWidth(110);ImGui::InputDouble("Stroke width",&width_,0,0,"%.3g");observe("width",enabled,110);
      ImGui::SetNextItemWidth(130);ImGui::InputText("Stroke (#AARRGGBB/none)",&stroke_);observe("stroke",enabled,130);
      ImGui::SameLine();ImGui::SetNextItemWidth(130);ImGui::InputText("Fill (#AARRGGBB/none)",&fill_);observe("fill",enabled,130);
      if(ImGui::Button("Apply name / style")) action([&] {
        auto value=row;value.name=name_;value.selection=with_roi_style(value.selection,
            {{"stroke_color",stroke_},{"fill_color",fill_},{"stroke_width",width_}});
        commit(d,d.nativeSnapshot(),selected_,value,publish);
      });
      observe("properties",enabled);
      ImGui::SetNextItemWidth(90);ImGui::InputDouble("Move U",&move_x_,0,0,"%.3g");observe("move_u",enabled,90);
      ImGui::SameLine();ImGui::SetNextItemWidth(90);ImGui::InputDouble("Move V",&move_y_,0,0,"%.3g");observe("move_v",enabled,90);
      ImGui::SameLine();ImGui::SetNextItemWidth(90);ImGui::InputDouble("Rotate degrees",&rotation_,0,0,"%.3g");observe("rotation",enabled,90);
      if(ImGui::Button("Apply move / rotation")) action([&] {
        auto value=row;const auto shape=d.nativeSnapshot().data().dataset.shape();
        const auto geometry=planar_annotation_geometry(shape,row.plane);
        SelectionEditor editor(value.selection,geometry.width,geometry.height,limits());
        value.selection=editor.moved({move_x_,move_y_},true);
        if(rotation_!=0)value.selection=SelectionEditor(value.selection,geometry.width,geometry.height,limits()).rotated(rotation_);
        commit(d,d.nativeSnapshot(),selected_,value,publish);
      });
      observe("geometry",enabled);
      }
      ImGui::EndDisabled();
    }
  }
  if(!error_.empty())ImGui::TextWrapped("Annotation: %s",error_.c_str());
  ImGui::EndDisabled();ImGui::PopID();
}
bool PlanarAnnotationPanel::State::pointer(Document &d,const PlanarCanvas &canvas,bool enabled,bool hovered,const PlanarAnnotationPublisher &publish) {
  sync(d);if(pathDraft_ || pathResize_.active())return false;auto &io=ImGui::GetIO();auto *context=ImGui::GetCurrentContext();
  if(mode_==Mode::Path) {
    auto style=with_roi_style(Selection("rectangle"),{{"stroke_color",stroke_},{"fill_color",fill_},{"stroke_width",width_}});
    PathAuthoringCanvas pathCanvas{canvas.id,int(canvas.plane),canvas.coordinate,canvas.width,canvas.height,
        canvas.origin,canvas.scale,canvas.viewport_low,canvas.viewport_high};
    // Toolbar focus is an input-routing state, not document unavailability.
    // Keep the private collection alive while its own buttons are pressed;
    // still require the ordinary canvas guard for new pointer/key actions.
    path_pointer_admitted_=enabled;
    return pathAuthoring_.pointer(d.nativeSnapshot(),pathCanvas,controller_enabled_,hovered,style,cap_,enabled);
  }
  const std::array<double,2> point{(io.MousePos.x-canvas.origin[0])/canvas.scale[0],(io.MousePos.y-canvas.origin[1])/canvas.scale[1]};
  const bool inside=point[0]>=0 && point[1]>=0 && point[0]<=canvas.width && point[1]<=canvas.height;
  bool completion_active=false;
  if(multi() && draft_ && !draft_->row && context->ActiveId)
    for(const auto *name:{"finish","remove_last","cancel_draft"}) {
      const auto found=controls_.find(name);
      completion_active|=found!=controls_.end() && found->at("enabled").get<bool>() &&
          found->at("id").get<ImGuiID>()==context->ActiveId;
    }
  const bool allowed=(enabled || finish_requested_ || vertices_requested_ || completion_active) && !io.WantTextInput && !io.AppFocusLost && (!io.KeyCtrl || (pathTransform_ && mode_==Mode::Edit &&
        ((draft_ && draft_->original.selection.is_path()) || (selected_ && rows_[*selected_].selection.is_path())))) && !io.KeySuper &&
      !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel) &&
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      (context->ActiveId==0 || context->ActiveId==canvas.id || completion_active);
  const bool consumed=mode_!=Mode::Navigate && (hovered || draft_.has_value());
  if(draft_ && draft_->canvas.plane==canvas.plane) {
    const auto current=d.nativeSnapshot();
    if(!allowed || !(draft_->canvas==canvas) || !current.same_document(draft_->source) ||
       !current.revisions().matches(draft_->source.revisions(),Change::All) ||
       io.MouseWheel!=0 || ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {cancel("capture_changed");return consumed;}
    input_={{"phase","drag"},{"plane",planeName(canvas.plane)},{"point",point},{"canvas_id",canvas.id}};
    if(multi() && !draft_->row) {
      bool finish=finish_requested_;
      if(hovered && inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const auto p=snap(point,canvas);const std::array<double,2> vertex{double(p[0]),double(p[1])};
        const auto first=draft_->points.front();
        finish|=draft_->candidate && (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) ||
            (mode_==Mode::Polygon && std::hypot((point[0]-first[0])*canvas.scale[0],(point[1]-first[1])*canvas.scale[1])<=6));
        if(!finish && draft_->points.back()!=vertex) {
          if(draft_->points.size()>=65536)throw std::invalid_argument("Planar annotation vertex cap exceeded");
          draft_->points.push_back(vertex);update_vertices();
        }
      }
      if(finish && draft_->candidate) {
        auto completed=std::move(*draft_);draft_.reset();finish_requested_=false;
        visuals_.erase(std::numeric_limits<std::size_t>::max());
        auto value=completed.original;value.selection=*completed.candidate;
        commit(d,completed.source,{},value,publish);input_["phase"]="finished";
      }
      return consumed;
    }
    if(ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      auto completed=std::move(*draft_);draft_.reset();
      visuals_.erase(std::numeric_limits<std::size_t>::max());
      if(completed.candidate && !completed.invalid) {
        auto value=completed.original;value.selection=*completed.candidate;
        const auto control=completed.handle && completed.handle->kind==SelectionHandleKind::Vertex
            ?std::optional<std::size_t>(completed.handle->index):std::nullopt;
        commit(d,completed.source,completed.row,value,publish,control);
      }
      input_["phase"]="released";
    } else if(ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      auto &state=*draft_;
      if(state.box) {
        state.box=update_selection_creation_box(*state.box,snap(point,canvas),io.KeyShift,io.KeyAlt);
        state.candidate=selection_creation_box_candidate(*state.box);
      } else if(state.handle) {
        auto editLimits=limits();if(state.original.selection.is_path())editLimits.max_working_bytes=cap_;
        SelectionEditor editor(state.original.selection,canvas.width,canvas.height,editLimits);
        switch(state.handle->kind) {
        case SelectionHandleKind::Move:state.candidate=editor.moved({point[0]-state.down[0],point[1]-state.down[1]},true);break;
        case SelectionHandleKind::Resize:
          try {
            if(state.original.selection.is_path())
              state.candidate=editor.resized(SelectionHandleMode::Bounds,state.handle->index,point,
                  io.KeyShift || pathAspect_,io.KeyCtrl || pathCenter_,editLimits);
            else
              state.candidate=editor.resized(state.handle->index,point,io.KeyShift,io.KeyAlt,editLimits);
            state.invalid=false;
          }
          catch(const CollapsedSelectionResize &) {
            if(!state.original.selection.is_path())state.candidate.reset();state.invalid=true;input_["phase"]="collapsed";
          }
          break;
        case SelectionHandleKind::Vertex:state.candidate=editor.moved_vertex(state.handle->index,point);break;
        case SelectionHandleKind::Rotate:state.candidate=editor.rotated(editor.rotation_degrees(state.down,point));break;
        }
      } else if(mode_==Mode::Line) {
        state.candidate=Selection("line",{state.down,point});
      } else if(mode_==Mode::Freehand) {
        if(state.points.empty() || state.points.back()!=point) {
          if(state.points.size()>=65536)throw std::invalid_argument("Planar freehand vertex cap exceeded");
          state.points.push_back(point);
        }
        if(state.points.size()>=3)state.candidate=Selection("freehand",state.points);
      }
    } else cancel("missing_release");
    return consumed;
  }
  if(draft_ || !allowed || !hovered || (!inside && mode_!=Mode::Edit) || mode_==Mode::Navigate || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))return consumed;
  const auto source=d.nativeSnapshot();
  if(mode_==Mode::Edit) {
    for(std::size_t n=rows_.size();n>0;--n) {
      const auto i=n-1;const auto &row=rows_[i];
      if(!planar_annotation_visible(row,canvas.plane,canvas.coordinate,source.display().t,source.display().c))continue;
      auto editLimits=limits();if(row.selection.is_path())editLimits.max_working_bytes=cap_;
      SelectionEditor editor(row.selection,canvas.width,canvas.height,editLimits);
      auto rotation=editor.rotation_handle(32,{canvas.scale[0],canvas.scale[1]});
      if(auto hit=row.selection.is_path() && pathTransform_
          ? editor.hit_test(point,{6*canvas.gui_scale/canvas.scale[0],6*canvas.gui_scale/canvas.scale[1]},{},SelectionHandleMode::Bounds,editLimits)
          : editor.hit_test(point,{6/canvas.scale[0],6/canvas.scale[1]},rotation)) {
        // Defer manager selection until release: expanding controls here would
        // change the captured canvas transform on the next frame.
        draft_.emplace(Draft{source,canvas,row,i,point,*hit,{},{},{}});break;
      }
    }
  } else {
    PlanarAnnotation row{"Annotation "+std::to_string(rows_.size()+1),Selection("rectangle"),canvas.plane,canvas.coordinate,
        source.display().t,source.display().c,{}};
    draft_.emplace(Draft{source,canvas,row,{},point,{},{},{},{point}});
    if(mode_==Mode::Rectangle || mode_==Mode::Ellipse)
      draft_->box=begin_selection_creation_box(mode_==Mode::Rectangle?SelectionCreationBoxKind::Rectangle:SelectionCreationBoxKind::Ellipse,canvas.width,canvas.height,snap(point,canvas));
    else if(mode_==Mode::Point)draft_->candidate=Selection("point",{point});
    else if(multi()) {
      const auto p=snap(point,canvas);draft_->points={{double(p[0]),double(p[1])}};update_vertices();
    }
  }
  if(draft_)input_={{"phase","began"},{"plane",planeName(canvas.plane)},{"point",point},{"canvas_id",canvas.id}};
  return consumed;
}
void PlanarAnnotationPanel::State::draw(Document &d,const PlanarCanvas &canvas,ImDrawList *draw,bool show,bool editable) {
  sync(d);const auto source=d.nativeSnapshot();
  scales_[std::size_t(canvas.plane)]=canvas.scale;
  std::vector<ImVec2> handle_points;
  auto visual=[&](const Selection &selection,std::optional<std::size_t> row,bool preview) {
    const auto key=row.value_or(std::numeric_limits<std::size_t>::max());
    auto found=visuals_.find(key);
    if(found==visuals_.end() || found->second.geometry!=selection || found->second.width!=canvas.width || found->second.height!=canvas.height || (selection.is_path() && found->second.scale!=canvas.scale)) {
      auto proposed=rows_;
      if(preview && row)proposed.at(*row).selection=selection;
      else if(!row && draft_) {auto candidate=draft_->original;candidate.selection=selection;proposed.push_back(std::move(candidate));}
      admit_planar_annotation_visuals(source.data().dataset.shape(),proposed,cap_);
      std::size_t other=0;
      for(const auto &[index,cached]:visuals_)if(index!=key) {
        const auto bytes=pathVisualBytes(cached.visual);
        if(bytes>cap_-other)throw std::invalid_argument("Planar visual cache exceeds the processing memory cap");
        other+=bytes;
      }
      auto geometry=selection.is_path()
          ? pathVisual(selection,canvas.width,canvas.height,canvas.scale,cap_-other)
          : roiVisual(selectionJson(selection),canvas.width,canvas.height,{64,210,240,255});
      found=visuals_.insert_or_assign(key,Visual{selection,std::move(geometry),canvas.width,canvas.height,canvas.scale}).first;
      ++visual_builds_;
    }
    const auto vertex_begin=draw->VtxBuffer.Size,index_begin=draw->IdxBuffer.Size;
    drawRoiVisual(found->second.visual,draw,canvas.origin,canvas.scale);
    layers_.push_back({{"plane",planeName(canvas.plane)},{"coordinate",canvas.coordinate},
      {"row",row?Json(*row):Json()},{"preview",preview},{"canvas_id",canvas.id},{"geometry",planarSelectionJson(selection)}});
    if(selection.is_path()) {
      layers_.back()["path_visual"]=roiVisualJson(found->second.visual);
      layers_.back()["emitted_vertices"]=draw->VtxBuffer.Size-vertex_begin;
      layers_.back()["emitted_indices"]=draw->IdxBuffer.Size-index_begin;
    }
  };
  struct ClipGuard {ImDrawList *draw;~ClipGuard(){if(draw)draw->PopClipRect();}};
  draw->PushClipRect({canvas.origin[0],canvas.origin[1]},
      {canvas.origin[0]+canvas.width*canvas.scale[0],canvas.origin[1]+canvas.height*canvas.scale[1]},true);
  ClipGuard clip{draw};
  if(show)for(std::size_t n=0;n<rows_.size();++n) {
    const auto &row=rows_[n];
    if(!planar_annotation_visible(row,canvas.plane,canvas.coordinate,source.display().t,source.display().c))continue;
    const bool preview=draft_ && draft_->row==n && draft_->candidate && draft_->canvas.plane==canvas.plane;
    const bool path_preview = pathDraft_ && pathDraft_->row == n;
    const auto &selection=path_preview?pathDraft_->value.selection:preview?*draft_->candidate:row.selection;
    visual(selection,n,preview || path_preview);
    if(editable && (selected_==n || (draft_ && draft_->row==n)) && mode_==Mode::Edit) {
      auto editLimits=limits();if(selection.is_path())editLimits.max_working_bytes=cap_;
      SelectionEditor editor(selection,canvas.width,canvas.height,editLimits);auto handles=selection.is_path() && pathTransform_?editor.handles(SelectionHandleMode::Bounds,editLimits):editor.handles();
      if(!(selection.is_path() && pathTransform_))if(auto rotation=editor.rotation_handle(32,{canvas.scale[0],canvas.scale[1]}))handles.push_back(*rotation);
      for(const auto &handle:handles) {
        const ImVec2 p{canvas.origin[0]+float(handle.position[0])*canvas.scale[0],canvas.origin[1]+float(handle.position[1])*canvas.scale[1]};
        handle_points.push_back(p);
        handles_.push_back({{"plane",planeName(canvas.plane)},{"kind",int(handle.kind)},{"index",handle.index},{"position",handle.position},{"point",{p.x,p.y}},{"gui_scale",canvas.gui_scale},{"radius_device",4*canvas.gui_scale},{"shape","circle"},{"bounds_hit_tolerance_device",6*canvas.gui_scale}});
      }
    }
  }
  if(draft_ && !draft_->row && draft_->canvas.plane==canvas.plane && draft_->candidate)visual(*draft_->candidate,{},true);
  draw->PopClipRect();clip.draw=nullptr;
  if(mode_==Mode::Path)pathAuthoring_.draw({canvas.id,int(canvas.plane),canvas.coordinate,canvas.width,canvas.height,
      canvas.origin,canvas.scale,canvas.viewport_low,canvas.viewport_high},draw,cap_);
  // Handles may sit outside the image while still inside the canvas. Keep the
  // parent canvas clip, so near-edge rotation handles remain visible/hittable.
  const auto low=draw->GetClipRectMin(),high=draw->GetClipRectMax();
  for(const auto &point:handle_points)
    if(point.x>=low.x-4*canvas.gui_scale && point.x<=high.x+4*canvas.gui_scale && point.y>=low.y-4*canvas.gui_scale && point.y<=high.y+4*canvas.gui_scale)
      draw->AddCircleFilled(point,4*canvas.gui_scale,IM_COL32(255,213,75,255));
}
Json PlanarAnnotationPanel::State::diagnostics() const {
  Json gesture;
  if(draft_)gesture={{"handle",draft_->handle?Json(draft_->handle->index):Json()},
    {"kind",draft_->handle?Json(int(draft_->handle->kind)):Json()},{"invalid",draft_->invalid},
    {"preview",draft_->candidate?planarSelectionJson(*draft_->candidate):Json()},{"plane",int(draft_->canvas.plane)}};
  Json resizeSource;
  if(pathResizeSource_){const auto &s=pathResizeSource_->source;const auto r=s.revisions();
    resizeSource={{"owner",s.id()},{"row",pathResizeSource_->row},{"plane",int(pathResizeSource_->value.plane)},
      {"coordinate",pathResizeSource_->value.coordinate},{"t",s.display().t},{"c",s.display().c},{"crosshair",s.display().crosshair},
      {"revisions",{{"pixels",r.pixels},{"metadata",r.metadata},{"selection",r.selection},{"display",r.display}}}};}
  return {{"mode",int(mode_)},{"active",active()},{"selected",selected_?Json(*selected_):Json()},{"visual_builds",visual_builds_},{"cached_visuals",visuals_.size()},
      {"gesture",gesture},{"path_authoring",pathAuthoring_.diagnostics()},{"controller_enabled",controller_enabled_},{"path_pointer_admitted",path_pointer_admitted_},{"path_transform",pathTransform_},{"resize",pathResize_.diagnostics()},{"resize_source",resizeSource},
      {"segments",pathControls_.diagnostics()},{"path_draft",pathDraft_?planarSelectionJson(pathDraft_->value.selection):Json()},{"error",error_},{"draft_vertices",draft_?draft_->points.size():0},{"draft_name",name_},{"draft_fill",fill_},{"draft_stroke",stroke_},{"count",rows_.size()},{"controls",controls_},{"input",input_},{"layers",layers_},{"handles",handles_},{"last_cancel",last_cancel_}};
}

PlanarAnnotationPanel::PlanarAnnotationPanel():state_(std::make_unique<State>()) {}
PlanarAnnotationPanel::~PlanarAnnotationPanel()=default;
PlanarAnnotationPanel::PlanarAnnotationPanel(PlanarAnnotationPanel &&) noexcept=default;
PlanarAnnotationPanel &PlanarAnnotationPanel::operator=(PlanarAnnotationPanel &&) noexcept=default;
void PlanarAnnotationPanel::controls(Document &d,bool enabled,std::size_t cap,const PlanarAnnotationPublisher &publish) {
  if(!ImGui::GetCurrentContext())throw std::logic_error("Planar annotation controls require an ImGui context");
  state_->controls(d,enabled,cap,publish);
}
void PlanarAnnotationPanel::begin_draw(Document &d,std::size_t cap) {
  state_->controller_enabled_=false;state_->path_pointer_admitted_=false;
  if(state_->active() || state_->pathResizeSource_)state_->cancel("read_only");
  state_->layers_=Json::array();state_->handles_=Json::array();state_->cap_=cap;state_->sync(d);
}
bool PlanarAnnotationPanel::pointer(Document &d,const PlanarCanvas &c,bool enabled,bool hovered,const PlanarAnnotationPublisher &publish) {
  try {return state_->pointer(d,c,enabled,hovered,publish);}
  catch(const std::exception &error) {state_->cancel("input_refusal");state_->error_=error.what();return true;}
}
void PlanarAnnotationPanel::draw(Document &d,const PlanarCanvas &c,ImDrawList *draw,bool show,bool editable) {
  try {state_->draw(d,c,draw,show,editable);}catch(const std::exception &error) {
    state_->cancel("render_refusal");state_->error_=error.what();state_->resource_refused_=true;state_->visuals_.clear();
  }
}
void PlanarAnnotationPanel::cancel(const char *reason) {if(state_)state_->cancel(reason);}
bool PlanarAnnotationPanel::active() const {return state_ && state_->active();}
Json PlanarAnnotationPanel::diagnostics() const {return state_?state_->diagnostics():Json::object();}
} // namespace imagejxx::desktop
