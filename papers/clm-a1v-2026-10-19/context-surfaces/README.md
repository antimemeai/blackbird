# Context surfaces catalogue

This catalogue lists every source surface the context touches or that touches the context, written for the
`arconaut-rd7.1` design (`docs/CONTEXT_LIFECYCLE.md`). It was read at `f7a79b1`. Two later rechecks are
appended to the files they correct:
- after the master merge, at `10827cf`;
- a replay correction.

| File | Covers |
| --- | --- |
| `core_state_persistence.md` | ContextStore state and records, replay as playback, the saved state and checkpoints, RetainedState, session lineage and the backstop, archived originals and locators |
| `request_provider_path.md` | `request()` assembly, prompt cache, every provider adapter's wire conversion, tool dispatch and result shaping, the other request makers (colleagues, participants, Jev, backstop, station), and every provider rule hard-coded in C++. Its three parallel read-only passes (adapters, tools, other request makers) are folded in. |
| `model_lua_operator.md` | Model tools, Lua bridges, shipped programs and workflows, the TUI and operator commands, the guidance text, tests and check scripts that pin the current behaviour |

Unconfirmed points are marked in place. Each file collects its own at the end.
