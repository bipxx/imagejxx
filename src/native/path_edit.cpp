#include <imagejxx/path_edit.hpp>
#include "kernel_control.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace imagejxx {
namespace {
using Point = std::array<double, 2>;
void require(bool value, const char *message) {
  if (!value) throw std::invalid_argument(message);
}
void canceled(const ExecutionControl &control) {
  if (control.cancellation && control.cancellation->canceled()) throw OperationCancelled();
}
std::size_t arity(int op) { return op == 4 ? 0 : op == 3 ? 6 : op == 2 ? 4 : 2; }
void admit(const ImageJCompositePath &source, std::size_t result_scalars,
           std::size_t result_segments, const PathEditLimits &limits,
           const ExecutionControl &control) {
  require(limits.max_segments && limits.max_command_scalars && limits.max_working_bytes,
          "Path edit limits must be positive");
  require(source.segment_count() <= limits.max_segments && result_segments <= limits.max_segments &&
              source.commands().size() <= limits.max_command_scalars && result_scalars <= limits.max_command_scalars,
          "Path edit command limit exceeded");
  // Fixed reservation covers small command scratch and staging objects.
  require(limits.max_working_bytes >= 4096, "Path edit working byte limit exceeded");
  const auto capacity = (limits.max_working_bytes - 4096) / sizeof(double);
  require(source.commands().size() <= capacity && result_scalars <= (capacity - source.commands().size()) / 2,
          "Path edit working byte limit exceeded");
  canceled(control);
}
struct Location { std::size_t begin, end; Point current, start; };
Location locate(const ImageJCompositePath &source, std::size_t index,
                const ExecutionControl &control) {
  const auto &commands = source.commands();
  Point current{}, start{};
  std::size_t ordinal = 0;
  for (std::size_t at = 0; at < commands.size();) {
    if ((ordinal & 1023u) == 0) canceled(control);
    const auto begin = at;
    const int op = int(commands[at++]);
    const auto n = arity(op);
    if (ordinal++ == index) return {begin, at + n, current, start};
    if (op == 0) current = start = {commands[at], commands[at + 1]};
    else if (op == 4) current = start;
    else current = {commands[at + n - 2], commands[at + n - 1]};
    at += n;
  }
  require(index == ordinal, "Path segment index outside geometry");
  return {commands.size(), commands.size(), current, start};
}
ImageJCompositePath result(const ImageJCompositePath &source, Location where,
                          const std::vector<double> &replacement,
                          const PathEditLimits &limits, const ExecutionControl &control) {
  canceled(control);
  const auto &input = source.commands();
  std::vector<double> output;
  output.reserve(input.size() - (where.end - where.begin) + replacement.size());
  auto copy = [&](std::size_t first, std::size_t last) {
    for (auto i = first; i < last; ++i) {
      if ((i & 1023u) == 0) canceled(control);
      output.push_back(input[i]);
    }
  };
  copy(0, where.begin);
  output.insert(output.end(), replacement.begin(), replacement.end());
  copy(where.end, input.size());
  ImageJRoiLimits roi;
  roi.max_vertices = limits.max_segments;
  roi.max_bytes = limits.max_command_scalars > std::numeric_limits<std::size_t>::max() / sizeof(double)
      ? std::numeric_limits<std::size_t>::max() : limits.max_command_scalars * sizeof(double);
  auto value = ImageJCompositePath(output, roi);
  canceled(control);
  // Freeze callback state after constructing the private immutable result.
  const auto frozen_control = control;
  ExecutionScope scope(frozen_control);
  kernel_checkpoint(); execution_progress(0, 1); kernel_checkpoint();
  execution_progress(1, 1); kernel_checkpoint();
  return value;
}
} // namespace

ImageJCompositePath edit_composite_path(const ImageJCompositePath &source,
    PathSegmentEdit operation, std::size_t index, const std::vector<double> &segment,
    const PathEditLimits &limits, const ExecutionControl &control) {
  require(operation == PathSegmentEdit::Replace || operation == PathSegmentEdit::InsertBefore || operation == PathSegmentEdit::Remove,
          "Unknown path segment operation");
  const bool insert = operation == PathSegmentEdit::InsertBefore;
  const bool remove = operation == PathSegmentEdit::Remove;
  require(index < source.segment_count() || (insert && index == source.segment_count()),
          "Path segment index outside geometry");
  if (remove) require(segment.empty(), "Removing a path segment requires no replacement command");
  else {
    require(!segment.empty() && segment.size() <= 7, "Path edit requires one command");
    const auto op = segment[0];
    require(std::isfinite(op) && op >= 0 && op <= 4 && std::floor(op) == op,
            "Invalid path edit opcode");
    require(segment.size() == 1 + arity(int(op)), "Path edit requires one complete command");
    for (std::size_t i = 1; i < segment.size(); ++i)
      require(std::isfinite(segment[i]) && std::abs(segment[i]) <= 1e9, "Invalid path edit coordinate");
  }
  // Source admission precedes even the bounded borrowed location scan.
  admit(source, 0, 0, limits, control);
  auto where = locate(source, index, control);
  if (insert) where.end = where.begin;
  const auto remaining = source.commands().size() - (where.end - where.begin);
  require(segment.size() <= std::numeric_limits<std::size_t>::max() - remaining, "Path command size overflow");
  admit(source, remaining + segment.size(), source.segment_count() + (insert ? 1 : 0) - (remove ? 1 : 0), limits, control);
  return result(source, where, segment, limits, control);
}

ImageJCompositePath split_composite_path(const ImageJCompositePath &source,
    std::size_t index, double t, const PathEditLimits &limits, const ExecutionControl &control) {
  require(std::isfinite(t) && t > 0 && t < 1, "Path split parameter must be in (0,1)");
  require(index < source.segment_count(), "Path split index outside geometry");
  admit(source, 0, 0, limits, control);
  const auto where = locate(source, index, control);
  const auto &input = source.commands();
  const int op = int(input[where.begin]);
  require(op != 0, "Cannot split a path move command");
  const auto extra = op == 4 ? 3u : 1u + arity(op);
  require(extra <= std::numeric_limits<std::size_t>::max() - input.size() && source.segment_count() < std::numeric_limits<std::size_t>::max(), "Path split size overflow");
  admit(source, input.size() + extra, source.segment_count() + 1, limits, control);
  auto point = [&](std::size_t offset) { return Point{input[where.begin + 1 + offset], input[where.begin + 2 + offset]}; };
  auto mix = [t](Point a, Point b) { return Point{a[0] * (1 - t) + b[0] * t, a[1] * (1 - t) + b[1] * t}; };
  std::vector<double> replacement;
  replacement.reserve(14);
  auto emit = [&](int opcode, std::initializer_list<Point> points) {
    replacement.push_back(opcode);
    for (auto p : points) { replacement.push_back(p[0]); replacement.push_back(p[1]); }
  };
  const auto a = where.current;
  if (op == 1 || op == 4) {
    const auto b = op == 4 ? where.start : point(0);
    emit(1, {mix(a, b)});
    if (op == 4) emit(4, {}); else emit(1, {b});
  } else if (op == 2) {
    const auto b = point(0), c = point(2), ab = mix(a,b), bc = mix(b,c), m = mix(ab,bc);
    emit(2, {ab,m}); emit(2, {bc,c});
  } else {
    const auto b = point(0), c = point(2), d = point(4);
    const auto ab = mix(a,b), bc = mix(b,c), cd = mix(c,d), abc = mix(ab,bc), bcd = mix(bc,cd), m = mix(abc,bcd);
    emit(3, {ab,abc,m}); emit(3, {bcd,cd,d});
  }
  return result(source, where, replacement, limits, control);
}
} // namespace imagejxx
