#!/usr/bin/env python3
"""Per provider request (site 1301) wire-body size measurement (read-only).

usage: blackbird-raw-reader records RAWRUN | python3 -I sizes.py RUNNAME RAWID > out.jsonl
Emits one JSON object per 1301 request: wire body bytes, model, effort, and, when the
body decodes, the exact byte span of top-level "input", "instructions", "tools" values,
the input item count, plus HTTP status (1302), terminal (1303), provider usage parsed
from the response stream (1404 reads of the request's curl child), and the head/tail of
the response body for any non-200 or unusual outcome.
"""
import sys, os, struct, json, re, collections, bisect
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bbraw import stream

run, rawid = sys.argv[1], sys.argv[2]
WANT = {1080, 1081, 1300, 1301, 1302, 1303, 1400, 1404}
USAGE_RE = re.compile(rb'"input_tokens":(\d+)')
CACHED_RE = re.compile(rb'"cached_tokens":(\d+)')
OUT_RE = re.compile(rb'"output_tokens":(\d+)')
REFUSE_RE = re.compile(rb'"type":"(?:response\.failed|response\.incomplete|error)"|"error":\{|"incomplete_details":\{')
UHEAD_RE = re.compile(rb'"usage":\{"input_tokens":')


def ref(b, off=16):
    return (bytes(b[off:off+16]), struct.unpack_from('<Q', b, off+16)[0])


def clk(b, off):
    m, r, e, sec, ns = struct.unpack_from('<iiiqq', b, off)
    return sec + ns * 1e-9 if r == 0 else None


WS = ' \t\r\n'


def top_spans(s):
    """Exact (start,end) char spans of top-level object values in JSON text s."""
    dec = json.JSONDecoder()
    spans = {}
    i = 0
    n = len(s)
    while i < n and s[i] in WS:
        i += 1
    if s[i] != '{':
        return None
    i += 1
    while True:
        while s[i] in WS:
            i += 1
        if s[i] == '}':
            return spans
        if s[i] == ',':
            i += 1
            continue
        key, i = dec.raw_decode(s, i)
        while s[i] in WS:
            i += 1
        assert s[i] == ':'
        i += 1
        while s[i] in WS:
            i += 1
        v0 = i
        _, i = dec.raw_decode(s, i)
        spans[key] = (v0, i)


def blen(s, sp):
    return len(s[sp[0]:sp[1]].encode('utf-8')) if sp else None


reqs = {}
children = {}           # child key -> req key
reg_begin = {}
begins_by_lane = collections.defaultdict(list)
cstate = {}             # child key -> dict(head, tail, carry, usage, cached, outtok, refuse, nbytes)
nrec = 0
for i, lane, part, src, site, o, body, n in stream(sys.stdin.buffer, lambda s: s in WANT):
    nrec += 1
    if body is None:
        continue
    key = (bytes(lane), o)
    if site == 1080:
        t = clk(body, 16)
        reg_begin[key] = t
        begins_by_lane[bytes(lane)].append(o)
    elif site == 1300:
        reqs.setdefault(key, {})['lane_ord'] = o
    elif site == 1301:
        r = ref(body)
        off = 40
        (ml,) = struct.unpack_from('<Q', body, off); off += 8
        method = body[off:off+ml].decode(); off += ml
        (ul,) = struct.unpack_from('<Q', body, off); off += 8
        url = body[off:off+ul].decode(); off += ul
        (bl,) = struct.unpack_from('<Q', body, off); off += 8
        wire = bytes(body[off:off+bl])
        d = dict(url=url, method=method, body_bytes=bl, rec_ord=o)
        try:
            s = wire.decode('utf-8')
            j = json.loads(s)
            d['model'] = j.get('model')
            d['effort'] = (j.get('reasoning') or {}).get('effort') if isinstance(j.get('reasoning'), dict) else None
            d['top_keys'] = list(j.keys())
            sp = top_spans(s)
            inp = j.get('input')
            if isinstance(inp, list):
                d['input_items'] = len(inp)
                types = collections.Counter()
                biggest = 0
                for it in inp:
                    if isinstance(it, dict):
                        types[it.get('type') or ('message:' + str(it.get('role')))] += 1
                    ln = len(json.dumps(it, separators=(',', ':'), ensure_ascii=False).encode())
                    biggest = max(biggest, ln)
                d['input_types'] = dict(types)
                d['max_item_bytes_approx'] = biggest
            elif isinstance(inp, str):
                d['input_items'] = 1
            d['input_bytes'] = blen(s, sp.get('input')) if sp else None
            d['instructions_bytes'] = blen(s, sp.get('instructions')) if sp else None
            d['tools_bytes'] = blen(s, sp.get('tools')) if sp else None
            if isinstance(j.get('tools'), list):
                d['n_tools'] = len(j['tools'])
            d['decoded'] = True
        except Exception as ex:
            d['decoded'] = False
            d['decode_error'] = repr(ex)[:200]
            d['head'] = wire[:200].decode('utf-8', 'replace')
        reqs.setdefault(r, {}).update(d)
    elif site == 1302:
        r = ref(body)
        (st,) = struct.unpack_from('<i', body, 64)
        reqs.setdefault(r, {})['http'] = st
    elif site == 1303:
        r = ref(body)
        br, stg, code = body[40], body[41], body[42]
        (det,) = struct.unpack_from('<q', body, 43)
        reqs.setdefault(r, {}).update(term_branch=br, term_code=code, term_detail=det)
    elif site == 1400:
        children[key] = ref(body)
        cstate[key] = dict(head=b'', tail=b'', carry=b'', ubuf=None,
                           refuse=collections.Counter(), nbytes=0)
    elif site == 1404:
        c = ref(body)
        ch, rq, res, er = struct.unpack_from('<BQqi', body, 40)
        if ch != 1 or res <= 0 or c not in cstate:
            continue
        p = bytes(body[61:])
        cs = cstate[c]
        cs['nbytes'] += len(p)
        if len(cs['head']) < 4096:
            cs['head'] = (cs['head'] + p)[:4096]
        cs['tail'] = (cs['tail'] + p)[-4096:]
        buf = cs['carry'] + p
        if cs['ubuf'] is not None and len(cs['ubuf']) < 600:
            cs['ubuf'] += p[:600]
        ms = list(UHEAD_RE.finditer(buf))
        if ms:
            cs['ubuf'] = buf[ms[-1].start():ms[-1].start() + 600]
        for m in REFUSE_RE.finditer(buf):
            cs['refuse'][m.group(0).decode('utf-8', 'replace')] += 1
        cs['carry'] = p[-256:]

for l in begins_by_lane.values():
    l.sort()

req_child = {}
for ck, rk in children.items():
    req_child.setdefault(rk, ck)

out = sys.stdout
for rk, r in reqs.items():
    if 'body_bytes' not in r:
        continue
    lane = rk[0]
    bl = begins_by_lane.get(lane, [])
    k = bisect.bisect_left(bl, rk[1]) - 1
    t0 = reg_begin.get((lane, bl[k])) if k >= 0 else None
    rec = dict(run=run, raw=rawid, req_ord=rk[1], lane=lane.hex(), t_begin=t0, **r)
    ck = req_child.get(rk)
    if ck:
        cs = cstate[ck]
        u = cs['ubuf'] or b''
        def g(rx):
            m = rx.search(u)
            return int(m.group(1)) if m else None
        rec.update(resp_bytes=cs['nbytes'], usage_input_tokens=g(USAGE_RE), usage_cached_tokens=g(CACHED_RE),
                   usage_output_tokens=g(OUT_RE), refuse_hits=dict(cs['refuse']))
        if r.get('http') not in (200,) or cs['refuse']:
            rec['resp_head'] = cs['head'][:1500].decode('utf-8', 'replace')
            rec['resp_tail'] = cs['tail'][-1500:].decode('utf-8', 'replace')
    out.write(json.dumps(rec) + '\n')
sys.stderr.write(f'{run}/{rawid}: records={nrec} requests={sum(1 for r in reqs.values() if "body_bytes" in r)} children={len(children)}\n')
