# Reviewed Windows TIFF policy fixture

These ten files preserve the exact `windows-x64-dynamic-release-001` artifacts
listed in [the original manifest](../../../licenses/io-dependencies.json).
`SOURCE.json` records their byte counts, SHA-256 identities, original source
archives, port/ABI provenance and commercial-use review. Original distribution
notices are retained in `licenses/`.

The fixture is input to the existing Windows runtime-policy tests, selected by
`IMAGEJXX_REVIEWED_WINDOWS_TIFF_PREFIX`. The fixture loader checks every original
member before making owned mutation-test copies. Missing or changed members
fail; an explicitly supplied empty or absent directory cannot silently skip.

This is a bounded policy-test fixture, not an SDK or the product's dependency
prefix. CI2's source-built Windows profile has separate identities and requires
its own real preparation and installed-product qualification. Do not update
these files to match source-built DLLs or change the original policy pins.
