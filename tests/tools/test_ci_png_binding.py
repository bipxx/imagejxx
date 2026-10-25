"""Configure-only controls for the selected source PNG identity.

The archives, png.h and FindZLIB module are visibly synthetic. The real CMake
FindPNG module performs discovery in a project(NONE) fixture; no compiler,
dependency build, SDK, native library load or product execution is involved.
Prefix-certificate verification is separate and is not claimed by these tests.
Run with ``python -I -B tests/tools/test_ci_png_binding.py``.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
from types import SimpleNamespace
import unittest


ROOT = Path(__file__).resolve().parents[2]
GUARD = ROOT / "cmake/SourceTiffRuntime.cmake"
ARM = "macos-arm64-ci-static-source-001"
INTEL = "macos-x86_64-ci-static-source-001"
WINDOWS = "windows-x64-ci-dynamic-source-001"
ARCHIVE = b"SYNTHETIC archive identity fixture; never linked or loaded.\n"
HEADER = '#define PNG_LIBPNG_VER_STRING "1.6.50"\n'


def literal(value):
    """Quote only authored fixture strings, without CMake variable expansion."""
    text = value.as_posix() if isinstance(value, Path) else value
    if "]=]" in text:
        raise ValueError("Unexpected CMake fixture delimiter")
    return "[=[" + text + "]=]"


@unittest.skipUnless(shutil.which("cmake"), "CMake metadata configure executable is unavailable")
class SourcePngBinding(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="imagejxx-png-binding-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve(strict=True)
        self.serial = 0

    def fixture(self):
        self.serial += 1
        directory = self.root / ("case-" + str(self.serial))
        prefix = directory / "prefix"
        source = directory / "source"
        modules = source / "modules"
        for path in (prefix / "lib", prefix / "include", modules):
            path.mkdir(parents=True, exist_ok=True)
        for name in ("libpng.a", "libpng16.a", "libz.a"):
            (prefix / "lib" / name).write_bytes(ARCHIVE)
        (prefix / "include/png.h").write_text(HEADER, encoding="utf-8")
        (prefix / "include/zlib.h").write_text("/* synthetic ZLIB metadata */\n", encoding="utf-8")
        (modules / "FindZLIB.cmake").write_text(
            "# SYNTHETIC dependency metadata, not a ZLIB implementation.\n"
            "set(ZLIB_FOUND TRUE)\n"
            f"set(ZLIB_INCLUDE_DIR {literal(prefix / 'include')})\n"
            f"set(ZLIB_LIBRARY {literal(prefix / 'lib/libz.a')})\n"
            "if(NOT TARGET ZLIB::ZLIB)\n"
            "  add_library(ZLIB::ZLIB UNKNOWN IMPORTED)\n"
            "  set_target_properties(ZLIB::ZLIB PROPERTIES\n"
            '    IMPORTED_LOCATION "${ZLIB_LIBRARY}"\n'
            '    INTERFACE_INCLUDE_DIRECTORIES "${ZLIB_INCLUDE_DIR}")\n'
            "endif()\n", encoding="utf-8")
        return SimpleNamespace(directory=directory, prefix=prefix, source=source,
                               modules=modules, cache=directory / "cache")

    def configure(self, fixture, *, profile=ARM, bind=True, overrides=None, discover=True):
        text = (
            "cmake_minimum_required(VERSION 3.18)\n"
            "project(SyntheticSourcePngIdentity NONE)\n"
            "# Configure metadata only; no compiler or host-profile qualification.\n"
            "set(CMAKE_BUILD_TYPE Release)\n"
            'set(CMAKE_FIND_LIBRARY_PREFIXES "lib")\n'
            'set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")\n'
            "set(CMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH FALSE)\n"
            "set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH FALSE)\n"
            "set(CMAKE_FIND_USE_CMAKE_SYSTEM_PATH FALSE)\n"
            "set(CMAKE_FIND_USE_PACKAGE_ROOT_PATH FALSE)\n"
            f"set(CMAKE_PREFIX_PATH {literal(fixture.prefix)})\n"
            f"set(CMAKE_MODULE_PATH {literal(fixture.modules)})\n"
            f"include({literal(GUARD)})\n")
        if profile is not None:
            text += f"set(IMAGEJXX_CI_SOURCE_PROFILE {literal(profile)})\n"
        for name, value in (overrides or {}).items():
            if name not in ("PNG_LIBRARY_RELEASE", "PNG_LIBRARY", "PNG_PNG_INCLUDE_DIR"):
                raise ValueError("Unexpected fixture override")
            kind = "PATH" if name == "PNG_PNG_INCLUDE_DIR" else "FILEPATH"
            text += f'set({name} {literal(value)} CACHE {kind} "Synthetic explicit override")\n'
        if bind:
            text += f"imagejxx_bind_source_png({literal(fixture.prefix)})\n"
        text += (
            'file(WRITE "${CMAKE_BINARY_DIR}/observed.txt"\n'
            '  "bound_release=${PNG_LIBRARY_RELEASE}\\n"\n'
            '  "bound_library=${PNG_LIBRARY}\\n"\n'
            '  "bound_headers=${PNG_PNG_INCLUDE_DIR}\\n")\n')
        if discover:
            text += (
                "find_package(PNG 1.6.50 EXACT REQUIRED MODULE)\n"
                "get_target_property(png_location PNG::PNG IMPORTED_LOCATION)\n"
                "get_target_property(png_release PNG::PNG IMPORTED_LOCATION_RELEASE)\n"
                "get_target_property(png_headers PNG::PNG INTERFACE_INCLUDE_DIRECTORIES)\n"
                'file(APPEND "${CMAKE_BINARY_DIR}/observed.txt"\n'
                '  "release=${PNG_LIBRARY_RELEASE}\\n"\n'
                '  "library=${PNG_LIBRARY}\\n"\n'
                '  "headers=${PNG_PNG_INCLUDE_DIR}\\n"\n'
                '  "include_dirs=${PNG_INCLUDE_DIRS}\\n"\n'
                '  "version=${PNG_VERSION_STRING}\\n"\n'
                '  "target_location=${png_location}\\n"\n'
                '  "target_release=${png_release}\\n"\n'
                '  "target_headers=${png_headers}\\n")\n')
        text += 'file(WRITE "${CMAKE_BINARY_DIR}/accepted.txt" "configure-only\\n")\n'
        (fixture.source / "CMakeLists.txt").write_text(text, encoding="utf-8")
        command = [shutil.which("cmake"), "-S", str(fixture.source), "-B", str(fixture.cache)]
        if shutil.which("ninja"):
            command += ["-G", "Ninja"]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60, check=False)
        return result

    def observed(self, fixture, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((fixture.cache / "accepted.txt").read_text(encoding="utf-8"), "configure-only\n")
        rows = (fixture.cache / "observed.txt").read_text(encoding="utf-8").splitlines()
        values = dict(row.split("=", 1) for row in rows)
        self.assertEqual(len(values), len(rows), "Duplicate configure observation")
        return values

    def refused(self, fixture, result, message):
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(message, result.stdout + result.stderr)
        self.assertFalse((fixture.cache / "observed.txt").exists())
        self.assertFalse((fixture.cache / "accepted.txt").exists())

    def assert_discovery(self, fixture, values, archive):
        expected = (fixture.prefix / "lib" / archive).as_posix()
        headers = (fixture.prefix / "include").as_posix()
        for key in ("release", "library", "target_location", "target_release"):
            self.assertEqual(values[key], expected, key)
        self.assertEqual(values["headers"], headers)
        self.assertEqual(values["version"], "1.6.50")
        for key in ("include_dirs", "target_headers"):
            self.assertEqual(set(values[key].split(";")), {headers}, key)

    def test_native_findpng_prefers_the_same_byte_unversioned_alias_without_binding(self):
        fixture = self.fixture()
        self.assertEqual((fixture.prefix / "lib/libpng.a").read_bytes(),
                         (fixture.prefix / "lib/libpng16.a").read_bytes())
        values = self.observed(fixture, self.configure(fixture, profile=None, bind=False))
        self.assert_discovery(fixture, values, "libpng.a")

    def test_both_mac_profiles_bind_canonical_archive_and_headers_before_native_findpng(self):
        for profile in (ARM, INTEL):
            with self.subTest(profile=profile):
                fixture = self.fixture()
                values = self.observed(fixture, self.configure(fixture, profile=profile))
                self.assertEqual(values["bound_release"], (fixture.prefix / "lib/libpng16.a").as_posix())
                self.assertEqual(values["bound_library"], values["bound_release"])
                self.assertEqual(values["bound_headers"], (fixture.prefix / "include").as_posix())
                self.assert_discovery(fixture, values, "libpng16.a")

    def test_existing_canonical_cache_overrides_are_retained(self):
        fixture = self.fixture()
        overrides = {"PNG_LIBRARY_RELEASE": fixture.prefix / "lib/libpng16.a",
                     "PNG_LIBRARY": fixture.prefix / "lib/libpng16.a",
                     "PNG_PNG_INCLUDE_DIR": fixture.prefix / "include"}
        values = self.observed(fixture, self.configure(fixture, overrides=overrides))
        self.assert_discovery(fixture, values, "libpng16.a")

    def test_default_and_windows_profiles_leave_native_findpng_alias_selection_unchanged(self):
        for profile in (None, "", WINDOWS):
            with self.subTest(profile=profile):
                fixture = self.fixture()
                values = self.observed(fixture, self.configure(fixture, profile=profile))
                self.assertEqual([values[key] for key in ("bound_release", "bound_library", "bound_headers")],
                                 ["", "", ""])
                self.assert_discovery(fixture, values, "libpng.a")

    def test_default_and_windows_noop_does_not_inspect_members_or_replace_overrides(self):
        for profile in (None, WINDOWS):
            with self.subTest(profile=profile):
                fixture = self.fixture()
                (fixture.prefix / "lib/libpng16.a").unlink()
                (fixture.prefix / "include/png.h").unlink()
                overrides = {name: "untouched-" + name for name in
                             ("PNG_LIBRARY_RELEASE", "PNG_LIBRARY", "PNG_PNG_INCLUDE_DIR")}
                values = self.observed(fixture, self.configure(
                    fixture, profile=profile, overrides=overrides, discover=False))
                self.assertEqual(values, {"bound_release": overrides["PNG_LIBRARY_RELEASE"],
                                          "bound_library": overrides["PNG_LIBRARY"],
                                          "bound_headers": overrides["PNG_PNG_INCLUDE_DIR"]})

    def test_foreign_release_and_library_overrides_refuse_even_identical_bytes(self):
        for variable in ("PNG_LIBRARY_RELEASE", "PNG_LIBRARY"):
            with self.subTest(variable=variable):
                fixture = self.fixture()
                foreign = fixture.directory / "foreign-libpng16.a"
                foreign.write_bytes(ARCHIVE)
                result = self.configure(fixture, overrides={variable: foreign})
                self.refused(fixture, result, "Selected source PNG library override differs: " + variable)

    def test_same_prefix_unversioned_alias_cache_override_is_not_silently_replaced(self):
        for variable in ("PNG_LIBRARY_RELEASE", "PNG_LIBRARY"):
            with self.subTest(variable=variable):
                fixture = self.fixture()
                result = self.configure(fixture, overrides={variable: fixture.prefix / "lib/libpng.a"})
                self.refused(fixture, result, "Selected source PNG library override differs: " + variable)

    def test_foreign_headers_refuse_even_identical_png_version(self):
        fixture = self.fixture()
        foreign = fixture.directory / "foreign-include"
        foreign.mkdir()
        (foreign / "png.h").write_text(HEADER, encoding="utf-8")
        result = self.configure(fixture, overrides={"PNG_PNG_INCLUDE_DIR": foreign})
        self.refused(fixture, result, "Selected source PNG header override differs")

    def test_missing_or_directory_canonical_members_refuse_before_discovery(self):
        for member in ("lib/libpng16.a", "include/png.h"):
            for directory in (False, True):
                with self.subTest(member=member, directory=directory):
                    fixture = self.fixture()
                    path = fixture.prefix / member
                    path.unlink()
                    if directory:
                        path.mkdir()
                    result = self.configure(fixture)
                    self.refused(fixture, result, "Selected source PNG canonical archive/header is unavailable")


class PngBindingCallerOrdering(unittest.TestCase):
    def test_selected_ui_profile_verifies_tiff_before_binding_before_findpng(self):
        source = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        discovery = source.index("  find_package(glfw3 3.4.0 EXACT CONFIG REQUIRED)\n")
        begin = source.rfind("if(IMAGEJXX_BUILD_CPP_UI OR IMAGEJXX_BUILD_PYTHON_UI)\n", 0, discovery)
        self.assertGreaterEqual(begin, 0)
        end = source.index("  add_library(imagejxx_desktop STATIC", begin)
        prefix = source[begin:end]
        guarded = (
            "  if(IMAGEJXX_CI_SOURCE_PROFILE)\n"
            "    # Full I/O profiles already verified this prefix. An explicit UI-only\n"
            "    # source profile must verify it before influencing dependency discovery.\n"
            "    if(NOT IMAGEJXX_CI_SOURCE_DEP_PREFIX)\n"
            "      find_package(TIFF 4.7.1 EXACT REQUIRED)\n"
            "      include(cmake/SourceTiffRuntime.cmake)\n"
            "      imagejxx_checked_selected_tiff_runtime(IMAGEJXX_SOURCE_DESKTOP_RUNTIME)\n"
            "    endif()\n"
            '    imagejxx_bind_source_png("${IMAGEJXX_CI_SOURCE_DEP_PREFIX}")\n'
            "  endif()\n")
        self.assertEqual(prefix.count(guarded), 1)
        # Removing the explicitly guarded source-profile addition retains the
        # original default-profile dependency-discovery sequence byte for byte.
        self.assertEqual(prefix.replace(guarded, "", 1),
            "if(IMAGEJXX_BUILD_CPP_UI OR IMAGEJXX_BUILD_PYTHON_UI)\n"
            "  find_package(glfw3 3.4.0 EXACT CONFIG REQUIRED)\n"
            "  find_package(nlohmann_json 3.12.0 EXACT CONFIG REQUIRED)\n"
            "  find_package(OpenGL REQUIRED)\n"
            "  include(cmake/ImGui.cmake)\n"
            "  find_package(ZLIB 1.3.1 EXACT REQUIRED)\n"
            "  find_package(PNG 1.6.50 EXACT REQUIRED)\n"
            "  find_path(IMAGEJXX_STB_INCLUDE_DIR stb_image.h PATH_SUFFIXES stb REQUIRED)\n"
            "  find_package(TIFF 4.7.1 EXACT QUIET)\n")


if __name__ == "__main__":
    unittest.main()
