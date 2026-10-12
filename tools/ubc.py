# DNF build 244 script packages (UE1 package v156, licensee 36): name/import/export tables, property flags, and an
# UnrealScript bytecode decoder (token formats checked in Engine.dll: docs/re/player-options.md "Method").
# Usage: python tools/ubc.py Engine:GameInfo.SetGameSpeed [Pkg:Path ...]   (packages in extracted-packages/)
import struct, sys, os
def rd_compact(d, p):
    b = d[p]; p += 1
    neg = b & 0x80; v = b & 0x3F
    if b & 0x40:
        sh = 6
        while True:
            b = d[p]; p += 1
            v |= (b & 0x7F) << sh; sh += 7
            if not (b & 0x80): break
    return (-v if neg else v), p

class Pkg:
    def __init__(self, path):
        d = self.d = open(path, 'rb').read()
        sig, ver, lic, flags, nc, no, ec, eo, ic, io = struct.unpack_from('<IHHIIIIIII', d, 0)
        self.names = []
        p = no
        for i in range(nc):
            l, p = rd_compact(d, p)
            s = d[p:p+l-1].decode('latin1'); p += l
            p += 4
            self.names.append(s)
        self.imports = []
        p = io
        for i in range(ic):
            cp, p = rd_compact(d, p); cn, p = rd_compact(d, p)
            pk = struct.unpack_from('<i', d, p)[0]; p += 4
            on, p = rd_compact(d, p)
            self.imports.append((cp, cn, pk, on))
        self.exports = []
        p = eo
        for i in range(ec):
            cl, p = rd_compact(d, p); su, p = rd_compact(d, p)
            pk = struct.unpack_from('<i', d, p)[0]; p += 4
            nm, p = rd_compact(d, p)
            fl = struct.unpack_from('<I', d, p)[0]; p += 4
            sz, p = rd_compact(d, p)
            off = 0
            if sz > 0: off, p = rd_compact(d, p)
            self.exports.append(dict(cls=cl, sup=su, pkg=pk, name=nm, flags=fl, size=sz, off=off))
    def objname(self, ref):
        if ref == 0: return 'None'
        if ref < 0: return self.names[self.imports[-ref-1][3]]
        return self.names[self.exports[ref-1]['name']]
    def path(self, ref):
        parts = []
        while ref:
            parts.append(self.objname(ref))
            ref = self.imports[-ref-1][2] if ref < 0 else self.exports[ref-1]['pkg']
        return '.'.join(reversed(parts))
    def clsname(self, ref):
        return 'Class' if ref == 0 else self.objname(ref)
    def find(self, path, cls=None):
        out = []
        for i, e in enumerate(self.exports):
            r = i + 1
            if self.path(r).endswith(path) and (cls is None or self.clsname(e['cls']) == cls):
                out.append(r)
        return out

def prop_flags(pk, ref):
    x = pk.exports[ref-1]; b = pk.d[x['off']:x['off']+x['size']]
    p = 0
    for i in range(3): _, p = rd_compact(b, p)
    dim = struct.unpack_from('<H', b, p)[0]; fl = struct.unpack_from('<I', b, p+2)[0]
    return dim, fl

P = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'research', 'packages') + os.sep
CONV = set(range(0x39, 0x60))
class Bad(Exception): pass

class Fn:
    def __init__(self, pkg, ref):
        self.pkg = pkg; self.ref = ref
        x = pkg.exports[ref-1]; d = pkg.d; b = d[x['off']:x['off']+x['size']]
        self.b = b; p = 0
        _, p = rd_compact(b, p)            # property list terminator (None)
        self.sup, p = rd_compact(b, p)
        self.next, p = rd_compact(b, p)
        _, p = rd_compact(b, p)            # ScriptText
        self.children, p = rd_compact(b, p)
        self.fname, p = rd_compact(b, p)
        p += 4
        _, p = rd_compact(b, p)
        self.line, self.textpos, self.ssize = struct.unpack_from('<iii', b, p); p += 12
        self.code0 = p
        self.native, = struct.unpack_from('<H', b, len(b)-7)
        self.flags, = struct.unpack_from('<I', b, len(b)-4)
        self.end = len(b) - 7

class Dec:
    def __init__(self, pkgs, natives):
        self.pkgs = pkgs; self.natives = natives
    def r8(self):
        v = self.b[self.p]; self.p += 1; return v
    def r16(self):
        v, = struct.unpack_from('<H', self.b, self.p); self.p += 2; return v
    def r32(self):
        v, = struct.unpack_from('<i', self.b, self.p); self.p += 4; return v
    def rf(self):
        v, = struct.unpack_from('<f', self.b, self.p); self.p += 4; return v
    def obj(self):
        v, self.p = rd_compact(self.b, self.p)
        s = self.pkg.path(v) if v else 'None'
        self.refs.append(s)
        return s
    def name(self):
        v, self.p = rd_compact(self.b, self.p); return self.pkg.names[v]
    def params(self):
        a = []
        while self.b[self.p] != 0x16:
            a.append(self.expr())
        self.p += 1
        return a
    def expr(self):
        t = self.r8()
        if t == 0x00: return 'local:' + self.obj().split('.')[-1]
        if t == 0x01: return self.obj()
        if t == 0x02: return 'default.' + self.obj()
        if t == 0x04: return 'return ' + self.expr()
        # DNF's switch carries a 2-byte size (a 1-byte read lost 177 functions, VideoPoker's PokerWin grant among them).
        if t == 0x05: self.r16(); return 'switch(%s)' % self.expr()
        if t == 0x06: return 'goto %04x' % self.r16()
        if t == 0x07: o = self.r16(); return 'if !(%s) goto %04x' % (self.expr(), o)
        if t == 0x08: return 'stop'
        if t == 0x09: self.r16(); return 'assert(%s)' % self.expr()
        if t == 0x0A:
            o = self.r16()
            return 'default:' if o == 0xFFFF else 'case %s: (next %04x)' % (self.expr(), o)
        if t == 0x0B: return 'nothing'
        if t == 0x0C:
            out = []
            while True:
                n = self.name(); o = self.r32()
                if n == 'None': break
                out.append('%s@%04x' % (n, o))
            return 'labels ' + ','.join(out)
        if t == 0x0D: return 'goto label ' + self.expr()
        if t == 0x0E: return 'eat ' + self.expr()
        if t == 0x0F: a = self.expr(); self.lets.append(a); return '%s = %s' % (a, self.expr())
        if t == 0x10: i = self.expr(); return '%s[%s]' % (self.expr(), i)
        if t == 0x11: return 'new(%s,%s,%s,%s)' % (self.expr(), self.expr(), self.expr(), self.expr())
        if t == 0x12:
            o = self.expr(); self.r16(); self.r16(); return '%s.static.%s' % (o, self.expr())
        if t == 0x13: c = self.obj(); return 'class<%s>(%s)' % (c, self.expr())
        if t == 0x14: a = self.expr(); self.lets.append(a); return '%s = %s' % (a, self.expr())
        if t == 0x16: return ')'
        if t == 0x17: return 'self'
        if t == 0x18: self.r16(); return self.expr()
        if t == 0x19:
            o = self.expr(); self.r16(); self.r16(); return '%s.{%s}' % (o, self.expr())
        if t == 0x1A: i = self.expr(); return '%s[%s]' % (self.expr(), i)
        if t == 0x1B: n = self.name(); self.calls.append(n); return '%s(%s)' % (n, ', '.join(self.params()))
        if t == 0x1C: f = self.obj(); self.calls.append(f.split('.')[-1]); return 'final %s(%s)' % (f, ', '.join(self.params()))
        if t == 0x1D: return str(self.r32())
        if t == 0x1E: return '%g' % self.rf()
        if t == 0x1F:
            e = self.b.index(b'\0', self.p); s = self.b[self.p:e].decode('latin1'); self.p = e + 1; return repr(s)
        if t == 0x20: return 'obj:' + self.obj()
        if t == 0x21: return "'%s'" % self.name()
        if t == 0x22: return 'rot(%d,%d,%d)' % (self.r32(), self.r32(), self.r32())
        if t == 0x23: return 'vect(%g,%g,%g)' % (self.rf(), self.rf(), self.rf())
        if t == 0x24: return str(self.r8())
        if t == 0x25: return '0'
        if t == 0x26: return '1'
        if t == 0x27: return 'true'
        if t == 0x28: return 'false'
        if t == 0x29: return 'nativeparm ' + self.obj()
        if t == 0x2A: return 'none'
        if t == 0x2C: return str(self.r8())
        if t == 0x2D: return self.expr()
        if t == 0x2E: c = self.obj(); return '%s(%s)' % (c.split('.')[-1], self.expr())
        if t == 0x2F: e = self.expr(); self.r16(); return 'foreach ' + e
        if t == 0x30: return 'iteratorpop'
        if t == 0x31: return 'iteratornext'
        if t in (0x32, 0x33): self.obj(); return '(%s %s %s)' % (self.expr(), '==' if t == 0x32 else '!=', self.expr())
        if t == 0x34:
            s = ''
            while True:
                c = self.r16()
                if not c: break
                s += chr(c)
            return repr(s)
        if t == 0x36: m = self.obj(); return '%s.%s' % (self.expr(), m.split('.')[-1])
        if t == 0x38: n = self.name(); self.calls.append(n); return 'global.%s(%s)' % (n, ', '.join(self.params()))
        if t in (0x61, 0x62, 0x63): return {0x61:'vect(0)',0x62:'vect(0,0,1)',0x63:'rot(0)'}[t]
        if t == 0x64: return str(self.r16())
        if t == 0x65: self.p += 3; return 'rot(bytes)'
        if t in (0x69, 0x6a, 0x6b): return 'rot1(%d)' % self.r32()
        if t in (0x6c, 0x6d, 0x6e): return 'vect1(%g)' % self.rf()
        if t == 0x6f: self.p += 8; return 'vectXY'
        if t == 0x66: return 'empty(%s)' % self.expr()
        if t == 0x60: a = self.expr(); return '%s = %s' % (a, self.expr())
        if t in CONV: return 'conv%02x(%s)' % (t, self.expr())
        if 0x80 <= t <= 0x8F: return self.nat((t - 0x80) * 256 + self.r8())
        if t >= 0x90: return self.nat(t)
        raise Bad('token %02x at %d' % (t, self.p - 1))
    def nat(self, n):
        nm = self.natives.get(n, 'native%d' % n); self.calls.append(nm)
        return '%s(%s)' % (nm, ', '.join(self.params()))
    def decode(self, fn):
        self.pkg = fn.pkg; self.b = fn.b; self.p = fn.code0; self.refs = []; self.calls = []; self.lets = []
        out = []
        while self.p < fn.end:
            at = self.p - fn.code0
            try:
                out.append('  %04x  %s' % (at, self.expr()))
            except Bad as ex:
                out.append('  %04x  !! %s' % (at, ex)); return out, False
            except Exception as ex:
                out.append('  %04x  !! %r' % (at, ex)); return out, False
        return out, self.p == fn.end

def load_all():
    pkgs = {}
    for f in sorted(os.listdir(P)):
        if f.endswith('.u'): pkgs[f[:-2]] = Pkg(P + f)
    natives = {}
    fns = []
    for pn, pk in pkgs.items():
        for i, x in enumerate(pk.exports):
            if pk.clsname(x['cls']) == 'Function' and x['size'] > 20:
                try:
                    f = Fn(pk, i + 1)
                except Exception:
                    continue
                f.pn = pn
                fns.append(f)
                if f.native: natives[f.native] = pk.path(i + 1).split('.')[-1]
    return pkgs, natives, fns

if __name__ == '__main__':
    pkgs, natives, fns = load_all()
    d = Dec(pkgs, natives)
    for want in sys.argv[1:]:
        pn, path = want.split(':')
        pk = pkgs[pn]
        for r in pk.find(path, 'Function'):
            f = Fn(pk, r)
            out, ok = d.decode(f)
            print('%s:%s flags=%08x native=%d children=%s ok=%s' % (pn, pk.path(r), f.flags, f.native,
                  pk.path(f.children) if f.children else '-', ok))
            print('\n'.join(out))
