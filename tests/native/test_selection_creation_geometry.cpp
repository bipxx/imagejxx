// Literal P05l creation geometry, independent of the production calculation.
// The Q-prefix rows are from qualified ImageJ1.54r Q01-Q17 observations and the
// independent reader. Other rows test the declared checked grid extension.
#include <imagejxx/selection_creation.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace imagejxx;
using Kind = SelectionCreationBoxKind;
using State = SelectionCreationBoxState;
using Box = std::array<std::int64_t, 4>;
std::size_t checks = 0, qualified_rows = 0, qualified_drags = 0;

void check(bool value, const std::string &message) {
  ++checks;
  if (!value)
    throw std::runtime_error(message);
}
bool same(const State &a, const State &b) {
  return a.kind == b.kind && a.image_width == b.image_width &&
         a.image_height == b.image_height && a.anchor == b.anchor &&
         a.bounds == b.bounds;
}
template <class Function> void invalid(Function work, const char *message) {
  bool rejected = false;
  try {
    work();
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected, message);
}
void exact_candidate(const State &state, Box expected,
                     const std::string &label) {
  check(state.bounds == expected, label + ": bounds differ from literal row");
  const auto before = state;
  const auto candidate = selection_creation_box_candidate(state);
  check(same(state, before), label + ": reading candidate mutated state");
  if (expected[2] == 0 || expected[3] == 0) {
    check(!candidate, label + ": zero-span candidate was published");
    return;
  }
  check(candidate.has_value(), label + ": nonempty candidate missing");
  const std::vector<std::array<double, 2>> points{
      {double(expected[0]), double(expected[1])},
      {double(expected[0] + expected[2]), double(expected[1] + expected[3])}};
  check(candidate->kind ==
            (state.kind == Kind::Rectangle ? "rectangle" : "ellipse") &&
            candidate->points == points && candidate->width == 1 &&
            candidate->attributes.empty(),
        label + ": candidate kind/coordinates/default style differ");
}
struct Step {
  std::array<int, 2> point;
  bool constrain, center;
  Box expected;
};
void qualified(const char *id, Kind kind, std::array<int, 2> anchor,
               std::initializer_list<Step> steps) {
  auto state = begin_selection_creation_box(kind, 32, 24, anchor);
  exact_candidate(state, {anchor[0], anchor[1], 0, 0}, id);
  for (const auto &step : steps) {
    const auto before = state;
    const auto next = update_selection_creation_box(
        state, step.point, step.constrain, step.center);
    check(same(state, before), std::string(id) + ": update mutated input state");
    exact_candidate(next, step.expected, id);
    state = next;
    ++qualified_drags;
  }
  ++qualified_rows;
}

void reference_prefixes() {
  qualified("Q01", Kind::Rectangle, {18, 6},
            {{{21, 8}, false, false, {18, 6, 3, 2}},
             {{25, 10}, false, false, {18, 6, 7, 4}}});
  qualified("Q02", Kind::Rectangle, {18, 6},
            {{{21, 8}, true, false, {18, 6, 4, 4}},
             {{25, 10}, true, false, {18, 6, 8, 8}}});
  qualified("Q03", Kind::Rectangle, {8, 7},
            {{{10, 8}, false, false, {8, 7, 2, 1}},
             {{12, 10}, false, false, {8, 7, 4, 3}}});
  qualified("Q04", Kind::Rectangle, {18, 6},
            {{{21, 8}, true, false, {18, 6, 4, 4}},
             {{25, 10}, true, false, {18, 6, 8, 8}}});
  qualified("Q05", Kind::Rectangle, {18, 6},
            {{{21, 8}, true, false, {18, 6, 4, 4}},
             {{25, 10}, false, false, {18, 6, 7, 4}}});
  qualified("Q06", Kind::Rectangle, {18, 6},
            {{{21, 8}, false, false, {18, 6, 3, 2}},
             {{25, 10}, true, false, {18, 6, 8, 8}}});
  qualified("Q07", Kind::Rectangle, {10, 7},
            {{{13, 9}, true, false, {10, 7, 4, 4}},
             {{17, 11}, true, false, {10, 7, 8, 8}}});
  qualified("Q08", Kind::Rectangle, {10, 7},
            {{{13, 9}, false, false, {10, 7, 3, 2}},
             {{17, 11}, false, false, {10, 7, 7, 4}}});
  qualified("Q09", Kind::Ellipse, {18, 6},
            {{{21, 8}, true, false, {17, 5, 4, 4}},
             {{25, 10}, true, false, {17, 4, 8, 8}}});
  qualified("Q10 last delivered drag", Kind::Rectangle, {18, 6},
            {{{21, 8}, false, false, {18, 6, 3, 2}}});
  qualified("Q11 pre-Escape candidate", Kind::Rectangle, {18, 6},
            {{{25, 10}, true, false, {18, 6, 8, 8}}});
  qualified("Q14 interior candidate only, not handle resize", Kind::Rectangle,
            {8, 7}, {{{12, 10}, false, false, {8, 7, 4, 3}}});
  qualified("Q15 area candidate, not old-line combination", Kind::Rectangle,
            {18, 6}, {{{21, 8}, true, false, {18, 6, 4, 4}},
                      {{25, 10}, true, false, {18, 6, 8, 8}}});
  qualified("Q16 area candidate, not point subtraction", Kind::Rectangle,
            {6, 5}, {{{12, 10}, false, false, {6, 5, 6, 5}}});
  qualified("Q17", Kind::Rectangle, {18, 6},
            {{{21, 8}, true, true, {16, 4, 4, 4}},
             {{25, 10}, true, true, {14, 2, 8, 8}}});
  check(qualified_rows == 15 && qualified_drags == 26,
        "Qualified prefix membership omitted a row or delivered drag");

  // Release is a read of the last delivered state, not a call to update using
  // its new pointer. Q10 releases at25,10 after its final drag at21,8.
  const auto last = update_selection_creation_box(
      begin_selection_creation_box(Kind::Rectangle, 32, 24, {18, 6}),
      {21, 8}, false, false);
  exact_candidate(last, {18, 6, 3, 2}, "Q10 release prefix");
  exact_candidate(last, {18, 6, 3, 2}, "Q10 repeated candidate read");
  auto borrowed = *selection_creation_box_candidate(last);
  borrowed.points[0][0] = -100;
  exact_candidate(last, {18, 6, 3, 2}, "Returned candidate owns its geometry");
}

void quadrants_and_clipping() {
  const std::array<std::array<int, 2>, 4> pointers{{
      {13, 12}, {13, 8}, {7, 12}, {7, 8}}};
  const std::array<Box, 4> rectangles{{
      {10, 10, 4, 4}, {10, 6, 4, 4}, {6, 10, 4, 4}, {6, 6, 4, 4}}};
  const std::array<Box, 4> ellipses{{
      {9, 9, 4, 4}, {9, 7, 4, 4}, {7, 9, 4, 4}, {7, 7, 4, 4}}};
  for (std::size_t i = 0; i < pointers.size(); ++i) {
    exact_candidate(update_selection_creation_box(
                        begin_selection_creation_box(Kind::Rectangle, 32, 24,
                                                     {10, 10}),
                        pointers[i], true, false),
                    rectangles[i], "Rectangle rounded-diagonal quadrant");
    exact_candidate(update_selection_creation_box(
                        begin_selection_creation_box(Kind::Ellipse, 32, 24,
                                                     {10, 10}),
                        pointers[i], true, false),
                    ellipses[i], "Ellipse signed-truncating midpoint quadrant");
  }
  struct Row {
    Kind kind;
    std::array<int, 2> anchor, pointer;
    bool constrain, center;
    Box expected;
  };
  const std::array<Row, 9> rows{{
      {Kind::Rectangle, {4, 3}, {10, 10}, false, false, {4, 3, 1, 1}},
      {Kind::Rectangle, {4, 3}, {-3, -2}, false, false, {0, 0, 4, 3}},
      {Kind::Ellipse, {4, 3}, {-3, -2}, false, false, {-3, -2, 7, 5}},
      {Kind::Rectangle, {4, 3}, {0, 0}, true, false, {0, 0, 5, 4}},
      {Kind::Rectangle, {1, 1}, {4, 3}, true, false, {1, 1, 4, 3}},
      {Kind::Rectangle, {0, 0}, {3, 2}, true, false, {0, 0, 4, 4}},
      {Kind::Ellipse, {0, 0}, {3, 2}, true, false, {-1, -1, 4, 4}},
      {Kind::Rectangle, {1, 1}, {4, 3}, false, true, {-2, -1, 6, 4}},
      {Kind::Ellipse, {1, 1}, {4, 3}, false, true, {-2, -1, 6, 4}},
  }};
  for (const auto &row : rows)
    exact_candidate(update_selection_creation_box(
                        begin_selection_creation_box(row.kind, 5, 4, row.anchor),
                        row.pointer, row.constrain, row.center),
                    row.expected, "Checked grid clipping/negative/center row");

  auto prior = update_selection_creation_box(
      begin_selection_creation_box(Kind::Rectangle, 32, 24, {10, 10}),
      {15, 12}, false, true);
  exact_candidate(prior, {5, 8, 10, 4}, "Unconstrained centered prefix");
  auto next = update_selection_creation_box(prior, {16, 14}, true, true);
  exact_candidate(next, {9, 6, 2, 8}, "Centered constraint reads previous bounds");
  exact_candidate(update_selection_creation_box(next, {16, 14}, true, true),
                  {4, 7, 12, 6}, "Repeated delivered drag retains stateful math");
  exact_candidate(update_selection_creation_box(
                      begin_selection_creation_box(Kind::Rectangle, 32, 24,
                                                   {10, 10}),
                      {16, 14}, true, true),
                  {6, 6, 8, 8}, "Fresh centered construction differs from prefix");
}

void zero_invalid_and_extreme() {
  const auto valid = begin_selection_creation_box(Kind::Rectangle, 32, 24, {18, 6});
  for (bool constrain : {false, true})
    for (bool center : {false, true})
      exact_candidate(update_selection_creation_box(valid, {18, 6}, constrain,
                                                    center),
                      {18, 6, 0, 0}, "Zero drag remains absent");
  exact_candidate(update_selection_creation_box(valid, {18, 10}, false, false),
                  {18, 6, 0, 4}, "Zero width remains absent");
  exact_candidate(update_selection_creation_box(valid, {25, 6}, false, false),
                  {18, 6, 7, 0}, "Zero height remains absent");
  const std::array<std::array<int, 2>, 4> invalid_canvases{{
      {0, 24}, {-1, 24}, {32, 0}, {32, -1}}};
  for (const auto canvas : invalid_canvases)
    invalid([&] { begin_selection_creation_box(Kind::Rectangle, canvas[0], canvas[1], {0, 0}); },
            "Invalid canvas admitted");
  const std::array<std::array<int, 2>, 4> invalid_anchors{{
      {-1, 0}, {0, -1}, {33, 0}, {0, 25}}};
  for (const auto anchor : invalid_anchors)
    invalid([&] { begin_selection_creation_box(Kind::Rectangle, 32, 24, anchor); },
            "Invalid anchor admitted");
  invalid([&] { begin_selection_creation_box(static_cast<Kind>(-1), 32, 24, {0, 0}); },
          "Unknown begin kind admitted");
  const auto reject_state = [&](State bad) {
    const auto before = bad;
    invalid([&] { update_selection_creation_box(bad, {21, 8}, true, true); },
            "Mutated state admitted by update");
    invalid([&] { selection_creation_box_candidate(bad); },
            "Mutated state admitted by candidate");
    check(same(bad, before), "Refused operation mutated caller state");
  };
  auto bad = valid;bad.kind = static_cast<Kind>(99);reject_state(bad);
  bad = valid;bad.image_height = 0;reject_state(bad);
  bad = valid;bad.anchor[0] = -1;reject_state(bad);
  bad = valid;bad.bounds[2] = -1;reject_state(bad);
  bad = valid;bad.bounds[3] = std::numeric_limits<std::int64_t>::max();reject_state(bad);
  bad = valid;bad.bounds[0] = std::numeric_limits<std::int64_t>::min();reject_state(bad);
  bad = valid;bad.bounds[1] = std::numeric_limits<std::int64_t>::max();reject_state(bad);
  bad = valid;bad.bounds = {19, 6, 0, 0};reject_state(bad);
  bad = valid;bad.bounds = {0, 0, 17, 6};reject_state(bad);
  constexpr int max = std::numeric_limits<int>::max();
  const auto edge = begin_selection_creation_box(Kind::Rectangle, max, max,
                                                {max - 1, max - 1});
  exact_candidate(update_selection_creation_box(edge, {max, max}, false, false),
                  {max - 1, max - 1, 1, 1}, "Native integer endpoint remains exact");
  const auto origin = begin_selection_creation_box(Kind::Rectangle, max, max, {0, 0});
  exact_candidate(update_selection_creation_box(
                      begin_selection_creation_box(Kind::Rectangle, 100000,
                                                   100000, {0, 0}),
                      {50000, 50000}, true, false),
                  {0, 0, 70711, 70711},
                  "Wide checked diagonal extension does not emulate Java overflow");
  invalid([&] { update_selection_creation_box(origin, {max, max}, true, false); },
          "Out-of-native-range rounded diagonal was silently narrowed");
  invalid([&] { update_selection_creation_box(origin, {max, max}, false, true); },
          "Centered extent overflow was silently narrowed");
  const auto ellipse_edge = begin_selection_creation_box(Kind::Ellipse, max, max,
                                                        {max - 1, max - 1});
  invalid([&] { update_selection_creation_box(ellipse_edge,
                                             {std::numeric_limits<int>::min(),
                                              std::numeric_limits<int>::min()},
                                             false, false); },
          "Extreme signed delta was silently narrowed");
  bad = origin;bad.bounds = {1, 0, max, 0};reject_state(bad);
  const auto boundary = begin_selection_creation_box(Kind::Rectangle, max, max, {max, max});
  exact_candidate(boundary, {max, max, 0, 0}, "Inclusive snapped boundary is valid");
  exact_candidate(update_selection_creation_box(boundary, {0, 0}, false, false),
                  {0, 0, max, max}, "Large finite geometry requires no pixel storage");
}
} // namespace

int main() {
  try {
    reference_prefixes();
    quadrants_and_clipping();
    zero_invalid_and_extreme();
    std::cout << "selection-creation-geometry qualified_rows=" << qualified_rows
              << " qualified_drags=" << qualified_drags << " checks=" << checks << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "selection-creation-geometry: " << error.what() << '\n';
    return 1;
  }
}
