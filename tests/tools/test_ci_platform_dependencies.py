"""Source-profile policy/transport checks, not native dependency build evidence.

No subprocess or network request is allowed by these tests. Real archive
identities, compiler execution and application qualification remain hosted work.
"""
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import tarfile
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "ci_platform_dependencies_under_test", ROOT / "scripts/ci_platform_dependencies.py")
builder = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(builder)
WINDOWS = "windows-x64-ci-dynamic-source-001"
ARM = "macos-arm64-ci-static-source-001"
INTEL = "macos-x86_64-ci-static-source-001"


class ProfileContracts(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.network = patch.object(builder.urllib.request, "urlopen",
                                    side_effect=AssertionError("Unexpected network access"))
        self.process = patch.object(builder.subprocess, "Popen",
                                    side_effect=AssertionError("Unexpected subprocess"))
        self.network.start(); self.addCleanup(self.network.stop)
        self.process.start(); self.addCleanup(self.process.stop)

    def catalog(self):
        value = json.loads((ROOT / builder.SOURCE_MANIFEST).read_bytes())
        (self.root / "licenses").mkdir()
        for entry in value["dependencies"].values():
            for notice in entry["notices"]:
                shutil.copyfile(ROOT / "licenses" / notice, self.root / "licenses" / notice)
        self.write_catalog(value)
        return value

    def write_catalog(self, value):
        (self.root / builder.SOURCE_MANIFEST).write_text(json.dumps(value), encoding="utf-8")

    def archive(self, rows, name="source.tar.gz"):
        destination = self.root / name
        with tarfile.open(destination, "w:gz") as bundle:
            for path, raw, kind in rows:
                member = tarfile.TarInfo(path)
                if kind == "file":
                    member.size = len(raw)
                    bundle.addfile(member, io.BytesIO(raw))
                elif kind == "directory":
                    member.type = tarfile.DIRTYPE
                    bundle.addfile(member)
                else:
                    member.type = tarfile.SYMTYPE if kind == "symlink" else tarfile.LNKTYPE
                    member.linkname = "../../outside"
                    bundle.addfile(member)
        return destination

    def test_profiles_are_closed_native_architectures(self):
        self.assertEqual(set(builder.PROFILES), {WINDOWS, ARM, INTEL})
        for name, system, architecture in ((WINDOWS, "win32", "AMD64"),
                                            (ARM, "darwin", "arm64"),
                                            (INTEL, "darwin", "x86_64")):
            value = builder.profile_definition(name)
            builder.require_host(value, system, architecture, 8)
            with self.assertRaisesRegex(ValueError, "native 64-bit"):
                builder.require_host(value, system, architecture, 4)
            with self.assertRaisesRegex(ValueError, "native 64-bit"):
                builder.require_host(value, "linux", architecture, 8)
        with self.assertRaisesRegex(ValueError, "native 64-bit"):
            builder.require_host(builder.profile_definition(ARM), "darwin", "x86_64", 8)
        for name in ("windows-x64-dynamic-release-001", "linux-x86_64-ci-static-source-001", "latest"):
            with self.assertRaisesRegex(ValueError, "Unknown"):
                builder.profile_definition(name)

    def test_wrong_host_refuses_before_output_or_acquisition(self):
        output = self.root / "never-created"
        with patch.object(builder.sys, "platform", "linux"):
            with self.assertRaisesRegex(ValueError, "native 64-bit"):
                builder.build(output, WINDOWS)
        self.assertFalse(output.exists())

    def test_windows_dynamic_roles_are_complete_and_exact(self):
        value = builder.profile_definition(WINDOWS)
        self.assertEqual(builder.library_layout(value), {
            "zlib": {"link": "lib/zlib.lib", "runtime": "bin/zlib1.dll"},
            "jpeg": {"link": "lib/jpeg.lib", "runtime": "bin/jpeg62.dll"},
            "lzma": {"link": "lib/lzma.lib", "runtime": "bin/liblzma.dll"},
            "png": {"link": "lib/libpng16.lib", "runtime": "bin/libpng16.dll"},
            "glfw": {"link": "lib/glfw3dll.lib", "runtime": "bin/glfw3.dll"},
            "tiff": {"link": "lib/tiff.lib", "runtime": "bin/tiff.dll"}})
        common = builder.common_definitions(value, self.root)
        self.assertEqual(common["CMAKE_MSVC_RUNTIME_LIBRARY"], "MultiThreadedDLL")
        self.assertEqual(common["CMAKE_BUILD_TYPE"], "Release")
        self.assertNotIn("CMAKE_OSX_ARCHITECTURES", common)
        definitions = builder.dependency_definitions(value, self.root, "chosen-nasm")
        self.assertEqual(definitions["libjpeg-turbo"]["ENABLE_SHARED"], "ON")
        self.assertEqual(definitions["libjpeg-turbo"]["ENABLE_STATIC"], "OFF")
        self.assertEqual(definitions["glfw"]["BUILD_SHARED_LIBS"], "ON")
        self.assertEqual(definitions["png"]["PNG_STATIC"], "OFF")
        self.assertEqual(definitions["tiff"]["BUILD_SHARED_LIBS"], "ON")

    def test_macos_static_architecture_deployment_and_simd_are_explicit(self):
        for name, architecture in ((ARM, "arm64"), (INTEL, "x86_64")):
            value = builder.profile_definition(name)
            common = builder.common_definitions(value, self.root)
            self.assertEqual(common["CMAKE_OSX_ARCHITECTURES"], architecture)
            self.assertEqual(common["CMAKE_OSX_DEPLOYMENT_TARGET"], "11.0")
            self.assertTrue(all(set(row) == {"link"} and row["link"].endswith(".a")
                                for row in builder.library_layout(value).values()))
            definitions = builder.dependency_definitions(value, self.root, "chosen-nasm")
            self.assertEqual(definitions["glfw"]["GLFW_BUILD_COCOA"], "ON")
            self.assertNotIn("GLFW_BUILD_X11", definitions["glfw"])
            self.assertEqual(definitions["libjpeg-turbo"]["ENABLE_STATIC"], "ON")
            self.assertEqual("CMAKE_ASM_NASM_COMPILER" in definitions["libjpeg-turbo"], name == INTEL)
        with self.assertRaisesRegex(ValueError, "requires NASM"):
            builder.dependency_definitions(builder.profile_definition(INTEL), self.root, None)
        builder.dependency_definitions(builder.profile_definition(ARM), self.root, None)

    def test_codec_options_do_not_enable_unreviewed_formats_or_tools(self):
        for name in builder.PROFILES:
            definitions = builder.dependency_definitions(builder.profile_definition(name), self.root, "nasm")
            self.assertEqual(set(definitions), set(builder.DEPENDENCIES) - {"stb"})
            for key in ("zlib", "jpeg", "old-jpeg", "lzma"):
                self.assertEqual(definitions["tiff"][key], "ON")
            for key in ("libdeflate", "jbig", "zstd", "webp", "lerc", "tiff-tools", "tiff-tests",
                        "tiff-contrib", "tiff-docs", "cxx"):
                self.assertEqual(definitions["tiff"][key], "OFF")
            self.assertEqual(definitions["libjpeg-turbo"]["WITH_TURBOJPEG"], "OFF")
            self.assertEqual(definitions["libjpeg-turbo"]["WITH_SIMD"], "ON")
            self.assertEqual(definitions["png"]["ZLIB_INCLUDE_DIR"], str(self.root / "include"))

    def test_actual_reviewed_catalog_and_json_vendor_are_read_only_inputs(self):
        before = (ROOT / builder.SOURCE_MANIFEST).read_bytes()
        dependencies, identity = builder.read_catalog(ROOT)
        self.assertEqual(set(dependencies), set(builder.DEPENDENCIES))
        self.assertEqual(identity["sha256"], hashlib.sha256(before).hexdigest())
        self.assertEqual((ROOT / builder.SOURCE_MANIFEST).read_bytes(), before)
        vendor = builder.read_json_vendor(ROOT)
        self.assertEqual(vendor["repository_path"], "third_party/nlohmann_json/SOURCE.json")
        self.assertIn("include/nlohmann/json.hpp", vendor["files_sha256"])

    def test_changed_catalog_fields_are_refused_without_acquisition(self):
        original = self.catalog()
        mutations = (
            lambda x: x.update(profile=WINDOWS),
            lambda x: x["dependencies"].pop("stb"),
            lambda x: x["dependencies"]["tiff"].update(commercial_use=False),
            lambda x: x["dependencies"]["tiff"].update(selected_license="CC-BY-NC-4.0"),
            lambda x: x["dependencies"]["tiff"].update(version="4.7.2"),
            lambda x: x["dependencies"]["tiff"]["archive"].update(bytes=True),
            lambda x: x["dependencies"]["tiff"]["archive"].update(url="http://example.invalid/file"),
            lambda x: x["dependencies"]["tiff"]["archive"].update(sha256="bad"),
            lambda x: x["dependencies"]["tiff"].update(notices={"../outside": "0" * 64}),
        )
        for index, mutation in enumerate(mutations):
            with self.subTest(index=index):
                changed = copy.deepcopy(original)
                mutation(changed)
                self.write_catalog(changed)
                with self.assertRaises(ValueError):
                    builder.read_catalog(self.root)
        self.write_catalog(original)
        notice = next(iter(original["dependencies"]["tiff"]["notices"]))
        (self.root / "licenses" / notice).write_bytes(b"changed notice")
        with self.assertRaisesRegex(ValueError, "notice changed"):
            builder.read_catalog(self.root)

    def test_effective_cache_must_match_every_requested_setting(self):
        path = self.root / "CMakeCache.txt"
        path.write_text('// comment\n# comment\nBUILD_SHARED_LIBS:BOOL=ON\n'
                        'ZLIB_LIBRARY_RELEASE:FILEPATH=C:/prefix/lib/zlib.lib\n', encoding="utf-8")
        actual = builder.cache_values(path)
        requested = {"BUILD_SHARED_LIBS": "ON", "ZLIB_LIBRARY_RELEASE": r"C:\prefix\lib\zlib.lib"}
        self.assertEqual(builder.check_effective(actual, requested), actual)
        for changed in ({**requested, "BUILD_SHARED_LIBS": "OFF"}, {**requested, "MISSING": "ON"}):
            with self.assertRaisesRegex(ValueError, "Effective CMake"):
                builder.check_effective(actual, changed)

    def test_cmake_control_syntax_paths_are_refused(self):
        for name in ("semi;colon", "line\nbreak", 'double"quote'):
            with self.subTest(name=name):
                with self.assertRaisesRegex(ValueError, "CMake list/control"):
                    builder.checked_path(self.root / name)

    def test_archive_escape_links_case_collisions_are_refused_before_extract(self):
        cases = [
            [("../escape", b"bad", "file")], [("/absolute", b"bad", "file")],
            [("C:/absolute", b"bad", "file")], [("root/link", b"", "symlink")],
            [("root/link", b"", "hardlink")],
            [("root/name", b"one", "file"), ("root/NAME", b"two", "file")],
        ]
        for index, rows in enumerate(cases):
            with self.subTest(index=index):
                archive = self.archive(rows, f"bad-{index}.tar.gz")
                destination = self.root / f"extract-{index}"
                with self.assertRaisesRegex(ValueError, "Unsafe|duplicate"):
                    builder.extract_source(archive, destination)
                self.assertFalse(destination.exists())

    def test_valid_verified_cached_source_needs_no_network(self):
        archive = self.archive([("root", b"", "directory"), ("root/file.txt", b"literal bytes", "file")])
        cache = self.root / "cache"; cache.mkdir()
        shutil.copyfile(archive, cache / "zlib.tar.gz")
        downloads = self.root / "downloads"; downloads.mkdir()
        sources = self.root / "sources"; sources.mkdir()
        dependency = {"archive": {"url": "https://example.invalid/pinned", **builder.pin(archive)}}
        source = builder.acquire("zlib", dependency, downloads, sources, cache)
        self.assertEqual((source / "file.txt").read_bytes(), b"literal bytes")
        self.assertEqual(builder.pin(downloads / "zlib.tar.gz"), builder.pin(archive))

    def test_changed_cache_and_oversized_download_refuse_before_extraction(self):
        cache = self.root / "cache"; cache.mkdir()
        (cache / "zlib.tar.gz").write_bytes(b"wrong")
        downloads = self.root / "downloads"; downloads.mkdir()
        sources = self.root / "sources"; sources.mkdir()
        dependency = {"archive": {"url": "https://example.invalid/pinned", "bytes": 4, "sha256": "0" * 64}}
        with self.assertRaisesRegex(ValueError, "Cached dependency fingerprint"):
            builder.acquire("zlib", dependency, downloads, sources, cache)
        self.assertEqual(list(downloads.iterdir()), [])
        self.assertEqual(list(sources.iterdir()), [])
        with patch.object(builder.urllib.request, "urlopen", return_value=io.BytesIO(b"12345")):
            with self.assertRaisesRegex(ValueError, "Downloaded dependency fingerprint"):
                builder.acquire("zlib", dependency, downloads, sources)
        self.assertEqual(list(downloads.iterdir()), [])
        self.assertEqual(list(sources.iterdir()), [])

    def test_prefix_manifest_is_complete_regular_and_does_not_self_hash(self):
        (self.root / "lib").mkdir(); (self.root / "include").mkdir(); (self.root / "share").mkdir()
        (self.root / "lib/tiff.lib").write_bytes(b"link")
        (self.root / "include/tiff.h").write_bytes(b"header")
        (self.root / builder.CERTIFICATE).write_bytes(b"not self hashed")
        rows = builder.prefix_files(self.root)
        self.assertEqual([row["path"] for row in rows], ["include/tiff.h", "lib/tiff.lib"])
        self.assertEqual([row["kind"] for row in rows], ["header", "link"])
        for row in rows:
            self.assertEqual({key: row[key] for key in ("bytes", "sha256")}, builder.pin(self.root / row["path"]))
        original = builder.is_link
        with patch.object(builder, "is_link", side_effect=lambda p: p.name == "tiff.lib" or original(p)):
            with self.assertRaisesRegex(ValueError, "regular, non-linked"):
                builder.prefix_files(self.root)

    def test_static_cleanup_is_only_known_unselected_zlib_files(self):
        (self.root / "lib").mkdir()
        (self.root / "lib/libz.a").write_bytes(b"selected archive")
        (self.root / "lib/libz.1.3.1.dylib").write_bytes(b"unselected dylib")
        changes = builder.normalize_static_prefix(self.root)
        self.assertEqual(changes[0]["path"], "lib/libz.1.3.1.dylib")
        self.assertEqual((self.root / "lib/libz.a").read_bytes(), b"selected archive")
        self.assertFalse((self.root / "lib/libz.1.3.1.dylib").exists())
        (self.root / "lib/foreign.dylib").write_bytes(b"not authorized")
        with self.assertRaisesRegex(ValueError, "Unexpected shared"):
            builder.normalize_static_prefix(self.root)
        self.assertTrue((self.root / "lib/foreign.dylib").is_file())

    def test_delivery_rejects_missing_roles_and_unselected_runtimes(self):
        for name in (WINDOWS, ARM):
            profile = builder.profile_definition(name)
            rows = [{"path": path} for role in builder.library_layout(profile).values() for path in role.values()]
            builder.check_delivery(profile, rows)
            with self.assertRaisesRegex(ValueError, "missing a selected"):
                builder.check_delivery(profile, rows[1:])
            for extra in ("bin/unselected.dll", "lib/libforeign.dylib", "bin/unrequested.exe"):
                with self.subTest(profile=name, extra=extra):
                    with self.assertRaisesRegex(ValueError, "unselected runtime/tool"):
                        builder.check_delivery(profile, rows + [{"path": extra}])

    def test_all_tiff_config_spellings_import_same_selected_closure(self):
        for name in (WINDOWS, ARM):
            with self.subTest(profile=name):
                prefix = self.root / name
                folder = prefix / "lib/cmake/tiff"; folder.mkdir(parents=True)
                (folder / "TiffConfigVersion.cmake").write_bytes(b"original version proof\n")
                builder.write_tiff_configs(prefix, builder.profile_definition(name))
                values = [(folder / (base + ".cmake")).read_text()
                          for base in ("tiff-config", "TiffConfig", "TIFFConfig")]
                self.assertEqual(values, [values[0]] * 3)
                self.assertIn('include("${CMAKE_CURRENT_LIST_DIR}/TiffTargets.cmake")', values[0])
                self.assertEqual("IMPORTED_IMPLIB" in values[0], name == WINDOWS)
                self.assertEqual("INTERFACE_LINK_LIBRARIES m" in values[0], name != WINDOWS)
                self.assertEqual((folder / "TIFFConfigVersion.cmake").read_bytes(), b"original version proof\n")

    def test_import_smoke_uses_all_three_real_config_routes_and_executables(self):
        calls = []
        builder.check_tiff_imports(self.root / "prefix", self.root / "probe",
                                  builder.profile_definition(WINDOWS),
                                  lambda label, argv: calls.append((label, [str(value) for value in argv])))
        self.assertEqual(len(calls), 9)
        self.assertEqual([label for label, _ in calls if label.endswith("-run")],
                         ["tiff-import-tiff-run", "tiff-import-Tiff-run", "tiff-import-TIFF-run"])
        self.assertTrue(all(argv[0].endswith("consumer.exe") for label, argv in calls if label.endswith("-run")))
        source = (self.root / "probe/source/consumer.c").read_text()
        for constant in ("LZW", "PACKBITS", "ADOBE_DEFLATE", "JPEG", "LZMA"):
            self.assertIn("COMPRESSION_" + constant, source)


if __name__ == "__main__":
    unittest.main()
