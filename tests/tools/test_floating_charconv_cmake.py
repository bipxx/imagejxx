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
