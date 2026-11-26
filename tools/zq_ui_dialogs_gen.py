"""zq_ui_dialogs_gen.py: the table of every build's ZQuest dialogs the interface profiles lay our editors
out by (src/port/zqeditor/zq_ui_dialogs_table.h), from zquiinv's zq_ui_dialogs.json (each catalog
build's dialogs read out of its own source).

    python tools/zq_ui_dialogs_gen.py <zq_ui_dialogs.json> [<out.h>]

Kept: every build's main editors (zquiinv's "main" categories) and every 2.53.1 DIALOG array (the
reference our editors are told apart by). A widget: its procedure's name, x, y, w, h, its text (dp,
when a string) and its tab page.

DIALOG arrays (1.90 to 2.53.x, and the older dialogs 2.55/3.0 kept) are taken as they are. The GUI
builder dialogs of 2.55 and 3.0 (a tree of Window, Column, Row, Rows<N>, Frame, TabPanel, Checkbox,
Label, TextField, ...) have no places: they are laid out here the way their toolkit stacks them, with
jwin-like sizes (6 px a character, 12 px a check box, 16 px a field, 21 px a button), and written as
the same widgets: jwin_win_proc, jwin_check_proc, jwin_text_proc, jwin_edit_proc, jwin_droplist_proc,
jwin_button_proc, jwin_list_proc, jwin_frame_proc. The outermost TabPanel becomes a jwin_tab_proc whose
text is its pages' names joined by '|', and every widget on a page carries the page's number (-1:
none); a TabPanel inside a page shows its first page only.
"""
import json
import re
import sys

CHAR_W = 6


def c_str(s):
    if s is None:
        return "NULL"
    out = '"'
    for ch in s:
        o = ord(ch)
        if ch == '\\':
            out += '\\\\'
        elif ch == '"':
            out += '\\"'
        elif ch == '\n':
            out += '\\n'
        elif ch == '\t':
            out += '\\t'
        elif o < 32 or o > 126:
            out += '\\%03o' % (o & 0xFF) if o < 256 else '?'
        else:
            out += ch
    return out + '"'


def num(v):
    return v if isinstance(v, int) and -32767 <= v <= 32767 else -32768


# ---- laying out a builder tree ------------------------------------------------------------------------

def texts(n):
    return [k.get("text") for k in (n.get("kids") or []) if isinstance(k, dict) and k.get("type") == "text"]


def prop_px(p, name, dflt):
    v = (p or {}).get(name)
    if isinstance(v, (int, float)):
        return int(v)
    if isinstance(v, str):
        m = re.match(r"^\s*(\d+)(?:_px)?\s*$", v)
        if m:
            return int(m.group(1))
        m = re.match(r"^\s*(\d+)_em\s*$", v)
        if m:
            return int(m.group(1)) * 8
    return dflt


def tw(s):
    return CHAR_W * len(s or "")


CHECK_HINT = re.compile(r"(CB|FLAG|Flag|CHECK|Check|LENS)")
FIELD_HINT = re.compile(r"(FIELD|Field|ATTRI|INITD|NUM|VAL|DROP|SWATCH)")
INFO = {"INFOBTN", "IBTN", "INFOBTN_EX", "DINFOBTN", "IBTN_T", "INFOBTN_T"}
CONTAINERS = {"Window", "Column", "Row", "Frame", "TabPanel", "TabRef", "ScrollingPane"}


TWO_CELL_MACROS = set()


def find_two_cell_macros(defs):
    def walk(n):
        if not isinstance(n, dict):
            return
        t = n.get("type", "")
        if (t not in CONTAINERS and not t.startswith("Rows<") and not t.startswith("Columns<") and
                t not in ("text", "ref", "expr") and len(texts(n)) >= 2):
            TWO_CELL_MACROS.add(t)
        for k in n.get("kids") or []:
            walk(k)
    for dfn in defs.values():
        if dfn.get("kind") == "GUI":
            walk(dfn.get("root"))
            parts = dfn.get("parts")
            for v in (parts.values() if isinstance(parts, dict) else []):
                walk(v)


class Box:
    """A laid-out piece: its size and its widgets, placed from its own top left."""

    def __init__(self, w=0, h=0, widgets=None):
        self.w, self.h = w, h
        self.widgets = widgets or []     # [type, x, y, w, h, text, page]


def leaf(type_, w, h, text=None):
    return Box(w, h, [[type_, 0, 0, w, h, text, -1]])


def shifted(b, dx, dy, page=None):
    out = []
    for t, x, y, w, h, s, pg in b.widgets:
        out.append([t, x + dx, y + dy, w, h, s, page if page is not None and pg == -1 else pg])
    return out


class Layout:
    def __init__(self, parts):
        self.parts = parts if isinstance(parts, dict) else {}
        self.tabs_done = False

    def kids(self, n):
        out = []
        for k in n.get("kids") or []:
            if not isinstance(k, dict):
                continue
            if k.get("type") == "ref" and k.get("name") in self.parts and isinstance(self.parts[k["name"]], dict):
                out.append(self.parts[k["name"]])
            elif k.get("type") not in ("text", "ref", "expr"):
                out.append(k)
        return out

    # a node's grid cells: a macro that has a help text (anywhere it is used) is an info button and its
    # widget, also where its help text is empty
    def cells(self, n):
        t = n.get("type", "")
        if t in TWO_CELL_MACROS:
            return [("info", n), ("main", n)]
        return [("main", n)]

    def node(self, n, page_ctx=False):
        t = n.get("type", "")
        p = n.get("props") or {}
        tx = texts(n)
        if t == "Window":
            inner = self.column(self.kids(n), 4)
            w, h = max(inner.w + 12, tw(p.get("title")) + 40), inner.h + 24 + 6
            b = Box(w, h, [["jwin_win_proc", 0, 0, w, h, p.get("title"), -1]])
            b.widgets += shifted(inner, 6, 24)
            return b
        if t == "Frame":
            inner = self.column(self.kids(n), 2)
            title = p.get("title")
            top = 12 if title else 4
            w, h = max(inner.w, tw(title)) + 8, inner.h + top + 4
            b = Box(w, h, [["jwin_frame_proc", 0, 0, w, h, None, -1]])
            if title:
                b.widgets.append(["jwin_text_proc", 4, 2, tw(title), 8, title, -1])
            b.widgets += shifted(inner, 4, top)
            return b
        if t == "Row":
            return self.row(self.kids(n), prop_px(p, "spacing", 4))
        if t in ("Column", "TabRef", "ScrollingPane"):
            return self.column(self.kids(n), prop_px(p, "spacing", 2))
        m = re.match(r"^(Rows|Columns)<(\d+)>$", t)
        if m:
            return self.grid(self.kids(n), int(m.group(2)), m.group(1) == "Columns")
        if t == "TabPanel":
            return self.tabs(n)
        if t in INFO:
            return leaf("zq_infobtn", 14, 14)
        if t == "DummyWidget":
            return Box(0, 0)
        if t == "Label":
            s = p.get("text") if isinstance(p.get("text"), str) else (tx[0] if tx else "")
            return leaf("jwin_text_proc", max(tw(s), 4), 9, s)
        if t == "Checkbox" or (CHECK_HINT.search(t) and tx):
            s = p.get("text") if isinstance(p.get("text"), str) else (tx[0] if tx else "")
            return leaf("jwin_check_proc", 13 + tw(s), 12, s)
        if t in ("TextField", "NumberField"):
            return leaf("jwin_edit_proc", prop_px(p, "width", 50), 16)
        if t in ("DropDownList", "DropDownField"):
            return leaf("jwin_droplist_proc", min(prop_px(p, "maxwidth", 140), 200), 16)
        if t == "Button":
            s = p.get("text") if isinstance(p.get("text"), str) else ""
            return leaf("jwin_button_proc", max(61, tw(s) + 16), 21, s)
        if t in ("List", "QRPanel"):
            return leaf("jwin_list_proc", 180, 100)
        if t in ("SelComboSwatch", "SelTileSwatch"):
            return leaf("zq_swatch", 36, 36)
        if tx and FIELD_HINT.search(t):
            s = tx[0]
            lab = ["jwin_text_proc", 0, 4, tw(s), 9, s, -1]
            fld = ["jwin_edit_proc", tw(s) + 4, 0, 50, 16, None, -1]
            return Box(tw(s) + 54, 16, [lab, fld])
        ks = self.kids(n)
        if ks:
            return self.column(ks, 2)
        if tx:
            return leaf("jwin_text_proc", tw(tx[0]), 9, tx[0])
        return Box(0, 0)

    def row(self, ks, sp):
        b = Box()
        x = 0
        for k in ks:
            for kind, n in self.cells(k):
                c = leaf("zq_infobtn", 14, 14) if kind == "info" else self.node(n)
                b.widgets += shifted(c, x, 0)
                x += c.w + sp
                b.h = max(b.h, c.h)
        b.w = max(0, x - sp)
        return b

    def column(self, ks, sp):
        b = Box()
        y = 0
        for k in ks:
            c = self.row([k], 4) if len(self.cells(k)) > 1 else self.node(k)
            b.widgets += shifted(c, 0, y)
            y += c.h + sp
            b.w = max(b.w, c.w)
        b.h = max(0, y - sp)
        return b

    def grid(self, ks, n, column_major):
        cells = []
        for k in ks:
            for kind, nd in self.cells(k):
                cells.append(leaf("zq_infobtn", 14, 14) if kind == "info" else self.node(nd))
        if not cells:
            return Box()
        if column_major:
            rows = n
            cols = (len(cells) + rows - 1) // rows
            pos = [(i % rows, i // rows) for i in range(len(cells))]
        else:
            cols = n
            rows = (len(cells) + cols - 1) // cols
            pos = [(i // cols, i % cols) for i in range(len(cells))]
        cw = [0] * cols
        rh = [0] * rows
        for c, (r, k) in zip(cells, pos):
            cw[k] = max(cw[k], c.w)
            rh[r] = max(rh[r], c.h)
        xs = [sum(cw[:k]) + 4 * k for k in range(cols)]
        ys = [sum(rh[:r]) + 3 * r for r in range(rows)]
        b = Box(xs[-1] + cw[-1], ys[-1] + rh[-1])
        for c, (r, k) in zip(cells, pos):
            b.widgets += shifted(c, xs[k], ys[r] + (rh[r] - c.h) // 2)
        return b

    def tabs(self, n):
        pages = [k for k in self.kids(n) if k.get("type") == "TabRef"] or self.kids(n)
        if self.tabs_done or not pages:
            return self.node(pages[0]) if pages else Box()
        self.tabs_done = True
        names = [((pg.get("props") or {}).get("name") or "Page") for pg in pages]
        boxes = [self.column(self.kids(pg), 2) for pg in pages]
        w = max([b.w for b in boxes] + [sum(tw(s) + 12 for s in names)]) + 8
        h = max(b.h for b in boxes) + 20 + 6
        out = Box(w, h, [["jwin_tab_proc", 0, 0, w, h, "|".join(names), -1]])
        for i, b in enumerate(boxes):
            out.widgets += shifted(b, 4, 20, i)
        return out


def gui_widgets(defn):
    lay = Layout(defn.get("parts"))
    root = defn.get("root")
    if not isinstance(root, dict):
        return []
    b = lay.node(root)
    if root.get("type") != "Window":
        b.widgets.insert(0, ["jwin_win_proc", 0, 0, b.w + 12, b.h + 30, defn.get("title"), -1])
        b.widgets[1:] = shifted(Box(0, 0, b.widgets[1:]), 6, 24)
    return b.widgets


# ---- the table --------------------------------------------------------------------------------------------

# The builder dialogs' laid-out widgets go in only with --gui (not yet in the release: ours are laid
# out from the DIALOG arrays alone).
WITH_GUI = "--gui" in sys.argv


def main():
    args = [a for a in sys.argv[1:] if a != "--gui"]
    src = args[0]
    dst = args[1] if len(args) > 1 else "src/port/zqeditor/zq_ui_dialogs_table.h"
    d = json.load(open(src, encoding="utf-8"))
    defs = d["dialogs"]
    find_two_cell_macros(defs)
    uses = []          # (build, name, hash, category)
    for build, names in d["builds"].items():
        cat_of = {}
        for cat, lst in d.get("main", {}).get(build, {}).items():
            for nm in lst:
                cat_of.setdefault(nm, cat)
        for name, h in names.items():
            if h not in defs:
                continue
            kind = defs[h].get("kind")
            if kind == "DIALOG" and (build == "2.53.1" or name in cat_of):
                uses.append((build, name, h, cat_of.get(name, "")))
            elif kind == "GUI" and name in cat_of and WITH_GUI:
                uses.append((build, name, h, cat_of[name]))
    order = []
    index = {}
    for _, _, h, _ in uses:
        if h not in index:
            index[h] = len(order)
            order.append(h)
    lines = []
    lines.append("// zq_ui_dialogs_table.h: generated by tools/zq_ui_dialogs_gen.py from zquiinv's zq_ui_dialogs.json (each")
    lines.append("// catalog build's ZQuest dialogs, read out of its own source; the 2.55/3.0 builder dialogs laid out")
    lines.append("// by the script). Do not edit; run the script again. Included once, by zq_ui_dialogs.cpp.")
    lines.append("#pragma once")
    lines.append("")
    lines.append("struct ZqUiDlgWidget { const char *type; short x, y, w, h; const char *text; short page; };")
    lines.append("struct ZqUiDlgDef { int first; int count; };")
    lines.append("struct ZqUiDlgUse { const char *build; const char *name; int def; const char *category; };")
    lines.append("")
    lines.append("static const ZqUiDlgWidget zq_ui_dlg_widgets[] =")
    lines.append("{")
    firsts = []
    total = 0
    gui = 0
    for h in order:
        dfn = defs[h]
        if dfn.get("kind") == "GUI":
            ws = [{"type": t, "x": x, "y": y, "w": w, "h": hh, "dp": s, "page": pg} for t, x, y, w, hh, s, pg in gui_widgets(dfn)]
            gui += 1
        else:
            ws = dfn["widgets"]
        firsts.append((total, len(ws)))
        for w in ws:
            text = w.get("dp") if isinstance(w.get("dp"), str) else None
            lines.append("    { %s, %d, %d, %d, %d, %s, %d }," % (c_str(w.get("type", "")), num(w.get("x")), num(w.get("y")),
                                                              num(w.get("w")), num(w.get("h")), c_str(text), w.get("page", -1)))
            total += 1
    if total == 0:
        lines.append("    { NULL, 0, 0, 0, 0, NULL, -1 },")
    lines.append("};")
    lines.append("")
    lines.append("static const ZqUiDlgDef zq_ui_dlg_defs[] =")
    lines.append("{")
    for f, n in firsts:
        lines.append("    { %d, %d }," % (f, n))
    lines.append("};")
    lines.append("")
    lines.append("static const ZqUiDlgUse zq_ui_dlg_uses[] =")
    lines.append("{")
    for b, n, h, cat in uses:
        lines.append("    { %s, %s, %d, %s }," % (c_str(b), c_str(n), index[h], c_str(cat)))
    lines.append("};")
    lines.append("")
    lines.append("static const int zq_ui_dlg_use_count = (int)(sizeof(zq_ui_dlg_uses) / sizeof(zq_ui_dlg_uses[0]));")
    open(dst, "w", newline="\n").write("\n".join(lines) + "\n")
    print("uses %d, definitions %d (builder dialogs %d), widgets %d -> %s" % (len(uses), len(order), gui, total, dst))


main()
