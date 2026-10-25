"""Source-prefix transport controls; synthetic files are not dependency builds.

The fixtures copy the real reviewed catalog and builder, then write visibly
synthetic libraries, headers and cache evidence. They exercise certificate and
post-configuration integrity only. No dependency is downloaded, compiled,
loaded or executed. Optional CMake checks use script mode or project(NONE)
metadata-only configure/install; they never enable a compiler. Root
verification owns execution of this test module.
"""
from copy import deepcopy
import errno
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
VERIFY_SCRIPT = ROOT / "scripts/verify_ci_platform_dependencies.py"
SPEC = importlib.util.spec_from_file_location("ci_platform_runtime_under_test", VERIFY_SCRIPT)
verify = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(verify)

WINDOWS = "windows-x64-ci-dynamic-source-001"
ARM = "macos-arm64-ci-static-source-001"
INTEL = "macos-x86_64-ci-static-source-001"
CERTIFICATE = "share/imagejxx-ci-build.json"
BUILDER_COPY = "share/imagejxx-ci/ci_platform_dependencies.py"
CATALOG_COPY = "share/imagejxx-ci/source-catalog.json"
DEPENDENCIES = ("zlib", "libjpeg-turbo", "liblzma", "png", "glfw", "tiff")
WINDOWS_ROLES = {
    "zlib": {"link": "lib/zlib.lib", "runtime": "bin/zlib1.dll"},
    "jpeg": {"link": "lib/jpeg.lib", "runtime": "bin/jpeg62.dll"},
    "lzma": {"link": "lib/lzma.lib", "runtime": "bin/liblzma.dll"},
    "png": {"link": "lib/libpng16.lib", "runtime": "bin/libpng16.dll"},
    "glfw": {"link": "lib/glfw3dll.lib", "runtime": "bin/glfw3.dll"},
    "tiff": {"link": "lib/tiff.lib", "runtime": "bin/tiff.dll"},
}
STATIC_ROLES = {
    "zlib": {"link": "lib/libz.a"}, "jpeg": {"link": "lib/libjpeg.a"},
    "lzma": {"link": "lib/liblzma.a"}, "png": {"link": "lib/libpng16.a"},
    "glfw": {"link": "lib/libglfw3.a"}, "tiff": {"link": "lib/libtiff.a"},
}
PROFILES = {
    WINDOWS: ("win32", "x86_64", "shared"),
    ARM: ("darwin", "arm64", "static"),
    INTEL: ("darwin", "x86_64", "static"),
}


def identity(path):
    raw = path.read_bytes()
    return {"bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


class PrefixTransport(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="imagejxx runtime transport ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.catalog = self.root / "reviewed-source-catalog.json"
        self.builder = self.root / "reviewed-builder.py"
        shutil.copyfile(ROOT / "licenses/ci-linux-source-dependencies.json", self.catalog)
        shutil.copyfile(ROOT / "scripts/ci_platform_dependencies.py", self.builder)
        self.serial = 0

    def prepared(self, profile=WINDOWS):
        self.serial += 1
        prefix = self.root / ("synthetic prefix " + str(self.serial))
        prefix.mkdir()
        system, architecture, linkage = PROFILES[profile]
        roles = deepcopy(WINDOWS_ROLES if system == "win32" else STATIC_ROLES)
        members = {}

        def write(relative, raw, kind):
            target = prefix / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(raw)
            members[relative] = {"path": relative, "kind": kind, **identity(target)}
            return {"path": relative, **identity(target)}

        write(BUILDER_COPY, self.builder.read_bytes(), "metadata")
        write(CATALOG_COPY, self.catalog.read_bytes(), "metadata")
        for dependency, library_roles in roles.items():
            for kind, relative in library_roles.items():
                write(relative, ("SYNTHETIC TRANSPORT ONLY " + dependency + " " + kind + "\n").encode(), kind)
        write("include/tiffio.h", b"/* synthetic header; never compiled */\n", "header")
        write("include/GLFW/glfw3.h", b"/* synthetic GLFW header; never compiled */\n", "header")
        # These are the original producer's native-platform identities. They
        # deliberately differ from the transferred temporary prefix and are
        # synthetic claims, not executed tool binaries or a qualified compiler.
        source_base = "C:/synthetic-ci-source" if system == "win32" else "/synthetic-ci-source"
        original_prefix = source_base + "/prefix"

        def claimed_tool(relative, field="path"):
            raw = ("SYNTHETIC TOOL IDENTITY " + relative).encode()
            return {field: source_base + "/tools/" + relative, "bytes": len(raw),
                    "sha256": hashlib.sha256(raw).hexdigest()}

        toolchain = {"python": {"version": "3.12.0 (synthetic transport only)",
                                **claimed_tool("python.exe" if system == "win32" else "python3")}}
        tools = ["cmake", "ninja"]
        if system == "darwin":
            tools.append("xcrun")
        if architecture == "x86_64":
            tools.append("nasm")
        for name in tools:
            version = write("share/imagejxx-ci/" + name + "-version.txt",
                            ("SYNTHETIC " + name + " version evidence; never executed\n").encode(), "metadata")
            toolchain[name] = {**claimed_tool(name + (".exe" if system == "win32" else "")),
                               "version_evidence": version}
        sdk_path = source_base + "/SDK/MacOSX.sdk" if system == "darwin" else ""
        if system == "darwin":
            toolchain["sdk"] = {"path": sdk_path, "version": "11.0",
                "settings": write("share/imagejxx-ci/SDKSettings.json",
                                  b'{"Version":"11.0","fixture":"synthetic transport only"}\n', "metadata")}

        common = {"CMAKE_BUILD_TYPE": "Release", "CMAKE_POSITION_INDEPENDENT_CODE": "ON",
                  "CMAKE_INSTALL_LIBDIR": "lib", "CMAKE_INSTALL_PREFIX": original_prefix,
                  "CMAKE_PREFIX_PATH": original_prefix, "CMAKE_FIND_USE_PACKAGE_REGISTRY": "OFF",
                  "CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY": "OFF",
                  "CMAKE_MAKE_PROGRAM": toolchain["ninja"]["path"]}
        if system == "win32":
            common.update(CMAKE_POLICY_DEFAULT_CMP0091="NEW", CMAKE_MSVC_RUNTIME_LIBRARY="MultiThreadedDLL")
        else:
            common.update(CMAKE_OSX_ARCHITECTURES=architecture, CMAKE_OSX_DEPLOYMENT_TARGET="11.0",
                          CMAKE_OSX_SYSROOT=sdk_path)
        shared, static = ("ON", "OFF") if system == "win32" else ("OFF", "ON")
        zlib = {"ZLIB_USE_STATIC_LIBS": static,
                "ZLIB_LIBRARY": original_prefix + "/" + roles["zlib"]["link"],
                "ZLIB_LIBRARY_RELEASE": original_prefix + "/" + roles["zlib"]["link"],
                "ZLIB_INCLUDE_DIR": original_prefix + "/include"}
        jpeg = {"ENABLE_SHARED": shared, "ENABLE_STATIC": static, "WITH_TURBOJPEG": "OFF",
                "WITH_TOOLS": "OFF", "WITH_TESTS": "OFF", "WITH_SIMD": "ON"}
        if architecture == "x86_64":
            jpeg["CMAKE_ASM_NASM_COMPILER"] = toolchain["nasm"]["path"]
        options = {
            "zlib": {"ZLIB_BUILD_EXAMPLES": "OFF",
                     **({"BUILD_SHARED_LIBS": "ON"} if system == "win32" else {})},
            "libjpeg-turbo": jpeg,
            "liblzma": {"BUILD_SHARED_LIBS": shared, "BUILD_TESTING": "OFF", "XZ_TOOL_XZ": "OFF",
                        "XZ_TOOL_XZDEC": "OFF", "XZ_TOOL_LZMADEC": "OFF", "XZ_TOOL_LZMAINFO": "OFF",
                        "XZ_TOOL_SCRIPTS": "OFF"},
            "png": {"PNG_SHARED": shared, "PNG_STATIC": static, "PNG_TESTS": "OFF", "PNG_TOOLS": "OFF",
                    **({"PNG_FRAMEWORK": "OFF"} if system == "darwin" else {}), **zlib},
            "glfw": {"BUILD_SHARED_LIBS": shared, "GLFW_BUILD_DOCS": "OFF", "GLFW_BUILD_TESTS": "OFF",
                     "GLFW_BUILD_EXAMPLES": "OFF", **({"GLFW_BUILD_COCOA": "ON"} if system == "darwin" else {})},
            "tiff": {"BUILD_SHARED_LIBS": shared, "tiff-tools": "OFF", "tiff-tests": "OFF",
                     "tiff-contrib": "OFF", "tiff-docs": "OFF", "tiff-install": "ON", "cxx": "OFF",
                     "zlib": "ON", "libdeflate": "OFF", "jpeg": "ON", "old-jpeg": "ON", "jbig": "OFF",
                     "lzma": "ON", "zstd": "OFF", "webp": "OFF", "lerc": "OFF", **zlib,
                     "JPEG_LIBRARY_RELEASE": original_prefix + "/" + roles["jpeg"]["link"],
                     "JPEG_INCLUDE_DIR": original_prefix + "/include",
                     "LIBLZMA_LIBRARY_RELEASE": original_prefix + "/" + roles["lzma"]["link"],
                     "LIBLZMA_INCLUDE_DIR": original_prefix + "/include"},
        }

        def cache_bytes(values):
            return ("# SYNTHETIC CACHE: no configure command was executed\n" + "".join(
                key + ":STRING=" + value + "\n" for key, value in values.items())).encode()

        compiler_c = claimed_tool("cl.exe" if system == "win32" else "clang", "resolved_path")
        compiler_cxx = claimed_tool("cl.exe" if system == "win32" else "clang++", "resolved_path")
        compiler = {"system": "Windows" if system == "win32" else "Darwin", "pointer_bytes": "8",
                    "c_id": "MSVC" if system == "win32" else "AppleClang", "c_version": "19.44" if system == "win32" else "17.0",
                    "c": compiler_c["resolved_path"], "cxx_id": "MSVC" if system == "win32" else "AppleClang",
                    "cxx_version": "19.44" if system == "win32" else "17.0", "cxx": compiler_cxx["resolved_path"],
                    "architecture": "x64" if system == "win32" else "", "sysroot": sdk_path}
        compiler["metadata"] = write("share/imagejxx-ci/compiler.txt", "".join(
            key + "=" + value + "\n" for key, value in compiler.items()).encode(), "metadata")
        compiler["cache"] = write("share/imagejxx-ci/compiler-CMakeCache.txt", cache_bytes({
            **common, "CMAKE_C_COMPILER": compiler["c"], "CMAKE_CXX_COMPILER": compiler["cxx"]}), "metadata")
        compiler.update(c_identity=compiler_c, cxx_identity=compiler_cxx)
        toolchain["compiler"] = compiler
        configurations = {}
        for name in DEPENDENCIES:
            requested = {**common, **options[name]}
            relative = "share/imagejxx-ci/configuration/" + name + ".CMakeCache.txt"
            configurations[name] = {
                "requested": requested, "effective": requested.copy(),
                "cache": write(relative, cache_bytes(requested), "metadata")}
        commands = [{"label": name + "-" + phase, "argv": ["SYNTHETIC-NOT-EXECUTED"],
                     "state": "passed", "exit_code": 0}
                    for name in DEPENDENCIES for phase in ("configure", "build", "install")]
        certificate = {
            "schema": "imagejxx.ci-platform-dependencies.001", "state": "prepared",
            "acceptance": False, "profile": profile,
            "source_manifest_path": "licenses/ci-linux-source-dependencies.json",
            "source_manifest_sha256": identity(self.catalog)["sha256"],
            "sources": json.loads(self.catalog.read_bytes())["dependencies"],
            "configuration": {
                "platform": {"name": profile, "system": system, "architecture": architecture,
                             "linkage": linkage, "deployment_target": "11.0" if system == "darwin" else None},
                "builder": {"repository_path": "scripts/ci_platform_dependencies.py",
                            "path": BUILDER_COPY, **identity(self.builder)},
                "source_catalog": {"path": CATALOG_COPY, **identity(self.catalog)},
                "toolchain": toolchain, "dependencies": configurations, "libraries": roles},
            "commands": commands, "installed_files": [members[name] for name in sorted(members)],
        }
        self.save(prefix, certificate)
        return prefix, certificate

    def save(self, prefix, certificate):
        (prefix / CERTIFICATE).write_text(json.dumps(certificate, indent=2) + "\n", encoding="utf-8")

    def validate(self, prefix, profile=WINDOWS):
        return verify.validate_prefix(prefix, profile, catalog=self.catalog, builder=self.builder)

    def change_member(self, prefix, certificate, relative, raw, *, update_inventory=False):
        path = prefix / relative
        path.write_bytes(raw)
        if update_inventory:
            row = next(row for row in certificate["installed_files"] if row["path"] == relative)
            row.update(identity(path))

    def symlink(self, link, target, *, directory=False):
        try:
            link.symlink_to(target, target_is_directory=directory)
        except OSError as error:
            if error.errno in (errno.EPERM, errno.EACCES, errno.ENOSYS) or getattr(error, "winerror", None) == 1314:
                self.skipTest("Host does not permit owned test symlinks")
            raise

    def test_windows_and_both_macos_profiles_accept_exact_transport(self):
        for profile in (WINDOWS, ARM, INTEL):
            with self.subTest(profile=profile):
                prefix, certificate = self.prepared(profile)
                original = (prefix / CERTIFICATE).read_bytes()
                self.assertEqual(self.validate(prefix, profile), certificate)
                self.assertEqual((prefix / CERTIFICATE).read_bytes(), original)
                self.assertEqual(set(certificate["configuration"]["libraries"]),
                                 {"zlib", "jpeg", "lzma", "png", "glfw", "tiff"})
                self.assertNotIn(CERTIFICATE, {row["path"] for row in certificate["installed_files"]})

    def test_changed_runtime_link_and_header_bytes_are_refused(self):
        for relative in ("bin/tiff.dll", "lib/tiff.lib", "include/tiffio.h", "include/GLFW/glfw3.h"):
            with self.subTest(relative=relative):
                prefix, certificate = self.prepared()
                original = (prefix / relative).read_bytes()
                self.change_member(prefix, certificate, relative, b"!" + original[1:])
                with self.assertRaisesRegex(ValueError, "member changed"):
                    self.validate(prefix)

    def test_changed_reviewed_builder_or_catalog_is_refused(self):
        for authority in (self.builder, self.catalog):
            with self.subTest(authority=authority.name):
                original = authority.read_bytes()
                prefix, _ = self.prepared()
                try:
                    authority.write_bytes(original + b"\n")
                    with self.assertRaises(ValueError):
                        self.validate(prefix)
                finally:
                    authority.write_bytes(original)

    def test_changed_copied_builder_or_catalog_is_refused_even_with_reinventoried_bytes(self):
        for relative in (BUILDER_COPY, CATALOG_COPY):
            with self.subTest(relative=relative):
                prefix, certificate = self.prepared()
                self.change_member(prefix, certificate, relative, (prefix / relative).read_bytes() + b"\n",
                                   update_inventory=True)
                self.save(prefix, certificate)
                with self.assertRaisesRegex(ValueError, "builder/catalog identity"):
                    self.validate(prefix)

    def test_profile_state_acceptance_and_platform_must_match_explicit_request(self):
        mutations = (
            lambda value: value.update(schema="imagejxx.ci-platform-dependencies.002"),
            lambda value: value.update(state="running"),
            lambda value: value.update(acceptance=True),
            lambda value: value.update(profile=ARM),
            lambda value: value["configuration"]["platform"].update(architecture="arm64"),
            lambda value: value["configuration"]["platform"].update(linkage="static"),
            lambda value: value["configuration"]["platform"].update(deployment_target="11.0"),
        )
        for index, mutation in enumerate(mutations):
            with self.subTest(mutation=index):
                prefix, certificate = self.prepared()
                mutation(certificate)
                self.save(prefix, certificate)
                with self.assertRaises(ValueError):
                    self.validate(prefix)
        prefix, _ = self.prepared()
        with self.assertRaisesRegex(ValueError, "Unknown"):
            self.validate(prefix, "windows-x64-dynamic-release-001")

    def test_catalog_dependency_change_or_noncommercial_claim_is_refused(self):
        for mutate in (lambda value: value["sources"].pop("stb"),
                       lambda value: value["sources"]["tiff"].update(commercial_use=False),
                       lambda value: value["sources"]["tiff"].update(version="other")):
            prefix, certificate = self.prepared()
            mutate(certificate)
            self.save(prefix, certificate)
            with self.assertRaisesRegex(ValueError, "Source/notice catalog"):
                self.validate(prefix)

    def test_duplicate_self_and_missing_inventory_members_are_refused(self):
        mutations = (
            lambda rows: rows.append(deepcopy(rows[0])),
            lambda rows: rows.append({"path": CERTIFICATE, "kind": "metadata", "bytes": 0, "sha256": "0" * 64}),
            lambda rows: rows.pop(),
            lambda rows: rows.clear(),
        )
        for index, mutation in enumerate(mutations):
            with self.subTest(mutation=index):
                prefix, certificate = self.prepared()
                mutation(certificate["installed_files"])
                self.save(prefix, certificate)
                with self.assertRaises(ValueError):
                    self.validate(prefix)

    def test_unlisted_extra_file_is_refused(self):
        prefix, _ = self.prepared()
        (prefix / "bin/unreviewed.dll").write_bytes(b"not in the prepared inventory")
        with self.assertRaisesRegex(ValueError, "file closure changed"):
            self.validate(prefix)

    def test_missing_member_is_refused(self):
        prefix, _ = self.prepared()
        (prefix / "include/tiffio.h").unlink()
        with self.assertRaises((ValueError, OSError)):
            self.validate(prefix)

    def test_member_paths_cannot_escape_or_embed_cmake_control_syntax(self):
        outside = self.root / "outside.txt"
        outside.write_bytes(b"must remain outside")
        paths = ("../outside.txt", outside.as_posix(), "include/../tiffio.h", "include//tiffio.h",
                 "include\\tiffio.h", "include/semi;colon", "include/line\nbreak", 'include/a"b',
                 "include/${expanded}")
        for relative in paths:
            with self.subTest(relative=relative):
                prefix, certificate = self.prepared()
                certificate["installed_files"].append({"path": relative, "kind": "header", **identity(outside)})
                self.save(prefix, certificate)
                with self.assertRaises(ValueError):
                    self.validate(prefix)
        self.assertEqual(outside.read_bytes(), b"must remain outside")

    def test_linked_file_and_linked_prefix_are_refused(self):
        prefix, _ = self.prepared()
        header = prefix / "include/tiffio.h"
        target = self.root / "original-header.txt"
        target.write_bytes(header.read_bytes())
        header.unlink()
        self.symlink(header, target)
        with self.assertRaises(ValueError):
            self.validate(prefix)
        alias = self.root / "linked-prefix"
        self.symlink(alias, prefix, directory=True)
        with self.assertRaises(ValueError):
            self.validate(alias)

    def test_linked_member_ancestor_is_refused(self):
        prefix, _ = self.prepared()
        include = prefix / "include"
        moved = self.root / "original-include"
        include.rename(moved)
        self.symlink(include, moved, directory=True)
        with self.assertRaisesRegex(ValueError, "Linked dependency"):
            self.validate(prefix)

    def test_library_roles_and_selected_member_kinds_are_exact(self):
        mutations = (
            lambda value: value["configuration"]["libraries"].pop("glfw"),
            lambda value: value["configuration"]["libraries"]["tiff"].update(runtime="bin/other.dll"),
            lambda value: next(row for row in value["installed_files"] if row["path"] == "lib/tiff.lib").update(kind="metadata"),
            lambda value: next(row for row in value["installed_files"] if row["path"] == "bin/tiff.dll").update(kind="link"),
            lambda value: value["installed_files"][0].update(kind="unknown"),
        )
        for index, mutation in enumerate(mutations):
            with self.subTest(mutation=index):
                prefix, certificate = self.prepared()
                mutation(certificate)
                self.save(prefix, certificate)
                with self.assertRaises(ValueError):
                    self.validate(prefix)

    def test_static_profile_refuses_inventoried_shared_runtime(self):
        for extension in (".dll", ".dylib"):
            with self.subTest(extension=extension):
                prefix, certificate = self.prepared(ARM)
                relative = "lib/unselected" + extension
                path = prefix / relative
                path.write_bytes(b"synthetic unselected shared runtime")
                certificate["installed_files"].append({"path": relative, "kind": "runtime", **identity(path)})
                self.save(prefix, certificate)
                with self.assertRaisesRegex(ValueError, "Static profile"):
                    self.validate(prefix, ARM)

    def test_each_dependency_effective_option_must_match_its_request(self):
        for name in DEPENDENCIES:
            with self.subTest(dependency=name):
                prefix, certificate = self.prepared()
                certificate["configuration"]["dependencies"][name]["effective"]["CMAKE_BUILD_TYPE"] = "Debug"
                self.save(prefix, certificate)
                with self.assertRaisesRegex(ValueError, "Effective dependency configuration"):
                    self.validate(prefix)

    def test_windows_cmake_separator_normalization_matches_producer_contract(self):
        prefix, certificate = self.prepared()
        configuration = certificate["configuration"]["dependencies"]["png"]
        configuration["requested"]["ZLIB_LIBRARY_RELEASE"] = r"C:\synthetic-ci-source\prefix\lib\zlib.lib"
        configuration["effective"]["ZLIB_LIBRARY_RELEASE"] = "C:/synthetic-ci-source/prefix/lib/zlib.lib"
        cache = configuration["cache"]["path"]
        raw = b"# synthetic normalized Windows CMake cache\n" + b"".join(
            (key + ":STRING=" + value + "\n").encode() for key, value in configuration["effective"].items())
        self.change_member(prefix, certificate, cache, raw, update_inventory=True)
        configuration["cache"].update(identity(prefix / cache))
        self.save(prefix, certificate)
        self.assertEqual(self.validate(prefix), certificate)

    def test_cache_evidence_pin_must_agree_even_if_member_inventory_is_replaced(self):
        prefix, certificate = self.prepared()
        cache = certificate["configuration"]["dependencies"]["tiff"]["cache"]["path"]
        self.change_member(prefix, certificate, cache, b"changed cache\n", update_inventory=True)
        self.save(prefix, certificate)
        with self.assertRaisesRegex(ValueError, "cache evidence changed"):
            self.validate(prefix)

    def test_resigned_cache_option_must_match_the_claimed_effective_configuration(self):
        prefix, certificate = self.prepared()
        configuration = certificate["configuration"]["dependencies"]["tiff"]
        cache = configuration["cache"]["path"]
        before = (prefix / cache).read_bytes()
        self.assertIn(b"CMAKE_BUILD_TYPE:STRING=Release", before)
        changed = before.replace(b"CMAKE_BUILD_TYPE:STRING=Release", b"CMAKE_BUILD_TYPE:STRING=Debug")
        self.change_member(prefix, certificate, cache, changed, update_inventory=True)
        configuration["cache"].update(identity(prefix / cache))
        self.save(prefix, certificate)
        # Both hashes agree with the altered file; unchanged Release claims
        # must still be checked against its actual contents.
        with self.assertRaises(ValueError):
            self.validate(prefix)

    def test_resigned_self_consistent_configuration_cannot_change_fixed_recipe(self):
        prefix, certificate = self.prepared()
        configuration = certificate["configuration"]["dependencies"]["tiff"]
        cache = configuration["cache"]["path"]
        before = (prefix / cache).read_bytes()
        self.assertIn(b"CMAKE_BUILD_TYPE:STRING=Release", before)
        changed = before.replace(b"CMAKE_BUILD_TYPE:STRING=Release", b"CMAKE_BUILD_TYPE:STRING=Debug")
        self.change_member(prefix, certificate, cache, changed, update_inventory=True)
        configuration["cache"].update(identity(prefix / cache))
        configuration["requested"]["CMAKE_BUILD_TYPE"] = "Debug"
        configuration["effective"]["CMAKE_BUILD_TYPE"] = "Debug"
        self.save(prefix, certificate)
        with self.assertRaises(ValueError):
            self.validate(prefix)

    def test_toolchain_must_include_bound_copied_version_evidence(self):
        prefix, certificate = self.prepared()
        certificate["configuration"]["toolchain"] = {}
        self.save(prefix, certificate)
        with self.assertRaises(ValueError):
            self.validate(prefix)
        prefix, certificate = self.prepared()
        tool = certificate["configuration"]["toolchain"]["cmake"]
        version = tool["version_evidence"]["path"]
        self.change_member(prefix, certificate, version, b"changed version evidence\n", update_inventory=True)
        self.save(prefix, certificate)
        with self.assertRaises(ValueError):
            self.validate(prefix)
        # Re-signing the inventory and version row cannot make empty evidence
        # stand in for the producer's copied version output.
        self.change_member(prefix, certificate, version, b"", update_inventory=True)
        tool["version_evidence"].update(identity(prefix / version))
        self.save(prefix, certificate)
        with self.assertRaises(ValueError):
            self.validate(prefix)

    def test_resigned_compiler_metadata_cannot_disagree_with_declared_native_profile(self):
        examples = ((WINDOWS, "system", "Darwin"), (WINDOWS, "pointer_bytes", "4"),
                    (WINDOWS, "c_id", "Clang"), (WINDOWS, "cxx_id", "GNU"),
                    (WINDOWS, "architecture", "ARM64"), (ARM, "c_id", "GNU"),
                    (INTEL, "cxx_id", "Clang"))
        keys = ("system", "pointer_bytes", "c_id", "c_version", "c", "cxx_id", "cxx_version",
                "cxx", "architecture", "sysroot")
        for profile, key, value in examples:
            with self.subTest(profile=profile, key=key, value=value):
                prefix, certificate = self.prepared(profile)
                compiler = certificate["configuration"]["toolchain"]["compiler"]
                compiler[key] = value
                relative = compiler["metadata"]["path"]
                raw = "".join(name + "=" + compiler[name] + "\n" for name in keys).encode()
                self.change_member(prefix, certificate, relative, raw, update_inventory=True)
                compiler["metadata"].update(identity(prefix / relative))
                self.save(prefix, certificate)
                with self.assertRaises(ValueError):
                    self.validate(prefix, profile)

    def test_compiler_identity_must_match_actual_copied_metadata_and_cache(self):
        prefix, certificate = self.prepared()
        compiler = certificate["configuration"]["toolchain"]["compiler"]
        metadata = compiler["metadata"]["path"]
        raw = (prefix / metadata).read_bytes().replace(b"system=Windows", b"system=Darwin")
        self.change_member(prefix, certificate, metadata, raw, update_inventory=True)
        compiler["metadata"].update(identity(prefix / metadata))
        self.save(prefix, certificate)
        with self.assertRaises(ValueError):
            self.validate(prefix)
        prefix, certificate = self.prepared()
        compiler = certificate["configuration"]["toolchain"]["compiler"]
        cache = compiler["cache"]["path"]
        original = (prefix / cache).read_bytes()
        requested = ("CMAKE_CXX_COMPILER:STRING=" + compiler["cxx"]).encode()
        self.assertIn(requested, original)
        raw = original.replace(requested, b"CMAKE_CXX_COMPILER:STRING=C:/foreign/compiler.exe")
        self.change_member(prefix, certificate, cache, raw, update_inventory=True)
        compiler["cache"].update(identity(prefix / cache))
        self.save(prefix, certificate)
        with self.assertRaises(ValueError):
            self.validate(prefix)

    def test_resigned_macos_dependency_cache_cannot_change_native_architecture_or_deployment(self):
        for profile, key, value in ((ARM, "CMAKE_OSX_ARCHITECTURES", "x86_64"),
                                    (INTEL, "CMAKE_OSX_ARCHITECTURES", "arm64"),
                                    (ARM, "CMAKE_OSX_DEPLOYMENT_TARGET", "10.15")):
            with self.subTest(profile=profile, key=key, value=value):
                prefix, certificate = self.prepared(profile)
                configuration = certificate["configuration"]["dependencies"]["tiff"]
                cache = configuration["cache"]["path"]
                before = (key + ":STRING=" + configuration["requested"][key]).encode()
                raw = (prefix / cache).read_bytes()
                self.assertIn(before, raw)
                raw = raw.replace(before, (key + ":STRING=" + value).encode())
                self.change_member(prefix, certificate, cache, raw, update_inventory=True)
                configuration["cache"].update(identity(prefix / cache))
                configuration["requested"][key] = value
                configuration["effective"][key] = value
                self.save(prefix, certificate)
                with self.assertRaises(ValueError):
                    self.validate(prefix, profile)

    def test_preparation_commands_must_be_present_and_terminal_passes(self):
        for changed in ([], [{"state": "running", "exit_code": 0}],
                        [{"state": "passed", "exit_code": 1}], [{"state": "failed", "exit_code": 0}]):
            with self.subTest(commands=changed):
                prefix, certificate = self.prepared()
                certificate["commands"] = changed
                self.save(prefix, certificate)
                with self.assertRaisesRegex(ValueError, "commands are incomplete"):
                    self.validate(prefix)

    def test_cmake_emission_requires_the_selected_tiff_import_library(self):
        prefix, _ = self.prepared()
        certificate = self.validate(prefix)
        output = self.root / "runtime.cmake"
        foreign = self.root / "foreign-tiff.lib"
        foreign.write_bytes((prefix / "lib/tiff.lib").read_bytes())
        with self.assertRaisesRegex(ValueError, "outside the prepared profile"):
            verify.emit_cmake(prefix, certificate, output, tiff_link=foreign)
        self.assertFalse(output.exists())

    @unittest.skipUnless(shutil.which("cmake"), "CMake script-mode executable is unavailable")
    def test_cli_generated_verify_catches_post_configuration_same_size_change(self):
        prefix, _ = self.prepared()
        output = self.root / "runtime.cmake"
        command = [sys.executable, "-I", "-B", str(VERIFY_SCRIPT), "--prefix", str(prefix),
                   "--profile", WINDOWS, "--catalog", str(self.catalog), "--builder", str(self.builder),
                   "--tiff-link", str(prefix / "lib/tiff.lib"), "--output", str(output)]
        emitted = subprocess.run(command, capture_output=True, text=True, timeout=30, check=False)
        self.assertEqual(emitted.returncode, 0, emitted.stdout + emitted.stderr)
        script = output.with_name("runtime-verify.cmake")
        self.assertTrue(script.is_file())
        self.assertIn("IMAGEJXX_IO_RUNTIME_FILES", output.read_text())
        cmake = shutil.which("cmake")
        before = subprocess.run([cmake, "-P", str(script)], capture_output=True, text=True, timeout=30, check=False)
        self.assertEqual(before.returncode, 0, before.stdout + before.stderr)
        runtime = prefix / "bin/tiff.dll"
        original = runtime.read_bytes()
        runtime.write_bytes(b"!" + original[1:])
        after = subprocess.run([cmake, "-P", str(script)], capture_output=True, text=True, timeout=30, check=False)
        self.assertNotEqual(after.returncode, 0)
        self.assertIn("Prepared dependency changed after configuration", after.stdout + after.stderr)

    @unittest.skipUnless(shutil.which("cmake"), "CMake configure executable is unavailable")
    def test_cmake_guard_accepts_direct_and_exact_findtiff_interface_metadata(self):
        # project(NONE) and explicit synthetic host variables exercise target
        # routing only. No compiler is selected, invoked, or qualified here.
        for route in ("direct", "interface"):
            with self.subTest(route=route):
                prefix, _ = self.prepared()
                source = self.root / ("metadata-project-" + route)
                source.mkdir()
                concrete = "TIFF::TIFF" if route == "direct" else "TIFF::tiff"
                text = (
                    "cmake_minimum_required(VERSION 3.18)\n"
                    "project(SyntheticPrefixTransport NONE)\n"
                    "# SYNTHETIC METADATA: not compiler/host qualification.\n"
                    "set(WIN32 TRUE)\nset(MSVC TRUE)\nset(CMAKE_SIZEOF_VOID_P 8)\n"
                    "set(CMAKE_CXX_COMPILER_ID MSVC)\n"
                    "set(CMAKE_CROSSCOMPILING FALSE)\nset(CMAKE_CXX_COMPILER_ARCHITECTURE_ID x64)\n"
                    "set(CMAKE_BUILD_TYPE Release)\n"
                    f'set(IMAGEJXX_CI_SOURCE_PROFILE "{WINDOWS}")\n'
                    f'set(Python3_EXECUTABLE "{Path(sys.executable).as_posix()}")\n'
                    f'add_library({concrete} SHARED IMPORTED)\n'
                    f'set_target_properties({concrete} PROPERTIES\n'
                    f' IMPORTED_IMPLIB_RELEASE "{(prefix / "lib/tiff.lib").as_posix()}"\n'
                    f' IMPORTED_LOCATION_RELEASE "{(prefix / "bin/tiff.dll").as_posix()}"\n'
                    f' INTERFACE_INCLUDE_DIRECTORIES "{(prefix / "include").as_posix()}")\n')
                if route == "interface":
                    text += ('add_library(TIFF::TIFF INTERFACE IMPORTED)\n'
                             'set_target_properties(TIFF::TIFF PROPERTIES INTERFACE_LINK_LIBRARIES TIFF::tiff)\n')
                text += (
                    f'include("{(ROOT / "cmake/SourceTiffRuntime.cmake").as_posix()}")\n'
                    'imagejxx_checked_selected_tiff_runtime(selected)\n'
                    'list(LENGTH selected count)\n'
                    'if(NOT count EQUAL 4 OR NOT EXISTS "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}")\n'
                    ' message(FATAL_ERROR "Missing synthetic transport outputs")\nendif()\n'
                    'file(WRITE "${CMAKE_BINARY_DIR}/transport.txt" "${selected}\\n${IMAGEJXX_CI_SOURCE_DEP_PREFIX}\\n")\n')
                (source / "CMakeLists.txt").write_text(text, encoding="utf-8")
                cache = self.root / ("metadata-cache-" + route)
                command = [shutil.which("cmake"), "-S", str(source), "-B", str(cache)]
                if shutil.which("ninja"):
                    command += ["-G", "Ninja"]
                configured = subprocess.run(command, capture_output=True, text=True, timeout=60, check=False)
                self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
                observed = (cache / "transport.txt").read_text().splitlines()
                self.assertEqual(observed, [";".join((prefix / name).as_posix() for name in (
                    "bin/tiff.dll", "bin/jpeg62.dll", "bin/liblzma.dll", "bin/zlib1.dll")), prefix.as_posix()])

    def emitted_recheck(self, prefix, profile=WINDOWS, *, name="postconfigure"):
        certificate = self.validate(prefix, profile)
        output = self.root / (name + ".cmake")
        link = certificate["configuration"]["libraries"]["tiff"]["link"]
        verify.emit_cmake(prefix, certificate, output, tiff_link=prefix / link)
        return output.with_name(output.stem + "-verify.cmake")

    def run_cmake_script(self, script):
        return subprocess.run([shutil.which("cmake"), "-P", str(script)],
                              capture_output=True, text=True, timeout=30, check=False)

    @unittest.skipUnless(shutil.which("cmake"), "CMake metadata configure/install executable is unavailable")
    def test_postconfigure_extra_member_blocks_whole_prefix_sdk_install(self):
        prefix, _ = self.prepared()
        script = self.emitted_recheck(prefix)
        before = self.run_cmake_script(script)
        self.assertEqual(before.returncode, 0, before.stdout + before.stderr)
        source = self.root / "closure-install-source"
        source.mkdir()
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.18)\n"
            "project(SyntheticPrefixInstall NONE)\n"
            f'install(SCRIPT "{script.as_posix()}")\n'
            f'install(DIRECTORY "{prefix.as_posix()}/" DESTINATION dependencies)\n', encoding="utf-8")
        cache = self.root / "closure-install-cache"
        command = [shutil.which("cmake"), "-S", str(source), "-B", str(cache)]
        if shutil.which("ninja"):
            command += ["-G", "Ninja"]
        configured = subprocess.run(command, capture_output=True, text=True, timeout=60, check=False)
        self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
        destination = self.root / "sdk-copy"
        command = [shutil.which("cmake"), "--install", str(cache), "--prefix", str(destination)]
        # Both a normal extra header and a hidden extra metadata file must be
        # caught by the install-time closure check, before the first SDK copy.
        for relative in ("include/unlisted.h", "share/.unlisted-metadata"):
            with self.subTest(extra=relative):
                extra = prefix / relative
                extra.write_bytes(b"postconfigure unlisted member\n")
                rejected = subprocess.run(command, capture_output=True, text=True, timeout=30, check=False)
                self.assertNotEqual(rejected.returncode, 0)
                self.assertIn("Prepared dependency file closure changed after configuration",
                              rejected.stdout + rejected.stderr)
                self.assertFalse(destination.exists())
                extra.unlink()
        # Removing only the new unlisted test file restores the exact sealed
        # input. The same configured install must then copy every exact member.
        accepted = subprocess.run(command, capture_output=True, text=True, timeout=30, check=False)
        self.assertEqual(accepted.returncode, 0, accepted.stdout + accepted.stderr)
        source_files = {path.relative_to(prefix).as_posix(): path.read_bytes()
                        for path in prefix.rglob("*") if path.is_file()}
        installed = destination / "dependencies"
        installed_files = {path.relative_to(installed).as_posix(): path.read_bytes()
                           for path in installed.rglob("*") if path.is_file()}
        self.assertEqual(installed_files, source_files)

    @unittest.skipUnless(shutil.which("cmake"), "CMake script-mode executable is unavailable")
    def test_postconfigure_linked_internal_parent_is_refused_before_member_reads(self):
        prefix, _ = self.prepared()
        script = self.emitted_recheck(prefix)
        before = self.run_cmake_script(script)
        self.assertEqual(before.returncode, 0, before.stdout + before.stderr)
        include = prefix / "include"
        moved = self.root / "retained-original-include"
        self.assertTrue(include.resolve(strict=True).is_relative_to(self.root))
        self.assertTrue(moved.resolve().is_relative_to(self.root))
        include.rename(moved)
        self.symlink(include, moved, directory=True)
        # The linked directory contains the original exact header bytes. A
        # rejection must therefore be the link check, not a hash mismatch.
        rejected = self.run_cmake_script(script)
        self.assertNotEqual(rejected.returncode, 0)
        self.assertIn("Prepared dependency member linked after configuration",
                      rejected.stdout + rejected.stderr)

    @unittest.skipUnless(shutil.which("cmake"), "CMake script-mode executable is unavailable")
    def test_postconfigure_linked_ancestor_above_prefix_is_refused(self):
        prefix, _ = self.prepared()
        parent = self.root / "original-prefix-parent"
        parent.mkdir()
        nested = parent / "prefix"
        self.assertTrue(prefix.resolve(strict=True).is_relative_to(self.root))
        self.assertTrue(nested.resolve().is_relative_to(self.root))
        prefix.rename(nested)
        script = self.emitted_recheck(nested)
        before = self.run_cmake_script(script)
        self.assertEqual(before.returncode, 0, before.stdout + before.stderr)
        moved = self.root / "retained-prefix-parent"
        self.assertTrue(parent.resolve(strict=True).is_relative_to(self.root))
        self.assertTrue(moved.resolve().is_relative_to(self.root))
        parent.rename(moved)
        self.symlink(parent, moved, directory=True)
        rejected = self.run_cmake_script(script)
        self.assertNotEqual(rejected.returncode, 0)
        self.assertIn("Prepared dependency directory linked after configuration",
                      rejected.stdout + rejected.stderr)

    @unittest.skipUnless(shutil.which("cmake"), "CMake script-mode executable is unavailable")
    def test_postconfigure_unlisted_directory_link_is_never_followed(self):
        prefix, _ = self.prepared()
        script = self.emitted_recheck(prefix)
        # A cycle cannot be part of the sealed prefix. The bounded direct-child
        # walk must reject it before descending; it must not recurse into it.
        self.symlink(prefix / "share/cycle", prefix, directory=True)
        rejected = self.run_cmake_script(script)
        self.assertNotEqual(rejected.returncode, 0)
        self.assertIn("Prepared dependency member linked after configuration",
                      rejected.stdout + rejected.stderr)

    def configure_desktop_metadata(self, profile, kind, suffix, *, mutation=None):
        prefix, _ = self.prepared(profile)
        self.validate(prefix, profile)
        source = self.root / ("desktop-metadata-source-" + str(self.serial))
        source.mkdir()
        cache = self.root / ("desktop-metadata-cache-" + str(self.serial))
        windows = profile == WINDOWS
        roles = WINDOWS_ROLES if windows else STATIC_ROLES
        targets = {}
        for target, role in (("ZLIB::ZLIB", "zlib"), ("PNG::PNG", "png"), ("glfw", "glfw")):
            fields = {"INTERFACE_INCLUDE_DIRECTORIES": (prefix / "include").as_posix()}
            if windows and kind == "SHARED":
                fields["IMPORTED_LOCATION" + suffix] = (prefix / roles[role]["runtime"]).as_posix()
                fields["IMPORTED_IMPLIB" + suffix] = (prefix / roles[role]["link"]).as_posix()
            else:
                fields["IMPORTED_LOCATION" + suffix] = (prefix / roles[role]["link"]).as_posix()
            targets[target] = fields
        json_headers = (prefix / "include").as_posix()
        stb_headers = json_headers
        if mutation is not None:
            target, field, mixed = mutation
            foreign = self.root / ("foreign-desktop-" + str(self.serial))
            foreign.mkdir()
            if field == "INTERFACE_INCLUDE_DIRECTORIES":
                replacement = foreign.as_posix()
                if mixed:
                    replacement = json_headers + ";" + replacement
            else:
                original = Path(targets[target][field])
                replacement_path = foreign / original.name
                replacement_path.write_bytes(original.read_bytes())
                replacement = replacement_path.as_posix()
            if target == "nlohmann_json::nlohmann_json":
                json_headers = replacement
            elif target == "stb":
                stb_headers = replacement
            else:
                targets[target][field] = replacement
        text = (
            "cmake_minimum_required(VERSION 3.18)\n"
            "project(SyntheticDesktopDependencies NONE)\n"
            "# SYNTHETIC TARGET METADATA; no compiler/host qualification.\n"
            "set(CMAKE_BUILD_TYPE Release)\n"
            f'set(WIN32 {"TRUE" if windows else "FALSE"})\n'
            f'set(IMAGEJXX_CI_SOURCE_PROFILE "{profile}")\n')
        for target, fields in targets.items():
            text += f"add_library({target} {kind} IMPORTED)\n"
            text += f"set_target_properties({target} PROPERTIES\n"
            text += "\n".join(f' {field} "{value}"' for field, value in fields.items()) + ")\n"
        text += (
            "add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)\n"
            "set_target_properties(nlohmann_json::nlohmann_json PROPERTIES\n"
            f' INTERFACE_INCLUDE_DIRECTORIES "{json_headers}")\n'
            f'set(IMAGEJXX_STB_INCLUDE_DIR "{stb_headers}")\n'
            f'include("{(ROOT / "cmake/SourceTiffRuntime.cmake").as_posix()}")\n'
            f'imagejxx_check_source_desktop_dependencies("{prefix.as_posix()}")\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/accepted.txt" "source-target origins admitted\\n")\n')
        (source / "CMakeLists.txt").write_text(text, encoding="utf-8")
        command = [shutil.which("cmake"), "-S", str(source), "-B", str(cache)]
        if shutil.which("ninja"):
            command += ["-G", "Ninja"]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60, check=False)
        return result, cache

    @unittest.skipUnless(shutil.which("cmake"), "CMake metadata configure executable is unavailable")
    def test_desktop_guard_accepts_declared_targets_and_supported_configurations(self):
        cases = ((WINDOWS, "SHARED", "_RELEASE"), (WINDOWS, "UNKNOWN", "_NOCONFIG"),
                 (WINDOWS, "SHARED", ""), (ARM, "STATIC", "_RELEASE"),
                 (ARM, "UNKNOWN", ""), (INTEL, "STATIC", "_NOCONFIG"))
        for profile, kind, suffix in cases:
            with self.subTest(profile=profile, kind=kind, suffix=suffix):
                result, cache = self.configure_desktop_metadata(profile, kind, suffix)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual((cache / "accepted.txt").read_text(), "source-target origins admitted\n")

    @unittest.skipUnless(shutil.which("cmake"), "CMake metadata configure executable is unavailable")
    def test_desktop_guard_refuses_foreign_actual_imports_even_with_identical_bytes(self):
        cases = [(WINDOWS, "SHARED", target, field) for target in ("ZLIB::ZLIB", "PNG::PNG", "glfw")
                 for field in ("IMPORTED_LOCATION_RELEASE", "IMPORTED_IMPLIB_RELEASE")]
        cases += [(ARM, "STATIC", target, "IMPORTED_LOCATION_RELEASE")
                  for target in ("ZLIB::ZLIB", "PNG::PNG", "glfw")]
        cases.append((WINDOWS, "UNKNOWN", "PNG::PNG", "IMPORTED_LOCATION_RELEASE"))
        for profile, kind, target, field in cases:
            with self.subTest(profile=profile, kind=kind, target=target, field=field):
                result, cache = self.configure_desktop_metadata(
                    profile, kind, "_RELEASE", mutation=(target, field, False))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Selected source desktop", result.stdout + result.stderr)
                self.assertIn(target, result.stdout + result.stderr)
                self.assertFalse((cache / "accepted.txt").exists())

    @unittest.skipUnless(shutil.which("cmake"), "CMake metadata configure executable is unavailable")
    def test_desktop_guard_refuses_foreign_or_additional_header_origins(self):
        for target in ("ZLIB::ZLIB", "PNG::PNG", "glfw", "nlohmann_json::nlohmann_json", "stb"):
            for mixed in (False, True):
                with self.subTest(target=target, mixed=mixed):
                    result, cache = self.configure_desktop_metadata(
                        WINDOWS, "SHARED", "_RELEASE",
                        mutation=(target, "INTERFACE_INCLUDE_DIRECTORIES", mixed))
                    self.assertNotEqual(result.returncode, 0)
                    expected = ("Selected source JSON/STB headers differ" if target in
                                ("nlohmann_json::nlohmann_json", "stb") else
                                "Selected source desktop headers differ")
                    self.assertIn(expected, result.stdout + result.stderr)
                    self.assertFalse((cache / "accepted.txt").exists())

    @unittest.skipUnless(shutil.which("cmake"), "CMake metadata configure executable is unavailable")
    def test_source_profile_rejects_nonrelease_and_multiconfiguration_consumers(self):
        for index, (build_type, configurations) in enumerate((
                ("", ""), ("Debug", ""), ("RelWithDebInfo", ""), ("Release", "Debug;Release"))):
            with self.subTest(build_type=build_type, configurations=configurations):
                source = self.root / ("configuration-source-" + str(index))
                source.mkdir()
                cache = self.root / ("configuration-cache-" + str(index))
                (source / "CMakeLists.txt").write_text(
                    "cmake_minimum_required(VERSION 3.18)\n"
                    "project(SyntheticConsumerConfiguration NONE)\n"
                    "set(CMAKE_CROSSCOMPILING FALSE)\nset(CMAKE_SIZEOF_VOID_P 8)\n"
                    f'set(IMAGEJXX_CI_SOURCE_PROFILE "{WINDOWS}")\n'
                    f'set(CMAKE_BUILD_TYPE "{build_type}")\n'
                    f'set(CMAKE_CONFIGURATION_TYPES "{configurations}")\n'
                    f'include("{(ROOT / "cmake/SourceTiffRuntime.cmake").as_posix()}")\n'
                    'imagejxx_checked_selected_tiff_runtime(selected)\n'
                    'file(WRITE "${CMAKE_BINARY_DIR}/accepted.txt" "incorrectly admitted\\n")\n',
                    encoding="utf-8")
                command = [shutil.which("cmake"), "-S", str(source), "-B", str(cache)]
                if shutil.which("ninja"):
                    command += ["-G", "Ninja"]
                result = subprocess.run(command, capture_output=True, text=True, timeout=60, check=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Source dependency profiles require a single-configuration Release consumer",
                              result.stdout + result.stderr)
                self.assertFalse((cache / "accepted.txt").exists())


if __name__ == "__main__":
    unittest.main()
