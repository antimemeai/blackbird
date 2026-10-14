# Terminal custody: one whole code and oracle review

2026-10-14. Independent reviewer: job_blackbird_map. This is the one requested
code/oracle attack of the H terminal custody unit, followed by the owner's finite
repairs and direct recheck. It does not open a second review panel or a new
qualification campaign. No product/test edits, builds, compiler workers or native
test executions were performed by this reviewer.

The scope is the terminal custody subplan and its sole plan-review dispositions,
`terminal_custody.hpp/cpp`, the confined TerminalUI startup/editor/poll/retained
resize integration, the narrow exported RawCommand mask/action/reference seams,
RawUiFrame's explicit preheld-DATA seam and immutable H field description, and
the whole `terminal_custody_raw_test.cpp` and `terminal_custody_native_test.py`.
The previously completed general UI and command-wake owners are not reopened.
Plain Main output is a separate unit and is not covered by this report.

The owner's supplied evidence is `context/station-owner/h-second-native.log`,
`h-third-cli-native.log`, and `h-final-first-syntax.log`, plus the final bounded
fixture relink/native handoff. Those green runs establish their actual exercised
cases; the four gaps below explain what they do not establish. The independently
authorized invalid-reference correction was completed during this same pass:
shared `raw_reference_valid`, RawCallerTrace constructor enforcement, UI delegate
validation and H `selected_context` reject invalid supplied references before
effects. The owner reports the final boundary strict-green in
`h-context-first-syntax.log`. This known correction is not another review layer.

## HC1 — resize observation uses the ambient issuer

At the reviewed source, `src/terminal_custody.cpp:410–421`,
`TerminalResize::observed()` checks its creating thread but does not construct
`RawUiOwnerBinding`. It clears the flag and calls `wake_->clear()` in the caller's
current binding. `install()` at 378–400 and `uninstall()` at 423–435 do bind the
retained owner. `RawCommandWake::clear()` at `src/raw_command.cpp:342–372` first
claims and publishes its lifecycle request from the current issuer; its later
internal wake-owner binding does not change that earlier admission/request.

Concrete trigger: create/install the selected resize owner, keep its creating
thread, enter a different RawThreadBinding whose CONTROL is exhausted, then
deliver/observe the real SIGWINCH. The foreign issuer can refuse the lifecycle
claim even though the creating issuer has room. With available foreign room the
2060 lifecycle record instead belongs to that foreign issuer. This violates the
retained H creating-issuer contract and is observable without a cross-thread
misuse or synthetic syscall result.

Repair: bind the retained owner for the whole actual observed/clear/rearm
operation and restore the enclosing binding afterward. Preserve the existing
signal and endpoint behavior. The direct oracle must use an actual signal and
endpoint, a different saturated ambient issuer, and independently check the
creating lane's lifecycle record, real rearm, and restored ambient binding.

`tests/terminal_custody_raw_test.cpp:589–603` currently observes while the
creating issuer is still bound. Lines 610–615 check foreign-binding destruction,
which cannot detect the missing binding in the earlier observed method. Extend
that same real resize case rather than introducing another wake framework. The
H owner acknowledged the finding and the intended contract.

## HC2 — an explicitly unselected resize owner activates ambient capture

At `src/terminal_custody.cpp:367–376`, the resize constructor always creates a
signal-mode RawCommandWake even if its explicit context is absent or has no
parent. The whole H call and RawUiOwner are unselected, but
`src/raw_command.cpp:206–219` obtains a prepared producer, binds it and captures
the wake setup whenever an ambient runtime is active. For an unselected owner,
`RawUiOwnerBinding` at `src/raw_ui.cpp:291–295` makes no binding change. Its
action/mask/native cleanup paths can consequently also emit using the ambient
issuer, without selected H cleanup credits.

The trigger is an active ambient runtime plus an explicitly absent H context.
The direct API's documented opt-out is not respected: it creates a new producer
and 205x/206x records despite having no selected H parent; subsequent uncredited
ambient capture can interfere with an unrelated lane. This is not a claim that
an ordinary completely unbound process fails: default unbound preparation and
the disabled signal publisher have their existing no-capture behavior.

Repair: establish explicit disabled binding for the unselected H wake/action
lifecycle, while preserving real native signal behavior. Selected owners still
retain their creating issuer and existing paid custody. Do not change the
process-wide capture mode or replace the already owned wake implementation.

The finite oracle is actual construct/install/raise/observe/uninstall/drop with
an active ambient runtime and no H parent: no extra producer or wake/H records,
no unrelated lane fence, and the actual acquired prior SIGWINCH handler restored.
The current resize fixture always supplies `parent()` and cannot distinguish
this leak. The H owner accepted the explicit opt-out correction.

## HC3 — editor CString sources omit the actual stored terminator

The editor preserves counted business strings but then references them as native
CString operands. At the reviewed `src/terminal_custody.cpp`:

- 731–743 captures temp environment role1 and native directory role8 excluding
  NUL; the latter feeds `stat(directory.c_str())` at 752.
- 771–791 captures template role2 and mutated path role3 excluding NUL;
  `mkstemp(path.data())` uses the former and later open/unlink use the latter.
- 855–872 captures editor environment role4 and command role5 excluding NUL;
  `args[2]` is the existing command's actual native buffer. By contrast the
  literal `/bin/sh`, `sh`, and `-c` operands already include their stored NUL.
- 1052–1059 captures the borrowed `what()` role9 CString excluding its NUL.

These source references therefore do not preserve the complete actual native
operand. The source/native ownership contract must explicitly distinguish that
boundary from counted originals. Draft input4120, returned draft role6, acquired
read chunks role7 and composed diagnostic role10 remain counted byte spans;
their lengths and embedded bytes must not change.

Repair roles1/2/3/4/5/8/9 using the actual existing buffer and native length plus
its existing terminator, with exact additional DATA credit before acquisition
or construction. Do not append or synthesize a zero byte, or scan a new metric
representation. An absent environment role remains absent with no invented
bytes. Keep business path/command construction semantics unchanged and update
the literal field description and plan to state these extents.

`tests/terminal_custody_raw_test.cpp:750–764` compares role4 to the editor string
without NUL and only checks a role5 prefix/quote. This makes the oracle agree
with the omission. Tighten the same native editor cases with independently
declared complete CString length/bytes and exact terminating byte for the
native-linked roles. Continue independently checking counted draft/chunk bytes,
native source references and unlink/close/child ownership. The CLI parser's
counted role6/7 assertions remain valid and must not be converted to Cstrings.
The H owner accepted the boundary correction and the exact +1 prepayment.

## HC4 — failed construction omits rollback state and can restore twice

`TerminalMode::resume()` at `src/terminal_custody.cpp:233–239` installs raw
termios, sets `active_ = true`, publishes4106 and then sends the entry control
packet. If that later operation fails, the constructor's inner catch at
208–214 restores termios and conditionally clears `active_`, but emits no4106
restored state. The outer catch at 216–219 can independently restore again and
does not update `active_` from its actual native result or publish4106.

Concrete paths are entry packet failure after successful tcsetattr, and failure
of the first constructor restore while `active_` remains true. The latter
reaches a second native restore from the outer catch. Failure of the enclosing
constructor terminal publication can also enter the outer restore directly.
The native4104/4105 calls are recorded; the defect is the missing corresponding
owned-state change and duplicate rollback path. The last recorded state can
remain active even when the actual terminal has been restored.

The plan declares one paid constructor fallback. Its actual cleanup bank also
contains unused exit-frame/attach/write credits, so this review does **not**
claim a demonstrated capacity failure from the second restore. Nevertheless,
two native attempts do not match the declared one-attempt path/equation and the
old constructor's single catch restoration. A failed native restore must not
be reported as successful merely to close that discrepancy.

Repair: consolidate the actual constructor rollback to one paid attempt, update
`active_` according to the actual tcsetattr outcome and emit its4106 state from
the preheld cleanup bank. Retain the original constructor failure. If native
restore fails, preserve honest still-active uncertainty rather than inventing
quiescence. No fresh unwind claim or additional output operation is needed.

The existing successful-construction/destructor short-write case checks a
different path. Add the same finite real-PTY constructor entry failure cut:
independently compare actual before/after native termios, assert the actual
restore outcome and4106 state, and require one rollback attempt. A forwarding
native failed-restore cut can check that failure leaves the recorded ownership
honest. The H owner accepted both the state omission and the duplicate path.

## Remaining source assessment and limits

The reviewed termios capture uses native flag, optional line, c_cc and speed
fields without struct padding or a new formatted representation. Notification
descriptors install ownership before publication, retain paid cleanup, and spend
the descriptor before a second syntactic cleanup can repeat close. Destructor
screen retirement is bounded by the existing eight writes and uses preheld
exit DATA/control before native termios restoration. The editor retains real
fd/path/child ownership before publication, captures positive native read chunks
before aggregation/limits, and uses existing lexical paid close/unlink/join paths.
These assessments do not repair or waive HC1–HC4.

The existing real PTY/native originals and controlled pipe-pressure cases are
useful specification oracles. Darwin PENDIN/FWASWRITTEN kernel-history handling
must remain explicit in their native comparison; captured original termios must
stay unchanged. This review does not add Linux qualification, recorder-death
qualification, raw fullscreen behavior, global allocator/codec work, or a plain
output owner. The current four findings and their finite direct checks are the
complete handoff for this one review. The owner implements and performs the
same bounded recheck; no review of those repairs is requested here.

## Same-review owner disposition and direct results

HC1–HC4 repaired without another review layer. Observation now binds the retained
creating issuer before clear/rearm, including with a saturated foreign ambient
CONTROL lane. All unselected resize construction/action/mask/observation/drop
uses an explicitly disabled existing binding; an active ambient runtime alone
creates no additional H/wake source. Roles1/2/3/4/5/8/9 now preserve their existing
CString terminator and prepay its extra byte; counted roles6/7/10 remain unchanged.
Constructor failure uses one actual rollback attempt, updates active state only
from native tcsetattr, and publishes4106 from its preheld cleanup bank.

The existing real resize/PTY/editor cases were extended directly. A real closed
stdout after native raw-mode acquisition causes constructor failure: two actual
native sets total (acquire plus one restore), independently restored termios,
and one inactive4106 rollback state. Three fresh-producer malformed supplied
context cases refuse before any H request/effect and preserve entry errno; an
actual invalid-identity fence is expected in these cases only. The absent-context
resize case still installs, receives SIGWINCH, rearms and restores the handler
without H/wake capture. All existing editor source cases now require complete
native CString extents. The forwarding short-write fixture exercises actual
2-byte native writes and owner retry/range accounting; it does not claim an
unchanged larger syscall requested a kernel short return.

Strict source/fixture compile and matching Debug link passed. Direct native and
actual CLI/PTy checks both passed in `context/station-owner/h-finite-native.log`
(11.43 seconds total), source-matched by `h-finite-build.log`. No workload,
performance, Linux-specific signal ABI or failed-native-rollback qualification
is inferred from these finite macOS cases.
