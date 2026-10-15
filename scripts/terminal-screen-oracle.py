"""Small independent VT cell oracle for renderer PTY tests, development only.
Supports the sequences emitted by Arco, not a general terminal emulator.
"""
import codecs
import re
import unicodedata

class Screen:
    def __init__(self):
        self.cells = {}
        self.styles = {}
        self.style = '0'
        self.prior = None
        self.join = self.regional = False
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
                        self.join = self.regional = False
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
                region = 0x1f1e6 <= ord(c) <= 0x1f1ff
                modifier = 0x1f3fb <= ord(c) <= 0x1f3ff
                combining = unicodedata.combining(c) or c in ('\u200d','\ufe0f','\ufe0e','\u20e3')
                if self.prior is not None and (combining or modifier or self.join or (region and self.regional)):
                    self.cells[self.prior] += c
                    if (self.join or (region and self.regional) or c in ('\ufe0f','\u20e3')) and self.col == self.prior[1]+1:
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
                self.join = c == '\u200d'
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
                      '👩‍💻🇺🇸', '', '👩‍💻🇺🇸')[frame]
            assert incremental.text().splitlines()[0] == prefix, 'specified first row'
            assert incremental.text().splitlines()[1] == 'keyword string'[:width], 'specified styled row'
            assert incremental.styles[1,0] == '1;38;2;188;168;239;48;2;37;40;51', 'specified keyword ink'
            assert (incremental.row,incremental.col) == (3,2), 'specified normal cursor'
    print('Incremental/full styled VT cells agree: ASCII, CJK, combining, emoji, stale areas, resize, byte-fragment delivery, CAN/SUB recovery and malformed detection')
