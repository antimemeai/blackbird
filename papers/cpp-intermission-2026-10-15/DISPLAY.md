# One displayed representation, one cell map

The audit's T1–T4 are one broken invariant: three consumers infer different
spacing from the same bytes, and style/fallback boundaries change those bytes.
The owned repair introduces a streaming display cursor over either contiguous
source or styled spans. Each item carries typed source byte range, emitted scalar
and optional tail, cell extent, and explicit replacement. Source/edit positions
remain bytes. Wrapping, rasterization, padding and caret placement use that cursor.
The cursor stores a borrowed input and bounded scalar lookahead; it never copies
history or allocates per scalar. Styled runs are formatting metadata, not a
segmentation boundary. A cluster uses its first spacing scalar's ink.

The selected existing profile remains Unicode-style joined emoji, host locale
scalar widths, narrow ambiguous characters. This repair does not negotiate2027,
claim full UAX29 or make the Alacritty engine implement Unicode grapheme widths.
Physical Unicode-profile emoji checks explicitly enable2027 on Ghostty; malformed
suffix/leading/style/fallback checks run both named pinned engines. A legacy peer
still has the existing configuration gap recorded by the audit.

VS16 promotion requires an immediately eligible Emoji-property base;
positive-width modifiers attach only to an immediately eligible Emoji_Modifier_Base.
A bare keycap mark preserves presentation width; digit/#/* plus VS16 obtains its
observed two cells through the guarded selector. Existing pinned
Unicode17 emoji data supplies those guards, with license/source manifest retained.
EP Extend* ZWJ EP joining remains guarded by the existing normative properties.
Unattached zero-advance scalars are omitted. Combining tails across style spans
attach to the existing base. A cluster exceeding4096 bytes becomes U+FFFD with
one actual cell BEFORE clipping/wrapping/caret decisions. Control/invalid bytes
retain the existing printable hex sanitizer; no raw terminal-control adoption.

Direct red checks precede implementation: literal X positions for invalid VS16,
modifier and keycap bases, leading zero-width marks, styled combining continuation,
oversized wide replacement, overwrite and last-cell clipping; valid modifier,
text-selector, flag and joined-emoji counterexamples. Wrapping must keep the valid
joined sequence intact, and physical cursor/right-border coordinates are literal,
not values calculated by an owned width scanner. New regression packet checks
replay exact production painter bytes through the already pinned external engines.
Readable component expectations continue to guard intentional presentation.

Root reviews this design and the red observations before product edits, owns the
shared build configuration and performs the one bounded implementation challenge.
No independent framework, full grapheme-editor rewrite, fuzzing, provider calls,
live activation or unrelated assurance scope is opened by this unit.

## Additional reviewed boundaries

The root challenge added literal digit/#/* plus VS16 observations. Under the
explicit2027 Ghostty profile each places X at2; the legacy Alacritty backend
places X at1. Valid modifier/keycap/joined sequences similarly retain their
known profile differences. The external packet check makes Unicode-profile
assertions only on Ghostty, and checks malformed suffix/zero/style/fallback
positions on both; it does not silently declare the backends equivalent.

A cluster wider than the whole destination row becomes one U+FFFD cell through
`display_fit` before wrap or caret placement. A cluster merely too wide for the
remaining row moves intact to the next row. A trailing source newline maps to the
following empty row. Full-source caret mapping chooses the trailing edge for a
byte offset inside a cluster, without changing editing's original byte offsets.
Vertical motion uses the same sanitizer expansion and inverts its selected byte
boundary, preserving the existing two-cell tab and four-cell literal hex policy.
No sanitizer allocation occurs for already-safe text; only the changed line/draft
is copied when escaping is required. Transcript history is never cloned.

Normative property data is reused from the intact, previously acquired Unicode17
emoji-data.txt (SHA256 and restoration in
[UNICODE_SOURCES.tsv](../ultramarine-repair-2026-10-15/UNICODE_SOURCES.tsv)). The new
owned table contains418 Emoji ranges and50 Emoji_Modifier_Base ranges. It shares
normative source material with the small independent VT oracle, while literal
physical coordinates supply the independent width oracle. No source code or
terminal library has been adopted.

## Direct observations so far

The permanent native corpus was written and linked against the unchanged Debug
libraries before product edits. It failed12 named assertions: invalid VS16,
modifier/keycap bases, leading combining/format, split-style combining/emoji,
wide replacement footprint, joined wrapping, final-cell fallback and overwrite.
The exact executable/log/packet remain in ignored `context/.../display/old-red*`.
A separate unchanged main executable also fails the actual105×52 composer caret
assertion after typing `ab👩‍💻X` into the explicit Unicode-profile Ghostty PTY;
its SHA and complete driver calls are retained under `display/composer-old*`.
Neither direct reproduction invokes a provider or resubmits the real session.

The initial repaired native display/chat/terminal tests passed3/3. Literal painter
packets passed the appropriate named-profile checks on both engines, and the
owned incremental VT oracle passed its new literal malformed-suffix/leading/style
cases at every input byte partition. Final width1/newline/vertical-mapping and
actual-composer checks follow the final focused build; those results are not
claimed by the initial pass. One compile warning in the edited sanitizer's mixed
boolean expression was resolved with explicit parentheses, without suppression;
a later shared build stopped on root's in-progress mailbox type migration. Both
setup outputs remain saved, and the same focused build will resume after that
conceptual owner's migration is ready.

## Final focused checks before the one independent implementation challenge

The completed integrated Debug build compiled the final display source and test
objects after their last edit. `chat_view`, `terminal`, and `display_text` pass;
`display_text_ghostty` and `display_text_alacritty` pass their literal physical
cells and borders. The production `tui_microsoft` case passes, including the new
Ghostty2027 composer105×52 and12×24 cursor/right-border assertions. Its unchanged
main executable control had failed that exact105×52 caret assertion.

The first combined run returned6/7 because the two explicitly new readable
fixtures had no baseline yet. Only those two new files were created and inspected;
the12 existing baselines are unchanged. The subsequent producer/expectation check
passes14/14. The independent streaming VT oracle passes existing and new literal
cases, including every byte split of malformed suffix/leading/style inputs.
Exact run logs and complete external driver calls are retained in ignored
`context/cpp-intermission-2026-10-15/display/`; the root's final source gate,
Linux/sanitizer counterpart and matched performance round remain separate checks.


## Independent challenge and second-layer fixes

The root's single implementation/oracle challenge tested immediate emoji authority,
with direct raw packets on the named pinned engines. Ghostty2027 ignores U+200B:
`👍 U+200B 🏽 X` places X at2 (Alacritty legacy places it at4). That proposed
barrier finding is rejected for the selected Unicode profile, and its2-cell
counterexample remains in the permanent corpus. The challenge instead found two
actual mismatches: `👍 VS16 🏽 X` places X at4 on both engines, and
`👩 ZWJ 1 U+20E3 X` places X at3 on both. The pre-fix compiled library fails both
new literal assertions in `display/challenge-red.log`.

The cursor now replaces immediate emoji authority on emitted tails such as VS16,
while retaining the directly observed ignored-ZWSP behavior. A bare keycap no longer
promotes its base; qualified digit/#/*+VS16 retains the observed two cells through
the selector guard. Source range, emitted bytes and resulting cell map stay shared.
The independent streaming oracle has literal counterpart cases rather than deriving
its expected coordinates from production. Internal chat wrapping now calls the same
whole-row `display_fit` even though ChatView's present minimum width makes its
one-cell case unreachable; existing shared-cursor/wrap/caret APIs cover that narrow
fallback directly, without adding a test-only public wrapping API.

Challenge raw packets/cells/commands, exact pre-fix red output, corrected build and
affected green logs stay under ignored `context/.../display/challenge-*`. This is
the second remediation layer, not a new or recursively reviewed assurance campaign.

Second-layer focused build and all6 affected CTests pass (`challenge-green.log`,
15.21s): native display/chat/terminal, both physical packet engines, and production
Microsoft scenario including the actual composer cursor and right border. The
streaming oracle's literal challenge cases pass separately. There are no pending
findings from this unit's single independent review. The root owns remaining
integration, physical blocked-output, Linux/sanitizer, performance and source gate.

## Source-gate correction: color original source before escaping

The full source gate caught an existing, meaningful workflows regression:
`ESC + ultracode` must display literal `\\x1bultracode` with `ultracode` in its
registered keyword ink. Sanitizing before registry matching had merged the escape's
printable letters with the token, so the existing assertion failed. The assertion
was retained unchanged. Matching continues to consume the original raw source.

The corrected adapter colors the raw logical line first. A single sanitizer walk
decodes the contiguous original, carrying its original byte-span style boundaries
to safe output. It does not decode spans independently or infer matching from hex
escape text. The shared safe-scalar/emission machinery also serves plain text and
the inverse byte map; the existing display cursor then segments across safe style
runs. Added counterexamples check actual control escaping, keyword ink across
five-column wrapping, a raw control splitting a would-be token, and NUL as a raw
matching boundary. This corrects the actual failed integration contract within the
same repair/recheck layer; it does not weaken the old oracle or open another review.

The corrected build and6 affected checks pass (`workflow-color-green.log`,18.29s):
workflows (including its unchanged raw-control keyword assertion and the new
counterexamples), terminal, display_text, both external packet engines and the
actual-main Microsoft composer scenario. The final source requires the root's
updated Linux/performance counterpart; earlier source qualification remains
retained with its original provenance. No source or test assertion is weakened.
