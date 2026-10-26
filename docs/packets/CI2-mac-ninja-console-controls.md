# CI2: bounded Darwin Ninja console experiment

This is a test-only prerequisite for the native build ownership decision. It
does not enable a product console profile or qualify generated CMake graphs.
The existing history build wrapper still refuses Darwin before starting a build.

## Hosted tool-admission failure and exact corrected profile

Run `40251241886` at `738a822` failed all four Mac experiment lanes during
setUp. Each owned version command exited successfully with cleanup complete,
but the binary reports `1.13.2.git.kitware.jobserver-pipe-1`, not the initially
assumed plain upstream banner. No lifecycle graph executed. Original archives,
API digests and a bounded four-lane summary remain under
`.tmp/manual-workflow-verification/ci2/ninja-console-*-failure001.zip` and
`ninja-console-version-failures001.json`. Historical expectations/results remain
at their original source identity; no native ownership success is inferred.

The corrected closed profile is PyPI distribution exactly `ninja==1.13.2`,
binary banner exactly `1.13.2.git.kitware.jobserver-pipe-1`, and the macOS
universal2 wheel SHA256
`fd82e26c0706ad4ab88e5fdd26f3fab0a987a90f810160f6c322e752c6af298b`.
The requirements file uses binary-only, no-dependency, required-hash installation
with force-reinstall, so an already-satisfied distribution cannot bypass wheel
hash verification. Retain pip's install report alongside the control log.
The shared metadata resolver selects the unique original RECORD native member,
checks size/SHA256, regular executable/Mach-O shape and requested path identity,
and retains native/METADATA/RECORD pins before and after each case. `--print-tool`
reuses that resolver; native tests still require the broker-owned version call.
No prefix version matching or process-owner fallback is introduced.

Immutable provenance: packaging commit
`dc4fad9143e6fd79f1fe6570037d9e0dfa8f5232` points to Kitware Ninja
`7659b3d8f4f6204f6f9a1b3a5c9832334fa2834f`, based on upstream v1.13.2
`3441b633c2fe2c494e958780ba0f4227b1327634`. The exact comparison leaves
`subprocess-posix.cc`, `build.cc` and `graph.cc` unchanged. Packaging installs the
native target directly; its separate Python module wrapper is not used.
See the [version source](https://github.com/Kitware/ninja/blob/7659b3d8f4f6204f6f9a1b3a5c9832334fa2834f/src/version.cc#L21),
[gitlink](https://api.github.com/repos/scikit-build/ninja-python-distributions/contents/ninja-upstream?ref=dc4fad9143e6fd79f1fe6570037d9e0dfa8f5232),
[source comparison](https://github.com/Kitware/ninja/compare/3441b633c2fe2c494e958780ba0f4227b1327634...7659b3d8f4f6204f6f9a1b3a5c9832334fa2834f),
[native installation](https://github.com/scikit-build/ninja-python-distributions/blob/dc4fad9143e6fd79f1fe6570037d9e0dfa8f5232/CMakeLists.txt#L35),
and [wheel provenance](https://pypi.org/project/ninja/1.13.2/#ninja-1.13.2-py3-none-macosx_10_9_universal2.whl).
These source facts establish the intended tool profile, not actual graph cleanup.

Thirteen portable metadata/graph methods and seven workflow methods passed
locally on Windows; Linux passed with one explicit Windows-shell skip. The first
Windows attempt retained one missing-Ninja metadata-configure skip, then the
existing configured CMake/Ninja tools supplied an unskipped run. Receipts use
`ninja-profile-controls-{windows003,linux002}` under the manual verification root.
The final shell assignment separates tool admission from export so its failure
propagates immediately. The final force-reinstall/install-report wiring passed
seven focused controls per host (one explicit Linux Windows-shell skip) in
`ninja-wheel-workflow-{windows,linux}001`; the thirteen unchanged tool controls
retain their own source identities above.
Fresh actual Darwin execution remains required.
The [exact-tool progress index](../parity/reviews/g010-history-reconnection001/ninja-exact-tool-progress.json)
binds current source bytes, distinct original control receipts and all four
original failed tool-admission artifacts. It does not repin older workflow
controls to the final force-reinstall/report change.

## Frozen experiment

Reuse the existing Darwin broker, group anchors, acknowledged kernel exit
witnesses and unrelated live control. The optional launcher seam preserves the
original eight lifecycle test bodies and their default Python routes. The new
class reuses fixture helpers without inheriting those test methods.

The mandatory `test_ci_mac_ninja_process.py --require-darwin` selector requires
the actual Darwin kernel, an explicit absolute executable in
`IMAGEJXX_NINJA_CONSOLE_CONTROL_EXE`, an owned successful `--version` result of
`1.13.2.git.kitware.jobserver-pipe-1`, and an unchanged executable hash. It runs
exactly six named cases:

| Case | Required observation |
| --- | --- |
| Normal console exit | Actual Ninja argv; payload is its direct child with the pinned group/session; terminal success and complete cleanup |
| Nonzero console exit | Payload exits 7; actual Ninja exits 1; complete owned cleanup |
| Surviving leaf | Acknowledged payload/leaf identities and kernel witnesses; Ninja exits successfully; remaining owned leaf is observed and drained |
| Timeout | The same acknowledged console route times out; kernel exits and group drain complete; unrelated live control survives |
| Owner death | Retained direct owner is killed; broker watch detects closure; actual Ninja/payload/leaf exit; unrelated control survives |
| Nonconsole refusal | Modified graph refuses before the graph owner call; no graph receipt or payload output. The separate setup version check is allowed |

The graph is one exact literal edge with `pool = console` and explicit shell
`exec`. It is not a parser or admission policy for arbitrary Ninja files.
Normal/nonzero cases check actual payload parent, PGID and SID; the remaining
executing cases also use acknowledged independent lifetime witnesses.

## Local evidence and hosted entry

Existing Windows/Linux runtimes pass the 21 original portable protocol/socket
methods and two new graph/native-entry metadata methods. Windows explicitly
skips four POSIX socket methods; Linux skips none. These runs do not execute the
six native cases. Receipts and unchanged source pins remain under
`.tmp/manual-workflow-verification/mac-ninja-portable-{windows,linux}001/`.
Both hosts also pass seven workflow argument/ordering controls; Linux explicitly
skips the Windows shell method. Those separate receipts use
`mac-ninja-workflow-{windows,linux}001/`.

The source-profile workflow installs the already reviewed Ninja tool at
`1.13.2` on macOS, resolves its absolute path, and requires the native selector
before the full pip build. An always-upload step retains the bounded control
log under a unique profile/Python artifact name. Existing eight-test Darwin,
macOS11 observation, installed IO/UI, direct SDK and complete-suite gates remain.
No local compiler, product, SDK or runtime was built or installed for this packet.

## Decision after actual native execution

Actual hosted results are still required. Even six passing cases establish only
this single-edge experiment. Broker death, failed bootstrap, generated root and
try_compile graphs, compiler/launcher routes, and complete product build/install
ownership remain open. Nonconsole containment remains unqualified.

If the experiment passes, evaluate one explicit Apple/Ninja profile using
console compile/link settings before `project()`, propagation into try_compile,
and terminal custom commands. Audit every reachable command edge and reject
uncovered edges or overrides before admitting a real build. Retain exact
configuration/tool/graph identities. Measure serialization cost against the
existing job time limits. Do not replace the broker with session enumeration or
claim a product pass from these metadata controls.

Original reference availability is a separate requirement; follow the
[seven-file recovery contract](CI2-original-reference-recovery.md).
