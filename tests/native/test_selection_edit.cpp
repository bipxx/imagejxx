#include <imagejxx/selection_edit.hpp>
#include <iostream>

using namespace imagejxx;
namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class Function> void invalid(Function work) {
  bool rejected = false;
  try {
    work();
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected, "Invalid selection edit was accepted");
}
} // namespace
int main() {
  try {
    Selection source("rectangle", {{2, 3}, {6, 5}}, 3);
    source.attributes["label"] = std::string("Preserved");
    SelectionEditor editor(source, 20, 12);
    const auto rotated = editor.rotated(90);
    check(rotated.kind == "rotated_rectangle" && rotated.points.size() == 3,
          "Parameterized rotated box");
    const auto frame = *rotated.box_frame();
    check(frame.center == std::array<double, 2>{4, 4} &&
              frame.u == std::array<double, 2>{0, 2} &&
              frame.v == std::array<double, 2>{-1, 0},
          "Quarter-turn axes");
    check(rotated.mask(20, 12).pixels ==
              rectangle_mask(20, 12, 3, 2, 2, 4).pixels,
          "Quarter-turn coverage");
    check(SelectionEditor(rotated, 20, 12)
                  .resized(4, frame.image_point({2, 2}))
                  .box_frame()
                  ->center == frame.image_point({.5, .5}),
          "Resize in local axes");
    const auto numeric = editor.box_geometry({10, 6}, {8, 4}, 90);
    check(numeric.box_frame()->u == std::array<double, 2>{0, 4} &&
              numeric.box_frame()->v == std::array<double, 2>{-2, 0},
          "Numeric rotated geometry");
    check(editor.rotation_degrees({4, 2}, {6, 4}) == 90,
          "Rotation pointer angle");
    const auto rotation = editor.rotation_handle(6, {2, 3});
    check(rotation &&
              editor.hit_test(rotation->position, {.01, .01}, rotation)->kind ==
                  SelectionHandleKind::Rotate,
          "Rotation handle hit");
    auto styled = editor.properties(4, {{"label", std::string("Updated")}});
    check(styled.width == 4 &&
              std::get<std::string>(styled.attributes.at("label")) == "Updated",
          "Native properties");
    check(editor.handles().size() == 8, "Rectangle resize handles");
    check(editor.hit_test({2, 3}, {.1, .1})->kind ==
              SelectionHandleKind::Resize,
          "Handle priority");
    check(editor.hit_test({4, 4}, {.1, .1})->kind == SelectionHandleKind::Move,
          "Interior move hit");
    check(!editor.hit_test({18, 10}, {.1, .1}), "Outside hit");
    auto moved = editor.moved({100, -100}, true);
    check(moved.points == std::vector<std::array<double, 2>>{{16, 0}, {20, 2}},
          "Bounded movement");
    check(moved.width == source.width &&
              metadata_equal(moved.attributes, source.attributes),
          "Style lost");
    check(editor.resized(4, {10, 9}, true).points ==
              std::vector<std::array<double, 2>>{{2, 3}, {14, 9}},
          "Aspect-preserving resize");
    check(editor.resized(3, {8, 4}, true, true).points ==
              std::vector<std::array<double, 2>>{{0, 2}, {8, 6}},
          "Centered resize");
    invalid([&] { (void)editor.resized(8, {3, 4}); });
    for (const auto *kind : {"rectangle", "ellipse"})
      for (bool rotate : {false, true}) {
        Selection original(kind, {{3, 2}, {9, 6}});
        if (rotate) original=SelectionEditor(original,20,12).rotated(90);
        SelectionEditor crossing(original,20,12);
        const auto box=*original.box_frame();
        const std::array<int,8> xs{-1,0,1,1,1,0,-1,-1},ys{-1,-1,-1,0,1,1,1,0};
        for (std::size_t handle=0;handle<8;++handle)
          for (bool aspect : {false,true}) for (bool center : {false,true}) {
            const std::array<double,2> anchor{center?0.:double(-xs[handle]),center?0.:double(-ys[handle])};
            bool collapsed=false;
            try {(void)crossing.resized(handle,box.image_point(anchor),aspect,center);}
            catch(const CollapsedSelectionResize &) {collapsed=true;}
            check(collapsed,"Collapsed resize did not have its distinct invalid-geometry type");
            const auto result=crossing.resized(handle,box.image_point({anchor[0]-.5*xs[handle],anchor[1]-.5*ys[handle]}),aspect,center);
            result.validate(20,12);
            check(crossing.selection()==original,"Crossing resize mutated its source");
          }
      }
    invalid([&] { (void)editor.hit_test({3, 4}, {0, 1}); });
    invalid([&] {
      (void)editor.moved({std::numeric_limits<double>::infinity(), 0});
    });
    Selection polygon("polygon", {{1, 1}, {10, 1}, {10, 9}, {1, 9}});
    SelectionEditor vertices(polygon, 20, 12);
    check(vertices.handles().size() == 4, "Polygon vertices");
    check(vertices.insertion_index({5, 1}) == 1 &&
              vertices.insertion_index({1, 5}) == 4,
          "Nearest and closing-edge insertion order");
    check(vertices.insertion_index({9, 2}, {1, 10}) == 2 &&
              vertices.insertion_index({9, 2}, {10, 1}) == 1,
          "Calibrated insertion metric");
    invalid([&] { (void)vertices.insertion_index({5, 1}, {0, 1}); });
    auto inserted = vertices.inserted_vertex(1, {5, 1});
    check(inserted.points.size() == 5 && inserted.points[1][0] == 5,
          "Vertex insertion");
    check(SelectionEditor(inserted, 20, 12).removed_vertex(1) == polygon,
          "Vertex deletion");
    invalid([&] {
      (void)SelectionEditor(Selection("polygon", {{1, 1}, {2, 1}, {1, 2}}), 20,
                            12)
          .removed_vertex(0);
    });
    check(!SelectionEditor(Selection("multipoint", {{1, 1}, {10, 1}}), 20, 12)
               .hit_test({5, 1}, {.1, .1}),
          "Multipoint gap treated as a segment");
    Image mask(20, 12, 1, PixelType::UInt8);
    mask.at(2, 3) = mask.at(8, 7) = 255;
    auto packed = Selection::from_mask(mask);
    packed.attributes["label"] = std::string("Disconnected");
    SelectionEditor maskEditor(packed, 20, 12);
    check(!maskEditor.hit_test({5, 5}, {.1, .1}), "Mask hole hit");
    for (double dx : {-.5, .5, 1.5, 2.25}) {
      auto actual = maskEditor.moved({dx, -1.5});
      const auto expected =
          affine_transform(mask, {1, 0, -dx, 0, 1, 1.5}, 20, 12,
                           Interpolation::Nearest, Border::Constant, 0);
      check(actual.mask(20, 12).pixels == expected.pixels,
            "Legacy nearest mask movement changed");
      check(metadata_equal(actual.attributes, packed.attributes),
            "Mask attributes lost");
    }
    Document document(Dataset::from_stack({Image(20, 12)}));
    document.select(source);
    const auto snapshot = document.snapshot();
    auto edit = snapshot.begin_edit(Change::Pixels | Change::Selection);
    edit.select(editor.moved({1, 1}));
    document.select(polygon);
    bool stale = false;
    try {
      document.commit(edit);
    } catch (const RevisionConflict &) {
      stale = true;
    }
    check(stale && document.snapshot().data().selection ==
                       std::optional<Selection>(polygon),
          "Stale gesture committed");
    check(!document.snapshot().dirty() && document.undo_steps() == 0,
          "Active selection changed pixel history");
    std::cout << "Selection geometry/edit/stale checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
