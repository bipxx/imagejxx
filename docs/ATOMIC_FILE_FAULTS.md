# Atomic file path and fault qualification

Atomic byte replacement, deferred byte publication and streaming publication now
capture an absolute destination before user callbacks or later publication can
change the process working directory. Their planned temporary paths use the same
captured location. Empty and interior-NUL destinations refuse before callbacks
or staging. Paths retain ordinary filesystem component semantics; this is lexical
path capture, not symlink or directory-identity locking.

The previous helper reproduced a real failure on Windows and Linux: a checkpoint
changed CWD, and a relative write replaced the other directory's same-named file.
Retained failing binaries/logs and the affected owned test files precede the fix.
Six native cases now cover first-hook and late publication changes through all
three entry points, alongside invalid-path and cleanup checks.

Native session saving also captures its path before encoding callbacks. Session
loading captures its input before its first checkpoint. Python checks and an
installed core-only C++ SDK consumer verify both directions, unrelated-file and
source preservation, invalid paths before hooks, and unchanged precancel behavior.
No public API, session wire format or new product dependency is introduced.

Linux/WSL qualification uses the production `atomic_file.cpp` in a test-only
standalone executable. It excludes the public session tests and makes no Linux
TIFF/GUI/package acceptance claim. Its source/compiler/kernel identities and owned
process-group completion are retained.

The real disk-full case mounts an exactly 1 MiB tmpfs inside a new user/mount
namespace, after making inherited mounts private. It refuses other filesystem
types/capacities before writing. An independent raw writer reaches kernel
`ENOSPC` after 1,044,480 bytes; each shared atomic writer then refuses a bounded
2 MiB payload while preserving the old target, caller bytes and directory
membership. The target is copied out before namespace teardown. The mount does
not consume the real drive's free space. Separate tests lower only the owned
child process's file-size limit and exercise all three writers under that limit.
No write/flush/close return values are mocked in that ENOSPC/file-limit packet.

Current evidence: `build/p06zd-atomic-faults001/atomic-fault-handoff001.json`.
Windows validation passes native14 (including the existing session IO suite),
source295/installed315 Python checks, both physical installed ImGui hosts, both
normal launchers, the real mixed launcher, all ten startup cases, API9, the IO SDK
and the core-only session-path SDK consumer. Linux basic/quota/file-limit cases
pass with no remaining owned process-group members.

Real flush/close error qualification, Windows disk-full proof, POSIX temporary
pathname ownership hardening, remaining instrumentation and complete matching
release qualification remain open. Normal successful flush/close and real Linux
disk-full checks do not substitute for those failure requirements. Windows handle
closure is governed by the [documented CloseHandle result contract](https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-closehandle);
its failure paths still need separate qualification. Full editor/Fiji family
acceptance remains open.


## Flush/close boundary checkpoint

`build/p06ze-flush-close001/flush-close-handoff001.json` retains a private test
translation unit that includes the actual production helper. Windows redirects
only its CreateFileW/ReOpenFile/FlushFileBuffers/CloseHandle calls; Linux uses
link-time open/fsync/close wrappers and raw-syscall backends. No fault switches,
wrappers or new dependencies enter the product library, Python modules or SDK.

The matrix covers immediate byte replacement, deferred byte publication and
streaming publication, each with existing and absent destinations. Windows54
covers valid controls, injected flush/retain errors, a real read-only-handle flush
refusal, released/recycled writer handles, real protected-handle close refusal,
and refused/recycled close after publication. Linux42 covers valid controls,
injected flush failure, actual pipe-fsync refusal, released/recycled descriptors,
and close results EIO/EINTR/EBADF. Kernel controls deliberately substitute a
restricted handle/pipe at the call boundary. They prove refusal propagation,
not actual device writeback failure. Injected close results model release and
reuse explicitly. Target bytes, caller bytes, unrelated witness bytes/handles,
stage removal and zero tracked resource leaks are checked for each case.

The prior Windows implementation leaked the writer when CloseHandle refused
closure, then separately leaked the retained handle if closure failed after a
successful rename. Both old executables and exact source preimages are retained.
The helper captures file identity at creation, preserves the original close error,
and retries refused closure only if volume and nonzero file ID still match.
After commit, it retains an unclosed matching handle for destruction with deletion
disabled; cleanup cannot turn the committed save into a reported failure.
Identity queries are nonthrowing Win32 calls. Unsupported identity providers do
not reject ordinary saves, but failure recovery on them is not qualified. A
persistent close denial can still prevent resource reclamation.

File-information support depends on the underlying provider; see Microsoft's
[GetFileInformationByHandleEx contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getfileinformationbyhandleex).
Linux consumes the descriptor before close and never retries it after an error,
preserving unrelated reused descriptors as required by the Linux
[close error/reuse discussion](https://man7.org/linux/man-pages/man2/close.2.html).
These Linux results are specific to the tested kernel, not a claim for every
POSIX implementation's EINTR behavior.

The rebuilt Linux fault-matrix executable is byte-identical before/after the
Windows-only repair. Recompiling the earlier standalone helper also yields the
exact p06zd ELF: its actual tmpfs ENOSPC/file-limit evidence is retained by binary
identity rather than rerunning an unchanged quota experiment. Linux TIFF/product
acceptance, POSIX pathname-substitution ownership, actual device flush/close
failure, Windows disk-full, remaining instrumentation and complete matching
release products remain open. Native15, source295/installed315, installed
physical hosts, normal/mixed launches, API9, startup SU01–SU10 and installed
IO/core-session SDK checks pass for the rebuilt Windows product.


## POSIX pathname ownership checkpoint

`build/p06zf-posix-ownership001/posix-ownership-handoff001.json` records a repair
for deterministic substitution during callbacks and detached staging. The POSIX
helper opens the destination parent once, creates with openat and O_EXCL, and
uses directory-relative renameat/linkat/unlinkat. Linux uses O_PATH for the
parent so a searchable/writeable directory does not need new read permission;
other POSIX platforms use O_RDONLY and remain unqualified here. Created and
retained descriptors use close-on-exec. A duplicate descriptor pins the original
inode across writer closure, including close errors that release/recycle the
writer descriptor. Cleanup checks the entry without following symlinks while
that inode is still held. Publication also checks that the destination parent's
pathname still resolves to its opened directory. Parent reuse therefore refuses
publication and cleanup stays anchored to the original directory.

The regression matrix replaces the created stage with an unrelated file,
symlink, nonempty directory or no entry, or renames its parent and substitutes
a new directory with same-named target/stage. Each runs while writing, after
close before publication, and on cancellation, through all three atomic entry
points with existing/absent targets: 90 cases. It checks both target locations,
caller/witness bytes, replacement entries, intentionally moved originals and
unchanged descriptor count. Twelve controls cover normal replacement/no-replace
success and existing-destination refusal. Three real RLIMIT_NOFILE/EMFILE cases
leave slots for the parent/writer but refuse the retained duplicate; they report
the retain phase, preserve the old destination and remove the owned stage with
no descriptor leak. No syscall outcome is mocked in this matrix.

The old helper actually publishes the replacement stage and separately redirects
publication into a substituted parent directory. Retained before-fix binaries,
source preimages, logs and native-filesystem artifacts prove both failures.
Windows-mounted WSL paths reject the open-parent rename used by this test; those
initial observations are preserved without parity credit. Final directory
substitution cases run on Linux's native filesystem. Every owned temporary tree
is archived with per-entry type/size/SHA256 and verified ZIP CRC before removal.
The Linux compiled-core CMake consumer, private flush/close42 matrix, real
exact1MiB tmpfs ENOSPC and file-size-limit regressions are separate gates.

The identity checks and subsequent name-based operations are separate kernel
calls. Another actor can still substitute the entry in that interval. This
checkpoint does **not** qualify concurrent adversarial mutation, anonymous-inode
publication, directory-fsync/crash durability, actual device writeback/close
failure, Windows disk-full, untested POSIX platforms or Linux TIFF/desktop/release
products. A renamed original file outside the held directory entry is left
untouched; searching for it by pathname would risk deleting unrelated files.
Continue the residual race as a distinct implementation obligation, without
repeating the deterministic matrix as proof of a stronger guarantee. See the
Linux [openat directory-descriptor rationale](https://man7.org/linux/man-pages/man2/openat.2.html),
[renameat semantics](https://man7.org/linux/man-pages/man2/rename.2.html) and
[unlinkat semantics](https://man7.org/linux/man-pages/man2/unlink.2.html).


## Linux descriptor-bound no-replace checkpoint

`build/p06zg-link-publication001/link-publication-handoff001.json` records a
repair for a late source-name substitution after the final stage identity check.
Linux no-replace publication first calls linkat with the retained descriptor and
AT_EMPTY_PATH. ENOENT/EPERM/EINVAL can retry via a trusted `/proc/self/fd/<fd>`
link with AT_SYMLINK_FOLLOW; EEXIST refuses immediately. Neither path links the
mutable stage name. Missing procfs or unsupported descriptor linking can therefore
refuse publication. No unsafe pathname fallback is provided. Existing targets
remain protected by the kernel's no-replace link semantics.

The private test schedules replacement exactly at entry to linkat, after product
identity checks. It renames the original and inserts a regular file, symlink,
nonempty directory or nothing, or unlinks the original. Deferred and streaming
writers each run with existing/absent targets. Twenty actual-route cases, twenty
kernel-invalid-flag argument controls for procfs retry, and twenty additional
missing-proc-source argument controls total 60. All actual link calls reach the
kernel; the latter 40 cases substitute arguments and are explicitly instrumented
boundary evidence, not a claim that this kernel naturally rejects AT_EMPTY_PATH
or that procfs was unmounted. Tests check exact output/witness/caller bytes,
replacement-entry survival, safe refusal for an unlinked original/unavailable
routes, and unchanged descriptor count. The observed unprivileged native run
has effective UID1000/CapEff0, eight direct descriptor-link successes and eight
instrumented procfs successes. The old helper publishes the late replacement.

A separate WSL mounted-drive experiment found both descriptor routes can publish
the replacement object on its 9P bridge. Retain that actual bad output, logs,
source preimages and binaries. Before any no-replace link, the new Linux helper
uses fstatfs on the held file and refuses V9FS_MAGIC with operation_not_supported.
Four actual 9P controls cover both writers and existing/absent targets, verify no
link syscall, no destination change, owned-stage cleanup and no descriptor leak.
This is a safe refusal boundary, not implemented no-replace saving on 9P. Other
providers are not inferred accepted from native ext4/real tmpfs evidence. The
kernel link interface is documented in [linkat](https://man7.org/linux/man-pages/man2/link.2.html);
provider discovery is documented in [statfs](https://man7.org/linux/man-pages/man2/statfs.2.html).

The initial Windows regression hit the prior test fixture's 1024-handle bound
while trying to force an exact handle-number reuse. Preserve its native failure.
The private fixture now bounds the sweep at 16384, retaining its actual exact-reuse
and unrelated-resource preservation assertions. No product fault controls or
new third-party implementation/dependency are introduced.

The no-replace source link is descriptor-bound only on a provider that honors
that kernel interface. Replace-existing rename and identity-check-to-unlink races
remain open; this patch does not solve them, directory-fsync/crash durability,
actual device flush/close failures, safe 9P no-replace publication, other platform
qualification, Linux TIFF/desktop/product releases or full editor/Fiji acceptance.
