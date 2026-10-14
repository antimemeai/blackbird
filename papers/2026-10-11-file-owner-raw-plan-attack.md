# File47–48: one independent whole-plan attack

2026-10-11. Reviewed the proposed, unimplemented
[subplan](2026-10-11-file-owner-raw-subplan.md) against current LocalTools,
filesystem/native acquisition sources and raw admission/publication machinery.
Read current workspace BLACKBIRD.md, repository AGENTS.md and
docs/BOUNDED_HARDENING.md. This is the requested single plan review, not an
implementation assurance result. No production edits, builds, provider calls or
reference execution. Verified all nine manifest entries' byte lengths and SHA-256
against their local source bytes; all match. Read the existing Mac/Linux path
study source and recorded output, without rerunning it.

The whole owner is appropriate: explicit selected parent, actual native requests
and returns, existing business strings, preserved output before later failure,
and prepaid lexical cleanup belong together. The proposal needs the concrete
corrections below before implementation. No dependency adoption or alternate
libc path resolver is required by these findings.

## 1. Read behavior is not established by the nine path references

**Required correction.** Specify the selected stream implementation's business
read behavior before replacing it with `open`/`read`. The current helper is
`std::ifstream`, not a native 8192-byte `read` loop
([tools.cpp](../src/tools.cpp), lines 8–22). Its 8192-byte application request is
not evidence of a corresponding kernel request. None of the nine pinned files
contains the filebuf/stdio read implementation. Plan lines 88 and 172 describe a
new native request size; lines 175–178 promise preserved caller behavior without
giving the mechanism or complete baseline.

The installed Mac SDK libc++ source makes this a substantive issue:

* `usr/include/c++/v1/fstream:759–813`: filebuf underflow obtains bytes through
  `std::fread`; zero bytes leave its return at EOF. There is no `ferror` distinction
  in this branch.
* `streambuf:311–337`: inherited `xsgetn` fills an application request through
  repeated buffer extraction/underflow.
* `istream:896–917`: a short `sgetn` sets both failbit and eofbit. An exception
  sets badbit instead.

Consequently, a native read error that reaches the ordinary char-filebuf path
as zero `fread` bytes can look like EOF to the current helper. A positive prefix
followed by an error can produce a successful prefix string. This is a source
deduction, not a newly executed fault test. An owned loop that throws on every
negative `read` changes that outcome; an unconditional EINTR retry changes it
again. Do not assume that the baseline EIO case necessarily produces the
`Error` promised by the proposed oracle. Keep native failure distinct from the
business helper's actual outcome, even when the old business outcome is success.

**Direct oracle.** Run the old helper and candidate on the selected profiles
against actual first-error, positive-prefix→error, EINTR, short-prefix→EOF,
exact-8192 EOF, limit refusal and append-allocation refusal. Compare returned
bytes, exception class and detail, not merely whether the candidate captured a
prefix. Establish that the baseline interception actually reaches the stdio
native acquisition: a wrapper around public `read` need not catch libc's private
stdio syscall entry. A controlled FIFO and real interruption can independently
exercise the business short/error behavior. Acquire/pin the relevant stream and
stdio implementation if the selected loop relies on its precise behavior.
Either preserve these behaviors or explicitly change the scope and rationale;
do not call an incidental behavior change instrumentation-only preservation.

## 2. The envelope has no acquisition clock

**Required correction.** Remove or replace plan line 106's claim that existing
physical envelopes carry the native acquisition clock. Current
`include/blackbird/raw_format.hpp:8` reserves 16 bytes. The actual layout is
`length:u32, site:u32, ordinal:u64` in
`include/blackbird/raw_channel.hpp:55–58` and
`src/raw_channel.cpp:168–175`. `RawNativePublisher::emit:203–223` adds only the
provided body parts. An ordinal is ordering, not native acquisition time.
Line 158 also rules out a new clock. There is no field or credit for one in the
literal layout.

**Direct oracle.** The closed raw-file byte oracle must expect these exact 16
envelope bytes and declared body members. It must not infer timestamps from
recorder receipt or ordinal. If a clock is separately required, it needs an
explicit acquired field and admission cost; that is a changed plan, not a field
already supplied by this transport.

## 3. Consolidate output variants and their prepaid maxima

**Required correction.** The native-pair table at plan lines 212–227 still gives
`49+capacity` for getcwd, realpath and mkstemp output, while lines 229–235 add
used-length presence/length and revise those prefixes to 50/58. This later
correction is sound in principle, but the table is no longer authoritative.
Write the precise variant for each success/failure and put its maximum in the
table that implementation will follow. A 49-byte claim followed by a 58-byte
success header can refuse *after* native acquisition. A 50-byte unknown-length
failure header is also larger than that original claim.

The successful mkstemp result needs a specified variant too: the returned
descriptor alone does not establish the body used-length encoding. Define
whether its captured extent is the already-known full mutable template or a
known CString prefix; failure carries the initialized caller extent with unknown
native used length. Do not trim failed output or label its zero-filled tail as a
native pathname.

The fixed tool/helper/observer terminal maxima at lines 237–239 name totals but
not all member/branch layouts. Declare those layouts before implementation so
the totals can be checked literally. In particular, native errno is `int`, while
Blackbird `Error` has an 8-bit code and 64-bit detail; they are different fields.
The reviewed native integer/stat sums otherwise follow the supplied types:
`Q=65`, int return max49, ssize return max53, pointer-presence return max46,
stat success55 Darwin/57 Linux, close+END159 and unlink-reference146. These
numbers do not include an acquisition timestamp.

**Direct oracle.** Give the selected producer exactly the declared maximum and
then one byte less, including nested fixed terminals, held close/END and held
unlink. At insufficient capacity the relevant native call count must stay zero;
at sufficient capacity native success and each failure variant must publish
without a later capacity surprise. Inspect literal stored fields/extent rather
than recomputing expectations through the candidate encoder. The existing
`maximum_grant` subtracts held and reserved bytes
(`src/raw_channel.cpp:540–548`), so a request larger than remaining maximum must
refuse rather than wait for impossible space.

## 4. Make immediate resource state transitions explicit

**Required correction.** Prepaying cleanup does not by itself establish resource
ownership. Attach a successful open/mkstemp descriptor, its successful request R
and the actual mutated temporary name to a preconstructed lexical guard using
only nonthrowing assignments *before* return/body publication, checking capture
results, path work or allocation. A returned descriptor cannot be left outside
the guard while a raw publication failure is converted into an exception.
The current mkstemp→try region is `tools.cpp:34–38`; new instrumentation introduces
failure points that do not exist between those statements today.

Similarly, consume descriptor ownership immediately after the actual close
attempt and consume temporary-name cleanup ownership immediately after a
successful rename, before either return capture can fail. Otherwise a failing
close-return capture can cause a duplicate close of a reused descriptor, and a
failing rename-return capture can unlink a newly created file at the old temp
name. Current source marks `closed=true` before checking close failure
(`tools.cpp:58–61`); retain that property for instrumentation failure too.

Cleanup must continue to native unlink even if close request/return publication
fails, consume only its prepared claims, and never throw from guard destruction.
Restore original business failure after recording actual cleanup failures.
Resource END means lexical ownership ended; it does not establish successful
kernel close when the actual close returned an error. Retain native platform
uncertainty, including EINTR, rather than equating once-only cleanup with a
successful close. Held claims can emit through a local raw fence
(`src/raw_runtime.cpp:2040–2084`); fresh admission cannot. The issuing handle must
outlive all such claims and borrowed buffers, as the proposal already requires.

**Direct oracle.** Cut capture immediately after successful mkstemp/open, after
actual close, and after successful rename. Count the actual close/unlink calls
and descriptor incarnations. Require once-only close attempt, cleanup of the
original temp on pre-rename failure, no unlink of a replacement at the old temp
name after rename, and native rename success preserved even if the outer owner
fails. Also force close-capture failure followed by failed unlink: the unlink
attempt/result must remain distinct and the original failure must survive.
Use the existing operation17 post-rename allocation oracle for its existing
boundary; these capture cuts attack the newly introduced native boundaries.

## 5. Preserve literal GCC fallback decisions, not summarized limits

**Required correction.** Plan lines 137–144 need these exact branches from the
pinned `libstdcxx-fs_ops.cc`:

* Canonical's `is_symlink` performs lstat, then `read_symlink` performs another
  lstat (`163–265`, `1178–1224`). The second result is a fresh acquisition; do
  not reuse the first `st_size` or hide the intervening race.
* The counter starts at40 and tests `--max_allowed_symlinks == 0` *after* reading
  a symlink. The 40th expansion is rejected; the preceding native read still
  happened. “40symlink limit” alone does not pin this boundary.
* Readlink's literal rule is “if full and current size>4096, ENAMETOOLONG;
  otherwise double.” It is not an unconditional 8192-byte limit. The initial
  nonzero `st_size+1` may be a non-power-of-two or already above4096. A full4097
  request fails immediately rather than growing to8192; a size4095 can grow
  to8190. Preserve this branch rather than imposing a synthesized bound.
* The final `exists(result,ec)` false result clears the path without forcing
  ENOENT when ec is clear. The initial false exists does explicitly establish
  ENOENT. Those are different branches. Keep the selected filesystem wrapper's
  exception/error-code mapping, including `filesystem_error` from canonical/
  absolute, distinct from current explicit `Error{io,errno}` native calls.

**Direct oracle.** Exercise the ENAMETOOLONG fallback with separate first/second
lstat results, full readlink capacities around4096 and the 39th/40th link,
and a final existence race. Assert actual syscall count/arguments and the caller
outcome. A controlled test-only native fixture can supply these rare acquisition
results; unrelated real calls forward. Record every positive full buffer before
the existing growth/error decision.

## 6. Pin C operand extent and embedded-NUL behavior

**Required correction.** “CString length and original bytes” must state whether
length includes the terminating NUL. Tool path strings can contain embedded NUL;
current file branches do not reject them (`tools.cpp:185`, `191–214`). The
business `filesystem::path` storage can contain a suffix that its native C call
does not use. Preserve the whole already-acquired business path where selected,
but frame the actual C operand through its first NUL; do not label the whole
C++ string as kernel-consumed bytes or introduce a new rejection silently.
The mutable mkstemp input/output extent is a separate actual buffer, including
its initialized terminator. It must not be reconstructed from the original
unmutated template during failure cleanup.

**Direct oracle.** Include embedded NUL in pathname as well as contents, an
invalid-UTF8 pathname, empty path, trailing slash/dot, binary replacement and
observer throws. Compare current business exception/result and exact native
operands. Assertions should distinguish supplied initialized storage, known
CString length, and unknown native failure used length.

## Path substitution findings and scope

The references support the proposal's selected absolute-input rationale, rather
than a universal claim about `realpath(NULL)`:

* LLVM operations `82–107` makes the input absolute before native realpath;
  GCC `fs_ops.cc:163–192` does likewise and only selects its fallback for
  ENAMETOOLONG.
* glibc canonicalize `296–305`, `372–375` checks each constructed full prefix
  using native readlink/directory checks. Linux namei `178–187` rejects a C
  pathname reaching PATH_MAX without a terminator. The glibc caller-output branch
  `387–409` itself distinguishes fixed-output copying from NULL-output strdup.
  The standalone relative getcwd-only long-result route is not the selected
  absolute-input call. I found no source counterexample to the plan's stated
  selected Linux bound.
* Darwin realpath `81–117` uses PATH_MAX both for NULL-output allocation and its
  internal arrays; its later native case/mount/symlink behavior is reason to
  retain the native function. Its installed manual's variant notes mean this
  pinned reference is evidence, not a claim that every installed binary was
  built from that exact commit.
* Both current_path implementations use the platform NULL/0 allocation extension.
  Both getcwd manuals permit caller-buffer ERANGE growth and leave failed buffer
  contents unusable as a path. Initialized whole-extent capture with unknown
  used length is appropriate. Do not cap getcwd/absolute at PATH_MAX.
* Linux realpath's ENOENT/EACCES partial-output extension and Darwin's failure
  problem-path output are not successful canonical paths. Capture the actual
  initialized failure extent with unknown used length, as proposed.

Existing Mac14/Linux17 examples show the recorded selected behavior, including
platform differences for empty absolute, trailing file slash/dot and deep cwd.
They complement these sources and do not settle the read-loop or newly added
capture/cleanup failure boundaries. Scope native call coverage as the calls the
owned adapter issues; retaining realpath/getcwd does not instrument every private
libc-internal acquisition.

The direct whole-owner fixture should cover the corrections above along with
the already-proposed unchanged native bytes, read-limit/append refusal, short
write/EINTR/zero, fsync/close/rename/unlink errors, edit conflicts, observer
exceptions, sticky admission cancellation and selected/unselected parent paths.
These are concrete repairs and direct oracles within this unit. They do not
require a second plan attack, a review of this report, or unrelated assurance.


## Implementation disposition, 2026-10-13

All six plan findings are incorporated in the authoritative file subplan and
selected owner implementation. Five additional pinned stream/stdio sources and
restoration records extend the original nine-entry acquisition manifest. The
selected loop preserves 8192-byte business append batches; Darwin negative
reads remain EOF/prefix outcomes, while GNU retries EINTR and preserves its
stream Error mapping. Native failures remain distinct captured returns in both.
There is no clock or producer conversion.

The genuine published-owner red is `context/file-owner-unit/limit-red.log`:
actual shared libc++ fread acquires16785408 source bytes through the fixture dyld
interposer, the existing business limit refuses, and the independent raw-disk
oracle fails because those originals are absent. Matching old headers, archives,
source, dylib, binary and hashes remain in `context/file-owner-unit/published`.
Initial public-read/main-image wrappers did not reach that private stdio path;
their zero-acquisition attempts are explicitly not evidence.

Native candidate checks compare unchanged binary read/write/edit bytes and real
field/ref ordering, observed failures and exact credit boundaries. Descriptor
custody is attached before capture; native close and successful rename consume
ownership before capture. Damaged actual shared publication after open, mkstemp,
close and rename produces8001 and still attempts held cleanup. Close-capture
failure plus native unlink EACCES preserves the original conflict and the
separate actual cleanup return. Successful rename followed by failed return
capture leaves a genuinely different old-temp-name replacement untouched.
These controlled publication cuts stop the actual recorder/supervisor and
restore the fixture word before resuming; they do not claim ordinary transport
failure is expected.

Exact independent grants are41+50 for tool admission,50+184+77+53 for a native
read with held cleanup, and16+24+1+8+8192 for positive original DATA. Each has a
maximum and maximum-minus-one native acquisition oracle. Native Call releases
unused terminal credit immediately after its actual return, rather than holding
four success-branch bytes until helper exit. No new wait or native effect is
introduced. Typed fixture-only stdio callbacks compare genuine first and partial
EBADF against the unchanged helper. Embedded NUL preserves whole business
arguments separately from first-NUL native operands; invalid UTF8 uses the
actual platform result rather than assuming APFS accepts the name.

GNU fallback native fixtures are conditional on Linux: fresh second lstat,
full4095/4096/4097 readlink boundaries, genuine39/40-link chains and actual final
exists deletion. They are source-written; Mac compilation does not execute or
qualify that platform-specific branch. Whole code/oracle review is still the
single next independent layer. Workload measurements and supported profile
qualification are outside this operator-authorized unit window.
