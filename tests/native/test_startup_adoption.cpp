// Independent selected-active publication assertions. This executable alone
// replaces operator new; no production failpoint or shared test hook is used.
#include <imagejxx/application.hpp>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

namespace {
thread_local bool observe = false;
thread_local std::size_t ordinal = 0;
thread_local std::size_t fail_ordinal = std::numeric_limits<std::size_t>::max();
thread_local std::size_t failed_ordinal = 0;
thread_local std::array<std::size_t, 256> sizes{};
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
}
void *operator new(std::size_t count) {
  if (observe) {
    const auto current = ++ordinal;
    if (current <= sizes.size()) sizes[current - 1] = count;
    if (current == fail_ordinal) {
      fail_ordinal = std::numeric_limits<std::size_t>::max();
      failed_ordinal = current;
      throw std::bad_alloc();
    }
  }
  if (auto memory = std::malloc(count ? count : 1)) return memory;
  throw std::bad_alloc();
}
void *operator new[](std::size_t count) { return ::operator new(count); }
void operator delete(void *value) noexcept { std::free(value); }
void operator delete[](void *value) noexcept { std::free(value); }
void operator delete(void *value, std::size_t) noexcept { std::free(value); }
void operator delete[](void *value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace imagejxx;
std::shared_ptr<Document> owner(const char *title) {
  Image plane(8, 6, 1, PixelType::UInt8);
  for (int y = 0; y < 6; ++y) for (int x = 0; x < 8; ++x)
    plane.at(x, y) = 3 * y + x;
  return std::make_shared<Document>(Dataset::from_stack({std::move(plane)}), title);
}
template<class Function> void refuses(Function function) {
  bool refused = false;
  try { function(); } catch (const std::exception &) { refused = true; }
  check(refused, "Expected complete batch refusal");
}
void exact_selection_and_legacy() {
  Application app;
  auto a = owner("first"), b = owner("second");
  const auto ids = app.adopt_all({a, b}, IncomingDocumentActivation{0});
  check(ids == std::vector<std::uint64_t>{a->id, b->id} &&
        app.document_ids() == ids && app.active_document_id() == a->id,
        "Selected incoming ordinal reordered owners or activated last");
  auto c = owner("third"), d = owner("fourth");
  app.adopt_all({c, d}, false);
  check(app.active_document_id() == a->id, "Legacy false overload changed active owner");
  auto e = owner("fifth"), f = owner("sixth");
  app.adopt_all({e, f}, true);
  check(app.active_document_id() == f->id, "Legacy true overload no longer selects last");
  const auto before = app.document_ids();
  const auto active = app.active_document_id();
  auto g = owner("unpublished");
  refuses([&] { app.adopt_all({g}, IncomingDocumentActivation{1}); });
  refuses([&] { app.adopt_all({}, IncomingDocumentActivation{0}); });
  refuses([&] { app.adopt_all({g, g}, IncomingDocumentActivation{0}); });
  check(app.document_ids() == before && app.active_document_id() == active,
        "Invalid selected ordinal/duplicate changed membership or activation");
}
void late_display_refusal() {
  Application app;
  auto a = owner("first"), b = owner("second");
  auto display = b->snapshot().display();
  display.zoom = -1;
  const auto a_before = a->snapshot();
  const auto b_before = b->snapshot();
  refuses([&] {
    app.adopt_all_with_history_policy(
        {{a, {}}, {b, DocumentInitialDisplay{b_before, display}}},
        HistoryPolicy{3, 1024 * 1024}, IncomingDocumentActivation{0});
  });
  check(app.document_ids().empty() && app.active_document_id() == 0,
        "Late selected batch validator published a prefix");
  check(a->snapshot().shares_data(a_before) && b->snapshot().shares_data(b_before) &&
        a->snapshot().revisions().matches(a_before.revisions(), Change::All) &&
        b->snapshot().revisions().matches(b_before.revisions(), Change::All) &&
        a->history().step_limit == 8 && b->history().step_limit == 8,
        "Late validator changed an incoming owner or its history policy");
  bool reject_second=false;
  const auto lease=b->constrain([&](const DocumentData &,const DisplayState &) {
    if(reject_second)throw std::invalid_argument("Owned retained second validator refusal");
  });
  reject_second=true;
  refuses([&] {
    app.adopt_all_with_history_policy({{a,{}},{b,{}}},HistoryPolicy{3,1048576},IncomingDocumentActivation{0});
  });
  check(app.document_ids().empty() && app.active_document_id()==0 &&
        a->history().step_limit==8 && b->history().step_limit==8 &&
        a->snapshot().shares_data(a_before) && b->snapshot().shares_data(b_before),
        "Second retained validator published a prefix or pruned first history");
}
std::size_t allocation_run(bool policy, std::size_t fail_at,
                           std::size_t *last_size = nullptr) {
  Application app;
  auto first = owner("first"), second = owner("second");
  std::vector<std::shared_ptr<Document>> plain{first, second};
  std::vector<DocumentAdoption> entries{{first, {}}, {second, {}}};
  ordinal = failed_ordinal = 0;
  fail_ordinal = fail_at;
  observe = true;
  bool refused = false;
  try {
    if (policy) {
      const auto result = app.adopt_all_with_history_policy(
          std::move(entries), HistoryPolicy{3, 1024 * 1024},
          IncomingDocumentActivation{0});
    } else {
      const auto result = app.adopt_all(std::move(plain), IncomingDocumentActivation{0});
    }
  } catch (const std::bad_alloc &) { refused = true; }
  catch (...) { observe = false; fail_ordinal = std::numeric_limits<std::size_t>::max(); throw; }
  observe = false;
  fail_ordinal = std::numeric_limits<std::size_t>::max();
  const auto count = ordinal;
  check(count > 0 && count <= sizes.size(), "Allocation trace exceeds bounded storage");
  if (last_size) *last_size = sizes[count - 1];
  if (fail_at != std::numeric_limits<std::size_t>::max()) {
    check(refused && failed_ordinal == fail_at,
          "Caller-thread allocation failure did not hit the selected staging ordinal");
    check(app.document_ids().empty() && app.active_document_id() == 0,
          "Selected-active staging allocation published a prefix or activation");
    check(first->history().step_limit == 8 && second->history().step_limit == 8 &&
          first->undo_steps() == 0 && second->undo_steps() == 0 &&
          !first->snapshot().dirty() && !second->snapshot().dirty(),
          "Selected-active refusal changed incoming history or dirty state");
  } else {
    check(!refused && app.active_document_id() == first->id &&
          app.document_ids() == std::vector<std::uint64_t>{first->id, second->id},
          "Selected-active allocation baseline failed");
  }
  return count;
}
// Return the control block across a non-inlined boundary. An unused local
// make_shared can be allocation-elided by optimized Clang/libc++, bypassing
// this executable's replacement operator new and invalidating calibration.
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
std::shared_ptr<const int> calibration_token() {
  return std::make_shared<const int>(0);
}
void allocation_failures() {
  ordinal = 0; observe = true;
  const auto token = calibration_token();
  observe = false;
  check(ordinal == 1, "Activation token calibration must be one allocation");
  const auto token_bytes = sizes[0];
  std::size_t final_bytes = 0;
  const auto plain_count = allocation_run(false, std::numeric_limits<std::size_t>::max(), &final_bytes);
  check(final_bytes == token_bytes,
        "Final plain selected-adoption allocation is not the calibrated activation token");
  // Every caller-thread ordinal is refused independently in a fresh Application.
  // The final plain allocation explicitly identifies the selected-active token.
  for (std::size_t index = 1; index <= plain_count; ++index) allocation_run(false, index);
  const auto policy_count = allocation_run(true, std::numeric_limits<std::size_t>::max());
  for (std::size_t index = 1; index <= policy_count; ++index) allocation_run(true, index);
  std::cout << "selected_active_token_bytes=" << token_bytes
            << " plain_allocation_ordinals=" << plain_count
            << " history_allocation_ordinals=" << policy_count << '\n';
}
}
int main() {
  try {
    exact_selection_and_legacy();
    late_display_refusal();
    allocation_failures();
    std::cout << "selected-active publication assertions complete\n";
    return 0;
  } catch (const std::exception &error) {
    observe = false;
    std::cerr << error.what() << '\n';
    return 1;
  }
}
