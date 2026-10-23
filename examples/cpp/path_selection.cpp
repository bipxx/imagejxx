#include <imagejxx/composite_path.hpp>
#include <imagejxx/selection_edit.hpp>
#include <imagejxx/session.hpp>
#include <imagejxx/roi_style.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
int checks = 0;
void check(bool condition) {
  ++checks;
  if (!condition)
    throw std::runtime_error("Path selection check " + std::to_string(checks));
}
template <class F> void refuses(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  check(caught);
}
} // namespace
int main() {
  try {
    const std::vector<double> commands{0, 0, 0, 2, 2, 4, 4, 0, 4};
    auto source = Selection::from_path(commands);
    source.width = 3;
    source.attributes = {{"opaque", std::string("retained")}};
    check(source.is_path() && source.points.empty() && selection_is_area(source));
    check(source.path_geometry().bounds() == std::array<double, 4>{0, 0, 4, 2});
    check(source.path_geometry().contains({2, 1}) &&
          !source.path_geometry().contains({2, 3}));
    SelectionEditor editor(source, 16, 16);
    check(editor.handles().size() == 3);
    auto moved = editor.moved({-20, 30});
    check(moved.path_geometry().contains({-18, 31}));
    check(SelectionEditor(moved, 16, 16).moved({20, -30}) == source);
    auto turn = editor.rotated(90, std::array<double, 2>{0, 0});
    check(turn.path_commands() == std::vector<double>({0, 0, 0, 2, -4, 2, 0, 4, 4}));
    check(turn.attributes == source.attributes && turn.width == 3);
    check(editor.moved_vertex(1, {2, 6}).path_geometry().bounds()[3] == 3);
    auto invalid = source;
    invalid.kind = "point";
    refuses([&] { invalid.validate(16, 16); });
    invalid = source;
    invalid.points.push_back({1, 1});
    refuses([&] { invalid.validate(16, 16); });
    refuses([&] { editor.insertion_index({2, 2}); });
    SelectionEditLimits limits;
    limits.max_vertices = 2;
    std::size_t callbacks = 0;
    ExecutionControl control;
    control.checkpoint = [&] { ++callbacks; };
    refuses([&] { editor.rotated(90, {}, limits, control); });
    check(callbacks == 0);
    limits = {};
    control.checkpoint = [&] {
      ++callbacks;
      editor = SelectionEditor(Selection("point", {{9, 9}}), 16, 16);
    };
    auto frozen = editor.rotated(90, std::array<double, 2>{0, 0}, limits, control);
    check(frozen == turn && callbacks > 0);
    Image image(16, 16, 1, PixelType::UInt16);
    DocumentData data{Dataset::from_stack({image})};
    data.selection = source;
    data.previous_selection = moved;
    data.rois.push_back({"curve", turn, {}, {}, {}, {}});
    auto owner = std::make_shared<Document>(std::move(data));
    auto invalid_previous=source;invalid_previous.kind="point";
    auto invalid_data=owner->snapshot().data();invalid_data.previous_selection=invalid_previous;
    refuses([&]{invalid_data.validate();});
    auto staging=owner->begin_edit();
    refuses([&]{staging.set_previous_selection(Selection("path"));});
    SessionCapture capture{{owner->snapshot()}};
    auto encoded = encode_session(capture);
    check(encoded[8] == '5');
    auto decoded = decode_session(encoded);
    check(decoded.source_version == 5);
    const auto &loaded = decoded.documents[0]->snapshot().data();
    check(loaded.selection == source && loaded.previous_selection == moved &&
          loaded.rois[0].selection == turn);
    check(encode_session(SessionCapture{{decoded.documents[0]->snapshot()}}) ==
          encoded);
    source.kind = "point";
    refuses([&] { source.validate(16, 16); });
    std::cout << "PASS " << checks << " native path selection checks\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
