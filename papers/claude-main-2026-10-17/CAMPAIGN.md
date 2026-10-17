# Native Claude and usable host installation

Operator2026-10-17 requests closing the existing owned-Claude live-validation
and main-model protocol gaps, then a repeatable install/update path so
`blackbird` works on this host. Work starts from master `d1a8634`, on
`work/claude-main-install-2026-10-17`.

The existing inventory and current official authentication/SDK docs ground this
work. Keep the existing account owner, refresh serialization, revision fences,
private transport configuration and exact raw originals. No SDK or runtime
dependency is adopted. Main-model Messages implementation is one conceptual
owner; install/update packaging is a separate independent owner. Each writes its
short plan, runs actual direct RED before implementation, then GREEN. Root
qualifies the browser-login-to-OAuth-Messages seam and actual live use, reviews
the returned changes once, fixes concrete findings and runs the required source
gate. Two layers of remediation/recheck only; no review of reviews.

Auth status initially shows only an owned Paper account, no Anthropic account.
The explicitly requested live follow-through now authorizes account setup and a
bounded real Claude request. A dedicated native sign-in was opened in Terminal;
browser consent and callback codes remain there, never in chat/model tools.
Operator reports completion; verify saved metadata before dispatch. Do not read
Claude/Codex credential caches or substitute separately billed API access.

Root's auth checks cover successful PKCE copy-code login followed by an actual
OAuth Messages request, rotated JSON refresh followed by new Bearer use,
negative callback state/denial with no token exchange, transient cooldown and
terminal refresh failure. These qualify existing behavior; only a concrete
failure warrants an auth implementation change. Synthetic tokens are fixture
values, not account credentials. Live validation remains an independent claim.

Main-model qualification must establish multi-step tool use, continued context,
opaque thinking/signature preservation, stop/error/cancellation behavior, usage
and original response bytes. Use the shared engine and native provider boundary;
do not remove the startup guard without a real adapter. Installation must pin a
coherent generation of binaries/helpers/assets/Lua, preserve invocation cwd and
operator settings, switch atomically, and work independently of the checkout's
future state. Test in isolated prefixes before activating the requested host
installation. Existing `~/.local/bin` is already on PATH and has no `blackbird`.

Keep source/action/result notes here and in owner plans; issue updates and
supported backups accompany delivery. The campaign adds no TUI implementation
tranche, so it does not repeat settled TUI performance measurements. Leave the
operator's untracked artwork and the separately launched AFL campaign alone.
