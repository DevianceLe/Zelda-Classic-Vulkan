# Writes q30_fonts_data.h: the fonts upstream ZQuest Classic (2.55 and 3.0) has that 2.53's
# fonts.dat lacks, as glyph tables built into the player (see q30_fonts.cpp).
#
# Source: upstream's resources/modules/classic/classic_fonts.dat (an Allegro 4 datafile; the
# same file in 2.55.17 and 3.0). Upstream's repository is under the GPLv3 (its LICENSE file);
# the font file carries no separate notice. The 30 fonts both files have are identical, so
# only the others are written. Run:
#   python -I q30_fonts_gen.py <classic_fonts.dat> <2.53 fonts.dat> q30_fonts_data.h
import hashlib, struct, sys

PASSWORD = b'longtan'   # zsys.cpp's datapwd after resolve_password

# upstream's core/fonts.cpp initFonts: font id -> datafile object and name
UPSTREAM = [
    (27, 'FONT_A80080C', 'Atari 800'), (28, 'FONT_ACORN', 'Acorn'), (29, 'FONT_ADOS', 'ADOS'),
    (31, 'FONT_APPLE2', 'Apple II'), (32, 'FONT_APPLE280', 'Apple II 80 Column'),
    (33, 'FONT_APPLE2GS', 'Apple IIgs'), (34, 'FONT_AQUA', 'Aquarius'), (35, 'FONT_ATARI400', 'Atari 400'),
    (36, 'FONT_C64', 'C64'), (37, 'FONT_C64HR', 'C64 HiRes'), (38, 'FONT_CGA', 'IBM CGA'),
    (39, 'FONT_COCO', 'COCO Mode I'), (40, 'FONT_COCO2', 'COCO Mode II'), (41, 'FONT_COUPE', 'Coupe'),
    (42, 'FONT_CPC', 'Amstrad CPC'), (43, 'FONT_FANTASY', 'Fantasy Letters'), (44, 'FONT_FDS_KANA', 'FDS Katakana'),
    (45, 'FONT_FDSLIKE', 'FDSesque'), (46, 'FONT_FDSROMAN', 'FDS Roman'), (47, 'FONT_FF', 'FF'),
    (48, 'FONT_FUTHARK', 'Elder Futhark'), (49, 'FONT_GAIA', 'Gaia'), (50, 'FONT_HIRA', 'Hira'),
    (51, 'FONT_JP', 'JP Unsorted'), (52, 'FONT_KONG', 'Kong'), (53, 'FONT_MANA', 'Mana'),
    (54, 'FONT_MARIOLAND', 'Mario'), (55, 'FONT_MOT', 'Mot CPU'), (56, 'FONT_MSX0', 'MSX Mode 0'),
    (57, 'FONT_MSX1', 'MSX Mode 1'), (58, 'FONT_PET', 'PET'), (59, 'FONT_PRESTRT', 'Homebrew'),
    (60, 'FONT_SATURN', 'Mr. Saturn'), (61, 'FONT_SCIFI', 'Sci-Fi'), (62, 'FONT_SHERWOOD', 'Sherwood'),
    (63, 'FONT_SINQL', 'Sinclair QL'), (64, 'FONT_SPEC', 'Spectrum'), (65, 'FONT_SPECLG', 'Spectrum Large'),
    (66, 'FONT_TI99', 'TI99'), (67, 'FONT_TRS', 'TRS'), (68, 'FONT_Z2', 'Zelda 2'), (69, 'FONT_ZX', 'ZX'),
    (70, 'FONT_LISA', 'Lisa'), (73, 'FONT_CV3', 'CV 3'), (74, 'FONT_CTRIG', 'Chrono'),
    (76, 'FONT_BAK', 'Betrayal at Krondor'), (77, 'FONT_GUNSTAR', 'Gunstar Heroes'), (78, 'FONT_SMW_CREDITS', 'SMW Credits'),
    (79, 'FONT_WL4', 'WLand 4'), (80, 'FONT_BSZ', 'BSZ'), (81, 'FONT_BSZ_PROP', 'BSZ Proportional'),
    (82, 'FONT_FF6', 'FF6'), (83, 'FONT_EVO_EDEN', 'EVO Search for Eden'), (84, 'FONT_SMT', 'SMT Font'),
    (85, 'FONT_ACTRAISER', 'Actraiser'), (86, 'FONT_BAK_RUNES', 'Betrayal at Krondor: Runes'),
    (87, 'FONT_BAK_SMALL', 'Betrayal at Krondor: Small'), (88, 'FONT_DISORIENT', 'Disoriented'), (89, 'FONT_DOOM', 'DOOM'),
    (90, 'FONT_DRACULA', 'Dracula'), (91, 'FONT_EJIM', 'Earthworm Jim'), (92, 'FONT_FALLOUT', 'Fallout'),
    (93, 'FONT_GRADIUS', 'Gradius'), (94, 'FONT_LAMU_MSX', 'LaMulana'), (95, 'FONT_MEGAMAN', 'Megaman'),
    (96, 'FONT_WINGDINGS', 'WingDings'), (97, 'FONT_PKMN2', 'Pokemon G2'), (98, 'FONT_SMRPG', 'SMRPG'),
    (99, 'FONT_UNDERTALE', 'Undertale'), (100, 'FONT_SMW', 'SMW Text'),
]
# Upstream's font 30 (FONT_ALLEGRO, 'Allegro') is Allegro 4's own built-in font. It is left out:
# the port carries nothing derived from Allegro, so a quest that asks for it gets the default font.

def unlzss(data):
    N, F, TH = 4096, 18, 2
    buf = bytearray(N); r = N - F; out = bytearray(); i = 0; flags = 0
    while i < len(data):
        flags >>= 1
        if not (flags & 256):
            flags = data[i] | 0xFF00; i += 1
            if i >= len(data): break
        if flags & 1:
            c = data[i]; i += 1; out.append(c); buf[r] = c; r = (r + 1) & (N - 1)
        else:
            if i + 1 >= len(data): break
            a = data[i] | ((data[i + 1] & 0xF0) << 4); n = (data[i + 1] & 0x0F) + TH; i += 2
            for k in range(n + 1):
                c = buf[(a + k) & (N - 1)]; out.append(c); buf[r] = c; r = (r + 1) & (N - 1)
    return bytes(out)

def magic_mask(pw):
    # src/port/al/files_packfile.cpp magic_mask (new format): the password folded over 4 bytes
    s = lambda c: c - 256 if c > 127 else c
    m = 0
    for i, c in enumerate(pw):
        m ^= (s(c) << ((i & 3) * 8)) & 0xFFFFFFFF
    for k in range(4):
        m ^= (s(pw[k % len(pw)]) << (24 - k * 8)) & 0xFFFFFFFF
    return m ^ 42

def open_datafile(path):
    raw = open(path, 'rb').read()
    # every byte is XORed with the password, the magic too, which is also masked
    d = bytes(b ^ PASSWORD[i % len(PASSWORD)] for i, b in enumerate(raw))
    magic = struct.unpack('>I', d[:4])[0] ^ magic_mask(PASSWORD)
    assert magic in (0x736C6821, 0x736C682E), 'not a packfile: %x' % magic    # 'slh!' packed, 'slh.' not
    body = unlzss(d[4:]) if magic == 0x736C6821 else d[4:]
    assert body[:4] == b'ALL.', 'not an Allegro datafile: %r' % body[:4]
    return body

def read_objects(d):
    p = 4
    count, = struct.unpack_from('>i', d, p); p += 4
    objs = {}
    for _ in range(count):
        props = {}
        while True:
            t = d[p:p + 4]; p += 4
            if t != b'prop': break
            ptype = d[p:p + 4]; size, = struct.unpack_from('>i', d, p + 4); p += 8
            props[ptype] = d[p:p + size].decode('latin1'); p += size
        csize, usize = struct.unpack_from('>ii', d, p); p += 8
        body = d[p:p + csize]; p += csize
        if usize < 0:
            body = unlzss(body)
        objs[props.get(b'NAME', '')] = (t, body)
    return objs

def read_font(body):
    """Allegro 4 FONT object -> (height, [(first, [(w, h, bytes), ...]), ...]); mono only."""
    fsize, = struct.unpack_from('>h', body, 0); p = 2
    assert fsize == 0, 'old-style font'
    nranges, = struct.unpack_from('>H', body, p); p += 2
    ranges = []; height = 0
    for _ in range(nranges):
        mono = body[p]; first, last = struct.unpack_from('>ii', body, p + 1); p += 9
        assert mono, 'color font'
        glyphs = []
        for c in range(first, last + 1):
            w, h = struct.unpack_from('>HH', body, p); p += 4
            n = ((w + 7) // 8) * h
            glyphs.append((w, h, body[p:p + n])); p += n
            height = max(height, h)
        ranges.append((first, glyphs))
    return height, ranges

def same_font(a, b):
    return a == b       # the object's bytes

def main(src, ours, out):
    up = read_objects(open_datafile(src))
    have = read_objects(open_datafile(ours))
    for name, (t, body) in have.items():
        if t == b'FONT' and name in up:
            assert same_font(up[name][1], body), name + ' differs from 2.53\'s'
    data = bytearray(); defs = []
    for fid, obj, title in UPSTREAM:
        assert obj not in have, obj + ' is in 2.53\'s fonts.dat'
        height, ranges = read_font(up[obj][1])
        assert len(ranges) <= 4
        offset = len(data)
        for first, glyphs in ranges:
            for w, h, bits in glyphs:
                assert w < 256 and h < 256
                data += bytes([w, h]) + bits
        defs.append((fid, title, obj, height, [(first, len(glyphs)) for first, glyphs in ranges], offset))
    digest = hashlib.sha256(open(src, 'rb').read()).hexdigest()
    with open(out, 'w', newline='\n') as f:
        f.write('// Generated by q30_fonts_gen.py; do not edit.\n')
        f.write('// From upstream ZQuest Classic\'s resources/modules/classic/classic_fonts.dat\n')
        f.write('// (sha256 %s, the same file in 2.55.17 and 3.0;\n' % digest)
        f.write('// GPLv3 like the rest of upstream\'s repository). The fonts upstream has and 2.53\'s fonts.dat\n')
        f.write('// lacks, by upstream\'s font id (core/fonts.h). q30_font_bits holds, range after range, each\n')
        f.write('// character\'s width and height (a byte each) and its rows of (width + 7) / 8 bytes, the\n')
        f.write('// leftmost pixel in the top bit (Allegro\'s monochrome glyphs).\n\n')
        f.write('static const unsigned char q30_font_bits[%d] =\n{\n' % len(data))
        for i in range(0, len(data), 32):
            f.write('    ' + ','.join(str(b) for b in data[i:i + 32]) + ',\n')
        f.write('};\n\n')
        f.write('static const Q30FontDef q30_font_defs[] =\n{\n')
        for fid, title, obj, height, ranges, offset in defs:
            rs = ', '.join('{ %d, %d }' % r for r in ranges)
            f.write('    { %d, "%s", %d, %d, { %s }, %d },  // %s\n' % (fid, title, height, len(ranges), rs, offset, obj))
        f.write('};\n')
    print('wrote', out, len(defs), 'fonts', len(data), 'bytes')

if __name__ == '__main__':
    main(*sys.argv[1:4])
