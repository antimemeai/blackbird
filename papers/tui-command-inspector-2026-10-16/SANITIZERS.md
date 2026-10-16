# Command inspector scoped sanitizer results

Final stable tranche sources were checked using the existing pinned macOS Clang 23.1.2 Debug C++20 sanitizer profiles. Builds used parallelism 3; CTests ran sequentially with existing deadlines. No suppressions, deadline changes, or product edits were made.

- ASan + UBSan (`build/asan`, address/undefined, no recover): all 12 selected cases passed in 16.75 seconds. Targets: tui_inspector_test, tui_components_test, command_observation_test, command_inspector_test, terminal_test. Cases: terminal; tui_components; tui_inspector; command_observation allocation, ranges, ranges-raw, busy, lifecycle; command_inspector semantic, raw, pressure, runtime-pressure. Exact build/check output: asan-build.log and asan-checks.log.
- TSan (`build/tsan`, thread): all 6 selected cases passed in 12.76 seconds. Targets: tui_inspector_test, tui_components_test, command_observation_test, command_inspector_test. Cases: tui_components; tui_inspector; command_observation busy and lifecycle; command_inspector semantic and runtime-pressure. Exact build/check output: tsan-build.log and tsan-checks.log.

No sanitizer diagnostic or test failure occurred in these scoped runs. These results cover the selected semantic, observation ownership/lifecycle, actual worker/raw adoption, and physical recording-pressure paths; they are not a full-suite sanitizer claim. Both build directories are idle. Original build/check logs remain under ignored context/tui-command-inspector-2026-10-16/.
