# CI2: Windows CI renderer capability

State, updated 2026-10-26: fixed source/notices acquired and bounded CI-use
scope reviewed; actual provider staging/loaded-module/GL qualification pending.
No provider has been installed locally and product redistribution is unadmitted.
Keep the unchanged actual-GL gate required.

The internal 216-member follow-up is complete and all 29 selected external
source/notice documents were acquired in a single 267,163-byte verified ZIP.
The SPIR-V Tools 2026.4.1 label is the pkg-config spelling of the exact selected
SDK tag (2026.4 plus .1), not an unresolved release-tag contradiction. Original
source, acquisition and review receipts are retained under the manual
verification root; no original manifest or predecessor receipt was repinned.

The two official Runtime/Build Tools term bodies were reviewed. Requiring a
product developer redistribution grant before using an unmodified disposable
CI-only renderer was broader than this task needs. The retained
[Runtime installation/use terms](https://visualstudio.microsoft.com/wp-content/uploads/2025/10/Visual-C-V14-License-Redistributable_and_Runtime_ENU.docx)
permit installing/using any number of copies; product redistribution remains a
separate unadmitted scope. This is not evidence about the provider's private
entitlements, nor permission to ship Microsoft or provider binaries ourselves.
Do not expand the trial into an entitlement, exact SBOM or reproducible-build
research campaign. Preserve component notices and any actually applicable use
conditions. The broader official VS third-party body remains unacquired; an
HTML landing is not its text or a blanket license for every IDE component.

For the bounded CI trial, stage after pip/wheel/SDK packaging and outside all
capture/diagnostic roots. The existing observation archiver recursively retains
its roots, so putting DLLs there would publish them inadvertently. Refuse
foreign existing files; retain identities/notices, not DLL payloads, in reports.
After owned processes finish, remove/check only the exact owned app-local copies
before artifact publication. Product wheels, SDKs, installers and uploaded
dependency prefixes must remain free of the provider. No system/registry changes,
PATH-only override, skipped smoke or altered pixel oracle is permitted.
Actual runtime/module/hash and unchanged hidden-GL/history/C2 results still gate
qualification; the text review supplies none of those observations.

Run `40251241886` at `738a822` passed the real Windows pip build/install and
direct scientific SDK. Installed smoke passed the TIFF test but failed
`test_settings_in_hidden_opengl_and_recreated_context` because the real GL3
desktop window could not be created. The retained original Windows3.12 archive
is artifact `12621861285`, API/archive SHA256
`cfafd599a3a344b2d7461b471eab4aff9c7b0a92e605a2d22634db869449ce12`.
Its build receipt is passed; this does not make the failed UI smoke acceptable.

## One CI-only candidate

Use pal1000 Mesa26.2.4 MSVC only as a candidate for disposable Windows runners.
Preserve the sealed TIFF/desktop source prefix and its historical fingerprints;
system OpenGL was external to that prefix. Do not change normal product renderer
selection or distribute this broad archive with the application.

Pinned release asset `609112860`, `mesa3d-26.2.4-release-msvc.7z`, is
70,257,286 bytes, SHA256
`351fc8c8b695878ffb3eaa044b3ead08672a48b1a045e3c3e3975811df0f6695`.
Builder commit is `f02a557d84a1a1cf9470a37944d6d1d29a67fcd0`.
The [release](https://github.com/pal1000/mesa-dist-win/releases/tag/26.2.4)
and [pinned build information](https://github.com/pal1000/mesa-dist-win/blob/f02a557d84a1a1cf9470a37944d6d1d29a67fcd0/buildinfo/msvc.txt)
establish provenance, not reproducible-build equivalence or blanket MIT coverage.

Static intake verified the archive, streamed DLLs only in memory through the
existing LLVM reader, retained bounded notices/import reports and retired the
archive after all handles/children closed. No DLL was extracted to disk, loaded
or installed. Original intake is
`.tmp/manual-workflow-verification/mesa-provider-intake001/receipt.json`;
the archive is unavailable locally and any reacquisition must use its unchanged pin.

The evidenced non-system load-time set is:

| Member | Bytes | SHA256 |
| --- | ---: | --- |
| x64/opengl32.dll | 139,264 | 75b295fc80df0eaa8f1264c28663f6932c8b72d36513eb092556608858b7f5e0 |
| x64/libgallium_wgl.dll | 61,868,032 | 53e49adf87590d5b0cab8553b1e98084812040cc1fca8c487695b17b7af3702e |

Both delay-import directories are zero. The first imports the second; remaining
normal imports are Windows system DLLs. Dynamic loading is present, so this is
not complete runtime closure. Exclude archive codecs/WARP/DXIL, EGL/GLES,
Vulkan, OpenCL and VA-API binaries. Selecting llvmpipe does not erase notices
for other code statically compiled into the shared Gallium DLL.

## Required admission and actual proof

1. Review exact preferred-source WGL/target-helper/GDI/software loader paths and
   compiled components. Stream official Mesa26.2.4 source (68,613,948 bytes,
   SHA256 `bce5f7fbebb934373b86c999a064d52fb5065878dc57f287f95346648ec832e9`)
   without retaining the archive. Preserve selected original source/notices and
   hash the complete compressed stream. Missing paths remain explicit.
2. Close component copyrights/licenses, static-runtime obligations and provider
   patches before admission. The binary archive contains only a93-byte mutable
   README link; it does not supply sufficient notices. LLVM23.1.2, zlib1.3.2,
   zstd1.5.7, SPIR-V Tools2026.4.1 and DirectX-Headers1.619.5 appear in the builder
   environment, which is not a per-DLL link manifest. Prefer exact source/license
   identities; do not label the whole archive MIT. [Mesa licensing](https://docs.mesa3d.org/license.html)
   and [LLVM recipe](https://github.com/pal1000/mesa-dist-win/blob/f02a557d84a1a1cf9470a37944d6d1d29a67fcd0/buildscript/modules/llvm.cmd)
   explain the remaining review boundary.
3. After admission, deploy only verified selected regular DLLs app-locally beside
   the actual Python executable and each standalone GUI executable on disposable
   CI. Refuse foreign existing files; record destinations/hashes and force
   `GALLIUM_DRIVER=llvmpipe`. No System32/registry changes or vendor deployment
   script. PATH alone cannot override the Windows system OpenGL search order.
4. Record actual loaded module identities and GL vendor/renderer/version. Pass
   the unchanged hidden-window test on both Python versions, actual standalone
   and Python history routes, then the [CFG4 pixel oracle](CFG4-appearance-consumers.md).
   No version override, skipped test or headless replacement supplies this proof.

## Lean storage record

Static binary intake used the documented necessary exception in
`mesa-intake-storage-exception001.json`: its estimated known peak included the
70 MB archive plus 9 MiB bounded metadata reserve, about 24 MiB over the normal
8 GiB limit. This was an admission estimate, not a measured live peak. The
archive was verified and retired immediately after inspection. Known generated
storage afterward was 8,535,658,031 bytes; two unreadable cache subtrees still
make the complete budget unverified. Preserve before/after inventories and
retirement identities. No compiler cache, SDK or runtime generation was created.
