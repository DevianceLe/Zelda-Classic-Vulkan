// A full-screen software page of ZQuest (tile editor, combo pages, ...). In
// the port the dialogs below it are hidden while it runs and it draws to the
// real screen; the Allegro build needs none of this.

#ifndef _ZQ_PAGE_H_
#define _ZQ_PAGE_H_

#ifdef ZC_PORT

void zcui_page_begin();
void zcui_page_end();

struct ZqPageScope
{
    ZqPageScope() { zcui_page_begin(); }
    ~ZqPageScope() { zcui_page_end(); }
};

#define ZQ_PAGE_SCOPE() ZqPageScope zq_page_scope_guard

#else

#define ZQ_PAGE_SCOPE() ((void)0)

#endif

// The program name in the window title.
#ifdef ZC_PORT
#define ZQ_WINDOW_NAME "ZQuest Vulkan 2.53.444.6"
#else
#define ZQ_WINDOW_NAME "ZQuest"
#endif

#endif
