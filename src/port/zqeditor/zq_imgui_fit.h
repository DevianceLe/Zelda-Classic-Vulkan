// Keeping ZQuest's ImGui editor windows usable when ZQuest's own window is small or changes size
// (the editors for 2.55 and 3.0 Nightly quests): a window opens at the size it asks for, but
// never larger than ZQuest's window, is kept inside it when that shrinks (a window outside it
// can't be dragged back), and gets its own size back when ZQuest's window grows again after
// squeezing it. Use zq_fit_next_window before ImGui::Begin and zq_keep_window_inside right
// after it, with the same size.

#ifndef ZQ_IMGUI_FIT_H
#define ZQ_IMGUI_FIT_H

#include <imgui.h>
#include <imgui_internal.h>

inline ImVec2 zq_fit_size(float w, float h)
{
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    return ImVec2(w < vp->WorkSize.x ? w : vp->WorkSize.x, h < vp->WorkSize.y ? h : vp->WorkSize.y);
}

inline void zq_fit_next_window(float w, float h)
{
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(zq_fit_size(w, h), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(200, 150), vp->WorkSize);
}

inline void zq_keep_window_inside(float w, float h)
{
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGuiStorage *st = ImGui::GetStateStorage();
    const ImGuiID squeezed_id = ImGui::GetID("##zq_squeezed");
    ImVec2 p = ImGui::GetWindowPos();
    ImVec2 s = ImGui::GetWindowSize();
    const ImVec2 fit = zq_fit_size(w, h);

    // squeezed by ZQuest's window (now or before): back to the size asked for, as far as it fits
    if(fit.x < w - 1 || fit.y < h - 1)
        st->SetBool(squeezed_id, true);

    if(st->GetBool(squeezed_id, false))
    {
        if(fit.x > s.x + 1 || fit.y > s.y + 1)
        {
            s = ImVec2(fit.x > s.x ? fit.x : s.x, fit.y > s.y ? fit.y : s.y);
            ImGui::SetWindowSize(s);
        }

        if(fit.x >= w - 1 && fit.y >= h - 1 && s.x >= w - 1 && s.y >= h - 1)
            st->SetBool(squeezed_id, false);
    }

    const ImVec2 lo = vp->WorkPos, hi(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);
    const ImVec2 was = p;

    if(p.x + s.x > hi.x)
        p.x = hi.x - s.x;

    if(p.y + s.y > hi.y)
        p.y = hi.y - s.y;

    if(p.x < lo.x)
        p.x = lo.x;

    if(p.y < lo.y)
        p.y = lo.y;

    if(p.x != was.x || p.y != was.y)
        ImGui::SetWindowPos(p);
}

#endif
