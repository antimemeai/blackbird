# Qualified integrated-mainline baseline

The persistent committed0bf6712 baseline Release build succeeded. Combined
interface qualification passes13/13 in23.30s, including actual Microsoft center;
the corrected native command-PTY pressure episode separately passes1/1 in8.85s.
No qualification attempt failed and no timing has run.

Executable:
/home/patrick/.local/share/blackbird-campaigns/tui-lua-composition-20261016-xjj_x_lg/baseline-release/blackbird

SHA2569bd9d9b5ac95545c9c2f32ba0752fedd31c662e9e8783c53cbda9762426717f5.
Keep it and its archive/job-runner companions at this original build path; embedded
absolute helper paths make binary-only relocation incorrect. Full source/profile,
compiler/library versions, configure/build/check arguments, hashes and qualification
state are in baseline-build-provenance.json and BASELINE_RESULTS.json.

The local artifact extractor initially failed after remote success because local
Python3.9 lacks tarfile's filter keyword. baseline-local.log and
baseline-fetch-only-failure.log retain that failure. The compatibility correction
then fetched existing artifacts only; baseline-fetch-only.log records success.
No rebuild or test was repeated. Remote originals remain untouched, and fixture
copies (including actual center commands/screens and unique command-PTY traces)
are under baseline-fixtures/.

All owned remote builds/checks are now idle. The previous command-center campaign
is unchanged. Root must explicitly release any later final-source capture and
single matched round; no performance claim follows from qualification time.
