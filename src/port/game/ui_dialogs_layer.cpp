// zplayer dialog host, ImGui side: draws the views the game thread builds
// (ui_dialogs_host.cpp) and records what the user does as events. Runs as a
// zcport UI layer on the platform thread, with the UI lock held; it never
// touches a DIALOG.
//
// Layout. A dialog with a jwin window is one ImGui window titled like it;
// the objects keep their jwin positions and sizes, scaled to the window
// (about 2.4 window pixels per jwin pixel at 96 dpi, less when the window
// is small), with a text size that scales along, so the layout stays the
// one players know. Alerts (jwin_alert) are laid out as message boxes, the
// menu (jwin_menu_proc) is the ImGui main menu bar. Only the newest dialog
// takes input; the ones below it are dimmed.
//
// Written from scratch for zplayer.

#define NOMINMAX

#include "ui_dialogs_internal.h"
#include "zcport/ui.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

using namespace zcui_dlg;

namespace
{

// ---------------------------------------------------------------------------
// frame-wide state (platform thread)
// ---------------------------------------------------------------------------

bool g_popup_prev = false;          // a popup was open at the end of last frame
int g_outside = 0;                  // mouse buttons pressed outside the UI
bool g_dimmed = false;
bool g_canvas_text = false;         // text input was switched on for a K_CUSTOM object

ImU32 col(ImGuiCol c, float alpha_mul = 1.0f)
{
    ImVec4 v = ImGui::GetStyle().Colors[c];
    v.w *= alpha_mul;
    return ImGui::ColorConvertFloat4ToU32(v);
}

float font_scale()
{
    const ImGuiStyle &s = ImGui::GetStyle();
    float f = s.FontScaleMain * s.FontScaleDpi;
    return f > 0.0f ? f : 1.0f;
}

bool any_key_down()
{
    for(int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
    {
        if(k >= ImGuiKey_ReservedForModCtrl && k <= ImGuiKey_ReservedForModSuper)
            continue;
        if(ImGui::IsKeyDown((ImGuiKey)k))
            return true;
    }
    return false;
}

bool any_key_pressed()
{
    for(int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
    {
        if(k >= ImGuiKey_ReservedForModCtrl && k <= ImGuiKey_ReservedForModSuper)
            continue;
        if(ImGui::IsKeyPressed((ImGuiKey)k, false))
            return true;
    }
    return false;
}

// Mouse clicks outside every UI window (with no popup open) are reported to
// the game through gui_mouse_b(); any input moves the activity counter.
void track_mouse(bool dialogs)
{
    ImGuiIO &io = ImGui::GetIO();
    bool over_ui = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
    static const int bits[3] = { 1, 2, 4 };   // left, right, middle as mouse_b
    bool clicked = false;
    for(int b = 0; b < 3; ++b)
    {
        if(ImGui::IsMouseClicked(b))
        {
            clicked = true;
            if(dialogs && !over_ui && !g_popup_prev)
                g_outside |= bits[b];
            else
                g_outside &= ~bits[b];
        }
        if(!ImGui::IsMouseDown(b))
            g_outside &= ~bits[b];
    }
    if(!dialogs)
        g_outside = 0;
    set_outside_buttons(g_outside);

    if(dialogs && (clicked || io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || io.MouseWheel != 0.0f ||
                   io.InputQueueCharacters.Size > 0 || any_key_pressed()))
        add_activity();
}

// ---------------------------------------------------------------------------
// events and values
// ---------------------------------------------------------------------------

LayerObject &object_state(HostDialog &hd, int obj)
{
    if(obj >= (int)hd.objects.size())
        hd.objects.resize(obj + 1);
    return hd.objects[obj];
}

unsigned int push_event(HostDialog &hd, Event e)
{
    e.serial = hd.next_serial++;
    hd.events.push_back(e);
    zcui::request_redraw();
    return e.serial;
}

// The value to show: the user's own change until the game has applied it.
int shown_value(HostDialog &hd, const WidgetView &w)
{
    LayerObject &o = object_state(hd, w.obj);
    if(o.pending_serial > hd.view.applied_serial)
        return o.pending_value;
    return w.value;
}

void change_value(HostDialog &hd, const WidgetView &w, Event::Type type, int value)
{
    Event e;
    e.type = type;
    e.obj = w.obj;
    e.value = value;
    LayerObject &o = object_state(hd, w.obj);
    o.pending_serial = push_event(hd, e);
    o.pending_value = value;
}

void click(HostDialog &hd, int obj)
{
    Event e;
    e.type = Event::CLICK;
    e.obj = obj;
    push_event(hd, e);
}

void key_event(HostDialog &hd, int ascii, int scancode)
{
    Event e;
    e.type = Event::KEY;
    e.obj = hd.focused_obj >= 0 ? hd.focused_obj : hd.view.canvas_focus;
    e.ascii = ascii;
    e.scancode = scancode;
    push_event(hd, e);
}

std::string to_latin1(const char *s)
{
    std::string out;
    for(const unsigned char *p = (const unsigned char *)s; *p; ++p)
    {
        if(*p < 0x80)
            out += (char)*p;
        else if((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80)
        {
            out += (char)(((*p & 0x1F) << 6) | (p[1] & 0x3F));
            ++p;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// keys for the dialog
// ---------------------------------------------------------------------------

int scancode_of_char(int c)
{
    if(c >= 'a' && c <= 'z')
        return KEY_A + (c - 'a');
    if(c >= 'A' && c <= 'Z')
        return KEY_A + (c - 'A');
    if(c >= '0' && c <= '9')
        return KEY_0 + (c - '0');
    if(c == ' ')
        return KEY_SPACE;
    return 0;
}

// The keys the dialog itself handles in jwin go to the game thread, which
// runs them through the dialog manager's order (keyboard shortcuts,
// d_keyboard_proc, Alt+letter for the menu, Escape). ImGui keeps what it
// does itself: moving the focus, pressing the focused widget, editing text.
void forward_keys(HostDialog &hd)
{
    ImGuiIO &io = ImGui::GetIO();
    bool text = io.WantTextInput;

    // a self-drawn object with the focus (ZQuest's script editor) takes the
    // editing keys, with key repeat
    if(hd.view.canvas_focus >= 0 && hd.focused_obj < 0 && !text)
    {
        static const struct { ImGuiKey key; int ascii; int scancode; } edit_keys[] =
        {
            { ImGuiKey_LeftArrow, 0, KEY_LEFT }, { ImGuiKey_RightArrow, 0, KEY_RIGHT },
            { ImGuiKey_UpArrow, 0, KEY_UP }, { ImGuiKey_DownArrow, 0, KEY_DOWN },
            { ImGuiKey_Home, 0, KEY_HOME }, { ImGuiKey_End, 0, KEY_END },
            { ImGuiKey_PageUp, 0, KEY_PGUP }, { ImGuiKey_PageDown, 0, KEY_PGDN },
            { ImGuiKey_Backspace, 8, KEY_BACKSPACE }, { ImGuiKey_Delete, 127, KEY_DEL }
        };
        for(size_t i = 0; i < sizeof(edit_keys) / sizeof(edit_keys[0]); ++i)
            if(ImGui::IsKeyPressed(edit_keys[i].key, true))
                key_event(hd, edit_keys[i].ascii, edit_keys[i].scancode);
    }

    for(int i = 0; i < 12; ++i)
        if(ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_F1 + i), false))
            key_event(hd, 0, KEY_F1 + i);

    if(ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !g_popup_prev)
        key_event(hd, 27, KEY_ESC);
    if(ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !g_popup_prev)
        key_event(hd, 13, KEY_ENTER);
    if(ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) && !g_popup_prev)
        key_event(hd, 13, KEY_ENTER_PAD);
    if(ImGui::IsKeyPressed(ImGuiKey_Tab, false))
        key_event(hd, 9, KEY_TAB);

    if(io.KeyAlt || io.KeyCtrl)
    {
        // Alt+letter: no character (Allegro reports 0); Ctrl+letter: 1..26
        for(int i = 0; i < 26; ++i)
            if(ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_A + i), false))
                key_event(hd, (io.KeyCtrl && !io.KeyAlt) ? i + 1 : 0, KEY_A + i);
        for(int i = 0; i < 10; ++i)
            if(ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_0 + i), false))
                key_event(hd, 0, KEY_0 + i);
        return;
    }

    // typed characters, unless an edit or a list takes them
    if(text || hd.list_focused)
        return;
    for(int i = 0; i < io.InputQueueCharacters.Size; ++i)
    {
        ImWchar c = io.InputQueueCharacters[i];
        if(c >= 32 && c < 127)
            key_event(hd, c, scancode_of_char(c));
    }
}

// ---------------------------------------------------------------------------
// bitmaps (d_bitmap_proc): ImGui user textures
// ---------------------------------------------------------------------------

struct Texture
{
    ImTextureData *tex;
    int owner_id, owner_obj;
    unsigned int hash;
    int last_frame;
};

std::vector<Texture> g_textures;

ImTextureData *texture_for(int dialog_id, int obj, const Image &img)
{
    int frame = ImGui::GetFrameCount();
    Texture *slot = NULL;
    for(size_t i = 0; i < g_textures.size() && !slot; ++i)
    {
        Texture &t = g_textures[i];
        if(t.owner_id == dialog_id && t.owner_obj == obj && t.tex->Width == img.w && t.tex->Height == img.h)
            slot = &t;
    }
    for(size_t i = 0; i < g_textures.size() && !slot; ++i)
    {
        Texture &t = g_textures[i];
        if(t.last_frame < frame - 1 && t.tex->Width == img.w && t.tex->Height == img.h)
        {
            slot = &t;
            slot->hash = ~img.hash;
        }
    }
    if(!slot)
    {
        if(g_textures.size() >= 8)
            return NULL;
        Texture t;
        t.tex = IM_NEW(ImTextureData)();
        t.tex->Create(ImTextureFormat_RGBA32, img.w, img.h);
        ImGui::RegisterUserTexture(t.tex);
        t.hash = ~img.hash;
        g_textures.push_back(t);
        slot = &g_textures.back();
    }
    slot->owner_id = dialog_id;
    slot->owner_obj = obj;
    slot->last_frame = frame;
    if(slot->hash != img.hash)
    {
        memcpy(slot->tex->GetPixels(), &img.rgba[0], img.rgba.size() * 4);
        if(slot->tex->Status == ImTextureStatus_OK || slot->tex->Status == ImTextureStatus_WantUpdates)
            ImTextureDataQueueUpload(slot->tex, 0, 0, img.w, img.h);
        slot->hash = img.hash;
    }
    return slot->tex;
}

// ---------------------------------------------------------------------------
// A label made only of the old font's symbols (arrows, a square): their byte
// values, or nothing when the label is ordinary text.
std::vector<unsigned char> old_symbols(const std::string &utf8)
{
    std::vector<unsigned char> out;
    if(utf8.empty() || (utf8.size() & 1))
        return out;
    for(size_t i = 0; i < utf8.size(); i += 2)
    {
        unsigned char a = (unsigned char)utf8[i], b = (unsigned char)utf8[i + 1];
        if(a != 0xC2 || !((b >= 0x88 && b <= 0x8B) || b == 0x8D || (b >= 0x98 && b <= 0x9B)))
        {
            out.clear();
            return out;
        }
        out.push_back(b);
    }
    return out;
}

// Draws those symbols centered in a button.
void draw_old_symbols(const std::vector<unsigned char> &sym, ImVec2 a, ImVec2 b)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImU32 c = col(ImGuiCol_Text);
    float n = (float)sym.size();
    float g = std::min(ImGui::GetFontSize() * 0.7f, std::min(b.y - a.y, (b.x - a.x) / n) * 0.62f);
    float step = g * 1.2f;
    float r = g * 0.5f;
    float x = (a.x + b.x) * 0.5f - step * (n - 1) * 0.5f;
    float y = (a.y + b.y) * 0.5f;
    float t = std::max(1.0f, g * 0.16f);
    for(size_t i = 0; i < sym.size(); ++i, x += step)
    {
        bool bar = (sym[i] & 0xF0) == 0x90;     // an arrow against a line
        switch(sym[i] & 0x0F)
        {
        case 0x8:
            dl->AddTriangleFilled(ImVec2(x, y - r), ImVec2(x + r, y + r), ImVec2(x - r, y + r), c);
            if(bar) dl->AddLine(ImVec2(x - r, y - r - t), ImVec2(x + r, y - r - t), c, t);
            break;
        case 0x9:
            dl->AddTriangleFilled(ImVec2(x - r, y - r), ImVec2(x + r, y - r), ImVec2(x, y + r), c);
            if(bar) dl->AddLine(ImVec2(x - r, y + r + t), ImVec2(x + r, y + r + t), c, t);
            break;
        case 0xA:
            dl->AddTriangleFilled(ImVec2(x - r, y), ImVec2(x + r, y - r), ImVec2(x + r, y + r), c);
            if(bar) dl->AddLine(ImVec2(x - r - t, y - r), ImVec2(x - r - t, y + r), c, t);
            break;
        case 0xB:
            dl->AddTriangleFilled(ImVec2(x + r, y), ImVec2(x - r, y + r), ImVec2(x - r, y - r), c);
            if(bar) dl->AddLine(ImVec2(x + r + t, y - r), ImVec2(x + r + t, y + r), c, t);
            break;
        default:
            dl->AddRectFilled(ImVec2(x - r * 0.85f, y - r * 0.85f), ImVec2(x + r * 0.85f, y + r * 0.85f), c);
            break;
        }
    }
}

// the pictures of self-drawn objects (K_CUSTOM)
// ---------------------------------------------------------------------------

struct CanvasTexture
{
    ImTextureData *tex;
    HostDialog *owner;
};

std::vector<CanvasTexture> g_canvas_textures;

ImTextureData *canvas_texture(HostDialog &hd)
{
    DialogView &v = hd.view;
    if(v.canvas_w <= 0 || v.canvas_h <= 0 || (int)hd.canvas.size() != v.canvas_w * v.canvas_h)
        return NULL;

    CanvasTexture *slot = NULL;
    for(size_t i = 0; i < g_canvas_textures.size() && !slot; ++i)
        if(g_canvas_textures[i].owner == &hd)
            slot = &g_canvas_textures[i];

    if(slot && (slot->tex->Width != v.canvas_w || slot->tex->Height != v.canvas_h))
    {
        slot->owner = NULL;
        slot = NULL;
    }

    if(!slot)
    {
        for(size_t i = 0; i < g_canvas_textures.size() && !slot; ++i)
        {
            CanvasTexture &t = g_canvas_textures[i];
            if(!t.owner && t.tex->Width == v.canvas_w && t.tex->Height == v.canvas_h)
                slot = &t;
        }
        if(!slot)
        {
            if(g_canvas_textures.size() >= 12)
                return NULL;
            CanvasTexture t;
            t.tex = IM_NEW(ImTextureData)();
            t.tex->Create(ImTextureFormat_RGBA32, v.canvas_w, v.canvas_h);
            ImGui::RegisterUserTexture(t.tex);
            t.owner = NULL;
            g_canvas_textures.push_back(t);
            slot = &g_canvas_textures.back();
        }
        slot->owner = &hd;
        hd.dirty_x0 = 0;
        hd.dirty_y0 = 0;
        hd.dirty_x1 = v.canvas_w;
        hd.dirty_y1 = v.canvas_h;
    }

    if(hd.dirty_x1 > hd.dirty_x0 && hd.dirty_y1 > hd.dirty_y0)
    {
        ImTextureData *tex = slot->tex;
        int w = hd.dirty_x1 - hd.dirty_x0, h = hd.dirty_y1 - hd.dirty_y0;
        unsigned int *to = (unsigned int *)tex->GetPixels();
        for(int y = 0; y < h; ++y)
            memcpy(to + (size_t)(hd.dirty_y0 + y) * v.canvas_w + hd.dirty_x0,
                   &hd.canvas[(size_t)(hd.dirty_y0 + y) * v.canvas_w + hd.dirty_x0], (size_t)w * 4);
        if(tex->Status == ImTextureStatus_OK || tex->Status == ImTextureStatus_WantUpdates)
            ImTextureDataQueueUpload(tex, hd.dirty_x0, hd.dirty_y0, w, h);
        hd.dirty_x0 = hd.dirty_x1 = 0;
    }
    return slot->tex;
}

// ---------------------------------------------------------------------------
// the objects of a dialog window
// ---------------------------------------------------------------------------

struct Place
{
    ImVec2 origin;      // screen position of the dialog area's corner
    float s;            // window pixels per jwin pixel
    float font;         // text size in pixels
    int box_x, box_y;
    ImVec2 pos(int x, int y) const { return ImVec2(origin.x + (x - box_x) * s, origin.y + (y - box_y) * s); }
};

void split_lines(const std::string &s, std::vector<std::string> &lines)
{
    lines.clear();
    size_t start = 0;
    for(;;)
    {
        size_t nl = s.find('\n', start);
        if(nl == std::string::npos)
        {
            lines.push_back(s.substr(start));
            return;
        }
        lines.push_back(s.substr(start, nl - start));
        start = nl + 1;
    }
}

int edit_filter(ImGuiInputTextCallbackData *data)
{
    int kind = (int)(intptr_t)data->UserData;
    ImWchar c = data->EventChar;
    if(c < 32 || c > 126)
        return 1;
    // jwin_numedit_proc takes no letters, jwin_hexedit_proc no punctuation
    // and no letters past F
    if(kind == K_NUMEDIT && isalpha((int)c))
        return 1;
    if(kind == K_HEXEDIT && ((isalpha((int)c) && !isxdigit((int)c)) || ispunct((int)c)))
        return 1;
    return 0;
}

void draw_text(const WidgetView &w, const Place &pl)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    std::vector<std::string> lines;
    split_lines(w.text, lines);
    ImVec2 p = pl.pos(w.x, w.y);
    float line_h = std::max(pl.font, 8.0f * pl.s);
    bool disabled = (w.flags & D_DISABLED) != 0;
    for(size_t i = 0; i < lines.size(); ++i)
    {
        if(lines[i].empty())
            continue;
        ImVec2 size = ImGui::CalcTextSize(lines[i].c_str());
        float x = p.x;
        if(w.kind == K_CTEXT)
            x -= size.x * 0.5f;
        else if(w.kind == K_RTEXT)
            x -= size.x;
        float y = p.y + i * line_h;
        // jwin texts cut the frame lines they sit on (group box labels)
        float m = 3.0f * zcui::scale();
        dl->AddRectFilled(ImVec2(x - m, y), ImVec2(x + size.x + m, y + size.y), col(ImGuiCol_WindowBg) | IM_COL32_A_MASK);
        dl->AddText(ImVec2(x, y), col(disabled ? ImGuiCol_TextDisabled : ImGuiCol_Text), lines[i].c_str());
    }
}

void draw_frame(const WidgetView &w, const Place &pl)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 a = pl.pos(w.x, w.y);
    ImVec2 b(a.x + w.w * pl.s, a.y + w.h * pl.s);
    float r = 5.0f * zcui::scale();
    // FR_DEEP / FR_INV: a sunken field; the others: a group box
    if(w.d1 == JW_FR_DEEP || w.d1 == JW_FR_INV)
        dl->AddRectFilled(a, b, col(ImGuiCol_FrameBg), r);
    dl->AddRect(a, b, col(ImGuiCol_Border), r, 0, 1.0f * zcui::scale());
}

void frame_padding_for(float height_px, float font_px)
{
    float pad_y = std::max(0.0f, (height_px - font_px) * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, pad_y));
}

void draw_widget(HostDialog &hd, const WidgetView &w, const Place &pl, bool input)
{
    DialogView &v = hd.view;
    bool first = (hd.frames == 0);
    ImVec2 p = pl.pos(w.x, w.y);
    ImVec2 size(std::max(1.0f, w.w * pl.s), std::max(1.0f, w.h * pl.s));
    char id[32];
    LayerObject &o = object_state(hd, w.obj);

    ImGui::PushID(w.obj);
    bool disabled = (w.flags & D_DISABLED) != 0;
    if(disabled)
        ImGui::BeginDisabled();

    const float pane_bar = 12.0f * zcui::scale();
    const bool clipped = w.pane >= 0 && w.pane < (int)v.widgets.size();
    if(clipped)
    {
        const WidgetView &pn = v.widgets[w.pane];
        ImVec2 a = pl.pos(pn.x, pn.y);
        ImVec2 b(a.x + pn.w * pl.s - (pn.limit > 0 ? pane_bar : 0.0f), a.y + pn.h * pl.s - (pn.d2 > 0 ? pane_bar : 0.0f));
        ImGui::PushClipRect(a, b, true);
    }

    switch(w.kind)
    {
    case K_PANE:
    {
        int y = w.value, x = w.d1;
        bool moved = false;
        ImGuiIO &io = ImGui::GetIO();
        ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), col(ImGuiCol_Border), 5.0f * zcui::scale(), 0, 1.0f * zcui::scale());
        if(input && io.MouseWheel != 0.0f && ImGui::IsMouseHoveringRect(p, ImVec2(p.x + size.x, p.y + size.y)) &&
           !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        {
            int step = (int)(io.MouseWheel * 24.0f);
            if(w.limit > 0)
                y -= step;
            else
                x -= step;
            moved = true;
        }
        if(w.limit > 0)
        {
            ImGui::SetCursorScreenPos(ImVec2(p.x + size.x - pane_bar, p.y));
            // the top of the bar is the start of the content
            moved |= ImGui::VSliderInt("##panev", ImVec2(pane_bar, size.y - (w.d2 > 0 ? pane_bar : 0.0f)), &y, w.limit, 0, "");
        }
        if(w.d2 > 0)
        {
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + size.y - pane_bar));
            ImGui::SetNextItemWidth(size.x - (w.limit > 0 ? pane_bar : 0.0f));
            moved |= ImGui::SliderInt("##paneh", &x, 0, w.d2, "");
        }
        if(moved)
        {
            Event e;
            e.type = Event::SET;
            e.obj = w.obj;
            e.value = std::min(std::max(y, 0), w.limit);
            e.value2 = std::min(std::max(x, 0), w.d2);
            push_event(hd, e);
        }
        break;
    }

    case K_FRAME:
        draw_frame(w, pl);
        break;

    case K_TEXT:
    case K_CTEXT:
    case K_RTEXT:
        if(w.text == "\xC2\x81")     // the old font's check mark
            ImGui::RenderCheckMark(ImGui::GetWindowDrawList(), p, col(ImGuiCol_Text), ImGui::GetFontSize());
        else
            draw_text(w, pl);
        break;

    case K_HLINE:
    case K_VLINE:
    {
        ImVec2 b = (w.kind == K_HLINE) ? ImVec2(p.x + size.x, p.y) : ImVec2(p.x, p.y + size.y);
        ImGui::GetWindowDrawList()->AddLine(p, b, col(ImGuiCol_Separator), 1.0f * zcui::scale());
        break;
    }

    case K_BUTTON:
    {
        bool toggled = (w.flags & D_SELECTED) != 0;
        if(toggled)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        ImGui::SetCursorScreenPos(p);
        snprintf(id, sizeof(id), "##b%d", w.obj);
        std::string label = w.text + id;
        std::vector<unsigned char> symbols = old_symbols(w.text);
        bool pressed = ImGui::Button(symbols.empty() ? label.c_str() : id, size);
        if(!symbols.empty())
            draw_old_symbols(symbols, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        if(pressed && input)
            click(hd, w.obj);
        if(toggled)
            ImGui::PopStyleColor();
        if(first && w.obj == v.focus_obj)
            ImGui::SetItemDefaultFocus();
        break;
    }

    case K_CHECK:
    case K_RADIO:
    {
        bool on = shown_value(hd, w) != 0;
        frame_padding_for(w.h * pl.s, pl.font);
        float x = p.x;
        if(w.kind == K_CHECK && w.d1 == 0 && !w.text.empty())
        {
            // text first, then the box
            ImGui::GetWindowDrawList()->AddText(ImVec2(x, p.y + std::max(0.0f, (w.h * pl.s - pl.font) * 0.5f)),
                                                col(disabled ? ImGuiCol_TextDisabled : ImGuiCol_Text), w.text.c_str());
            x += ImGui::CalcTextSize(w.text.c_str()).x + ImGui::GetStyle().ItemInnerSpacing.x;
        }
        ImGui::SetCursorScreenPos(ImVec2(x, p.y));
        snprintf(id, sizeof(id), "##c%d", w.obj);
        std::string label = ((w.kind == K_CHECK && w.d1 == 0) ? std::string() : w.text) + id;
        bool pressed;
        if(w.kind == K_CHECK)
        {
            bool value = on;
            pressed = ImGui::Checkbox(label.c_str(), &value);
            if(pressed && input)
            {
                Event e;
                e.type = Event::CLICK;
                e.obj = w.obj;
                o.pending_serial = push_event(hd, e);
                o.pending_value = (w.flags & D_EXIT) ? on : value;
            }
        }
        else
        {
            pressed = ImGui::RadioButton(label.c_str(), on);
            if(pressed && input && !on)
            {
                Event e;
                e.type = Event::CLICK;
                e.obj = w.obj;
                o.pending_serial = push_event(hd, e);
                o.pending_value = 1;
            }
        }
        ImGui::PopStyleVar();
        if(first && w.obj == v.focus_obj)
            ImGui::SetItemDefaultFocus();
        break;
    }

    case K_EDIT:
    case K_NUMEDIT:
    case K_HEXEDIT:
    {
        int max = std::max(0, w.limit);
        if((int)o.edit.size() != max + 1)
        {
            o.edit.assign(max + 1, 0);
            o.edit_active = false;
        }
        if(!o.edit_active && o.pending_serial <= v.applied_serial)
        {
            size_t n = std::min(w.text.size(), (size_t)max);
            memcpy(&o.edit[0], w.text.data(), n);
            o.edit[n] = 0;
        }
        frame_padding_for(w.h * pl.s, pl.font);
        ImGui::SetCursorScreenPos(p);
        ImGui::SetNextItemWidth(size.x);
        if(first && w.obj == v.focus_obj && input)
            ImGui::SetKeyboardFocusHere();
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackCharFilter;
        if(w.flags & JW_READONLY)
            flags |= ImGuiInputTextFlags_ReadOnly;
        snprintf(id, sizeof(id), "##e%d", w.obj);
        bool changed = ImGui::InputText(id, &o.edit[0], o.edit.size(), flags, edit_filter, (void *)(intptr_t)w.kind);
        o.edit_active = ImGui::IsItemActive();
        ImGui::PopStyleVar();
        if(changed && input)
        {
            Event e;
            e.type = Event::TEXT;
            e.obj = w.obj;
            e.text = to_latin1(&o.edit[0]);
            o.pending_serial = push_event(hd, e);
        }
        break;
    }

    case K_DROPLIST:
    {
        int sel = shown_value(hd, w);
        const char *preview = (sel >= 0 && sel < (int)w.items.size()) ? w.items[sel].c_str() : "";
        frame_padding_for(w.h * pl.s, pl.font);
        ImGui::SetCursorScreenPos(p);
        ImGui::SetNextItemWidth(size.x);
        snprintf(id, sizeof(id), "##d%d", w.obj);
        if(ImGui::BeginCombo(id, preview, ImGuiComboFlags_HeightLarge))
        {
            for(int i = 0; i < (int)w.items.size(); ++i)
            {
                ImGui::PushID(i);
                bool is_sel = (i == sel);
                if(ImGui::Selectable(w.items[i].c_str(), is_sel) && input && i != sel)
                    change_value(hd, w, Event::SET, i);
                if(is_sel)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar();
        if(first && w.obj == v.focus_obj)
            ImGui::SetItemDefaultFocus();
        break;
    }

    case K_LIST:
    case K_ABCLIST:
    {
        int sel = shown_value(hd, w);
        ImGui::SetCursorScreenPos(p);
        snprintf(id, sizeof(id), "##l%d", w.obj);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f * zcui::scale()));
        if(ImGui::BeginListBox(id, size))
        {
            if(ImGui::IsWindowFocused())
                hd.list_focused = true;
            // Allegro's abc list jumps to the first item with the letter typed
            if(w.kind == K_ABCLIST && input && ImGui::IsWindowFocused())
            {
                ImGuiIO &io = ImGui::GetIO();
                for(int c = 0; c < io.InputQueueCharacters.Size; ++c)
                {
                    int ch = toupper((int)io.InputQueueCharacters[c]);
                    if(!isalnum(ch))
                        continue;
                    for(int i = 0; i < (int)w.items.size(); ++i)
                    {
                        if(!w.items[i].empty() && toupper((unsigned char)w.items[i][0]) == ch)
                        {
                            if(i != sel)
                            {
                                change_value(hd, w, Event::SET, i);
                                sel = i;
                            }
                            break;
                        }
                    }
                }
            }
            for(int i = 0; i < (int)w.items.size(); ++i)
            {
                ImGui::PushID(i);
                bool is_sel = (i == sel);
                if(ImGui::Selectable(w.items[i].c_str(), is_sel, ImGuiSelectableFlags_AllowDoubleClick) && input)
                {
                    if(i != sel)
                    {
                        change_value(hd, w, Event::SET, i);
                        sel = i;                // the row clicked stays where it is
                        o.list_shown = i;
                    }
                    if(ImGui::IsMouseDoubleClicked(0))
                    {
                        Event e;
                        e.type = Event::DCLICK;
                        e.obj = w.obj;
                        push_event(hd, e);
                    }
                }
                if(is_sel && o.list_shown != sel)
                    ImGui::SetScrollHereY(0.5f);
                ImGui::PopID();
            }
            o.list_shown = sel;
            ImGui::EndListBox();
        }
        ImGui::PopStyleVar();
        break;
    }

    case K_TEXTBOX:
    {
        ImGui::SetCursorScreenPos(p);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f * zcui::scale(), 3.0f * zcui::scale()));
        snprintf(id, sizeof(id), "##t%d", w.obj);
        if(ImGui::BeginChild(id, size, ImGuiChildFlags_Borders))
        {
            if(w.wrap)
                ImGui::TextWrapped("%s", w.text.c_str());
            else
                ImGui::TextUnformatted(w.text.c_str());
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        break;
    }

    case K_SLIDER:
    {
        int value = shown_value(hd, w);
        int max = std::max(1, w.limit);
        ImGui::SetCursorScreenPos(p);
        snprintf(id, sizeof(id), "##s%d", w.obj);
        bool changed;
        if(w.h > w.w)
        {
            changed = ImGui::VSliderInt(id, size, &value, 0, max, "");
        }
        else
        {
            frame_padding_for(size.y, pl.font);
            ImGui::SetNextItemWidth(size.x);
            changed = ImGui::SliderInt(id, &value, 0, max, "");
            ImGui::PopStyleVar();
        }
        if(changed && input)
            change_value(hd, w, Event::SET, value);
        if(first && w.obj == v.focus_obj)
            ImGui::SetItemDefaultFocus();
        break;
    }

    case K_TAB:
    {
        int desired = shown_value(hd, w);
        float tab_h = 16.0f * pl.s;
        ImGui::SetCursorScreenPos(p);
        frame_padding_for(tab_h, pl.font);
        snprintf(id, sizeof(id), "##tab%d", w.obj);
        int now = -1;
        if(ImGui::BeginChild(id, ImVec2(size.x, tab_h + 2.0f * zcui::scale()), ImGuiChildFlags_None,
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground))
        {
            if(ImGui::BeginTabBar("##bar", ImGuiTabBarFlags_FittingPolicyScroll))
            {
                for(int i = 0; i < (int)w.items.size(); ++i)
                {
                    ImGuiTabItemFlags f = (i == desired && o.tab_shown != desired) ? ImGuiTabItemFlags_SetSelected : 0;
                    std::string label = w.items[i] + "##" + std::to_string(i);
                    if(ImGui::BeginTabItem(label.c_str(), NULL, f))
                    {
                        now = i;
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        if(now >= 0 && now != desired && o.tab_shown == desired && input)
            change_value(hd, w, Event::TAB, now);
        o.tab_shown = now;
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRect(ImVec2(p.x, p.y + tab_h), ImVec2(p.x + size.x, p.y + size.y), col(ImGuiCol_Border),
                    5.0f * zcui::scale(), 0, 1.0f * zcui::scale());
        break;
    }

    case K_CUSTOM:      // shown by draw_canvas(); presses by press_canvases()
        break;

    case K_BITMAP:
        if(w.image >= 0 && w.image < (int)v.images.size())
        {
            const Image &img = v.images[w.image];
            ImTextureData *tex = texture_for(hd.id, w.obj, img);
            if(tex)
            {
                ImGui::SetCursorScreenPos(p);
                ImGui::Image(tex->GetTexRef(), size);
            }
        }
        break;
    }

    if(ImGui::IsItemFocused() && w.kind != K_TEXT && w.kind != K_CTEXT && w.kind != K_RTEXT &&
       w.kind != K_FRAME && w.kind != K_HLINE && w.kind != K_VLINE)
        hd.focused_obj = w.obj;

    if(clipped)
        ImGui::PopClipRect();
    if(disabled)
        ImGui::EndDisabled();
    if(!w.tooltip.empty() && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(p, ImVec2(p.x + size.x, p.y + size.y)))
        ImGui::SetTooltip("%s", w.tooltip.c_str());
    ImGui::PopID();
}

// ---------------------------------------------------------------------------
// windows
// ---------------------------------------------------------------------------

inline bool is_backdrop(int kind)
{
    return kind == K_FRAME || kind == K_HLINE || kind == K_VLINE;
}

// Everything the dialog's procedures drew, behind its widgets.
void draw_canvas(HostDialog &hd, const Place &pl)
{
    DialogView &v = hd.view;
    ImTextureData *tex = canvas_texture(hd);
    if(!tex)
        return;
    int x0 = std::max(v.box_x, 0), y0 = std::max(v.box_y, 0);
    int x1 = std::min(v.box_x + v.box_w, v.canvas_w / CANVAS_SCALE);
    int y1 = std::min(v.box_y + v.box_h, v.canvas_h / CANVAS_SCALE);
    if(x1 <= x0 || y1 <= y0)
        return;
    float tw = (float)tex->Width, th = (float)tex->Height;
    ImVec2 uv0(x0 * (float)CANVAS_SCALE / tw, y0 * (float)CANVAS_SCALE / th);
    ImVec2 uv1(x1 * (float)CANVAS_SCALE / tw, y1 * (float)CANVAS_SCALE / th);
    ImGui::GetWindowDrawList()->AddImage(tex->GetTexRef(), pl.pos(x0, y0), pl.pos(x1, y1), uv0, uv1);
}

// A press goes to the last object under the mouse, as in the dialog manager;
// the UI's own widgets come first.
void press_canvases(HostDialog &hd, const Place &pl)
{
    if(ImGui::IsAnyItemHovered() || !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        return;

    bool dbl = false, press = false;
    int buttons = 0;
    static const int bits[3] = { 1, 2, 4 };
    for(int b = 0; b < 3; ++b)
    {
        if(ImGui::IsMouseDoubleClicked(b))
            dbl = true;
        else if(ImGui::IsMouseClicked(b))
            press = true;
        else
            continue;
        buttons |= bits[b];
    }
    if(!dbl && !press)
        return;

    ImVec2 m = ImGui::GetMousePos();
    DialogView &v = hd.view;
    for(int i = (int)v.widgets.size() - 1; i >= 0; --i)
    {
        const WidgetView &w = v.widgets[i];
        if(w.kind != K_CUSTOM || (w.flags & D_DISABLED))
            continue;
        ImVec2 a = pl.pos(w.x, w.y);
        if(m.x < a.x || m.y < a.y || m.x >= a.x + w.w * pl.s || m.y >= a.y + w.h * pl.s)
            continue;
        Event e;
        e.type = dbl ? Event::DCLICK : Event::CANVAS;
        e.obj = w.obj;
        e.value = buttons;
        push_event(hd, e);
        return;
    }
}

void report_dialog_mouse(const Place &pl)
{
    ImGuiIO &io = ImGui::GetIO();
    int x = pl.box_x + (int)floorf((io.MousePos.x - pl.origin.x) / pl.s);
    int y = pl.box_y + (int)floorf((io.MousePos.y - pl.origin.y) / pl.s);
    int b = (io.MouseDown[0] ? 1 : 0) | (io.MouseDown[1] ? 2 : 0) | (io.MouseDown[2] ? 4 : 0);
    set_dialog_mouse(x, y, b);
}

std::string window_id(const HostDialog &hd, const std::string &title)
{
    return (title.empty() ? std::string(" ") : title) + "###zcdlg" + std::to_string(hd.id);
}

// The dialog's look while it does not take input: below another dialog
// (dimmed), or for a moment after a prompt (looks the same).
void begin_inactive(bool below, bool frozen)
{
    if(below)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 0.55f);
        ImGui::BeginDisabled();
    }
    else if(frozen)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
        ImGui::BeginDisabled();
    }
}

void end_inactive(bool below, bool frozen)
{
    if(below || frozen)
    {
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
    }
}

void draw_window(HostDialog &hd, bool top, bool input, bool frozen)
{
    DialogView &v = hd.view;
    ImGuiViewport *vp = ImGui::GetMainViewport();
    const ImGuiStyle &style = ImGui::GetStyle();
    float dpi = zcui::scale();

    bool titled = v.has_window && !v.title.empty();
    float title_h = titled ? ImGui::GetFontSize() + style.FramePadding.y * 2.0f : 0.0f;
    float pad = 12.0f * dpi;
    float margin = 24.0f * dpi;
    float bw = (float)std::max(1, v.box_w), bh = (float)std::max(1, v.box_h);
    // the editor's dialogs are laid out larger than the player's
    float s = std::min((v.text_scale > 1.0f ? 1.85f : 2.4f) * dpi, std::min((vp->WorkSize.x - 2 * margin - 2 * pad) / bw,
                                           (vp->WorkSize.y - 2 * margin - 2 * pad - title_h) / bh));
    if(s < 0.6f)
        s = 0.6f;

    if(top && !g_dimmed)
    {
        ImGui::GetBackgroundDrawList()->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                                                      IM_COL32(0, 0, 0, 96));
        g_dimmed = true;
    }

    ImGui::SetNextWindowPos(vp->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(bw * s + 2 * pad, bh * s + 2 * pad + title_h), ImGuiCond_Always);
    if(top && (hd.frames == 0 || !hd.was_top))
        ImGui::SetNextWindowFocus();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse;
    if(!titled)
        flags |= ImGuiWindowFlags_NoTitleBar;
    if(!top)
        flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;

    bool open = true;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    std::string wid = window_id(hd, v.title);
    bool shown = ImGui::Begin(wid.c_str(), (v.closable && input) ? &open : NULL, flags);
    ImGui::PopStyleVar();
    if(shown)
    {
        Place pl;
        pl.origin = ImGui::GetCursorScreenPos();
        pl.s = s;
        pl.font = 8.6f * s * v.text_scale;
        if(v.text_scale > 1.0f)
            pl.font = std::min(pl.font, 16.5f * dpi);
        pl.box_x = v.box_x;
        pl.box_y = v.box_y;
        ImGui::PushFont(NULL, pl.font / font_scale());
        begin_inactive(!top, top && frozen);
        hd.focused_obj = -1;
        hd.list_focused = false;
        // frames and lines first, then what the procedures drew, then the controls
        for(size_t i = 0; i < v.widgets.size(); ++i)
            if(is_backdrop(v.widgets[i].kind))
                draw_widget(hd, v.widgets[i], pl, input);
        draw_canvas(hd, pl);
        for(size_t i = 0; i < v.widgets.size(); ++i)
            if(!is_backdrop(v.widgets[i].kind))
                draw_widget(hd, v.widgets[i], pl, input);
        if(top && input)
            press_canvases(hd, pl);
        if(top)
            report_dialog_mouse(pl);
        end_inactive(!top, top && frozen);
        ImGui::PopFont();
    }
    ImGui::End();

    if(!open && input && v.win_obj >= 0)
    {
        Event e;
        e.type = Event::CLOSE;
        e.obj = v.win_obj;
        push_event(hd, e);
    }
}

// jwin_alert(): up to three centred lines and a row of buttons.
void draw_alert(HostDialog &hd, bool top, bool input, bool frozen)
{
    DialogView &v = hd.view;
    ImGuiViewport *vp = ImGui::GetMainViewport();
    const ImGuiStyle &style = ImGui::GetStyle();
    float dpi = zcui::scale();
    bool first = (hd.frames == 0);

    if(top && !g_dimmed)
    {
        ImGui::GetBackgroundDrawList()->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                                                      IM_COL32(0, 0, 0, 96));
        g_dimmed = true;
    }

    ImGui::SetNextWindowPos(vp->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f * dpi, 0.0f), ImVec2(vp->WorkSize.x - 32.0f * dpi, vp->WorkSize.y - 32.0f * dpi));
    if(top && (first || !hd.was_top))
        ImGui::SetNextWindowFocus();
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize;
    if(v.title.empty())
        flags |= ImGuiWindowFlags_NoTitleBar;
    if(!top)
        flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;

    bool open = true;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f * dpi, 18.0f * dpi));
    std::string wid = window_id(hd, v.title);
    bool shown = ImGui::Begin(wid.c_str(), (v.closable && input) ? &open : NULL, flags);
    ImGui::PopStyleVar();
    if(shown)
    {
        begin_inactive(!top, top && frozen);
        hd.focused_obj = -1;
        float avail = ImGui::GetContentRegionAvail().x;

        // the lines
        for(size_t i = 0; i < v.widgets.size(); ++i)
        {
            const WidgetView &w = v.widgets[i];
            if(w.kind != K_CTEXT && w.kind != K_TEXT && w.kind != K_RTEXT)
                continue;
            std::vector<std::string> lines;
            split_lines(w.text, lines);
            for(size_t j = 0; j < lines.size(); ++j)
            {
                if(lines[j].empty())
                    continue;
                float tw = ImGui::CalcTextSize(lines[j].c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail - tw) * 0.5f));
                ImGui::TextUnformatted(lines[j].c_str());
            }
        }

        // the buttons, all as wide as the widest
        ImGui::Dummy(ImVec2(0.0f, 10.0f * dpi));
        float bw = 96.0f * dpi;
        int count = 0;
        for(size_t i = 0; i < v.widgets.size(); ++i)
        {
            const WidgetView &w = v.widgets[i];
            if(w.kind != K_BUTTON)
                continue;
            bw = std::max(bw, ImGui::CalcTextSize(w.text.c_str()).x + style.FramePadding.x * 4.0f);
            ++count;
        }
        float total = bw * count + style.ItemSpacing.x * (count > 0 ? count - 1 : 0);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail - total) * 0.5f));
        bool line_started = false;
        for(size_t i = 0; i < v.widgets.size(); ++i)
        {
            const WidgetView &w = v.widgets[i];
            if(w.kind != K_BUTTON)
                continue;
            if(line_started)
                ImGui::SameLine();
            line_started = true;
            ImGui::PushID(w.obj);
            if(w.flags & D_DISABLED)
                ImGui::BeginDisabled();
            if(ImGui::Button(w.text.c_str(), ImVec2(bw, 0.0f)) && input)
                click(hd, w.obj);
            if(first && w.obj == v.focus_obj)
                ImGui::SetItemDefaultFocus();
            if(ImGui::IsItemFocused())
                hd.focused_obj = w.obj;
            if(w.flags & D_DISABLED)
                ImGui::EndDisabled();
            ImGui::PopID();
        }
        end_inactive(!top, top && frozen);
    }
    ImGui::End();

    if(!open && input && v.win_obj >= 0)
    {
        Event e;
        e.type = Event::CLOSE;
        e.obj = v.win_obj;
        push_event(hd, e);
    }
}

// ---------------------------------------------------------------------------
// the menu bar (jwin_menu_proc)
// ---------------------------------------------------------------------------

// Frames left to move the keyboard into a menu dropped by Alt+letter (a
// popup may need a frame or two before its items can take the focus).
int g_focus_menu = 0;

void draw_menu_items(HostDialog &hd, int first, int count, bool input)
{
    DialogView &v = hd.view;
    bool focus_first = g_focus_menu > 0;
    for(int i = first; i < first + count && i < (int)v.menu.size(); ++i)
    {
        const MenuNode &n = v.menu[i];
        if(focus_first && !n.separator && !(n.flags & D_DISABLED))
        {
            ImGui::SetKeyboardFocusHere();
            focus_first = false;
        }
        ImGui::PushID(i);
        if(n.separator)
            ImGui::Separator();
        else if(n.first_child >= 0)
        {
            if(ImGui::BeginMenu(n.label.c_str(), !(n.flags & D_DISABLED)))
            {
                draw_menu_items(hd, n.first_child, n.child_count, input);
                ImGui::EndMenu();
            }
        }
        else
        {
            if(ImGui::MenuItem(n.label.c_str(), n.shortcut.empty() ? NULL : n.shortcut.c_str(),
                               (n.flags & D_SELECTED) != 0, !(n.flags & D_DISABLED)) && input)
            {
                Event e;
                e.type = Event::MENU_PICK;
                e.obj = v.menu_obj;
                e.menu = n.item;
                push_event(hd, e);
            }
            if(g_focus_menu > 0 && ImGui::IsItemFocused())
                g_focus_menu = 0;
        }
        ImGui::PopID();
    }
}

void draw_menu_bar(HostDialog &hd, bool top, bool input, bool frozen)
{
    DialogView &v = hd.view;
    int request = -1;
    if(v.open_menu_serial != hd.open_menu_seen)
    {
        hd.open_menu_seen = v.open_menu_serial;
        if(input)
            request = v.open_menu;
    }

    // the editor's bar fits the one-line strip its screen leaves for it
    bool compact = (v.overlay || v.text_scale > 1.0f) && v.screen_w > 0 && v.screen_h > 0;
    if(compact)
    {
        ImGuiViewport *vp = ImGui::GetMainViewport();
        float s = std::min(vp->Size.x / v.screen_w, vp->Size.y / v.screen_h);
        // (with a limit: the small layout is scaled up a long way)
        ImGui::PushFont(NULL, std::min(26.0f, std::max(13.0f, 12.0f * s)) / font_scale());
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 1.0f));
    }
    begin_inactive(!top, top && frozen);
    if(ImGui::BeginMainMenuBar())
    {
        for(int i = 0; i < v.menu_top && i < (int)v.menu.size(); ++i)
        {
            const MenuNode &n = v.menu[i];
            if(n.separator)
                continue;
            ImGui::PushID(i);
            if(n.first_child >= 0)
            {
                if(i == request)
                {
                    ImGui::OpenPopup(n.label.c_str());
                    g_focus_menu = 4;
                }
                if(ImGui::BeginMenu(n.label.c_str(), !(n.flags & D_DISABLED)))
                {
                    draw_menu_items(hd, n.first_child, n.child_count, input);
                    ImGui::EndMenu();
                }
            }
            else if(ImGui::MenuItem(n.label.c_str(), n.shortcut.empty() ? NULL : n.shortcut.c_str(),
                                    (n.flags & D_SELECTED) != 0, !(n.flags & D_DISABLED)) && input)
            {
                Event e;
                e.type = Event::MENU_PICK;
                e.obj = v.menu_obj;
                e.menu = n.item;
                push_event(hd, e);
            }
            ImGui::PopID();
        }
        ImGui::EndMainMenuBar();
    }
    end_inactive(!top, top && frozen);
    if(compact)
    {
        ImGui::PopStyleVar();
        ImGui::PopFont();
    }
    if(g_focus_menu > 0)
        --g_focus_menu;
}

// A context menu (jwin_do_menu) at the mouse.
void draw_popup(HostDialog &hd, bool input)
{
    DialogView &v = hd.view;
    if(hd.frames == 0)
    {
        ImGui::SetNextWindowPos(ImGui::GetMousePos());
        ImGui::OpenPopup("##zcmenu");
    }
    if(ImGui::BeginPopup("##zcmenu"))
    {
        draw_menu_items(hd, 0, v.menu_top, input);
        ImGui::EndPopup();
    }
    else if(hd.frames > 0 && !hd.popup_closed && input && v.menu_obj >= 0)
    {
        hd.popup_closed = true;
        Event e;
        e.type = Event::CLOSE;
        e.obj = v.menu_obj;
        push_event(hd, e);
    }
}

// ---------------------------------------------------------------------------
// the "press a key" prompt
// ---------------------------------------------------------------------------

void draw_prompt(Shared &sh)
{
    ImGuiViewport *vp = ImGui::GetMainViewport();
    float dpi = zcui::scale();
    ImGui::SetNextWindowPos(vp->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(260.0f * dpi, 0.0f), ImVec2(vp->WorkSize.x, vp->WorkSize.y));
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26.0f * dpi, 20.0f * dpi));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f * dpi);
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyle().Colors[ImGuiCol_SliderGrab]);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;
    if(ImGui::Begin("##zcprompt", NULL, flags))
    {
        float avail = ImGui::GetContentRegionAvail().x;
        bool first = true;
        for(int i = 0; i < 3; ++i)
        {
            const std::string &line = sh.prompt_lines[i];
            if(line.empty())
                continue;
            if(first)
                ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * 1.25f);
            float tw = ImGui::CalcTextSize(line.c_str()).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail - tw) * 0.5f));
            if(first)
            {
                ImGui::TextUnformatted(line.c_str());
                ImGui::PopFont();
                ImGui::Dummy(ImVec2(0.0f, 4.0f * dpi));
                first = false;
            }
            else
                ImGui::TextDisabled("%s", line.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

} // namespace

// ---------------------------------------------------------------------------
// the layer
// ---------------------------------------------------------------------------

void zcui_dlg::layer(void *)
{
    count_layer_frame();
    Shared &sh = shared();
    bool dialogs = false;       // something other than the editor's menu bar
    for(size_t i = sh.floor; i < sh.stack.size(); ++i)
        if(!sh.stack[i]->view.overlay)
            dialogs = true;
    track_mouse(dialogs);
    g_dimmed = false;

    if(sh.stack.size() <= sh.floor)
    {
        g_popup_prev = false;
        zcui::set_capture(false, false);
        return;
    }

    if(sh.swallow_keys && !sh.prompt && !any_key_down())
        sh.swallow_keys = false;

    // There is one bar at the top of the window: a dialog with its own menu
    // (ZQuest's subscreen editor) takes it over from the ones below.
    size_t bar_owner = sh.stack.size();
    for(size_t i = sh.floor; i < sh.stack.size(); ++i)
        if(sh.stack[i]->view.ready && !sh.stack[i]->view.popup && sh.stack[i]->view.menu_obj >= 0)
            bar_owner = i;

    for(size_t i = sh.floor; i < sh.stack.size(); ++i)
    {
        HostDialog &hd = *sh.stack[i];
        bool top = (i + 1 == sh.stack.size());
        DialogView &v = hd.view;
        if(!v.ready)
            continue;
        bool frozen = top && (sh.prompt || sh.swallow_keys);
        bool input = top && !frozen;
        if(input && !v.overlay)
            forward_keys(hd);
        if(v.popup)
            draw_popup(hd, input);
        else if(v.menu_obj >= 0 && i == bar_owner)
            draw_menu_bar(hd, top, input, frozen);
        if(v.alert)
            draw_alert(hd, top, input, frozen);
        else if(!v.overlay && !v.popup && (v.has_window || !v.widgets.empty()))
            draw_window(hd, top, input, frozen);
        ++hd.frames;
        hd.was_top = top;
    }

    if(sh.prompt)
        draw_prompt(sh);

    g_popup_prev = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup);

    // Letters reach a dialog as typed text: its shortcuts ("&Yes", "&No") and
    // a self-drawn text editor (ZQuest's script editor) need the system's text
    // input, which ImGui turns on only for its own fields. (On Android it
    // would bring up the on-screen keyboard for every dialog.)
    {
        HostDialog &last = *sh.stack.back();
        ImGuiIO &io = ImGui::GetIO();
#ifdef __ANDROID__
        bool want = dialogs && !last.view.overlay && last.view.canvas_focus >= 0 &&
                    last.focused_obj < 0 && !io.WantTextInput;
#else
        bool want = dialogs && !last.view.overlay && !io.WantTextInput;
#endif
        int n = 0;
        SDL_Window **windows = SDL_GetWindows(&n);
        if(windows && n > 0)
        {
            // (ImGui turns it off when one of its fields loses the focus)
            if(want && !SDL_TextInputActive(windows[0]))
                SDL_StartTextInput(windows[0]);
            else if(!want && g_canvas_text && !io.WantTextInput)
                SDL_StopTextInput(windows[0]);
        }
        SDL_free(windows);
        g_canvas_text = want;
    }

    if(dialogs)
        zcui::set_capture(false, false);
    else
        zcui::set_capture(ImGui::GetIO().WantCaptureMouse || g_popup_prev, g_popup_prev);
}

void zcui_dlg::canvas_release(HostDialog *hd)
{
    for(size_t i = 0; i < g_canvas_textures.size(); ++i)
        if(g_canvas_textures[i].owner == hd)
            g_canvas_textures[i].owner = NULL;
}
