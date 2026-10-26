# Disposable Windows CI notices

`windows-gl-notices.zip` preserves the original reviewed notice corpus and
source context for the pinned Mesa test tool. `windows-gl-notices.json` binds
its exact bytes and the nested original captures. These are copyright/source
records, not provider DLLs, SDKs, a complete dependency checkout, or an assertion
that every included source file is linked into the selected DLLs. Individual
licenses remain distinct; the corpus is not blanket MIT.

This input serves only the disposable CI-use profile. Preserve the notice bundle
beside each temporary tool copy. Provider binaries are downloaded separately,
pinned, staged only after product packaging, and removed after owned processes
close. No provider binary belongs in these notices or uploaded diagnostic roots.
Product redistribution remains unadmitted. This directory is explicitly excluded
from the source distribution; normal wheels and installed SDKs do not consume it.
