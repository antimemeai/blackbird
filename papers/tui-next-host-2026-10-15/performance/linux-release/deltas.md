# All presenter-tranche latency deltas

Milliseconds. Each entry is **before → after (absolute delta)**. All 16 endpoint/geometry combinations are shown.
Nearest-rank quantiles; 30 trials per source and size. Positive delta means a longer observation.
The retained tertiary JSON also includes p90/p99 and percentage deltas. Values are rounded independently.

| Geometry | Endpoint | p50 before → after (Δ) | p95 before → after (Δ) | Max before → after (Δ) |
| --- | --- | ---: | ---: | ---: |
| 110x35-fresh | startup | 23.378 → 23.676 (+0.298) | 38.235 → 150.905 (+112.671) | 69.518 → 477.668 (+408.150) |
| 110x35-fresh | input | 2.354 → 2.422 (+0.069) | 3.894 → 3.853 (-0.042) | 3.999 → 4.030 (+0.032) |
| 110x35-fresh | paste | 38.236 → 38.263 (+0.027) | 60.132 → 104.216 (+44.084) | 61.619 → 348.122 (+286.503) |
| 110x35-fresh | palette_open | 1.478 → 1.544 (+0.067) | 1.949 → 1.976 (+0.026) | 2.481 → 11.291 (+8.810) |
| 110x35-fresh | palette_next | 0.974 → 1.051 (+0.076) | 1.413 → 1.561 (+0.148) | 1.521 → 1.993 (+0.472) |
| 110x35-fresh | resize_draft | 1.241 → 1.246 (+0.006) | 1.739 → 1.914 (+0.174) | 1.881 → 2.036 (+0.154) |
| 110x35-fresh | resize | 2.816 → 2.850 (+0.034) | 5.328 → 5.284 (-0.044) | 5.660 → 5.492 (-0.168) |
| 110x35-fresh | resize_restore | 3.540 → 3.498 (-0.041) | 6.752 → 6.462 (-0.290) | 7.073 → 6.604 (-0.469) |
| 80x24-fresh | startup | 21.947 → 21.853 (-0.094) | 157.867 → 130.939 (-26.928) | 192.365 → 277.836 (+85.471) |
| 80x24-fresh | input | 1.304 → 1.376 (+0.072) | 1.881 → 1.820 (-0.062) | 2.177 → 1.950 (-0.226) |
| 80x24-fresh | paste | 37.393 → 37.636 (+0.243) | 58.030 → 55.791 (-2.239) | 59.067 → 95.766 (+36.700) |
| 80x24-fresh | palette_open | 1.445 → 1.497 (+0.052) | 2.052 → 2.151 (+0.099) | 3.164 → 3.253 (+0.089) |
| 80x24-fresh | palette_next | 0.974 → 0.975 (+0.001) | 1.308 → 1.250 (-0.058) | 1.892 → 1.381 (-0.512) |
| 80x24-fresh | resize_draft | 1.223 → 1.170 (-0.053) | 1.798 → 2.072 (+0.274) | 2.213 → 2.159 (-0.055) |
| 80x24-fresh | resize | 1.257 → 1.260 (+0.003) | 2.249 → 2.249 (+0.000) | 2.278 → 2.263 (-0.014) |
| 80x24-fresh | resize_restore | 1.686 → 1.682 (-0.004) | 3.097 → 3.073 (-0.024) | 5.354 → 3.090 (-2.264) |
