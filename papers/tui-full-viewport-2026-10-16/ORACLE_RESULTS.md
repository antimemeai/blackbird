# Actual application viewport results

## Original RED

Before root edited or rebuilt the application, `scripts/check-tui-viewport`
opens the actual retained-result instrument at101x30 and resizes to32x12. It
requests the complete physical cell plane and checks every cell of final column31.
The single-line composer requires right corners at `(31,8)=╮` and `(31,10)=╯`,
vertical border `(31,9)=│`, and a blank physical bottom-right `(31,11)`.

Both profiles fail on the unchanged application. Ghostty retains v at `(31,6)`,
l at the required top-right corner and a horizontal line at the required lower
corner. Alacritty leaves the required right-side box cells blank. These are
direct physical observations, independent of a logical grid or a full-render
reference. The isolated local turn workflow is never invoked. Original state,
cell responses, recording, identity and command history remain under ignored
`context/tui-full-viewport-2026-10-16/red/`; both own sessions and daemons close.

## Oracle corrections before final qualification

The first repaired-application attempts fail only one mistaken expected caption
character: column31 of `RETAINED RESULT LAB · Source active` is t, not v. All
other final-column/border/style assertions already pass on both profiles. Correct
the literal expectation, preserving original attempts under `green/`. This typo
does not account for the original application's missing/stale box corners.

The next attempts reach Unicode editing and reject the proposed whole-grapheme
Backspace expectation. The current native editor's `previous`/`next` helpers step
UTF-8 scalars. Backspace after e+combining-acute therefore removes acute and
retains e, exactly as both profiles observe. This viewport unit preserves that
existing editor contract: assert retained e, delete the following narrow scalar,
backspace e, and backspace the width-two scalar while checking its entire old
cell footprint. No editor implementation changes. Original attempts remain under
`green-corrected/`. Root explicitly agrees to this bounded scope.

The final episode also checks midcursor draft retention; full-width composer
rows after growth/shrink; literal Unicode cells/cursor; tiny8x3 output across all
eight columns, tiny2x2 and1x1 physical bottom-right glyphs; return from tiny;
instrument close/reopen; and exit0 with terminal modes restored. The local
unexpected-turn sentinel is checked after exit and again after cleanup.

## Final complete owning recheck

After root fixes its graphics-cache and resize-back recovery findings, the same
complete script passes both adopted profiles. Each episode passes all18 semantic
observation phases, exit0 with no signal, restored terminal modes, no unexpected
turn sentinel and clean owned-session/daemon cleanup. The final executable
SHA256 is
`c63c42ad42ea7a464bcc94ab0b697eb48d1c23d83def310cb35879581c086257`.

| Profile | Result | Original artifacts under `context/tui-full-viewport-2026-10-16/` |
| --- | --- | --- |
| Ghostty | 18 phases, exit/modes/no-turn/cleanup pass | `final/ghostty-a93efcf53a/` and `final-ghostty.log` |
| Alacritty | 18 phases, exit/modes/no-turn/cleanup pass | `final/alacritty-008493460f/` and `final-alacritty.log` |

The original cropped Debug executable was
`a844a44b1aeb7e0485e41f34ab864ca198f462bda19d662162f9e20a14ad80de`;
the initial repaired executable used for the two oracle corrections was
`0c30020a8a436cc1ef6dbc6f9b199dc59512b3bad7518f23507985508bbd8e66`.
All eight attempts remain, including the original failing observations and
incorrect oracle assumptions. No failed attempt is relabelled as a clean pass.

Root's one direct script review closes with no finding after the caption/scalar
corrections. Python compilation and relevant diff checks pass. This lane performs
no native builds, dependency adoption, live-user activation, AFL work or repository
index/Beads/commit changes. Root owns source-gate qualification and delivery.
