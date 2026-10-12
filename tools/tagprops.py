# Tagged-property reader for DNF packages (class defaults in .u files, instances in map .dnf files). Built 2026-10-11
# for docs/re/dlc.md; the format was found on the way (docs/re/progress.md "Property tag format").
#   info byte: low nibble = type (1 byte, 2 int, 3 bool [value = bit 0x80], 4 float, 5 object, 6 name, 9 array,
#   10 struct [struct name follows], 13 string); bits 4-6 = size code (0:1 1:2 2:4 3:12 4:16 byte/word/dword follows
#   for 5/6/7); bit 7 = static-array element (an index byte follows the size). An array (9) carries its inner class
#   name before the size, and its payload starts with a count byte (also for byte arrays).
# Usage: python tools/tagprops.py defaults <Pkg> <Class>            (Pkg: a extracted-packages name, DLC03_Game too)
#        python tools/tagprops.py instances <Map.dnf path> <Class> [Key ...]
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ubc
rc = ubc.rd_compact
R = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'research', 'packages')

class Fail(Exception): pass

def plist(p, b, pos, end):
    out = []
    while True:
        if pos >= end: raise Fail('eof')
        ni, pos = rc(b, pos)
        if ni < 0 or ni >= len(p.names): raise Fail('name')
        nm = p.names[ni]
        if nm == 'None': return out, pos
        info = b[pos]; pos += 1
        t, code, arr = info & 15, (info >> 4) & 7, info & 0x80
        inner = None; idx = None
        if t == 10: _, pos = rc(b, pos)
        if t == 9:
            i2, pos = rc(b, pos); inner = p.names[i2]
        if code < 5: sz = (1, 2, 4, 12, 16)[code]
        elif code == 5: sz = b[pos]; pos += 1
        elif code == 6: sz = struct.unpack_from('<H', b, pos)[0]; pos += 2
        else: sz = struct.unpack_from('<I', b, pos)[0]; pos += 4
        if t == 3: sz = 0
        if arr and t != 3:
            idx = b[pos]; pos += 1
            if idx & 0x80: idx = ((idx & 0x7f) << 8) | b[pos]; pos += 1
        if pos + sz > end: raise Fail('size')
        pl = b[pos:pos+sz]; q = pos; pos += sz
        if t == 1: v = pl[0]
        elif t == 2: v = struct.unpack('<i', pl)[0]
        elif t == 3: v = bool(arr)
        elif t == 4: v = struct.unpack('<f', pl)[0]
        elif t == 6: v = p.names[rc(pl, 0)[0]]
        elif t == 13: l, o = rc(pl, 0); v = pl[o:o+l-1].decode('latin1')
        elif t == 5: v = ('obj', rc(pl, 0)[0])
        elif t == 10:
            v, e = plist(p, b, q, q + sz)
            if e != q + sz: raise Fail('struct')
            v = dict(v)
        elif t == 9:
            n = pl[0] if pl else 0
            if inner == 'ByteProperty': v = list(pl[1:1+n])
            elif inner == 'FloatProperty': v = list(struct.unpack_from('<%df' % n, pl, 1))
            elif inner == 'IntProperty': v = list(struct.unpack_from('<%di' % n, pl, 1))
            elif inner in ('StructProperty', 'StrProperty', 'NameProperty', 'ObjectProperty', 'ClassProperty'):
                pp = 1; v = []
                for _ in range(n):
                    if inner == 'StructProperty':
                        r, e = plist(p, b, q + pp, q + sz); pp = e - q; v.append(dict(r))
                    elif inner == 'StrProperty':
                        l, o = rc(pl, pp); v.append(pl[pp+o:pp+o+l-1].decode('latin1')); pp += o + l
                    else:
                        r, pp = rc(pl, pp); v.append(p.names[r] if inner == 'NameProperty' else ('obj', r))
            else: v = ('raw', inner, pl.hex())
        else: v = ('raw', t, pl.hex())
        out.append((nm if idx is None else '%s[%d]' % (nm, idx), v))

def class_defaults(p, r):
    """The defaults list of a Class export: the earliest offset whose property list ends exactly at the export's end."""
    x = p.exports[r-1]; b = p.d[x['off']:x['off']+x['size']]
    for s in range(len(b) - 2):
        try:
            lst, e = plist(p, b, s, len(b))
        except Exception:
            continue
        if e == len(b) and lst: return lst
    return None

def instance_props(p, ex):
    """Tagged properties of an actor export (index into p.exports); the list starts at offset 0."""
    x = p.exports[ex]; b = p.d[x['off']:x['off']+x['size']]
    for s in range(min(40, len(b))):
        try:
            lst, e = plist(p, b, s, len(b))
            return lst
        except Exception:
            pass
    return None

def load(name):
    for d in ('', 'dlc03', 'dlc01'):
        f = os.path.join(R, d, name + '.u')
        if os.path.exists(f): return ubc.Pkg(f)
    raise SystemExit('no package ' + name)

if __name__ == '__main__':
    if sys.argv[1] == 'defaults':
        p = load(sys.argv[2])
        for r in p.find(sys.argv[3], 'Class'):
            if p.path(r).split('.')[-1] != sys.argv[3]: continue
            print(p.path(r))
            for k, v in class_defaults(p, r) or []: print(' ', k, '=', v)
    elif sys.argv[1] == 'instances':
        p = ubc.Pkg(sys.argv[2]); keys = sys.argv[4:]
        for i, x in enumerate(p.exports):
            if p.clsname(x['cls']) != sys.argv[3]: continue
            d = dict(instance_props(p, i) or [])
            print(p.objname(i+1), {k: v for k, v in d.items() if not keys or k in keys})
