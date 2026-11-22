# Writes the coverage test scripts (cov_<group>.zs) and items.txt from gaps.txt: every ZASM command
# and register listed there is called once by a small internal function whose @zasm is taken from
# upstream's own bindings (or, for names only older compilers emit, written from the command table).
#   python -I gen_coverage.py <upstream 3.0 bindings dir> <our compiler's components/zasm/table.cpp>
# The scripts print "C: <name> <values>" lines; feature_suite.py compares them with upstream's.
import os, re, sys, collections

HERE = os.path.dirname(os.path.abspath(__file__))
BIND, TABLE = sys.argv[1], sys.argv[2]

gaps = []
for l in open(os.path.join(HERE, 'gaps.txt'), encoding='utf-8'):
    m = re.match(r'(cmd|reg) (\S+)\s+(\d+)\s+pack\s+(\d+)\s+(.*)', l)
    if m:
        f = re.search(r'scripting/types/(\w+)\.cpp', m.group(5))
        gaps.append(dict(kind=m.group(1), name=m.group(2), pack=int(m.group(4)), note=m.group(5),
                         tfile=f.group(1) if f else ''))

# --- the command table: argument types -----------------------------------------------------------
argty = {}
for m in re.finditer(r'\{ "(\w+)", \w+, (\d), \{ ([^}]*) \}', open(TABLE, encoding='utf-8').read()):
    argty[m.group(1)] = [a.strip() for a in m.group(3).split(',')][:int(m.group(2))]

# --- upstream's bindings ---------------------------------------------------------------------------
classes = {}          # class -> dict(ref, base)
decls = []            # dict(cls, kind 'var'|'fn', name, type, params, zasm lines | var, array, const)
for fn in sorted(os.listdir(BIND)):
    if not fn.endswith('.zh'):
        continue
    cls = None; depth = 0; doc = []; pend_ref = None; acc = None
    for raw in open(os.path.join(BIND, fn), encoding='utf-8'):
        line = raw.strip()
        # (a declaration over several lines is taken whole)
        if acc is not None:
            acc += ' ' + line
            if not line.endswith(';'):
                continue
            line = acc; acc = None
        elif line.startswith('internal ') and not line.endswith(';') and '(' in line and cls is not None:
            acc = line
            continue
        if line.startswith('//'):
            doc.append(line[2:].rstrip());
            m = re.match(r'// @zasm_ref (\w+)', line)
            if m and cls is None: pend_ref = m.group(1)
            continue
        m = re.match(r'class (\w+)(?:\s*:\s*(\w+))?', line)
        if m and cls is None:
            ext = re.search(r'@extends (\w+)', '\n'.join(doc))
            cls = m.group(1); classes[cls] = dict(ref=pend_ref, base=m.group(2) or (ext.group(1) if ext else None), file=fn)
            pend_ref = None; doc = []; depth = line.count('{') - line.count('}')
            continue
        if cls is not None:
            depth += line.count('{') - line.count('}')
            if depth <= 0 and '}' in line:
                cls = None; doc = []; continue
        if line.startswith('internal ') and line.endswith(';') and cls is not None:
            text = '\n'.join(doc)
            zl = []
            m = re.search(r'@zasm\n((?:[ \t]+.*\n?)+)', text + '\n')
            if m:
                zl = [x.split(';')[0].strip() for x in m.group(1).split('\n') if x.strip() and not x.strip().startswith('@')]
            mv = re.search(r'@zasm_var (\w+)', text)
            body = line[len('internal '):-1].strip()
            mf = re.match(r'(?:const\s+)?([\w\[\]]+)\s+(\w+)\s*\((.*)\)$', body)
            if mf and zl:
                decls.append(dict(cls=cls, kind='fn', type=mf.group(1), name=mf.group(2), params=mf.group(3), zasm=zl))
            elif mv:
                mv2 = re.match(r'(const\s+)?([\w\[\]]+)\s+(\w+)\s*(\[\])?$', body)
                if mv2:
                    t = mv2.group(2); arr = bool(mv2.group(4)) or t.endswith('[]')
                    decls.append(dict(cls=cls, kind='var', type=t.replace('[]', ''), name=mv2.group(3), var=mv.group(1),
                                      array=arr, const=bool(mv2.group(1))))
        doc = [] if not line.startswith('//') else doc

def ref_of(cls):
    seen = 0
    while cls and seen < 8:
        c = classes.get(cls)
        if not c: return None
        if c['ref']: return c['ref']
        cls = c['base']; seen += 1
    return None

# objects the scripts make for each reference register (ZScript type, how it is made)
OBJ = {
    'REFNPC': ('npc', 'Screen->CreateNPC(20)'), 'REFLWPN': ('lweapon', 'Screen->CreateLWeapon(1)'),
    'REFEWPN': ('eweapon', 'Screen->CreateEWeapon(129)'), 'REFITEM': ('itemsprite', 'Screen->CreateItem(1)'),
    'REFSPRITE': ('npc', 'Screen->CreateNPC(20)'), 'REFITEMDATA': ('itemdata', 'Game->LoadItemData(1)'),
    'REFNPCDATA': ('npcdata', 'Game->LoadNPCData(20)'), 'REFDMAPDATA': ('dmapdata', 'Game->LoadDMapData(0)'),
    'REFMAPDATA': ('mapdata', 'Game->LoadMapData(1, 0x14)'), 'REFMSGDATA': ('messagedata', 'Game->LoadMessageData(1)'),
    'REFCOMBODATA': ('combodata', 'Game->LoadComboData(16)'), 'REFSHOPDATA': ('shopdata', 'Game->LoadShopData(0)'),
    'REFSPRITEDATA': ('spritedata', 'Game->LoadSpriteData(1)'), 'REFBOTTLETYPE': ('bottledata', 'Game->LoadBottleData(1)'),
    'REFBOTTLESHOP': ('bottleshopdata', 'Game->LoadBottleShopData(1)'), 'REFDROPSETDATA': ('dropsetdata', 'Game->LoadDropset(1)'),
    'REFGENERICDATA': ('genericdata', 'Game->LoadGenericData(1)'), 'REFBITMAP': ('bitmap', 'Game->CreateBitmap(32, 32)'),
    'REFRNG': ('randgen', 'Game->LoadRNG()'), 'REFSTACK': ('stack', 'Game->LoadStack()'),
    'REFPALDATA': ('paldata', 'Graphics->CreatePalData()'), 'REFPORTAL': ('portal', 'Screen->CreatePortal()'),
    'REFSAVPORTAL': ('savedportal', 'Game->CreateSavedPortal()'), 'REFMUSIC': ('musicdata', 'Audio->LoadMusicData(1)'),
    'REFSUBSCREENDATA': ('subscreendata', 'Game->LoadASubData(0)'),
    'REFSUBSCREENPAGE': ('subscreenpage', 'Game->LoadASubData(0)->Pages[0]'),
    'REFSUBSCREENWIDG': ('subscreenwidget', 'Game->LoadASubData(0)->Pages[0]->Widgets[0]'),
    'REFCOMBOTRIGGER': ('combotrigger', 'Game->LoadComboData(16)->Triggers[0]'),
    'REFFFC': ('ffc', 'Screen->LoadFFC(1)'), 'REFSAVEMENU': ('save_menu', 'Game->LoadSaveMenu(1)'), 'REFWEAPDATA': ('weapondata', 'Game->LoadItemData(1)->WeaponData'),
}
SKIP_REF = {'REFFILE', 'REFDIRECTORY', 'REFWEBSOCKET'}
# registers by upstream's scripting/types file, for names only older compilers emit
TFILE_REF = {'hero': None, 'game': None, 'input': None, 'misc': None, 'eweapon': 'REFEWPN', 'lweapon': 'REFLWPN',
             'npc': 'REFNPC', 'itemsprite': 'REFITEM', 'mapdata': 'REFMAPDATA', 'screendata': None, 'npcdata': 'REFNPCDATA',
             'itemdata': 'REFITEMDATA'}
# never called: end the game, write files, wait on input, the network, or bring the script itself down
DANGER = re.compile(r'^(GAME(END|EXIT|RELOAD|SAVEQUIT|SAVECONTINUE|CONTINUE)|SAVE|END|QUIT|QUIT_NO_DEALLOC|WARP|PITWARP|'
                    r'BREAKPOINT|MAKEVARGARRAY|LOAD_INTERNAL_ARRAY(_REF)?|GOTO\w*|CALLFUNC|RETURN\w*|WAIT\w*|PC|WEBSOCKET\w*|'
                    r'FILE\w*|DIRECTORY\w*|SAVEMENU_OPEN|SHOWSAVESCREEN|SHOWF6SCREEN|CHANGEFFSCRIPT\w*|RUNGENFRZSCR)$')

def dummy(t):
    t = t.strip()
    if '...' in t: return None
    m = re.match(r'(const\s+)?([\w]+)(\[\])?\s+(\w+)(\s*=.*)?$', t)
    if not m: return None
    ty, arr = m.group(2), m.group(3)
    if arr: return 'cvbuf' if ty == 'char32' else 'cvibuf' if ty in ('int', 'untyped', 'long') else None
    if ty in ('int', 'long', 'untyped', 'float'): return '1'
    if ty == 'bool': return 'false'
    if ty == 'rgb': return '<rgb>0'
    for ref, (oty, _) in OBJ.items():
        if oty == ty: return 'o_' + ty
    if re.match(r'[A-Z]\w*$', ty): return '0'     # an enum
    return None

def zs_type(t):
    t = t.replace('const ', '')
    return t if t not in ('long',) else 'long'

by_item = collections.defaultdict(list)
for d in decls:
    if d['kind'] == 'var':
        by_item[d['var']].append(d)
    else:
        for z in d['zasm']:
            toks = z.split()
            for k in toks:
                by_item[k].append(d)

items_out = []
groups = collections.OrderedDict()
def group_of(g, d):
    if d is not None: return classes[d['cls']]['file'][:-3]
    return g['tfile'] or 'misc'

for g in gaps:
    n = g['name']; how = None; code = None; d = None
    cands = [x for x in by_item.get(n, []) if not (x['kind'] == 'fn' and ('<' in x['params'] or 'T ' in x['params']))]
    if DANGER.match(n):
        how = 'skip: ends the game, writes files, waits or controls the script'
    elif g['kind'] == 'reg' and any(c['kind'] == 'var' for c in cands):
        d = next(c for c in cands if c['kind'] == 'var')
        how = 'var'
    elif cands:
        fns = [c for c in cands if c['kind'] == 'fn']
        # the simplest function using it
        fns.sort(key=lambda c: (len(c['zasm']), len(c['params'])))
        d = fns[0] if fns else None
        how = 'fn' if d else None
    if how is None:
        if g['kind'] == 'reg':
            how = 'oldreg'
        elif n in argty and all(a in ('REG_R', 'REG_W', 'REG_RW', 'NUM') for a in argty[n]):
            how = 'oldcmd'
        else:
            how = 'skip: not in upstream\'s bindings and its arguments can\'t be written'
    grp = group_of(g, d)
    g.update(how=how, decl=d, group=grp)
    groups.setdefault(grp, []).append(g)

# merge small groups so each script has a fair number of items
merged = collections.OrderedDict()
for grp, lst in groups.items():
    key = grp if len(lst) >= 8 else 'misc'
    merged.setdefault(key, []).extend(lst)

# Items that stop the script's later frames in upstream (end the game, warp, start a wipe, open a menu):
# run last in their group, so all others are still reached.
LAST = {'INCQST', 'CREATEPORTAL', 'CLOSEWIPE', 'CLOSEWIPESHAPE', 'OPENWIPE', 'OPENWIPESHAPE', 'SUBDATA_OPEN', 'STACKWRITEATRV'}
for grp in merged:
    merged[grp].sort(key=lambda g: g['name'] in LAST)

def ident(s): return re.sub(r'\W', '_', s)

lines_items = []
for grp, lst in merged.items():
    fun = []; inits = {}; body = []; idx = 0
    for g in lst:
        n = g['name']; how = g['how']; d = g['decl']; nm = ident(n)
        g['group'] = grp
        if how.startswith('skip'):
            lines_items.append('%s %s %s pack %d %s' % (grp, g['kind'], n, g['pack'], how)); continue
        if how == 'var':
            ref = classes[d['cls']]['ref']
            if ref in SKIP_REF:
                lines_items.append('%s %s %s pack %d skip: object type not made in tests' % (grp, g['kind'], n, g['pack'])); continue
            oty = OBJ.get(ref, (None,))[0] if ref else None
            if ref and not oty:
                lines_items.append('%s %s %s pack %d skip: no test object for %s' % (grp, g['kind'], n, g['pack'], ref)); continue
            ty = zs_type(d['type']); ty = 'untyped' if ty not in ('int', 'bool', 'long', 'untyped') else ty
            rp = []; wp = []; rz = []; wz = []; ra = []
            if ref: rp.append('%s o' % oty); ra.append('o_' + oty); inits[ref] = OBJ[ref]
            if d['array']: rp.append('int i'); ra.append('1')
            if d['array']: rz.append('POP D0')
            if ref: rz.append('POP ' + ref)
            fun.append('// @zasm\n%s\n//   SETR D2 %s\ninternal %s cvr_%s(%s);\n' % ('\n'.join('//   ' + z for z in rz) if rz else '//   ; (no object)', d['var'], ty, nm, ', '.join(rp)))
            fun.append('// @zasm\n//   POP D2\n%s\n//   SETR %s D2\ninternal void cvw_%s(%s);\n' % ('\n'.join('//   ' + z for z in rz) if rz else '//   ; (no object)', d['var'], nm, ', '.join(rp + ['%s v' % ty])))
            a = ', '.join(ra)
            body.append('\t\t{ untyped v = cvr_%s(%s); %scvw_%s(%s)%s printf("C: %s r %%d w %%d\\n", v, cvr_%s(%s)); }' % (
                nm, a, '' if not d['const'] else '/* const: ', nm, ', '.join(ra + ['v']), ';' if not d['const'] else '; */', n, nm, a))
            lines_items.append('%s %s %s pack %d var %s.%s%s' % (grp, g['kind'], n, g['pack'], d['cls'], d['name'], '[]' if d['array'] else ''))
        elif how == 'fn':
            pops = [z.split()[1] for z in d['zasm'] if z.split()[0] == 'POP' and len(z.split()) > 1 and z.split()[1].startswith('REF')]
            ref = pops[0] if pops else None
            # values the code takes off the stack: one more than the parameters = the object itself
            npop = 0
            for z in d['zasm']:
                t = z.split()
                if t[0] == 'POP': npop += 1
                elif t[0] == 'POPARGS': npop += int(round(float(t[2]) * 10000))
            nparam = len([p for p in re.split(r',(?![^(]*\))', d['params']) if p.strip()])
            if not ref and npop == nparam + 1:
                ref = ref_of(d['cls'])
                if ref is None:
                    lines_items.append('%s %s %s pack %d skip: member of %s, no test object' % (grp, g['kind'], n, g['pack'], d['cls'])); continue
            if ref in SKIP_REF:
                lines_items.append('%s %s %s pack %d skip: object type not made in tests' % (grp, g['kind'], n, g['pack'])); continue
            ps = [p for p in re.split(r',(?![^(]*\))', d['params']) if p.strip()]
            args = [dummy(p) for p in ps]
            zl = d['zasm']
            uses_ref = bool(ref)
            if None in args or (ref and uses_ref and ref not in OBJ):
                lines_items.append('%s %s %s pack %d skip: arguments can\'t be made (%s)' % (grp, g['kind'], n, g['pack'], d['params'][:60])); continue
            sig = []; call = []
            if ref and uses_ref:
                oty = OBJ[ref][0]; inits[ref] = OBJ[ref]; sig.append('%s o' % oty); call.append('o_' + oty)
            for p, a in zip(ps, args):
                p2 = re.sub(r'\s*=.*$', '', p.strip()).replace('const ', '')
                p2 = re.sub(r'^[A-Z]\w*\s', 'int ', p2)     # an enum type: int
                sig.append(p2); call.append(a)
                if a.startswith('o_'):
                    for ref2, (oty2, mk) in OBJ.items():
                        if 'o_' + oty2 == a: inits[ref2] = (oty2, mk)
            rty = d['type']
            ret = rty if rty in ('void', 'int', 'bool', 'long', 'untyped') else ('untyped' if not rty.endswith('[]') else 'untyped')
            fun.append('// @zasm\n%s\ninternal %s cvf_%s_%d(%s);\n' % ('\n'.join('//   ' + z for z in zl), ret, nm, idx, ', '.join(sig)))
            if ret == 'void':
                body.append('\t\t{ cvf_%s_%d(%s); printf("C: %s called\\n"); }' % (nm, idx, ', '.join(call), n))
            else:
                body.append('\t\t{ untyped v = cvf_%s_%d(%s); printf("C: %s r %%d\\n", v); }' % (nm, idx, ', '.join(call), n))
            lines_items.append('%s %s %s pack %d fn %s.%s(%s)' % (grp, g['kind'], n, g['pack'], d['cls'], d['name'], d['params'][:50]))
            idx += 1
        elif how == 'oldreg':
            ref = TFILE_REF.get(g['tfile'])
            rz = (['POP ' + ref] if ref else [])
            rp = (['%s o' % OBJ[ref][0]] if ref else []); ra = (['o_' + OBJ[ref][0]] if ref else [])
            if ref: inits[ref] = OBJ[ref]
            fun.append('// @zasm\n//   SETV D0 0\n%s//   SETR D2 %s\ninternal untyped cvr_%s(%s);\n' % (''.join('//   %s\n' % z for z in rz), n, nm, ', '.join(rp)))
            body.append('\t\t{ untyped v = cvr_%s(%s); printf("C: %s r %%d\\n", v); }' % (nm, ', '.join(ra), n))
            lines_items.append('%s %s %s pack %d oldreg (%s)' % (grp, g['kind'], n, g['pack'], ref or 'no object'))
        elif how == 'oldcmd':
            ops = ['D2' if a.startswith('REG') else '1' for a in argty[n]]
            fun.append('// @zasm\n//   SETV D2 1\n//   %s %s\ninternal untyped cvc_%s();\n' % (n, ' '.join(ops), nm))
            body.append('\t\t{ untyped v = cvc_%s(); printf("C: %s r %%d\\n", v); }' % (nm, n))
            lines_items.append('%s %s %s pack %d oldcmd %s %s' % (grp, g['kind'], n, g['pack'], n, ' '.join(ops)))
    if not body:
        continue
    out = ['// Coverage group "%s": generated by gen_coverage.py from gaps.txt; every line prints "C: NAME ..."' % grp,
           '// frames: %d' % (66 + 10 + len(body) + 60), '#include "std.zh"', '', 'char32 cvbuf[256] = "test";', 'int cvibuf[256];']
    for ref, (oty, mk) in sorted(inits.items()):
        out.append('%s o_%s;' % (oty, oty))
    out.append('')
    out += fun
    out.append('global script LREx1Ex2ItemSwitch\n{\n\tvoid run()\n\t{')
    out.append('\t\tfor(int f = 0; f < 10; ++f) Waitframe();')
    for ref, (oty, mk) in sorted(inits.items()):
        out.append('\t\to_%s = %s;' % (oty, mk))
    out.append('\t\tWaitframe();')
    for i, b in enumerate(body):
        out.append(b)
        out.append('\t\tWaitframe();')
    out.append('\t\tprintf("C: DONE\\n");\n\t\twhile(true) Waitframe();\n\t}\n}')
    open(os.path.join(HERE, 'cov_%s.zs' % grp), 'w', newline='\n').write('\n'.join(out) + '\n')
open(os.path.join(HERE, 'items.txt'), 'w', newline='\n').write(
    '# group kind name pack how (written by gen_coverage.py)\n' + '\n'.join(lines_items) + '\n')
cnt = collections.Counter(l.split()[0] for l in lines_items if ' skip' not in l)
print('groups:', ', '.join('%s %d' % kv for kv in cnt.items()))
print('items: %d, called %d, skipped %d' % (len(lines_items), sum(cnt.values()), sum(1 for l in lines_items if ' skip' in l)))
