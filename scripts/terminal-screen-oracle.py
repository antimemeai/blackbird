"""Small independent VT cell oracle for renderer PTY tests, development only.
Supports the sequences emitted by Arco, not a general terminal emulator.
"""
import codecs
import re
import unicodedata

# Unicode17 Extended_Pictographic data; Unicode LicenseV3 in LICENSES/.
# Normative values are shared source material, not a renderer/parser oracle.
def property_ranges(encoded):
    return tuple((int(pair[0], 16), int(pair[-1], 16))
                 for span in encoded.split() for pair in [span.split('..')])

PICTOGRAPHIC = property_ranges("""
a9 ae 203c 2049 2122 2139 2194..2199 21a9..21aa 231a..231b 2328
23cf 23e9..23f3 23f8..23fa 24c2 25aa..25ab 25b6 25c0 25fb..25fe 2600..2604 260e
2611 2614..2615 2618 261d 2620 2622..2623 2626 262a 262e..262f 2638..263a
2640 2642 2648..2653 265f..2660 2663 2665..2666 2668 267b 267e..267f 2692..2697
2699 269b..269c 26a0..26a1 26a7 26aa..26ab 26b0..26b1 26bd..26be 26c4..26c5 26c8 26ce..26cf
26d1 26d3..26d4 26e9..26ea 26f0..26f5 26f7..26fa 26fd 2702 2705 2708..270d 270f
2712 2714 2716 271d 2721 2728 2733..2734 2744 2747 274c
274e 2753..2755 2757 2763..2764 2795..2797 27a1 27b0 27bf 2934..2935 2b05..2b07
2b1b..2b1c 2b50 2b55 3030 303d 3297 3299 1f004 1f02c..1f02f 1f094..1f09f
1f0af..1f0b0 1f0c0 1f0cf..1f0d0 1f0f6..1f0ff 1f170..1f171 1f17e..1f17f 1f18e 1f191..1f19a 1f1ae..1f1e5 1f201..1f20f
1f21a 1f22f 1f232..1f23a 1f23c..1f23f 1f249..1f25f 1f266..1f321 1f324..1f393 1f396..1f397 1f399..1f39b 1f39e..1f3f0
1f3f3..1f3f5 1f3f7..1f3fa 1f400..1f4fd 1f4ff..1f53d 1f549..1f54e 1f550..1f567 1f56f..1f570 1f573..1f57a 1f587 1f58a..1f58d
1f590 1f595..1f596 1f5a4..1f5a5 1f5a8 1f5b1..1f5b2 1f5bc 1f5c2..1f5c4 1f5d1..1f5d3 1f5dc..1f5de 1f5e1
1f5e3 1f5e8 1f5ef 1f5f3 1f5fa..1f64f 1f680..1f6c5 1f6cb..1f6d2 1f6d5..1f6e5 1f6e9 1f6eb..1f6f0
1f6f3..1f6ff 1f7da..1f7ff 1f80c..1f80f 1f848..1f84f 1f85a..1f85f 1f888..1f88f 1f8ae..1f8af 1f8bc..1f8bf 1f8c2..1f8cf 1f8d9..1f8ff
1f90c..1f93a 1f93c..1f945 1f947..1f9ff 1fa58..1fa5f 1fa6e..1faff 1fc00..1fffd
""")

EXTEND = property_ranges("""
300..36f 483..489 591..5bd 5bf 5c1..5c2 5c4..5c5 5c7 610..61a 64b..65f 670
6d6..6dc 6df..6e4 6e7..6e8 6ea..6ed 711 730..74a 7a6..7b0 7eb..7f3 7fd 816..819
81b..823 825..827 829..82d 859..85b 897..89f 8ca..8e1 8e3..902 93a 93c 941..948
94d 951..957 962..963 981 9bc 9be 9c1..9c4 9cd 9d7 9e2..9e3
9fe a01..a02 a3c a41..a42 a47..a48 a4b..a4d a51 a70..a71 a75 a81..a82
abc ac1..ac5 ac7..ac8 acd ae2..ae3 afa..aff b01 b3c b3e..b3f b41..b44
b4d b55..b57 b62..b63 b82 bbe bc0 bcd bd7 c00 c04
c3c c3e..c40 c46..c48 c4a..c4d c55..c56 c62..c63 c81 cbc cbf..cc0 cc2
cc6..cc8 cca..ccd cd5..cd6 ce2..ce3 d00..d01 d3b..d3c d3e d41..d44 d4d d57
d62..d63 d81 dca dcf dd2..dd4 dd6 ddf e31 e34..e3a e47..e4e
eb1 eb4..ebc ec8..ece f18..f19 f35 f37 f39 f71..f7e f80..f84 f86..f87
f8d..f97 f99..fbc fc6 102d..1030 1032..1037 1039..103a 103d..103e 1058..1059 105e..1060 1071..1074
1082 1085..1086 108d 109d 135d..135f 1712..1715 1732..1734 1752..1753 1772..1773 17b4..17b5
17b7..17bd 17c6 17c9..17d3 17dd 180b..180d 180f 1885..1886 18a9 1920..1922 1927..1928
1932 1939..193b 1a17..1a18 1a1b 1a56 1a58..1a5e 1a60 1a62 1a65..1a6c 1a73..1a7c
1a7f 1ab0..1add 1ae0..1aeb 1b00..1b03 1b34..1b3d 1b42..1b44 1b6b..1b73 1b80..1b81 1ba2..1ba5 1ba8..1bad
1be6 1be8..1be9 1bed 1bef..1bf3 1c2c..1c33 1c36..1c37 1cd0..1cd2 1cd4..1ce0 1ce2..1ce8 1ced
1cf4 1cf8..1cf9 1dc0..1dff 200c 20d0..20f0 2cef..2cf1 2d7f 2de0..2dff 302a..302f 3099..309a
a66f..a672 a674..a67d a69e..a69f a6f0..a6f1 a802 a806 a80b a825..a826 a82c a8c4..a8c5
a8e0..a8f1 a8ff a926..a92d a947..a951 a953 a980..a982 a9b3 a9b6..a9b9 a9bc..a9bd a9c0
a9e5 aa29..aa2e aa31..aa32 aa35..aa36 aa43 aa4c aa7c aab0 aab2..aab4 aab7..aab8
aabe..aabf aac1 aaec..aaed aaf6 abe5 abe8 abed fb1e fe00..fe0f fe20..fe2f
ff9e..ff9f 101fd 102e0 10376..1037a 10a01..10a03 10a05..10a06 10a0c..10a0f 10a38..10a3a 10a3f 10ae5..10ae6
10d24..10d27 10d69..10d6d 10eab..10eac 10efa..10eff 10f46..10f50 10f82..10f85 11001 11038..11046 11070 11073..11074
1107f..11081 110b3..110b6 110b9..110ba 110c2 11100..11102 11127..1112b 1112d..11134 11173 11180..11181 111b6..111be
111c0 111c9..111cc 111cf 1122f..11231 11234..11237 1123e 11241 112df 112e3..112ea 11300..11301
1133b..1133c 1133e 11340 1134d 11357 11366..1136c 11370..11374 113b8 113bb..113c0 113c2
113c5 113c7..113c9 113ce..113d0 113d2 113e1..113e2 11438..1143f 11442..11444 11446 1145e 114b0
114b3..114b8 114ba 114bd 114bf..114c0 114c2..114c3 115af 115b2..115b5 115bc..115bd 115bf..115c0 115dc..115dd
11633..1163a 1163d 1163f..11640 116ab 116ad 116b0..116b7 1171d 1171f 11722..11725 11727..1172b
1182f..11837 11839..1183a 11930 1193b..1193e 11943 119d4..119d7 119da..119db 119e0 11a01..11a0a 11a33..11a38
11a3b..11a3e 11a47 11a51..11a56 11a59..11a5b 11a8a..11a96 11a98..11a99 11b60 11b62..11b64 11b66 11c30..11c36
11c38..11c3d 11c3f 11c92..11ca7 11caa..11cb0 11cb2..11cb3 11cb5..11cb6 11d31..11d36 11d3a 11d3c..11d3d 11d3f..11d45
11d47 11d90..11d91 11d95 11d97 11ef3..11ef4 11f00..11f01 11f36..11f3a 11f40..11f42 11f5a 13440
13447..13455 1611e..16129 1612d..1612f 16af0..16af4 16b30..16b36 16f4f 16f8f..16f92 16fe4 16ff0..16ff1 1bc9d..1bc9e
1cf00..1cf2d 1cf30..1cf46 1d165..1d169 1d16d..1d172 1d17b..1d182 1d185..1d18b 1d1aa..1d1ad 1d242..1d244 1da00..1da36 1da3b..1da6c
1da75 1da84 1da9b..1da9f 1daa1..1daaf 1e000..1e006 1e008..1e018 1e01b..1e021 1e023..1e024 1e026..1e02a 1e08f
1e130..1e136 1e2ae 1e2ec..1e2ef 1e4ec..1e4ef 1e5ee..1e5ef 1e6e3 1e6e6 1e6ee..1e6ef 1e6f5 1e8d0..1e8d6
1e944..1e94a 1f3fb..1f3ff e0020..e007f e0100..e01ef
""")

def grapheme_extend(character):
    scalar = ord(character)
    return scalar >= 0x300 and any(a <= scalar <= b for a, b in EXTEND)

def pictographic(character):
    scalar = ord(character)
    return scalar >= 0xa9 and any(a <= scalar <= b for a, b in PICTOGRAPHIC)

class Screen:
    def __init__(self):
        self.cells = {}
        self.styles = {}
        self.style = '0'
        self.prior = None
        self.join = self.regional = self.pictograph = False
        self.saved_cursor = (0, 0)
        self.synchronized_update = False
        self.row = self.col = 0
        self.pending = ''
        self.decoder = codecs.getincrementaldecoder('utf-8')('replace')
    def feed(self, data):
        text = self.pending + self.decoder.decode(data)
        self.pending = ''
        pos = 0
        while pos < len(text):
            c = text[pos]
            if c == '\x1b':
                if pos+1 >= len(text): break
                if text[pos+1] in ('7','8'):
                    if text[pos+1] == '7': self.saved_cursor = (self.row,self.col)
                    else: self.row,self.col = self.saved_cursor
                    self.prior = None
                    pos += 2;continue
                if text[pos+1] == '_':
                    end = text.find('\x1b\\', pos+2)
                    abort = re.search('[\x18\x1a]', text[pos+2:])
                    if abort and (end < 0 or pos+2+abort.start() < end):
                        pos += 3+abort.start();continue  # CAN/SUB return to ground.
                    if end < 0: break
                    pos = end+2;continue  # APC graphics does not occupy text cells.
                if text[pos+1] == '[':
                    match = re.match(r'\x1b\[([0-9;?]*)([@-~])', text[pos:])
                    if not match:
                        # libvterm parser.c treats CAN/SUB in CSI as an abort,
                        # even when the control's bytes cross feed boundaries.
                        # Only a syntactically incomplete supported prefix may
                        # take this path; malformed/truncated input stays pending.
                        abort = re.match(r'\x1b\[[0-9;?]*[\x18\x1a]', text[pos:])
                        if abort:
                            pos += len(abort.group(0));continue
                        break
                    values, cmd = match.groups()
                    if cmd == 'H':
                        parts = values.split(';')
                        self.prior = None
                        self.join = self.regional = self.pictograph = False
                        self.row = int(parts[0] or 1)-1
                        self.col = int(parts[1] or 1)-1 if len(parts)>1 else 0
                    elif cmd == 'm': self.style = values or '0'
                    elif cmd == 'K':
                        for key in list(self.cells):
                            if key[0] == self.row and (values == '2' or key[1] >= self.col):
                                del self.cells[key]
                                self.styles.pop(key, None)
                    elif cmd == 'J':
                        for key in list(self.cells):
                            if values == '2' or key >= (self.row,self.col):
                                del self.cells[key]
                                self.styles.pop(key, None)
                    elif cmd in ('h', 'l') and values == '?2026': self.synchronized_update = cmd == 'h'
                    elif cmd == 'h' and values == '?1049': self.cells.clear()
                    pos += len(match.group(0));continue
                pos += 2;continue
            if c == '\r': self.col = 0
            elif c == '\n': self.row += 1
            elif ord(c) >= 32:
                if unicodedata.category(c) == 'Cf' and self.prior is None:
                    pos += 1
                    continue
                region = 0x1f1e6 <= ord(c) <= 0x1f1ff
                modifier = 0x1f3fb <= ord(c) <= 0x1f3ff
                combining = unicodedata.combining(c) or unicodedata.category(c) == 'Cf' or c in ('\u200d','\ufe0f','\ufe0e','\u20e3')
                if self.prior is not None and (combining or modifier or (self.join and pictographic(c)) or (region and self.regional)):
                    self.cells[self.prior] += c
                    if ((self.join and pictographic(c)) or (region and self.regional) or c in ('\ufe0f','\u20e3')) and self.col == self.prior[1]+1:
                        self.cells[self.row,self.col] = ''
                        self.styles[self.row,self.col] = self.style
                        self.col += 1
                else:
                    self.prior = (self.row,self.col)
                    self.cells[self.prior] = c
                    self.styles[self.prior] = self.style
                    width = 2 if unicodedata.east_asian_width(c) in ('W','F') else 1
                    if width == 2:
                        self.cells[self.row,self.col+1]=''
                        self.styles[self.row,self.col+1]=self.style
                    self.col += width
                joined = self.join and pictographic(c)
                self.join = c == '\u200d' and self.pictograph
                if c == '\u200d': self.pictograph = False
                elif joined or not grapheme_extend(c): self.pictograph = pictographic(c)
                self.regional = region and not self.regional
            pos += 1
        self.pending = text[pos:]
    def text(self):
        if not self.cells: return ''
        rows = max(key[0] for key in self.cells)+1
        return '\n'.join(''.join(self.cells.get((r,c),' ') for c in range(max((k[1] for k in self.cells if k[0]==r),default=-1)+1)).rstrip() for r in range(rows))

if __name__ == '__main__':
    import subprocess, sys
    # Direct parser cases: cancel a supported incomplete control, then execute
    # recovery at a fixed cursor. Every feed split must reach the same cells.
    for cancel in (b'\x18', b'\x1a'):
        for prefix in (b'\x1b[1;38', b'\x1b_Ga=d'):
            data = b'\x1b[1;1HOLD' + prefix + cancel + b'\x1b[2;3H\x1b[0mOK'
            for split in range(len(data)+1):
                screen = Screen()
                screen.feed(data[:split])
                screen.feed(data[split:])
                assert not screen.pending, 'CAN/SUB did not cancel incomplete control'
                assert screen.cells == {(0,0):'O', (0,1):'L', (0,2):'D',
                                        (1,2):'O', (1,3):'K'}, 'cancel/recovery cells'
                assert (screen.row,screen.col,screen.style) == (1,4,'0'), 'cancel/recovery cursor/style'
    for data in (b'\x1b[12', b'\x1b[12:', b'\x1b_Gunfinished', b'\x1b[12:\x18'):
        screen = Screen()
        screen.feed(data)
        assert screen.pending, 'malformed/truncated control was discarded'
    # Specified spacing cells; ordinary ZWJ text is not an emoji ligature.
    for data, expected, cursor in (
        (' \u200d \u200d X'.encode(), {(0,0): ' \u200d', (0,1): ' \u200d',
                                    (0,2): ' ', (0,3): 'X'}, (0,4)),
        ('A\u200dB\u200dCx'.encode(), {(0,0): 'A\u200d', (0,1): 'B\u200d',
                                    (0,2): 'C', (0,3): 'x'}, (0,4)),
        ('\u200d\u200dA'.encode(), {(0,0): 'A'}, (0,1)),
        ('👩\u200b\u200d💻X'.encode(), {(0,0): '👩\u200b\u200d', (0,1): '',
                                       (0,2): '💻', (0,3): '', (0,4): 'X'}, (0,5)),
        ('👩\u200e\u200d💻X'.encode(), {(0,0): '👩\u200e\u200d', (0,1): '',
                                       (0,2): '💻', (0,3): '', (0,4): 'X'}, (0,5)),
        ('👩\u200d́💻X'.encode(), {(0,0): '👩\u200d́', (0,1): '',
                                    (0,2): '💻', (0,3): '', (0,4): 'X'}, (0,5)),
    ):
        for split in range(len(data)+1):
            screen = Screen()
            screen.feed(data[:split]); screen.feed(data[split:])
            assert screen.cells == expected, 'plain joiner spacing cells'
            assert (screen.row,screen.col) == cursor, 'plain joiner cursor'
    lines = subprocess.check_output([sys.argv[1], '--packets'], text=True).splitlines()
    incremental = Screen()
    for index in range(0, len(lines), 3):
        width, height = map(int, lines[index].split())
        incremental.cells = {k:v for k,v in incremental.cells.items() if k[0]<height and k[1]<width}
        incremental.styles = {k:v for k,v in incremental.styles.items() if k[0]<height and k[1]<width}
        cold = Screen()
        for byte in bytes.fromhex(lines[index+1]): incremental.feed(bytes([byte]))
        cold.feed(bytes.fromhex(lines[index+2]))
        assert incremental.cells == cold.cells, ('delta/full glyph mismatch', index//3)
        assert incremental.styles == cold.styles, ('delta/full style mismatch', index//3)
        assert (incremental.row,incremental.col) == (cold.row,cold.col), 'cursor mismatch'
        assert not incremental.pending, 'incomplete control packet'
        frame = index // 3
        if frame == 6:
            assert incremental.cells == {(0,0): 'x'}, 'tiny specification cells'
            assert (incremental.row,incremental.col) == (0,1), 'tiny specification cursor'
        else:
            prefix = ('hello long', '界éx', 'h', '👩‍💻🇺🇸', '👩‍💻🇺🇸',
                      '👩‍💻🇺🇸', '', '👩‍💻🇺🇸', 'A\u200dB\u200dCx')[frame]
            assert incremental.text().splitlines()[0] == prefix, 'specified first row'
            assert incremental.text().splitlines()[1] == 'keyword string'[:width], 'specified styled row'
            assert incremental.styles[1,0] == '1;38;2;188;168;239;48;2;37;40;51', 'specified keyword ink'
            assert (incremental.row,incremental.col) == (3,2), 'specified normal cursor'
    print('Incremental/full styled VT cells agree: ASCII, CJK, combining, emoji, stale areas, resize, byte-fragment delivery, CAN/SUB recovery and malformed detection')
