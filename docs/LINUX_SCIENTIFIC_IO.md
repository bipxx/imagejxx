# Linux scientific IO and installed products

This packet qualifies the existing C++ scientific IO service and Dear ImGui
hosts on Ubuntu 24.04 x86_64, GCC 13.3 and Python 3.12.3. It does not expand the
admitted TIFF formats. Qt and Java are not part of the products.

The selected static libtiff 4.7.1 closure uses libjpeg-turbo 3.1.3, liblzma
5.8.2 and zlib 1.3.1. Its 17 configured codecs match the reviewed Windows
closure. The exact library, archive and retained notice identities are in
[the Linux IO inventory](../licenses/linux-io-dependencies.json). Other Linux
builds require their own evidence. This inventory does not enforce discovery
of arbitrary system TIFF prefixes.

The private core zlib remains separate. Its internal error/debug globals now
have private names as well as its public codec symbols, allowing both static
codecs to link into the same desktop executable. Upstream codec sources and
their source archive pins are unchanged.

Linux exposed a cross-extension RTTI issue: an eager Dataset created by the
scientific IO extension was not recognized by the processing extension when
constructing a read observer for an IEEE source. The five internal provider
classes now have stable named type identities. Admission still checks the
reviewed concrete provider graph; a custom provider cannot claim an eager
policy to bypass that check. The focused Python IO suite passes 1,122 cases.

The complete Linux build passes 238 native and desktop CTest checks. The
independently configured installed core consumer needs no TIFF, Python or
graphics discovery. The installed IO consumer passes all 24 format round
trips, including exact Float32 IEEE words, and neither consumer links Python
or graphics. The SDK includes a relocatable TIFF package metadata adapter for
upstream 4.7.1's static dependency imports.

Portability fixes also correct temporary transaction use in annotation tests,
a GCC cast, a missing forward declaration, and the operation registry test's
stale inventory. The command smoke input now uses Float32 for Remove NaNs.
Shortcut observations retain every field in compact JSON within the existing
byte bound. Numeric zoom verification scrolls physically to clipped controls
using observed clip rectangles before clicking them.

Windows regression evidence includes 84 native/desktop checks, three focused
UI checks and 10,436 passing cases in both source and installed Python. Both
Linux Python runs collect the identical 10,436 case IDs: 10,426 pass and exactly
10 Windows-only DLL delivery checks are exempt. Those ten pass in both Windows
runs. There are no test failures, errors or unexpected skips. The original
Linux wrappers incorrectly rejected these explicit platform skips; their failed
receipts are retained. Separate qualification audits validate the exact IDs,
reasons, source freezes and passing Windows counterparts without relabeling or
rerunning those completed executions.

Fresh source and installed Linux C++ hosts each execute 137 scenarios, including
all four RGB carriers, IEEE viewing, rank operations and every existing TIFF UI
profile. Python validates both sets of actual reports and generates its own
input-driven observations. The final audit is
`build/p08k-linux-io001/final-audit001.json`. This closes this bounded Linux
qualification packet. All99 remains 42 in progress / 57 open / 0 complete.

Evidence and failed attempts are retained under
`build/p08k-linux-io001`. The passing build is `products006`; the independent
SDK gate is `sdk001`. Source and installed products are under the owned Linux
workspace's `install001` directory. Build staging uses a persistent native
filesystem directory, rather than `/tmp` or a mounted Windows build tree.

Old fixture outputs were archived with CRC and per-entry hashes before their
originals were removed. Intentional test symlinks were stored as links without
following their targets. All cleanup is now complete: 118 owned targets were archived and removed,
freeing 9,014,291,218 net bytes. This includes old fixture outputs, build caches,
failed attempts and source/installed UI captures. Each archive passed CRC and
per-entry hash verification before removal; link targets were not followed.
The selected dependency/environment prefixes, installed products and independent
SDK consumer remain available in the 368 MiB owned Linux workspace. Actual UI
reports and carriers are retained in `cleanup001/scratch001.zip` under their
original workspace-relative names; restore those entries before a future
comparison that needs their original paths.
