import sys, os, re, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bbraw import stream
n = 0
for i, lane, part, src, site, o, body, rawlen in stream(sys.stdin.buffer, lambda s: s == 1301):
    if body is None or site != 1301: continue
    b = bytes(body); n += 1
    m = re.search(rb'CONTEXT_SIZE input_bytes=(\d+) entries=(\d+)', b)
    print(f"req {n}: wire {len(b)} B  size_line={'yes '+m.group(1).decode()+'/'+m.group(2).decode() if m else 'NO'}  budget_guidance={'yes' if b'CONTEXT_BUDGET basis' in b else 'no'}")
