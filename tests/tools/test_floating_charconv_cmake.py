"""Source/configuration guard for every private floating-charconv owner.

This checks the repository's literal CMake source lists. It does not configure a
compiler or qualify the macOS 11 backend; the actual Apple build remains needed.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
EXPECTED_OWNERS = {
    "src/native/execution.cpp": {"imagejxx_core"},
    "src/io/layout.cpp": {"imagejxx_io"},
    "src/desktop/channel_display_panel.cpp": {"imagejxx_desktop"},
    "src/desktop/gaussian_panel.cpp": {"imagejxx_desktop"},
    "src/desktop/histogram_panel.cpp": {"imagejxx_desktop"},
    "src/desktop/rank_panel.cpp": {
        "imagejxx_desktop", "imagejxx_rank_panel_tests"
    },
    "src/desktop/threshold_panel.cpp": {"imagejxx_desktop"},
}


class FloatingCharconvCMakeTests(unittest.TestCase):
    def test_example_observer_headers_and_all_literal_compilation_owners(self):
        expected = {
            "rank_observations.hpp": {"tests/native/test_rank_host.cpp", "examples/cpp/rank_command.cpp", "tests/native/test_observation_decimal.cpp"},
            "saved_roi_geometry_observations.hpp": {"tests/native/test_saved_roi_geometry_host.cpp", "examples/cpp/saved_roi_geometry.cpp", "tests/native/test_observation_decimal.cpp"},
            "shortcut_observations.hpp": {"examples/cpp/history_settings.cpp", "examples/cpp/shortcuts.cpp", "tests/native/test_observation_decimal.cpp"},
        }
        actual = {name: set() for name in expected}
        for directory in (ROOT / "examples/cpp", ROOT / "tests/native"):
            for path in directory.rglob("*.cpp"):
                text = path.read_text(encoding="utf-8-sig")
                for name in expected:
                    if re.search(r'#\s*include\s+"[^"\n]*\b' + re.escape(name) + r'"', text):
                        actual[name].add(path.relative_to(ROOT).as_posix())
        self.assertEqual(actual, expected)
        required = set().union(*expected.values())
        owners = {name: set() for name in required}
        for name in ("CMakeLists.txt", "examples/cpp/CMakeLists.txt"):
            path = ROOT / name
            text = re.sub(r"(?m)#.*$", "", path.read_text(encoding="utf-8"))
            for target, arguments in re.findall(r"\badd_executable\(\s*(\w+)\b([^)]*)\)", text):
                for source in re.findall(r"\b[^\s();\"]+\.cpp\b", arguments):
                    logical = (path.parent / source).relative_to(ROOT).as_posix()
                    if logical in owners:
                        owners[logical].add(target)
        self.assertTrue(all(len(targets) == 1 for targets in owners.values()), owners)
        for name in expected:
            text = (ROOT / "examples/cpp" / name).read_text(encoding="utf-8")
            self.assertIn('#include "observation_decimal.hpp"', text)
            self.assertIn("observation_decimal::general17(", text)
            self.assertNotIn("std::to_chars(", text)
        helper = (ROOT / "examples/cpp/observation_decimal.hpp").read_text(encoding="utf-8")
        self.assertNotRegex(helper, r'#\s*include\s+"|#\s*include\s+<boost/')
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("add_test(NAME native_observation_decimal COMMAND imagejxx_observation_decimal_tests)", cmake)
        self.assertNotRegex(cmake, r"target_link_libraries\(\s*imagejxx_observation_decimal_tests\b")

    def test_all_seven_consumers_and_eight_compilation_memberships_are_private(self):
        consumers = {
            path.relative_to(ROOT).as_posix()
            for path in (ROOT / "src").rglob("*.cpp")
            if re.search(
                r'^\s*#\s*include\s+["<][^">]*floating_charconv\.hpp[">]',
                path.read_text(encoding="utf-8"), re.MULTILINE
            )
        }
        self.assertEqual(consumers, set(EXPECTED_OWNERS))
        # The audited declarations use literal source paths; fail when an owner
        # is missing instead of guessing the value of a future CMake variable.
        cmake = re.sub(
            r"(?m)#.*$", "", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        )
        owners = {source: set() for source in consumers}
        for target, arguments in re.findall(
            r"\b(?:add_library|add_executable|target_sources)\s*"
            r"\(\s*([A-Za-z_][A-Za-z_0-9]*)\b([^)]*)\)", cmake
        ):
            for source in set(re.findall(r"\bsrc/[^\s();\"]+\.cpp\b", arguments)):
                if source in owners:
                    owners[source].add(target)
        self.assertEqual(owners, EXPECTED_OWNERS)
        self.assertEqual(sum(map(len, owners.values())), 8)

        call = r"\bimagejxx_use_private_charconv\(\s*([A-Za-z_][A-Za-z_0-9]*)\s*\)"
        all_calls = re.findall(call, cmake)
        apple_calls = [
            target
            for body in re.findall(r"\bif\(APPLE\)(.*?)\bendif\(\)", cmake, re.DOTALL)
            for target in re.findall(call, body)
        ]
        required = set().union(*owners.values())
        self.assertCountEqual(all_calls, sorted(required))
        self.assertCountEqual(apple_calls, all_calls)

    def test_standalone_rank_panel_keeps_existing_core_backend_ownership(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        block = re.search(
            r"add_executable\(imagejxx_rank_panel_tests\b.*?"
            r"(?=\n\s*add_executable\()", cmake, re.DOTALL
        )
        self.assertIsNotNone(block)
        text = block.group(0)
        self.assertRegex(
            text,
            r"target_link_libraries\(imagejxx_rank_panel_tests\s+PRIVATE\s+"
            r"imagejxx::core\s+imgui::imgui\s+nlohmann_json::nlohmann_json\)",
        )
        self.assertNotIn("TARGET_OBJECTS", text)
        self.assertNotIn("imagejxx_require_charconv_objects", text)
        self.assertNotIn("_imagejxx_charconv_strict_fp", text)
        self.assertNotIn("target_compile_options", text)

    def test_shared_helper_adds_only_private_compile_requirements(self):
        helper = (ROOT / "cmake/FloatingCharconv.cmake").read_text(encoding="utf-8")
        configure = re.search(
            r"function\(_imagejxx_configure_charconv target\)(.*?)endfunction\(\)",
            helper, re.DOTALL,
        )
        use = re.search(
            r"function\(imagejxx_use_private_charconv target\)(.*?)endfunction\(\)",
            helper, re.DOTALL,
        )
        self.assertIsNotNone(configure)
        self.assertIsNotNone(use)
        self.assertRegex(
            configure.group(1),
            r'target_include_directories\("\$\{target\}"\s+SYSTEM\s+PRIVATE\b',
        )
        self.assertRegex(
            use.group(1),
            r'target_compile_definitions\("\$\{target\}"\s+PRIVATE\s+'
            r"IMAGEJXX_PORTABLE_FLOAT_CHARCONV=1\)",
        )
        for body in (configure.group(1), use.group(1)):
            self.assertNotRegex(body, r"\b(?:PUBLIC|INTERFACE)\b")
            self.assertNotIn("_imagejxx_charconv_strict_fp", body)


if __name__ == "__main__":
    unittest.main()
