# arconaut-e8n root cause: the missing 1302/1303 records are an analyzer artifact

2026-10-20. Read-only investigation. Nothing was fixed.

## Answer

The 1302 (HTTP status) and 1303 (adapter terminal) records exist for all 15
requests. Each one has HTTP status 200 and terminal branch 1 (returned). No raw
bytes were lost. Hypothesis (e) holds: the stream-timing analyzer discards the
records it had already read.

The analyzer is `~/projects/blackbird_runs/bin/stream-timing/timing.py`. At line 51,
the 1300 handler replaces the request's dictionary outright:

```python
reqs[key] = dict(lane=bytes(lane), ord=o, timeout=timeout, present=present)
```

The 1301, 1302 and 1303 handlers merge into that same dictionary with
`reqs.setdefault(r, {})...`. If a request's 1302/1303 come before its 1300 in the
`blackbird-raw-reader records` stream, the 1302/1303 handlers create the entry
first. The 1300 handler then overwrites it, and `http`, `term_branch`, `term_code`
and `term_detail` are lost. The 1301 fields (url, model, effort) survive because
1301 always comes after 1300 in the stream. That is why the 15 rows have a url and
model but no status.

## Why 1302/1303 can come before 1300 in the stream

The reader emits records in the order the recorder preserved them. That is not
lane-ordinal order across partitions.

- `RawPartition` has three values: `data = 1`, `control = 2`, `failure = 3`
  (`include/blackbird/raw_format.hpp:7` at fd19b5d).
- `RawProviderTrace` emits 1300 through `data()` on the data partition
  (`src/raw_provider.cpp:24-36`, `:88`). 1301 also goes to data (`:110`, `:116`).
  1302 and 1303 go through `immediate(..., &*terminal_)`, which uses the control
  partition when credit is held (`:37-43`, `:125`, `:136`). 1400 is on data. Every
  positive stdout read (1404 with bytes) is on data (`:256`). Polls, spawn, wait,
  close, reap and release are on control.
- On every tour, the recorder visits each lane's partitions in the order failure,
  control, data. It takes at most one batch of up to 256 KiB from each partition
  (`src/raw_runtime.cpp:972-992` at fd19b5d: "Failure and control are visited
  before data, with at most one batch per partition"). This matches the design in
  `docs/INSTRUMENTATION_RAW_DESIGN.md` ("Check failure/control partitions first").
- The data partition carries each request's 80-170 KB 1301 body and all of its
  stdout bytes, so it falls behind control. If data is far enough behind, a whole
  request's control records reach disk before that request's data records. Those
  control records include 1302 and 1303, which are emitted right after the child
  exits (`src/provider_auth.cpp:543` `trace->http`, `:546` `trace->finish(1)`).

The analyzer handles child events correctly, because it collects them per child and
sorts them by ordinal. It does not handle the request record correctly.

## Evidence per affected request

All runs were built from `fd19b5d8d30b98995b0ae50dc0e0d2f43bd74fa3` (every
`meta.txt` in the campaign has this `checkout_commit`). The source was read at
that commit with `git show fd19b5d:...`.

Restored into
`/private/tmp/claude-501/-Users-patrickbeam-projects-blackbird/7a9f433b-5f15-4206-8722-548bda7b4ce9/scratchpad/e8n/`:
the full sessions of `a-t08-luna`, `r-t03-luna` and `r2-t07-paper`, and the raw run
`bb9fbf15fe9e6592d159ba4aab` of `r2-t05-paper`. The restore command was
`tar -xf RUN/session.tar.zst --use-compress-program=unzstd -C DEST`. Nothing under
`~/projects/blackbird_runs` was modified. Records were decoded with the campaign's
`context/cpp26-toolchain/build/release/blackbird-raw-reader records RUN`. For
a-t08-luna, its output matched `collected/*.records.bin` byte for byte (`cmp`).

"idx" is the record's position in the reader output. "ord" is the lane ordinal.
"p" is the partition. Every 1302 shows status 200 and every 1303 shows branch 1.

| run / raw run | lane | 1300 ord | 1612 ord (p2) | 1302 ord, idx (p2) | 1303 ord, idx (p2) | 1300 idx (p1) | 1301 idx (p1) | 1400 ord, idx (p1) | lane max ord |
|---|---|---|---|---|---|---|---|---|---|
| a-t08-luna / bbf929703426b0d6e9c14fefe9 | 5d3dd74c | 92004 | 92005 | 92568, 101326 | 92569, 101327 | 102416 | 104465 | 92008, 104466 | 248310 |
| a-t08-luna / bbf929… | 5d3dd74c | 96859 | 96860 | 97566, 105941 | 97567, 105942 | 111075 | 111076 | 96863, 111077 | 248310 |
| a-t08-luna / bbf929… | 5d3dd74c | 112501 | 112502 | 114026, 122606 | 114027, 122607 | 127716 | 129309 | 112505, 129310 | 248310 |
| a-t08-luna / bbf929… | 5d3dd74c | 119305 | 119306 | 121699, 133393 | 121700, 133394 | 137865 | 137866 | 119309, 137867 | 248310 |
| a-t08-luna / bbf929… | 5d3dd74c | 139371 | 139372 | 140084, 158442 | 140085, 158443 | 164803 | 164804 | 139375, 164805 | 248310 |
| a-t08-luna / bbf929… | 5d3dd74c | 158150 | 158151 | 159351, 177622 | 159352, 177623 | 183388 | 183389 | 158154, 183390 | 248310 |
| a-t08-luna / bbf929… | 5d3dd74c | 186215 | 186216 | 186943, 209768 | 186944, 209769 | 212976 | 213393 | 186219, 213394 | 248310 |
| a-t08-luna / bbf929… | 5d3dd74c | 191933 | 191934 | 192669, 218783 | 192670, 218784 | 223086 | 223087 | 191937, 223088 | 248310 |
| a-t08-luna / bbf929… | 5d3dd74c | 244691 | 244692 | 246658, 276312 | 246659, 276313 | 277992 | 278009 | 244695, 278010 | 248310 |
| r-t03-luna / bbceaaecbfe0e75cb425ba6724 | a9060308 | 197256 | 197257 | 198140, 257873 | 198141, 257874 | 261952 | 261953 | 197260, 261954 | 1621800 |
| r-t03-luna / bbceaa… | a9060308 | 254189 | 254190 | 256034, 355083 | 256035, 355084 | 367548 | 367549 | 254193, 367550 | 1621800 |
| r-t03-luna / bbceaa… | a9060308 | 279883 | 279884 | 281652, 389676 | 281653, 389677 | 400840 | 400841 | 279887, 400842 | 1621800 |
| r2-t05-paper / bb9fbf15fe9e6592d159ba4aab | 178a2d5b | 804241 | 804242 | 804782, 894918 | 804783, 894919 | 897592 | 897593 | 804246, 897594 | 2091045 |
| r2-t05-paper / bb9fbf… | 178a2d5b | 2025896 | 2025897 | 2026431, 2188922 | 2026432, 2188923 | 2192958 | 2192959 | 2025901, 2192960 | 2091045 |
| r2-t07-paper / bbf04ade69022e113d3e9935b4 | a37ea15a | 510181 | 510182 | 510814, 1110099 | 510815, 1110100 | 1112391 | 1112392 | 510186, 1112393 | 559638 |

In all 15 rows, and in no other request, the 1302/1303 idx is lower than the 1300 idx.
A request that is reported correctly (a-t08-luna, request 77295) shows the normal
order: 1300 and 1301 at idx ~85693, then 1302 at 87128 and 1303 at 87129. The
check script found exactly 9, 3, 2 and 1 such requests in the four runs, which
matches the bead's counts. The other raw runs of these sessions
(`bb40077e…`, `bbb2e513…`, `bb9ee01f…`) had none. The 8.9 GB
`r2-t05-paper/bbc76360…` was not restored, because the two r2-t05-paper cases
were both found in `bb9fbf15…`.

Each request also has its full child sequence: 1401 spawn, 1403 stdin write,
1404 reads ending in EOF, 1405 waitid exit 0, then 1302, 1303, 1405, 1407, 1408
reap and 1409 release. For example, for request 92004 these are child ord 92013
through 92575. Re-running the analyzer with only line 51 changed to
`reqs.setdefault(key, {}).update(...)` turns all 23 a-t08-luna requests into
`(200, 1, 0, 0, 0, True)`. That scratch copy is
`/private/tmp/claude-501/-Users-patrickbeam-projects-blackbird/7a9f433b-5f15-4206-8722-548bda7b4ce9/scratchpad/e8n/tools/timing_patched.py`.

Timing relative to process exit is irrelevant here, because nothing is missing. For
the record, none of these requests ended its run. 14 are in the middle of their
lanes. The last one, a-t08-luna 244691, is followed by about 3,600 more ordinals on
its lane.

## Hypotheses

- (a) No terminal credit: rejected. `body` returns early if `!terminal_`
  (`src/raw_provider.cpp:100`), so the 1301 record proves `terminal_` was held.
  The 1612 record proves the link claim succeeded. The 1302 and 1303 records are
  present themselves.
- (b) `raw_emit` failure and fence: rejected. `raw_fence` would emit site 8001 on
  the failure partition (`src/raw_runtime.cpp:2140-2150`). None of the four raw
  runs has any failure-partition record.
- (c) Loss between producer and recorder: rejected. In all four raw runs, every
  lane's ordinals are exactly 1..max, with no gaps and no duplicates. Within each
  (lane, partition), ordinals increase in stream order. The affected requests are
  not at process exit.
- (d) A different code path: rejected. All 15 used the ordinary provider path.
  The auth-linked application request has its 1612 auth/application link,
  1324 source link and 1314 response acquisition. The URL is
  `chatgpt.com/backend-api/codex/responses` for luna and the paper provider's URL
  for paper. The emitting calls are the ones at `provider_auth.cpp:453`
  (construct), `:466` (body), `:543` (http) and `:546` (finish).
- (e) Parser artifact: confirmed, as described above.

## Is the raw losslessness contract broken?

No. Every emitted record in these runs was preserved, with complete per-lane
ordinal coverage. The contract in `docs/INSTRUMENTATION.md` promises preservation,
not cross-partition order on disk. The design says outright that partitions are
drained separately, with control first.

There is one documentation gap. `docs/RAW_READER.md` describes the `records`
stream format but does not say that records from one lane's data, control and
failure partitions interleave in recorder order. Consumers must order events by
`(lane, ordinal)`, and must not assume a record precedes another just because its
ordinal is lower. The analysis note `STREAM_TIMING.md` stated the right rule
("lane-local ordinals give their exact order"), and the analyzer applied it to
child events but not to the request record.

Consequences for `STREAM_TIMING.md`: in the outcome census, the
`(None, None, None, None, 0, True): 15` row belongs in `(200, 1, 0, 0, 0, True)`,
which becomes 718. The notes paragraph about the missing records is wrong. The
timing distributions are unaffected. They already included these 15 requests,
because the filter is `http != 400` and `None != 400`.

## Red-test design

The defect is in the out-of-tree analyzer, so the red test targets
`timing.py`. Blackbird itself needs no product test.

Input: a synthetic `BBRAWOUT` v1 stream. It has a 16-byte header with an empty
description, then records with tag 1, a 64-byte zero cursor, a 32-byte origin
(lane L, partition, 0, source_start), a u32 length and an envelope
`u32 length, u32 site, u64 ord` followed by the body. Finish with tag 0. Use a single
lane L and write the records in this stream order:

1. Partition 2: 1401 (child Ref (L,8), stage 12, result 0, errno 0, pid, group),
   ord 9.
2. Partition 2: 1404 (child (L,8), channel 1, requested 65536, result 0, errno 0),
   ord 11. This is the EOF read.
3. Partition 2: 1405 (child (L,8), method 1, result 0, errno 0, si_pid, si_uid,
   si_code 1, si_status 0), ord 12.
4. Partition 2: 1302 (request Ref (L,4), child Ref (L,8), status 200), ord 13.
5. Partition 2: 1303 (request Ref (L,4), branch 1, stage 0, ErrorCode 0,
   detail 0), ord 14.
6. Partition 1: 1300 (owner_present 0, timeout 180), ord 4.
7. Partition 1: 1301 (Ref (L,4), method `POST`, url `https://example.invalid/v1/responses`,
   body `{"model":"m","reasoning":{"effort":"high"}}`), ord 7.
8. Partition 1: 1400 (request Ref (L,4), flags 0,0,1,0), ord 8.
9. Partition 1: 1404 (child (L,8), channel 1, requested 65536, result 5, errno 0,
   bytes `hello`), ord 10.

Expected output: one row with `http == 200`, `term_branch == 1`,
`term_code == 0`, `term_detail == 0`, `curl_exit == 0`, `eof == true`, `reads == 1`
and `url` set.

Current code: `http`, `term_branch`, `term_code` and `term_detail` are all `None`.
That is the red result.

Control case: the same records with items 6-9 written first must give the same row.
This passes before and after the fix, which shows the test isolates stream order.

This test was run. The generator is
`/private/tmp/claude-501/-Users-patrickbeam-projects-blackbird/7a9f433b-5f15-4206-8722-548bda7b4ce9/scratchpad/e8n/tools/redtest.py`,
called with `control-first` or `data-first`. With control-first order, the current
`timing.py` gives `http: None, term_branch: None, term_code: None, term_detail: None`,
while `curl_exit: 0, eof: true, reads: 1` and `url` are set. That reproduces the
campaign rows exactly. With data-first order it gives `200, 1, 0, 0`. The
one-line-patched copy gives `200, 1` for both orders.

Real-data case: run `timing.py` on the a-t08-luna raw run `bbf929703426b0d6e9c14fefe9`.
The expected outcome census is `(200, 1, 0, 0, 0, True): 23`. The current code gives
14 such rows plus 9 `None` rows.

A Blackbird-side characterization test belongs in `tests/raw_reader_test.cpp`.
Emit data ord 1 and then control ord 2 on one lane, let the recorder take one
tour, and assert that `records` yields ord 2 before ord 1. This passes on the
current code. Its purpose is to pin the documented behavior that consumers must
handle, not to show a defect.
