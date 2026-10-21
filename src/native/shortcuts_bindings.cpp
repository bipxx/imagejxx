#include <imagejxx/shortcuts.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <exception>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace py = pybind11;
using namespace imagejxx;
namespace {
// Preserve Python's ordinary ValueError class/message while exposing the
// genuinely caught native category only at these explicit capped entry points.
// A module-wide translator would also alter unrelated native exceptions.
template <class Function> decltype(auto) capped_shortcut_call(Function &&call) {
  try {
    return std::forward<Function>(call)();
  } catch (const std::length_error &failure) {
    auto instance = py::reinterpret_borrow<py::object>(PyExc_ValueError)(failure.what());
    instance.attr("native_category") = "length_error";
    PyErr_SetObject(PyExc_ValueError, instance.ptr());
    throw py::error_already_set();
  }
}

std::string text(py::handle value) {
  if (!PyUnicode_Check(value.ptr()))
    throw py::type_error("Shortcut text must be a string");
  return py::cast<std::string>(value);
}
bool boolean(py::handle value) {
  if (!PyBool_Check(value.ptr()))
    throw py::type_error("Shortcut flags must be booleans");
  return py::cast<bool>(value);
}
template <class E> E enumeration(py::handle value, E last) {
  if (!py::isinstance<E>(value))
    throw py::type_error("Expected the declared shortcut enum type");
  const auto result = py::cast<E>(value);
  if (static_cast<unsigned>(result) > static_cast<unsigned>(last))
    throw py::value_error("Unknown shortcut enum value");
  return result;
}
ShortcutModifiers modifiers(py::handle value) {
  return enumeration(value, static_cast<ShortcutModifiers>(31));
}
std::vector<std::string> lines(py::handle value) {
  if (!PyList_Check(value.ptr()))
    throw py::type_error("Shortcut override lines must be a list of strings");
  std::vector<std::string> result;
  result.reserve(static_cast<std::size_t>(PyList_GET_SIZE(value.ptr())));
  for (auto item : py::reinterpret_borrow<py::list>(value))
    result.push_back(text(item));
  return result;
}
std::map<std::string, bool> availability(py::handle value) {
  if (!PyDict_Check(value.ptr()))
    throw py::type_error("Shortcut availability must be a dictionary");
  std::map<std::string, bool> result;
  for (auto item : py::reinterpret_borrow<py::dict>(value)) {
    auto id = text(item.first);
    const auto &commands = shortcut_commands();
    if (std::none_of(commands.begin(), commands.end(),
                     [&](const ShortcutCommand &c) { return c.id == id; }))
      throw py::value_error("Unknown registered shortcut command");
    result.emplace(std::move(id), boolean(item.second));
  }
  return result;
}
std::size_t limit(py::handle value, std::size_t ceiling) {
  if (PyBool_Check(value.ptr()) || !PyLong_Check(value.ptr()))
    throw py::type_error("Shortcut limits must be integers");
  const auto result = PyLong_AsSize_t(value.ptr());
  if (PyErr_Occurred()) {
    PyErr_Clear();
    throw py::value_error("Shortcut limits must fit in size_t");
  }
  if (!result || result > ceiling)
    throw py::value_error("Shortcut limits must be positive and within owned ceilings");
  return result;
}
template <class T>
void flag(py::class_<T> &type, const char *name, bool T::*member) {
  type.def_property(name, [member](const T &v) { return v.*member; },
                    [member](T &v, py::handle value) {
                      v.*member = boolean(value);
                    });
}
void bound(py::class_<ShortcutLimits> &type, const char *name,
           std::size_t ShortcutLimits::*member) {
  const auto ceiling = ShortcutLimits{}.*member;
  type.def_property(name, [member](const ShortcutLimits &v) { return v.*member; },
                    [member, ceiling](ShortcutLimits &v, py::handle value) {
                      v.*member = limit(value, ceiling);
                    });
}
} // namespace

void bind_shortcuts(py::module_ &m) {
  m.attr("shortcut_registry_version") = shortcut_registry_version;
  py::enum_<ShortcutPlatform>(m, "ShortcutPlatform")
      .value("Windows", ShortcutPlatform::Windows)
      .value("Linux", ShortcutPlatform::Linux)
      .value("MacOS", ShortcutPlatform::MacOS);
  auto keys = py::enum_<ShortcutKey>(m, "ShortcutKey");
#define SHORTCUT_KEY(name) keys.value(#name, ShortcutKey::name)
  SHORTCUT_KEY(A); SHORTCUT_KEY(B); SHORTCUT_KEY(C); SHORTCUT_KEY(D);
  SHORTCUT_KEY(E); SHORTCUT_KEY(F); SHORTCUT_KEY(G); SHORTCUT_KEY(H);
  SHORTCUT_KEY(I); SHORTCUT_KEY(J); SHORTCUT_KEY(K); SHORTCUT_KEY(L);
  SHORTCUT_KEY(M); SHORTCUT_KEY(N); SHORTCUT_KEY(O); SHORTCUT_KEY(P);
  SHORTCUT_KEY(Q); SHORTCUT_KEY(R); SHORTCUT_KEY(S); SHORTCUT_KEY(T);
  SHORTCUT_KEY(U); SHORTCUT_KEY(V); SHORTCUT_KEY(W); SHORTCUT_KEY(X);
  SHORTCUT_KEY(Y); SHORTCUT_KEY(Z);
  SHORTCUT_KEY(Digit0); SHORTCUT_KEY(Digit1); SHORTCUT_KEY(Digit2);
  SHORTCUT_KEY(Digit3); SHORTCUT_KEY(Digit4); SHORTCUT_KEY(Digit5);
  SHORTCUT_KEY(Digit6); SHORTCUT_KEY(Digit7); SHORTCUT_KEY(Digit8);
  SHORTCUT_KEY(Digit9);
  SHORTCUT_KEY(F1); SHORTCUT_KEY(F2); SHORTCUT_KEY(F3); SHORTCUT_KEY(F4);
  SHORTCUT_KEY(F5); SHORTCUT_KEY(F6); SHORTCUT_KEY(F7); SHORTCUT_KEY(F8);
  SHORTCUT_KEY(F9); SHORTCUT_KEY(F10); SHORTCUT_KEY(F11); SHORTCUT_KEY(F12);
  SHORTCUT_KEY(F13); SHORTCUT_KEY(F14); SHORTCUT_KEY(F15); SHORTCUT_KEY(F16);
  SHORTCUT_KEY(F17); SHORTCUT_KEY(F18); SHORTCUT_KEY(F19); SHORTCUT_KEY(F20);
  SHORTCUT_KEY(F21); SHORTCUT_KEY(F22); SHORTCUT_KEY(F23); SHORTCUT_KEY(F24);
  SHORTCUT_KEY(Escape); SHORTCUT_KEY(Enter); SHORTCUT_KEY(Tab); SHORTCUT_KEY(Space);
  SHORTCUT_KEY(Backspace); SHORTCUT_KEY(Delete); SHORTCUT_KEY(Insert);
  SHORTCUT_KEY(Home); SHORTCUT_KEY(End); SHORTCUT_KEY(PageUp); SHORTCUT_KEY(PageDown);
  SHORTCUT_KEY(Left); SHORTCUT_KEY(Right); SHORTCUT_KEY(Up); SHORTCUT_KEY(Down);
  SHORTCUT_KEY(Minus); SHORTCUT_KEY(Equal); SHORTCUT_KEY(LeftBracket);
  SHORTCUT_KEY(RightBracket); SHORTCUT_KEY(Backslash); SHORTCUT_KEY(Semicolon);
  SHORTCUT_KEY(Apostrophe); SHORTCUT_KEY(Grave); SHORTCUT_KEY(Comma);
  SHORTCUT_KEY(Period); SHORTCUT_KEY(Slash);
#undef SHORTCUT_KEY
  py::enum_<ShortcutModifiers>(m, "ShortcutModifiers")
      .value("None_", ShortcutModifiers::None)
      .value("Primary", ShortcutModifiers::Primary)
      .value("Ctrl", ShortcutModifiers::Ctrl)
      .value("Alt", ShortcutModifiers::Alt)
      .value("Shift", ShortcutModifiers::Shift)
      .value("Super", ShortcutModifiers::Super)
      .def("__or__", [](py::handle a, py::handle b) {
        return modifiers(a) | modifiers(b);
      }, py::is_operator());
  py::enum_<ShortcutScope>(m, "ShortcutScope")
      .value("Shell", ShortcutScope::Shell).value("Document", ShortcutScope::Document);
  py::enum_<ShortcutDiagnosticKind>(m, "ShortcutDiagnosticKind")
      .value("InactiveUnknown", ShortcutDiagnosticKind::InactiveUnknown)
      .value("Conflict", ShortcutDiagnosticKind::Conflict)
      .value("Reserved", ShortcutDiagnosticKind::Reserved);
  py::enum_<ShortcutEditStatus>(m, "ShortcutEditStatus")
      .value("NoOp", ShortcutEditStatus::NoOp).value("Changed", ShortcutEditStatus::Changed);
  py::enum_<ShortcutFocus>(m, "ShortcutFocus")
      .value("None_", ShortcutFocus::None).value("Workspace", ShortcutFocus::Workspace)
      .value("Canvas", ShortcutFocus::Canvas).value("ROIManager", ShortcutFocus::ROIManager)
      .value("Control", ShortcutFocus::Control).value("Text", ShortcutFocus::Text)
      .value("Modal", ShortcutFocus::Modal).value("Capture", ShortcutFocus::Capture);
  py::enum_<ShortcutDecisionReason>(m, "ShortcutDecisionReason")
      .value("Dispatch", ShortcutDecisionReason::Dispatch)
      .value("Unfocused", ShortcutDecisionReason::Unfocused)
      .value("Rearming", ShortcutDecisionReason::Rearming)
      .value("Capture", ShortcutDecisionReason::Capture)
      .value("Modal", ShortcutDecisionReason::Modal)
      .value("Control", ShortcutDecisionReason::Control)
      .value("Gesture", ShortcutDecisionReason::Gesture)
      .value("ROIManagerSelectAll", ShortcutDecisionReason::ROIManagerSelectAll)
      .value("LocalClaim", ShortcutDecisionReason::LocalClaim)
      .value("NoEdge", ShortcutDecisionReason::NoEdge)
      .value("Repeat", ShortcutDecisionReason::Repeat)
      .value("Unbound", ShortcutDecisionReason::Unbound)
      .value("Unavailable", ShortcutDecisionReason::Unavailable)
      .value("Ambiguous", ShortcutDecisionReason::Ambiguous)
      .value("NoWorkspace", ShortcutDecisionReason::NoWorkspace);
  py::enum_<ShortcutDisposition>(m, "ShortcutDisposition")
      .value("None_", ShortcutDisposition::None)
      .value("LocalContext", ShortcutDisposition::LocalContext)
      .value("GlobalCommand", ShortcutDisposition::GlobalCommand);

  py::class_<ShortcutChord>(m, "ShortcutChord")
      .def(py::init([](py::handle key, py::handle mods) {
        ShortcutChord result{enumeration(key, ShortcutKey::Slash), modifiers(mods)};
        (void)format_shortcut_chord(result);
        return result;
      }), py::arg("key") = ShortcutKey::A,
           py::arg("modifiers") = ShortcutModifiers::None)
      .def_property("key", [](const ShortcutChord &v) { return v.key; },
                    [](ShortcutChord &v, py::handle key) {
                      auto proposed = v;
                      proposed.key = enumeration(key, ShortcutKey::Slash);
                      (void)format_shortcut_chord(proposed);
                      v = proposed;
                    })
      .def_property("modifiers", [](const ShortcutChord &v) { return v.modifiers; },
                    [](ShortcutChord &v, py::handle mods) {
                      auto proposed = v;
                      proposed.modifiers = modifiers(mods);
                      (void)format_shortcut_chord(proposed);
                      v = proposed;
                    })
      .def("__eq__", [](const ShortcutChord &v, const ShortcutChord &other) {
        return v == other;
      }, py::is_operator())
      .def("__ne__", [](const ShortcutChord &v, const ShortcutChord &other) {
        return v != other;
      }, py::is_operator());
  py::class_<ShortcutCommand>(m, "ShortcutCommand")
      .def_property_readonly("id", [](const ShortcutCommand &v) { return v.id; })
      .def_property_readonly("label", [](const ShortcutCommand &v) { return v.label; })
      .def_property_readonly("scope", [](const ShortcutCommand &v) { return v.scope; })
      .def_property_readonly("default_chord", [](const ShortcutCommand &v) { return v.default_chord; });
  py::class_<ShortcutBinding>(m, "ShortcutBinding")
      .def_property_readonly("command_id", [](const ShortcutBinding &v) { return v.command_id; })
      .def_property_readonly("chord", [](const ShortcutBinding &v) { return v.chord; });
  py::class_<ShortcutDiagnostic>(m, "ShortcutDiagnostic")
      .def_property_readonly("kind", [](const ShortcutDiagnostic &v) { return v.kind; })
      .def_property_readonly("command_id", [](const ShortcutDiagnostic &v) { return v.command_id; })
      .def_property_readonly("other_command_id", [](const ShortcutDiagnostic &v) { return v.other_command_id; })
      .def_property_readonly("chord", [](const ShortcutDiagnostic &v) { return v.chord; })
      .def_property_readonly("other_chord", [](const ShortcutDiagnostic &v) { return v.other_chord; })
      .def_property_readonly("platforms", [](const ShortcutDiagnostic &v) { return v.platforms; });
  // A local translator also covers Settings.update, whose dedicated native
  // validator throws this type. Diagnostics are copied onto each exception.
  m.add_object("ShortcutProfileError", py::reinterpret_steal<py::object>(
      PyErr_NewException("imagejxx._native.ShortcutProfileError", PyExc_ValueError, nullptr)));
  py::register_local_exception_translator([](std::exception_ptr error) {
    try {
      if (error) std::rethrow_exception(error);
    } catch (const ShortcutProfileError &failure) {
      auto type = py::module_::import("imagejxx._native").attr("ShortcutProfileError");
      auto instance = type(failure.what());
      instance.attr("diagnostics") = py::cast(failure.diagnostics());
      PyErr_SetObject(type.ptr(), instance.ptr());
    }
  });

  auto limits = py::class_<ShortcutLimits>(m, "ShortcutLimits");
  limits.def(py::init([](py::handle entries, py::handle entry_bytes,
                         py::handle total_bytes, py::handle id_bytes,
                         py::handle chord_bytes, py::handle commands,
                         py::handle working_bytes) {
    const ShortcutLimits ceiling;
    return ShortcutLimits{limit(entries, ceiling.max_override_entries),
                          limit(entry_bytes, ceiling.max_entry_bytes),
                          limit(total_bytes, ceiling.max_total_entry_bytes),
                          limit(id_bytes, ceiling.max_command_id_bytes),
                          limit(chord_bytes, ceiling.max_chord_bytes),
                          limit(commands, ceiling.max_registry_commands),
                          limit(working_bytes, ceiling.max_native_working_bytes)};
  }), py::arg("max_override_entries") = 1024, py::arg("max_entry_bytes") = 256,
      py::arg("max_total_entry_bytes") = 262144, py::arg("max_command_id_bytes") = 96,
      py::arg("max_chord_bytes") = 96, py::arg("max_registry_commands") = 4096,
      py::arg("max_native_working_bytes") = 4194304);
  bound(limits, "max_override_entries", &ShortcutLimits::max_override_entries);
  bound(limits, "max_entry_bytes", &ShortcutLimits::max_entry_bytes);
  bound(limits, "max_total_entry_bytes", &ShortcutLimits::max_total_entry_bytes);
  bound(limits, "max_command_id_bytes", &ShortcutLimits::max_command_id_bytes);
  bound(limits, "max_chord_bytes", &ShortcutLimits::max_chord_bytes);
  bound(limits, "max_registry_commands", &ShortcutLimits::max_registry_commands);
  bound(limits, "max_native_working_bytes", &ShortcutLimits::max_native_working_bytes);
  py::class_<ShortcutProfile>(m, "ShortcutProfile")
      .def(py::init<>())
      .def("override_lines", &ShortcutProfile::override_lines)
      .def("inactive_rows", &ShortcutProfile::inactive_rows)
      .def("diagnostics", &ShortcutProfile::diagnostics)
      .def("bindings", [](const ShortcutProfile &v, py::handle platform) {
        return v.bindings(enumeration(platform, ShortcutPlatform::MacOS));
      }, py::arg("platform"))
      .def("chord", [](const ShortcutProfile &v, py::handle command, py::handle platform) {
        return v.chord(text(command), enumeration(platform, ShortcutPlatform::MacOS));
      }, py::arg("command_id"), py::arg("platform"))
      .def("working_bytes", &ShortcutProfile::working_bytes);
  py::class_<ShortcutEditResult>(m, "ShortcutEditResult")
      .def_property_readonly("profile", [](const ShortcutEditResult &v) { return v.profile; })
      .def_property_readonly("status", [](const ShortcutEditResult &v) { return v.status; })
      .def_property_readonly("bindings_changed", [](const ShortcutEditResult &v) { return v.bindings_changed; });
  auto event = py::class_<ShortcutEvent>(m, "ShortcutEvent");
  event.def(py::init([](py::handle key, py::handle ctrl, py::handle alt,
                        py::handle shift, py::handle super, py::handle edge,
                        py::handle repeat) {
    return ShortcutEvent{enumeration(key, ShortcutKey::Slash), boolean(ctrl),
                         boolean(alt), boolean(shift), boolean(super),
                         boolean(edge), boolean(repeat)};
  }), py::arg("key") = ShortcutKey::A, py::kw_only(), py::arg("ctrl") = false,
      py::arg("alt") = false, py::arg("shift") = false, py::arg("super") = false,
      py::arg("down_edge") = false, py::arg("repeat") = false)
      .def_property("key", [](const ShortcutEvent &v) { return v.key; },
                    [](ShortcutEvent &v, py::handle key) {
                      v.key = enumeration(key, ShortcutKey::Slash);
                    });
  flag(event, "ctrl", &ShortcutEvent::ctrl); flag(event, "alt", &ShortcutEvent::alt);
  flag(event, "shift", &ShortcutEvent::shift); flag(event, "super", &ShortcutEvent::super);
  flag(event, "down_edge", &ShortcutEvent::down_edge); flag(event, "repeat", &ShortcutEvent::repeat);
  auto context = py::class_<ShortcutContext>(m, "ShortcutContext");
  context.def(py::init([](py::handle focus, py::handle focused, py::handle claimed,
                          py::handle gesture, py::handle rearmed, py::handle available) {
    return ShortcutContext{enumeration(focus, ShortcutFocus::Capture),
                           boolean(focused), boolean(claimed), boolean(gesture),
                           boolean(rearmed), availability(available)};
  }), py::arg("focus") = ShortcutFocus::None, py::kw_only(),
      py::arg("window_focused") = true, py::arg("local_key_claimed") = false,
      py::arg("gesture_active") = false, py::arg("rearmed") = true,
      py::arg("availability") = py::dict())
      .def_property("focus", [](const ShortcutContext &v) { return v.focus; },
                    [](ShortcutContext &v, py::handle focus) {
                      v.focus = enumeration(focus, ShortcutFocus::Capture);
                    })
      .def_property("availability", [](const ShortcutContext &v) { return v.availability; },
                    [](ShortcutContext &v, py::handle available) {
                      v.availability = availability(available);
                    });
  flag(context, "window_focused", &ShortcutContext::window_focused);
  flag(context, "local_key_claimed", &ShortcutContext::local_key_claimed);
  flag(context, "gesture_active", &ShortcutContext::gesture_active);
  flag(context, "rearmed", &ShortcutContext::rearmed);
  py::class_<ShortcutDecision>(m, "ShortcutDecision")
      .def_property_readonly("command_id", [](const ShortcutDecision &v) { return v.command_id; })
      .def_property_readonly("reason", [](const ShortcutDecision &v) { return v.reason; })
      .def_property_readonly("disposition", [](const ShortcutDecision &v) { return v.disposition; })
      .def("consumed", &ShortcutDecision::consumed);

  m.def("shortcut_commands", [] { return std::vector<ShortcutCommand>(shortcut_commands()); });
  m.def("parse_shortcut_chord", [](py::handle value) { return parse_shortcut_chord(text(value)); }, py::arg("text"));
  m.def("format_shortcut_chord", &format_shortcut_chord, py::arg("chord"));
  m.def("resolve_shortcut_chord", [](const ShortcutChord &chord, py::handle platform) {
    return resolve_shortcut_chord(chord, enumeration(platform, ShortcutPlatform::MacOS));
  }, py::arg("chord"), py::arg("platform"));
  m.def("compile_shortcuts", [](py::handle values, const ShortcutLimits &caps) {
    return capped_shortcut_call([&] { return compile_shortcuts(lines(values), caps); });
  }, py::arg("override_lines"), py::arg("limits") = ShortcutLimits{});
  m.def("assign_shortcut", [](const ShortcutProfile &profile, py::handle command,
                               py::handle chord, const ShortcutLimits &caps) {
    return capped_shortcut_call([&] { return assign_shortcut(profile, text(command), text(chord), caps); });
  }, py::arg("profile"), py::arg("command_id"), py::arg("chord"), py::arg("limits") = ShortcutLimits{});
  m.def("clear_shortcut", [](const ShortcutProfile &profile, py::handle command,
                              const ShortcutLimits &caps) {
    return capped_shortcut_call([&] { return clear_shortcut(profile, text(command), caps); });
  }, py::arg("profile"), py::arg("command_id"), py::arg("limits") = ShortcutLimits{});
  m.def("reset_shortcut", [](const ShortcutProfile &profile, py::handle command,
                              const ShortcutLimits &caps) {
    return capped_shortcut_call([&] { return reset_shortcut(profile, text(command), caps); });
  }, py::arg("profile"), py::arg("command_id"), py::arg("limits") = ShortcutLimits{});
  m.def("reset_shortcuts", [](const ShortcutProfile &profile, const ShortcutLimits &caps) {
    return capped_shortcut_call([&] { return reset_shortcuts(profile, caps); });
  },
        py::arg("profile"), py::arg("limits") = ShortcutLimits{});
  m.def("evaluate_shortcut", [](const ShortcutProfile &profile, py::handle platform,
                                 const ShortcutEvent &input, const ShortcutContext &owner) {
    return evaluate_shortcut(profile, enumeration(platform, ShortcutPlatform::MacOS), input, owner);
  }, py::arg("profile"), py::arg("platform"), py::arg("event"), py::arg("context"),
     "Pure native decision for caller-supplied state; does not deliver OS or ImGui input.");
  m.def("evaluate_shortcuts", [](const ShortcutProfile &profile, py::handle platform,
                                  py::handle inputs, const ShortcutContext &owner) {
    if (!PyList_Check(inputs.ptr()))
      throw py::type_error("Shortcut frame events must be a list");
    std::vector<ShortcutEvent> copied;
    copied.reserve(static_cast<std::size_t>(PyList_GET_SIZE(inputs.ptr())));
    for (auto item : py::reinterpret_borrow<py::list>(inputs)) {
      if (!py::isinstance<ShortcutEvent>(item))
        throw py::type_error("Shortcut frame events must contain ShortcutEvent values");
      copied.push_back(py::cast<ShortcutEvent>(item));
    }
    return evaluate_shortcuts(profile, enumeration(platform, ShortcutPlatform::MacOS), copied, owner);
  }, py::arg("profile"), py::arg("platform"), py::arg("events"), py::arg("context"),
     "Pure native frame decision; the caller supplies actual focus and local ownership.");
}
