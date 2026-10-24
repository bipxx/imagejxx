// Independent installed-SDK consumer: no desktop, ImGui or Python dependency.
#include <imagejxx/native_workflow_program.hpp>
#include <imagejxx/selection_edit.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace imagejxx;
namespace {
int checks = 0;
void require(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template<class F> void refuses(F action) {
  bool refused = false;
  try { action(); } catch (const std::invalid_argument &) { refused = true; }
  require(refused, "Expected invalid intent to refuse");
}
NativeWorkflowOperation replace(const Selection &candidate, DatasetShape shape) {
  NativeWorkflowOperation step;
  step.operation = "selection_creation";
  step.selection_creation = SelectionCreationRequest{
      SelectionCreationOperation::Replace, candidate, SelectionCreationGeometry::NativePathV4};
  step.selection_creation_extent = shape;
  return step;
}
}

int main() {
  try {
    // The Q has an interior x maximum 10; the C has an interior y maximum 9.5.
    // These extrema are deliberately different from the control hull (12,10).
    const std::vector<double> commands{0,0,-0.,1,8,0,2,12,4,8,8,3,6,10,2,10,0,8,4};
    auto path = Selection::from_path(commands);
    path.attributes["opaque"] = std::string("SDK retained");
    path.width = 2.5;
    SelectionEditor editor(path, 32, 32);
    require(editor.bounds() == std::array<double,4>{0,0,10,9.5}, "Wrong analytic bounds");
    const auto handles = editor.handles(SelectionHandleMode::Bounds);
    require(handles.size() == 8, "Missing path bounds handles");
    require(handles[4].position == std::array<double,2>{10,9.5}, "SE handle used control hull");
    require(editor.handles().size() != 8, "Default handle contract changed");
    refuses([&]{ (void)editor.resized(4,{20,19}); });
    refuses([&]{ (void)editor.resized(4,{20,19},false,false,SelectionEditLimits{}); });
    const auto twice = editor.resized(SelectionHandleMode::Bounds, 4, {20,19});
    const std::vector<double> doubled{0,0,0,1,16,0,2,24,8,16,16,3,12,20,4,20,0,16,4};
    require(twice.path_commands() == doubled, "Affine coordinates differ from literal expectation");
    require(twice.attributes == path.attributes && twice.width == 2.5, "Resize lost style/attributes");
    const auto side = editor.resized(SelectionHandleMode::Bounds, 3, {20,4.75});
    require(std::signbit(side.path_commands()[2]), "Untouched axis lost signed zero");
    require(editor.resized(SelectionHandleMode::Bounds, 4,{10,9.5}) == path, "Identity changed original geometry");
    require(editor.resized(SelectionHandleMode::Bounds, 4,{-10,-9.5}).path_commands()[4] == -8, "Crossing did not reflect x");
    require(editor.resized(SelectionHandleMode::Bounds, 4,{-10,-9.5}).path_commands()[10] == -8, "Crossing did not reflect y");
    refuses([&]{ (void)editor.resized(SelectionHandleMode::Bounds, 4,{0,9.5}); });
    const auto line = Selection::from_path({0,1,2,1,5,2});
    SelectionEditor line_editor(line,32,32);
    require(line_editor.handles(SelectionHandleMode::Bounds).size() == 2,
            "Degenerate source should permit only W/E resize");
    refuses([&]{ (void)line_editor.resized(SelectionHandleMode::Bounds, 4,{8,8}); });
    refuses([&]{ (void)line_editor.resized(SelectionHandleMode::Bounds, 3,{8,2},true); });

    Image image(32,32,1,PixelType::UInt16);
    for (std::size_t i=0; i<image.pixels.size(); ++i) image.pixels[i]=double(i);
    DatasetReadObserver reads;
    Document owner{Dataset::from_stack({image}).observe_reads(reads)};
    const Selection original("rectangle",{{1,1},{3,4}});
    const Selection previous("ellipse",{{0,0},{2,2}});
    owner.select(previous);
    owner.select(original);
    const auto before=owner.snapshot();
    const auto request=SelectionCreationRequest{SelectionCreationOperation::Replace,path,
                                              SelectionCreationGeometry::NativePathV4};
    const auto command=resolve_selection_creation_command(before,request);
    require(std::get<std::int64_t>(command.record().at("version"))==4,"Missing explicit V4 capture");
    require(command.result()==path,"Replace changed candidate geometry");
    auto direct_edit = prepare_selection_creation(command);
    owner.commit(direct_edit);
    require(owner.undo_steps()==1,"Authored selection has no undo entry");
    require(owner.snapshot().data().previous_selection==original,"Direct Previous is wrong");
    require(owner.snapshot().data().dataset.shares_pixels(before.data().dataset),"Authoring replaced pixels");
    owner.undo();
    require(owner.snapshot().data().selection==original,"Undo lost original selection");
    require(owner.snapshot().data().previous_selection==previous,"Undo lost original Previous");
    owner.redo();
    require(owner.snapshot().data().selection==path,"Redo lost authored selection");
    const auto roundtrip=selection_creation_command_from_record(before,command.record());
    require(roundtrip.result()==path,"Strict V4 replay changed geometry");
    auto bad=request;bad.operation=SelectionCreationOperation::Add;
    refuses([&]{ (void)resolve_selection_creation_command(before,bad); });

    Document workflow_owner{Dataset::from_stack({image})};
    workflow_owner.select(previous);workflow_owner.select(original);
    const auto source=workflow_owner.snapshot();
    const auto program=make_native_workflow_program({replace(path,source.data().dataset.shape()),replace(twice,source.data().dataset.shape())});
    require(std::get<std::int64_t>(program.record().at("version"))==8,"Missing portable V8 intent");
    const auto restored=native_workflow_program_from_json(program.json());
    const auto prepared=prepare_native_workflow(source,restored.steps());
    require(workflow_owner.snapshot().shares_data(source),"Preparation published live state");
    auto workflow_edit = prepared.begin_edit();
    workflow_owner.commit(workflow_edit);
    require(workflow_owner.undo_steps()==1,"Selection-only workflow did not retain authored history");
    require(workflow_owner.snapshot().data().selection==twice,"Composed workflow result is wrong");
    require(workflow_owner.snapshot().data().previous_selection==original,"Workflow leaked intermediate Previous");
    workflow_owner.undo();
    require(workflow_owner.snapshot().data().previous_selection==previous,"Workflow Undo lost Previous");
    const auto current=workflow_owner.snapshot();
    const auto noop=prepare_native_workflow(current,{replace(path,current.data().dataset.shape()),replace(original,current.data().dataset.shape())});
    auto noop_edit = noop.begin_edit();
    workflow_owner.commit(noop_edit);
    require(workflow_owner.undo_steps()==0,"Net-no-op workflow created history");
    require(workflow_owner.snapshot().data().previous_selection==previous,"Net-no-op changed Previous");
    require(reads.counts().plane_reads==0 && reads.counts().region_reads==0,"Geometry read source samples");
    std::cout << "PASS " << checks << " independent path authoring SDK checks\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
