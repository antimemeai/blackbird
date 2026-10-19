import sys, os, struct
sys.path.insert(0, sys.argv[1])
from bbraw import stream
best = None
for i, lane, part, src, site, o, body, rawlen in stream(sys.stdin.buffer, lambda s: s == 1301):
    if body is None or site != 1301: continue
    b = bytes(body)
    if best is None or len(b) > len(best): best = b
start = best.index(b'{')
open(sys.argv[2], 'wb').write(best[start:])
print(len(best))
