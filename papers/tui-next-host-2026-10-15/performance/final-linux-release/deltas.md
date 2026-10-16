# Final repaired-source latency deltas

Milliseconds. Each entry is **before → after (absolute delta)**. All 16 endpoint/geometry combinations are shown.
Nearest-rank quantiles; 30 trials per source and size. Positive delta means a longer observation.
The retained tertiary JSON also includes p90/p99 and percentage deltas. Values are rounded independently.

| Geometry | Endpoint | p50 before → after (Δ) | p95 before → after (Δ) | Max before → after (Δ) |
| --- | --- | ---: | ---: | ---: |
| 110x35-fresh | startup | 22.803 → 22.883 (+0.080) | 37.566 → 87.705 (+50.139) | 650.330 → 162.279 (-488.051) |
| 110x35-fresh | input | 2.389 → 2.481 (+0.093) | 2.996 → 3.651 (+0.654) | 3.402 → 4.351 (+0.949) |
| 110x35-fresh | paste | 38.631 → 39.462 (+0.831) | 57.193 → 59.687 (+2.493) | 106.561 → 96.377 (-10.184) |
| 110x35-fresh | palette_open | 1.508 → 1.514 (+0.006) | 2.042 → 2.114 (+0.072) | 3.123 → 2.699 (-0.424) |
| 110x35-fresh | palette_next | 1.078 → 1.083 (+0.006) | 1.318 → 1.575 (+0.257) | 2.306 → 5.464 (+3.158) |
| 110x35-fresh | resize_draft | 1.257 → 1.278 (+0.021) | 1.658 → 1.717 (+0.060) | 2.464 → 2.712 (+0.248) |
| 110x35-fresh | resize | 2.955 → 2.913 (-0.042) | 3.447 → 5.453 (+2.006) | 3.567 → 5.570 (+2.003) |
| 110x35-fresh | resize_restore | 3.685 → 3.622 (-0.064) | 4.317 → 6.885 (+2.568) | 4.400 → 7.158 (+2.757) |
| 80x24-fresh | startup | 22.135 → 21.735 (-0.400) | 42.606 → 29.295 (-13.311) | 104.699 → 37.700 (-66.999) |
| 80x24-fresh | input | 1.401 → 1.390 (-0.011) | 3.063 → 1.927 (-1.135) | 4.043 → 2.055 (-1.988) |
| 80x24-fresh | paste | 38.441 → 38.465 (+0.024) | 48.726 → 55.372 (+6.646) | 59.199 → 66.033 (+6.834) |
| 80x24-fresh | palette_open | 1.511 → 1.498 (-0.013) | 1.995 → 1.940 (-0.055) | 2.162 → 1.943 (-0.219) |
| 80x24-fresh | palette_next | 0.993 → 1.010 (+0.016) | 1.803 → 1.290 (-0.513) | 1.835 → 1.331 (-0.504) |
| 80x24-fresh | resize_draft | 1.212 → 1.177 (-0.035) | 1.685 → 1.514 (-0.172) | 1.736 → 1.802 (+0.066) |
| 80x24-fresh | resize | 1.321 → 1.328 (+0.006) | 2.254 → 1.813 (-0.441) | 2.345 → 2.281 (-0.063) |
| 80x24-fresh | resize_restore | 1.735 → 1.718 (-0.017) | 3.051 → 1.883 (-1.169) | 3.160 → 2.981 (-0.178) |
