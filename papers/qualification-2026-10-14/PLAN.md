# Instrumentation qualification: first state report

Test unchanged source 200fc5e; HEAD 33e78d1 adds publication documentation only.
No optimization, production edits, library adoption, provider requests or evals.

1. Inspect existing native entrypoints/oracles, then run current macOS Release
and ASan/UBSan owner cases plus raw admission/channel/audit/runtime/metadata/
reader/recorder crash cuts and command cancellation/recovery integrations.
Existing successful131 Debug/all 194 TU checks are not repeated.
2. Run the same owner cases on Linux Release and ASan, including Linux native
long-CWD and interposition branches. Maximum6 aggregate compiler workers;
one writer per tree. Linux 4 plus macOS 2 where builds overlap.
3. Run existing PTY startup/input driver against current Release and the retained
previous segment Release serially, with private fresh sessions, no prompts,
ordinary caches and every sample retained. Compile/test load ends before timing.
4. Measure unchanged raw runtime through a context-owned native workload,
64/256/4096-byte bodies with exact independent event/body disk comparison.
Separate producer timing from drain/close and preservation lag; exercise bursts,
full/wrap and independent reader where directly supportable. Report unmeasured
cost categories rather than infer allocation/syscall counts from latency.
5. Record each failure and its actual testcase/platform. Do not weaken assertions
or optimize to improve results before the operator sees/discusses state.

Linux archive driver currently omits scripts/check-main-input. For this run use
an ignored context copy with explicit repository root and that source included;
remove wrapper campaign timeouts (individual native case timeouts remain).
This is test packaging, not a product/tooling source change.
