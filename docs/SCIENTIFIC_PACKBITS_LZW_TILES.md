# PackBits and LZW tiled TIFF

The C++ IO service, Python API and both Dear ImGui hosts support predictor-free
PackBits and LZW tiles for UInt16, UInt8, RGB8 and Float32. Eight appended formats
use ordinals 32–39. Previous ordinals, default strip output and workflow versions
1–12 retain their contracts. Version 13 strictly requires tiled PackBits or LZW,
with an explicit sample layout and floating policy. Replay opens an inactive
review form; storage/compression changes invalidate estimates and consent.

Output uses little-endian classic TIFF, 64×64 tiles and zero edge padding.
PackBits encoding restarts each row at the tile row width. LZW retains strict
receiver-clock dictionary widths, clear/reset/EOI handling and zero byte-padding
rules. The shared bounded block dispatcher reuses the existing codecs and raw
serializer. Input ignores padding during floating admission and retains exact
logical IEEE words. No new dependency or non-commercial component was added.

Preflight accounts for padded raw tiles, codec-specific encoded bounds, provider
storage, tables and working buffers before reading planes. Cancellation and
callback failures retain exclusive staging, atomic replacement and the previous
destination. Tiled prediction remains refused and is the next implementation gap.

The independent corpus contains 336 valid manually encoded files, 336 libtiff
carriers and 220 malformed/unsupported controls. Both endian orders, edge and
entropy geometries, dictionary-width/reset transitions, padding and floating
policies are covered. Independent decoders agree on all 672 valid logical word
streams; the previous installed service refuses all 1,784 inspect/read attempts.

Windows passes 87 native/desktop checks and 14,020 cases in each source/installed
Python run. Linux passes 238 native/desktop checks and 14,010 cases in each
source/installed Python run, with exactly ten Windows-only exemptions. All four
inventories retain the previous 12,255 IDs and add 1,765. Both independent SDK
consumers pass 40 formats; Linux C++ passes 181 scenarios per build. The new
24 UI scenarios have 192 observations across both platforms, hosts and install
contexts, with 576 exact XYZ CPU readbacks. These exercise rendered ImGui inputs
and CPU data; they do not establish OS mouse or GPU output equivalence.
Independent libtiff readers verify 2,944 new native exports. Installed Linux
hosts also run from an external working directory with source/cache absent.

A publication fault test exposed an admission race in its diagnostic allocation
prefix. Its setup now holds the scheduler while admission captures Queued, then
releases it before real preparation/publication. Exact ordinal, request-prefix,
atomicity and resource-release assertions remain. Ten Windows repetitions and
the full Linux suite pass. Earlier failed runs remain available in the evidence.
The application runtime did not need a change for this test correction.

Scratch is archived with CRC and per-entry hashes before removal. The native
source/build cache is retained, and all previous qualified install prefixes are
preserved. The existing fixed benchmark remains the performance baseline; this
packet makes no speed claim. See `build/p08o-packbits-lzw-tiles001/qualification001.json`
and `docs/parity/reviews/packbits-lzw-tile-review-001.json`.

All 40 implemented variants remain partial in the family ledger. Formal reciprocal
fixture/accepted-run registration, G080/release certification, tiled prediction,
BigTIFF, broader metadata/sample layouts and remaining editor/Fiji gaps are open.
This is no ImageJ tiled-reopening claim. All99 remains 42 in progress / 57 open /
0 complete.
