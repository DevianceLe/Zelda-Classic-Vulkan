//--------------------------------------------------------
//  Zelda Classic
//  by Jeremy Craner, 1999-2000
//
//  zq_files.cc
//
//  File support for ZQuest.
//
//--------------------------------------------------------

#ifndef __GTHREAD_HIDE_WIN32API
#define __GTHREAD_HIDE_WIN32API 1
#endif                            //prevent indirectly including windows.h

#include "precompiled.h" //always first
#include "zq_page.h"

#include <string.h>
#include <stdio.h>

#include "gui.h"
#include "zq_files.h"
#include "zdefs.h"
#include "zq_misc.h"
#include "zquest.h"
#include "qst.h"
#include "zsys.h"
#include "zq_class.h"
#include "tiles.h"
#include "zq_tiles.h"
#include "zq_custom.h"
#include "zc_malloc.h"
#ifdef ZC_ZQEDITOR
#include <string>
#include <vector>
#include "port/zqeditor/zq_q255.h"
#include "port/zqeditor/zq_writers.h"
#include "port/zqeditor/zq_tile_refs.h"
#include "port/zqeditor/zq_exports.h"
#include "port/game/quest_version.h"
#include "port/zqeditor/zq_zinfo.h"
#endif

#ifdef _MSC_VER
#define getcwd _getcwd
#define stricmp _stricmp
#define strupr _strupr
#endif

char qtbuf[31];

void reset_qt(int index)
{
    bound(index,0,MAXQTS-1);
    char *s=QuestTemplates[index].name;
    
    for(int i=0; i<31; i++)
        *(s++)=0;
        
    s=QuestTemplates[index].path;
    
    for(int i=0; i<2048; i++)
        *(s++)=0;
}

void init_qts()
{
    for(int i=0; i<MAXQTS; i++)
        reset_qt(i);
        
    strcpy(QuestTemplates[0].name,"New Default (can't change)");
    qt_count=1;
}

static DIALOG editqt_dlg[] =
{
    /* (dialog proc)     (x)   (y)   (w)   (h)   (fg)     (bg)    (key)    (flags)     (d1)           (d2)     (dp) */
    { jwin_win_proc,     32,   68,   257,  102,   vc(14),  vc(1),  0,       D_EXIT,          0,             0, (void *) "Quest Template Editor", NULL, NULL },
    { d_timer_proc,         0,    0,     0,    0,    0,       0,       0,       0,          0,          0,         NULL, NULL, NULL },
    { jwin_button_proc,     50,   140,  61,   21,   vc(14),  vc(1),  13,      D_EXIT,     0,             0, (void *) "Load", NULL, NULL },
    { jwin_button_proc,     130,  140,  61,   21,   vc(14),  vc(1),  27,      D_EXIT,     0,             0, (void *) "OK", NULL, NULL },
    { jwin_button_proc,     210,  140,  61,   21,   vc(14),  vc(1),  27,      D_EXIT,     0,             0, (void *) "Cancel", NULL, NULL },
    { jwin_text_proc,       44,   103,   40,  8,    vc(15),  vc(1),  0,       0,          0,             0, (void *) "Name:", NULL, NULL },
    { jwin_edit_proc,       77,   99,   200,    16,      vc(12),  vc(1),  0,       0,          30,             0,       NULL, NULL, NULL },
    { jwin_text_proc,       44,   118,  112,  8,    vc(15),  vc(1),  0,       0,          0,             0, (void *) "Template File:", NULL, NULL },
    { jwin_text_proc,       44,   128,  233,  8,    vc(13),  vc(1),  0,       D_DISABLED,          0,             0,       NULL, NULL, NULL },
    { d_keyboard_proc,   0,    0,    0,    0,    0,    0,    0,       0,       KEY_F1,   0, (void *) onHelp, NULL, NULL },
    { NULL,                 0,    0,    0,    0,   0,       0,       0,       0,          0,             0,       NULL,                           NULL,  NULL }
};

void edit_qt()                                              //this is used to set the quest template for the current quest
{
    char tpath[2048];
    char tpath2[2048];
    strcpy(temppath, header.templatepath);
    
    if(temppath[0]==0)
    {
        getcwd(temppath,2048);
        fix_filename_case(temppath);
        fix_filename_slashes(temppath);
        put_backslash(temppath);
    }
    
    bool gotname;
    gotname=getname("Load Quest Template (.zqt)","zqt",NULL,temppath,true);
    
    if(gotname)
    {
        strcpy(tpath, temppath);
        chop_path(tpath);
        getcwd(tpath2,2048);
        fix_filename_case(tpath2);
        fix_filename_slashes(tpath2);
        put_backslash(tpath2);
        
        if(!strcmp(tpath, tpath2))
        {
            strcpy(header.templatepath, get_filename(temppath));
        }
        else
        {
            strcpy(header.templatepath, temppath);
        }
        
        if(!valid_zqt(temppath))
        {
            jwin_alert("ZQuest","Invalid Quest Template",NULL,NULL,"O&K",NULL,'k',0,lfont);
            memset(header.templatepath, 0, 2048);
        }
    }
}

void edit_qt(int index)
{
    int ret;
    quest_template tqt;
    char tpath[2048];
    char tpath2[2048];
    tqt=QuestTemplates[index];
    editqt_dlg[0].dp2=lfont;
    
    do
    {
        editqt_dlg[6].dp=QuestTemplates[index].name;
        editqt_dlg[8].dp=QuestTemplates[index].path;
        strcpy(temppath, QuestTemplates[index].path);
        bool gotname;
        
        if(is_large)
            large_dialog(editqt_dlg);
            
        ret=zc_popup_dialog(editqt_dlg,6);
        
        switch(ret)
        {
        case 2:
            gotname=getname("Load Quest Template (.zqt)","zqt",NULL,temppath,true);
            
            if(gotname)
            {
                strcpy(tpath, temppath);
                chop_path(tpath);
                getcwd(tpath2,2048);
                fix_filename_case(tpath2);
                fix_filename_slashes(tpath2);
                put_backslash(tpath2);
                
                if(!strcmp(tpath, tpath2))
                {
                    strcpy(QuestTemplates[index].path, get_filename(temppath));
                }
                else
                {
                    strcpy(QuestTemplates[index].path, temppath);
                }
            }
            
            break;
            
        case 3:
        
            if(!valid_zqt(temppath))
            {
                ret=2;
                jwin_alert("ZQuest","Invalid Quest Template",NULL,NULL,"O&K",NULL,'k',0,lfont);
                break;
            }
            
            if(index==qt_count)
            {
                ++qt_count;
            }
            
            break;
            
        case 4:
            QuestTemplates[index]=tqt;
            break;
        }
    }
    while(ret==2);
}

const char *qtlist(int index, int *list_size)
{
    if(index>=0)
    {
        if(index>=qt_count)
            index=qt_count-1;
            
        return QuestTemplates[index].name;
    }
    
    *list_size=qt_count;
    return NULL;
}

static ListData qtlist_dlg_list(qtlist, &font);

static DIALOG qtlist_dlg[] =
{
    // (dialog proc)     (x)   (y)   (w)   (h)   (fg)     (bg)    (key)    (flags)     (d1)           (d2)     (dp)
    { jwin_win_proc,          0,      0,    303,    149,    vc(14),                vc(1),                  0,    D_EXIT,     0,           0, (void *) "Pick Quest Template",   NULL,   NULL  },
    { d_timer_proc,           0,      0,      0,      0,    0,                     0,                      0,    0,          0,           0,  NULL,                             NULL,   NULL  },
    { jwin_list_proc,         5,     26,    271,     87,    jwin_pal[jcBOXFG],     jwin_pal[jcLIGHT],      0,    D_EXIT,     0,           0, (void *) &qtlist_dlg_list,        NULL,   NULL  },
    { jwin_button_proc,     278,     44,     21,     21,    vc(14),                vc(1),                 13,    D_EXIT,     0,           0, (void *) "\x88",                  NULL,   NULL  },
    { jwin_button_proc,     278,     73,     21,     21,    vc(14),                vc(1),                 13,    D_EXIT,     0,           0, (void *) "\x89",                  NULL,   NULL  },
    { jwin_button_proc,      30,    120,     61,     21,    vc(14),                vc(1),                 13,    D_EXIT,     0,           0, (void *) "Edit",                  NULL,   NULL  },
    { jwin_button_proc,     110,    120,     61,     21,    vc(14),                vc(1),                 27,    D_EXIT,     0,           0, (void *) "OK",                    NULL,   NULL  },
    { jwin_button_proc,     190,    120,     61,     21,    vc(14),                vc(1),                 27,    D_EXIT,     0,           0, (void *) "Cancel",                NULL,   NULL  },
    { d_keyboard_proc,        0,      0,      0,      0,    0,                     0,                      0,    0,          KEY_DEL,     0, (void *) qtlist_del,              NULL,   NULL  },
    { NULL,                   0,      0,      0,      0,    0,                     0,                      0,    0,          0,           0,  NULL,                             NULL,   NULL  }
};

int qtlist_del()
{
    if(qtlist_dlg[2].d1>0 && qtlist_dlg[2].d1<qt_count-1)
        return D_CLOSE;
        
    return D_O_K;
}

int ListQTs(bool edit)
{
    qtlist_dlg[0].dp2=lfont;
    int index=0;
    quest_template *BackupQTs = (quest_template*)zc_malloc(sizeof(quest_template)*MAXQTS);
    
    memcpy(BackupQTs,QuestTemplates,sizeof(quest_template)*qt_count);
    
    int backup_qt_count=qt_count;
    
    while(index>-1)
    {
        bool hasroom=false;
        
        if(qt_count<MAXQTS)
        {
            hasroom=true;
            
            if(edit)
            {
                strcpy(QuestTemplates[qt_count++].name,"<new template>");
            }
        }
        
        if(is_large)
            large_dialog(qtlist_dlg);
            
        qtlist_dlg[2].x=int(qtlist_dlg[0].x+(edit?5:15)*(is_large?1.5:1));
        qtlist_dlg[3].proc=edit?jwin_button_proc:d_dummy_proc;
        qtlist_dlg[4].proc=edit?jwin_button_proc:d_dummy_proc;
        qtlist_dlg[5].proc=edit?jwin_button_proc:d_dummy_proc;
        qtlist_dlg[6].x=int(qtlist_dlg[0].x+(edit?110:80)*(is_large?1.5:1));
        qtlist_dlg[7].x=int(qtlist_dlg[0].x+(edit?190:160)*(is_large?1.5:1));
        qtlist_dlg[8].proc=edit?d_keyboard_proc:d_dummy_proc;
        
        int ret=zc_popup_dialog(qtlist_dlg,2);
        
        index=qtlist_dlg[2].d1;
        
        int doedit=false;
        
        switch(ret)
        {
        case 2:
            if(edit)
            {
                doedit=true;
            }
            else
            {
                if(index>0&&!valid_zqt(QuestTemplates[index].path))
                {
                    jwin_alert("ZQuest","Invalid Quest Template",NULL,NULL,"O&K",NULL,'k',0,lfont);
                }
                else
                {
                    strcpy(header.templatepath, QuestTemplates[index].path);
                    index=-1;
                }
            }
            
            break;
            
        case 5:
            doedit=true;
            break;
            
        case 3:
            if(index>1&&index<qt_count-1)
            {
                zc_swap(QuestTemplates[index],QuestTemplates[index-1]);
                --qtlist_dlg[2].d1;
                index=qtlist_dlg[2].d1;
            }
            
            break;
            
        case 4:
            if(index>0&&index<qt_count-2)
            {
                zc_swap(QuestTemplates[index],QuestTemplates[index+1]);
                ++qtlist_dlg[2].d1;
                index=qtlist_dlg[2].d1;
            }
            
            break;
            
        case 6:
            if(index>0&&!valid_zqt(QuestTemplates[index].path))
            {
                jwin_alert("ZQuest","Invalid Quest Template",NULL,NULL,"O&K",NULL,'k',0,lfont);
            }
            else
            {
                if(!edit)
                {
                    strcpy(header.templatepath, QuestTemplates[index].path);
                }
                
                index=-1;
            }
            
            break;
            
        case 0:
        case 7:
            if(edit)
            {
                qt_count=backup_qt_count+1;
                memcpy(QuestTemplates,BackupQTs,sizeof(quest_template)*qt_count);
            }
            
            index=-2;
            break;
            
        case 8:
            char buf[30];
            strncpy(buf,QuestTemplates[index].name,30);
            
            if(jwin_alert("Confirm Deletion", "Delete this quest template?",buf,"(The file will still exist.)","Yes","No",'y',27,lfont)==1)
            {
                for(int i=index; i<MAXQTS-1; i++)
                    QuestTemplates[i]=QuestTemplates[i+1];
                    
                reset_qt(MAXQTS-1);
                --qt_count;
            }
            
            break;
        }
        
        if(edit&&hasroom)
        {
            //      strcpy(QuestTemplates[--qt_count].name,"              ");
            reset_qt(--qt_count);
            sprintf(QuestTemplates[qt_count].name,"Untitled");
        }
        
        if(index>0 && doedit)
        {
            edit_qt(index);
        }
    }
    
    zc_free(BackupQTs);
    return index;
}

int onQuestTemplates()
{
    ListQTs(true);
    return D_O_K;
}

int NewQuestFile(int template_slot)
{
    memset(filepath,0,255);
    memset(temppath,0,255);
#ifdef ALLEGRO_MACOSX
    sprintf(filepath, "../../../");
    sprintf(temppath, "../");
#endif
    first_save=false;
    box_start(1, "Initializing Quest", lfont, pfont, false);
    box_out("Please wait.");
    box_eol();
    box_out("This may take a few moments.");
    box_eol();
    
    init_quest(QuestTemplates[template_slot].path);
    saved=true;
    box_end(false);
    refresh(rALL);
    return D_O_K;
}

static int ruleset=0;
int d_ruleset_radio_proc(int msg,DIALOG *d,int c);
int d_rulesettext_proc(int msg, DIALOG *d, int c);

static DIALOG ruleset_dlg[] =
{
    { jwin_win_proc,           0,     0,  230,   180,  vc(14),              vc(1),                 0,       D_EXIT,     0,             0, (void *) "New Quest", NULL, NULL },
    { jwin_button_proc,       76,   153,   61,    21,  vc(14),              vc(1),                13,       D_EXIT,     0,             0, (void *) "OK", NULL, NULL },
    { jwin_frame_proc,   102,   80,   112,  43,   0,       0,      0,       0,             FR_ETCHED,       0,       NULL, NULL, NULL },
    
    { d_dummy_proc,			    20,    71,   61,    9,  vc(14),              vc(1),                 0,       0,     0,             0,       0, NULL, NULL },
    { d_ruleset_radio_proc,       20,    81,   61,    9,  vc(14),              vc(1),                 0,       0,     0,             0, (void *) "Authentic NES", NULL, NULL },
    { d_ruleset_radio_proc,       20,    91,   61,    9,  vc(14),              vc(1),                 0,       0,     0,             0, (void *) "Fixed NES", NULL, NULL },
    { d_ruleset_radio_proc,       20,   101,   61,    9,  vc(14),              vc(1),                 0,       0,     0,             0, (void *) "BS-Zelda", NULL, NULL },
    { d_ruleset_radio_proc,       20,   111,   61,    9,  vc(14),              vc(1),                 0,       D_SELECTED,     0,             0, (void *) "Zelda 3-esque", NULL, NULL },
    { d_rulesettext_proc,      108,   85,   0,    0,  vc(14),              vc(1),                 0,       0,     0,             0,       NULL, NULL, NULL },
    // 9
    { jwin_text_proc,       16,   24,  128,    8,    vc(14),  vc(1),  0,       0,          0,             0, (void *) "You have embarked on a new quest! Please specify which", NULL, NULL },
    { jwin_text_proc,       16,   34,  128,    8,    vc(14),  vc(1),  0,       0,          0,             0, (void *) "initial set of Quest Rules you want to enable for this", NULL, NULL },
    { jwin_text_proc,       16,   44,  128,    8,    vc(14),  vc(1),  0,       0,          0,             0, (void *) "quest. Quest Rules affect the behaviour and features of", NULL, NULL },
    { jwin_text_proc,       16,   54,  128,    8,    vc(14),  vc(1),  0,       0,          0,             0, (void *) "the game engine and how different your quest is from", NULL, NULL },
    { jwin_text_proc,       16,   64,  128,    8,    vc(14),  vc(1),  0,       0,          0,             0, (void *) "the NES game 'The Legend of Zelda'.", NULL, NULL },
    // 14
    { jwin_text_proc,       16,   28,  128,    8,    vc(14),  vc(1),  0,       0,          0,             0, (void *) "You have embarked on a new quest!", NULL, NULL },
    { jwin_text_proc,       16,   38,  128,    8,    vc(14),  vc(1),  0,       0,          0,             0, (void *) "Please select an initial set of Quest Rules", NULL, NULL },
    { jwin_text_proc,       16,   48,  128,    8,    vc(14),  vc(1),  0,       0,          0,             0, (void *) "for your quest. Quest Rules affect", NULL, NULL },
    { jwin_text_proc,       16,   58,  128,    8,    vc(14),  vc(1),  0,       0,          0,             0, (void *) "the features of the game engine.", NULL, NULL },
    
    { jwin_text_proc,       16,  130,  128,    8,    vc(14),  vc(1),  0,       0,          0,             0, (void *) "After creation, individual Quest Rules can be", NULL, NULL },
    { jwin_text_proc,       16,  140,  128,    8,    vc(14),  vc(1),  0,       0,          0,             0, (void *) "set in the Rules submenu of the Quest menu.", NULL, NULL },
    // There's no d_timer_proc; don't be silly.
    { NULL,                 0,    0,    0,    0,   0,       0,       0,       0,          0,             0,       NULL,                           NULL,  NULL }
};

int d_ruleset_radio_proc(int msg,DIALOG *d,int c)
{
    int temp = ruleset;
    int ret = jwin_radiofont_proc(msg,d,c);
    
    if(ruleset_dlg[3].flags & D_SELECTED) ruleset =0;
    else if(ruleset_dlg[4].flags & D_SELECTED) ruleset =1;
    else if(ruleset_dlg[5].flags & D_SELECTED) ruleset =2;
    else if(ruleset_dlg[6].flags & D_SELECTED) ruleset =3;
    else if(ruleset_dlg[7].flags & D_SELECTED) ruleset =4;
    
    if(temp != ruleset)
    {
        return D_REDRAW;
    }
    
    return ret;
}

int d_rulesettext_proc(int msg, DIALOG *d, int)
{
    if(msg!=MSG_DRAW)
        return D_O_K;
        
    char buf[31];
    char buf2[31];
    char buf3[31];
    char buf4[31];
    
    switch(ruleset)
    {
    case 1: // Original NES
        sprintf(buf,  "Matches the behaviour of");
        sprintf(buf2, "'The Legend of Zelda'.");
        sprintf(buf3, "All but a few rules are off.");
        sprintf(buf4, " ");
        break;
        
    case 2: // Fixed NES
        sprintf(buf,  "Various bugs in 'The Legend");
        sprintf(buf2, "of Zelda' are fixed.");
        sprintf(buf3, "All Quest Rules in the NES");
        sprintf(buf4, "Fixes category are on.");
        break;
        
    case 3: // BS Zelda
        sprintf(buf,  "Fixed NES, plus BS-Zelda");
        sprintf(buf2, "animations, new enemy tiles,");
        sprintf(buf3, "fast scrolling, new push-");
        sprintf(buf4, "blocks, transition wipes, etc.");
        break;
        
    case 4: // Zelda 3-esque
        sprintf(buf,  "BS-Zelda, plus diagonal Link");
        sprintf(buf2, "movement, new message strings,");
        sprintf(buf3, "magic and arrow meters, more");
        sprintf(buf4, "sound effects, drowning, etc.");
        break;
    }
    
    FONT *f = is_large ? font : sfont2;
    textprintf_ex(screen,f,d->x,d->y,jwin_pal[jcBOXFG],jwin_pal[jcBOX],buf);
    textprintf_ex(screen,f,d->x,d->y+(is_large?12:8),jwin_pal[jcBOXFG],jwin_pal[jcBOX],buf2);
    textprintf_ex(screen,f,d->x,d->y+(is_large?24:16),jwin_pal[jcBOXFG],jwin_pal[jcBOX],buf3);
    textprintf_ex(screen,f,d->x,d->y+(is_large?36:24),jwin_pal[jcBOXFG],jwin_pal[jcBOX],buf4);
    return D_O_K;
}

int PickRuleset()
{
    ruleset_dlg[0].dp2=lfont;
    
    // Large Mode conversion
    if(!is_large)
        ruleset_dlg[2].proc = d_dummy_proc;
        
    int start = (is_large?14:9);
    int end = (is_large?17:13);
    
    for(int i = start; i <= end; i++)
    {
        ruleset_dlg[i].proc = d_dummy_proc;
    }
    
    if(is_large)
        large_dialog(ruleset_dlg);
        
    int ret = zc_popup_dialog(ruleset_dlg,1);
    
    if(ret==1)
    {
        if(ruleset>0)
        {
            for(int i=0; i<qr_MAX; i++)
                set_bit(quest_rules,i,0);
        }
        
        // ZC engine fixes
        // These should always be on.
        set_bit(quest_rules, qr_DMGCOMBOLAYERFIX, 1);
        set_bit(quest_rules, qr_WARPSIGNOREARRIVALPOINT, 1);
        set_bit(quest_rules, qr_NOARRIVALPOINT, 1);
        set_bit(quest_rules, qr_CMBCYCLELAYERS, 1);
        set_bit(quest_rules, qr_KILLALL, 1);
        set_bit(quest_rules, qr_ITEMBUBBLE, 1);
        set_bit(quest_rules, qr_RINGAFFECTDAMAGE, 1);
        //Script errors on by default
        set_bit(quest_rules, qr_SCRIPTERRLOG, 1);
        
        switch(ruleset)
        {
        case 1: // Original NES
            set_bit(quest_rules, qr_OLDPICKUP, 1);
            set_bit(quest_rules, qr_OLDSTYLEWARP, 1);
            break;
            
        case 4: // Zelda 3-esque
        
            // Make the water combos drownable
            for(int i=0; i < MAXCOMBOS; i++)
            {
                if(combo_class_buf[combobuf[i].type].water!=0)
                {
                    combobuf[i].walk = 0;
                }
            }
            
            set_bit(quest_rules, qr_DROWN, 1);
            set_bit(quest_rules, qr_HIDECARRIEDITEMS, 1);
            set_bit(quest_rules, qr_ALLOWMSGBYPASS, 1);
            set_bit(quest_rules, qr_ALLOWFASTMSG, 1);
            set_bit(quest_rules, qr_MSGDISAPPEAR, 1);
            set_bit(quest_rules, qr_MSGFREEZE, 1);
            set_bit(quest_rules, qr_ENABLEMAGIC, 1);
            set_bit(quest_rules, qr_NOWANDMELEE, 1);
            set_bit(quest_rules, qr_TRUEARROWS, 1);
            set_bit(quest_rules, qr_Z3BRANG_HSHOT, 1);
            set_bit(quest_rules, qr_TRANSSHADOWS, 1);
            set_bit(quest_rules, qr_SHADOWS, 1);
            set_bit(quest_rules, qr_LTTPWALK, 1);
            set_bit(quest_rules, qr_MORESOUNDS, 1);
            
            // Fallthrough
        case 3: // BS Zelda
            set_bit(quest_rules, qr_TIME, 1);
            set_bit(quest_rules, qr_NOBOMBPALFLASH, 1);
            set_bit(quest_rules, qr_NEWENEMYTILES, 1);
            set_bit(quest_rules, qr_FASTDNGN, 1);
            set_bit(quest_rules, qr_SMOOTHVERTICALSCROLLING, 1);
            set_bit(quest_rules, qr_COOLSCROLL, 1);
            set_bit(quest_rules, qr_BSZELDA, 1);
            set_bit(quest_rules, qr_SOLIDBLK, 1);
            set_bit(quest_rules, qr_HESITANTPUSHBLOCKS, 1);
            set_bit(quest_rules, qr_INSTABURNFLAGS, 1);
            set_bit(quest_rules, qr_FADE, 1); // Interpolated fading
            set_bit(quest_rules, qr_EXPANDEDLTM, 1);
            set_bit(quest_rules, qr_NOBOMBPALFLASH, 1);
            
            // Fallthrough
        case 2: // Fixed NES
            // Copied from zq_rules.cpp
            int fixesrules[] =
            {
                qr_FREEFORM, qr_SAFEENEMYFADE, qr_ITEMSONEDGES, qr_LINKDUNGEONPOSFIX, qr_RLFIX,
                qr_NOLEVEL3FIX, qr_BOMBHOLDFIX, qr_HOLDNOSTOPMUSIC, qr_CAVEEXITNOSTOPMUSIC,
                qr_OVERWORLDTUNIC, qr_SWORDWANDFLIPFIX, /*qr_WPNANIMFIX,*/ qr_PUSHBLOCKCSETFIX,
                qr_TRAPPOSFIX, qr_NOBORDER, qr_OLDPICKUP, qr_SUBSCREENOVERSPRITES,
                qr_BOMBDARKNUTFIX, qr_OFFSETEWPNCOLLISIONFIX, qr_ITEMSINPASSAGEWAYS, qr_NOFLICKER, qr_FIREPROOFLINK2,
                qr_NOITEMOFFSET, qr_LADDERANYWHERE, -1
            };
            for(int i=0; fixesrules[i]!=-1; i++)
            {
                if(i!=qr_OLDPICKUP)
                    set_bit(quest_rules, fixesrules[i], 1);
            }
            
            break;
            
        }
    }
    
    return D_O_K;
}

#ifdef ZC_ZQEDITOR
// File > New asks which version of ZC the new quest is for (below, with the saving code).
static int zq_pick_new_version();
static bool zq_new_version_uses_template(int v);
static void zq_new_quest_as(int v);
#endif

int onNew()
{
    if(checksave()==0)
        return D_O_K;

#ifdef ZC_ZQEDITOR
    int v = zq_pick_new_version();

    if(v < 0)
        return D_O_K;

    // a version whose own ZQuest shipped a new-quest file starts from that file as it is
    if(zq_new_version_uses_template(v))
    {
        zq_new_quest_as(v);
        return D_O_K;
    }
#endif
    /*
      int ret=ListQTs(false);
      if (ret==-2)
      {
        return D_O_K;
      }
    */
    int ret=0;
    NewQuestFile(ret);

    if(RulesetDialog > 0)
        PickRuleset();

#ifdef ZC_ZQEDITOR
    zq_new_quest_as(v);
#endif
    return D_O_K;
}

#ifdef ZC_ZQEDITOR
int zq255_saving_not_ready()
{
    jwin_alert("ZQuest","This quest was made in ZC 2.55 or a 3.0 Nightly.",
               "ZQuest can open these quests but can't save them yet.",
               "Nothing was saved.","O&K",NULL,'k',0,lfont);
    return D_O_K;
}
#endif

#ifdef ZC_ZQEDITOR
// A 2.53 file can't hold a 2.55 / 3.0 quest's data. (File > Export no longer needs this: those
// quests export upstream's own files, below; kept for any other export that writes a 2.53 file.)
int zq255_export_not_ready()
{
    jwin_alert("ZQuest","This quest was made in ZC 2.55 or a 3.0 Nightly.",
               "This export writes a 2.53 file, which can't hold",
               "this quest's data yet. Nothing was exported.","O&K",NULL,'k',0,lfont);
    return D_O_K;
}

// ---- exports and imports of 2.55 and 3.0 Nightly quests (port/zqeditor/zq_exports.h) ------
//
// For these quests File > Export and Import write and read upstream's files for the quest's
// version (.zdmap, .zqs, .zcombo, .ztileset, .guy, .zgp, and the quest itself as .zqt or .qsu),
// from and into ZQuest's data and its store for these quests, so the quest's own-version save
// writes what came in. 2.53 quests keep 2.53's files.

// A message of any length in 2.53's alerts, which show three lines: wrapped to what the screen
// takes, and going on in further alerts when it needs more than three lines, so none of it is
// cut.
static void zq_long_alert(const char *title, const std::string &text)
{
    const size_t width = is_large ? 96 : 48;
    std::vector<std::string> lines(1);
    size_t i = 0;

    while(i < text.size())
    {
        size_t sp = text.find(' ', i);
        std::string word = text.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
        i = sp == std::string::npos ? text.size() : sp + 1;

        if(!lines.back().empty() && lines.back().size() + 1 + word.size() > width)
            lines.push_back(std::string());

        lines.back() += (lines.back().empty() ? "" : " ") + word;
    }

    for(size_t k = 0; k < lines.size(); k += 3)
        jwin_alert(title, lines[k].c_str(), k + 1 < lines.size() ? lines[k + 1].c_str() : NULL,
                   k + 2 < lines.size() ? lines[k + 2].c_str() : NULL, "O&K", NULL, 'k', 0, lfont);
}

// What an export or import of a newer quest's file came to: done (note: anything the user
// should know) or not (why).
static void zq_newer_file_done(bool saving, const char *path, bool ok, const std::string &why, const std::string &note)
{
    char name[256];
    extract_name((char *)path, name, FILENAMEALL, sizeof(name));
    std::string text = std::string(ok ? (saving ? "Saved " : "Loaded ") : (saving ? "Unable to save " : "Unable to load ")) + name;
    const std::string &more = ok ? note : why;

    if(!more.empty())
        text += ": " + more;

    zq_long_alert(ok ? "ZQuest" : "Error", text);
}

// The item and enemy listers' Save and Load for a 2.55 / 3.0 quest (upstream's .zitem and .znpc
// files, zqx_export_one / zqx_import_one). True when the item or enemy changed.
bool zq_newer_one_file(bool item, int index, bool saving)
{
    char title[160];
    extern char *item_string[];
    extern char *guy_string[];
    const char *name = item ? item_string[index] : guy_string[index];

    if(saving)
        snprintf(title, sizeof(title), "Save %s '%s' #%d (.%s)", item ? "Item" : "NPC", name, index, item ? "zitem" : "znpc");
    else
        snprintf(title, sizeof(title), "Load %s (replacing '%s' #%d) (.%s)", item ? "Item" : "NPC", name, index, item ? "zitem" : "znpc");

    if(!getname(title, item ? "zitem" : "znpc", NULL, datapath, false))
        return false;

    std::string why, note;
    const bool ok = saving ? zqx_export_one(item, index, temppath, why) : zqx_import_one(item, index, temppath, why, note);
    zq_newer_file_done(saving, temppath, ok, why, note);
    return ok && !saving;
}

static int zq_newer_export_dmaps()
{
    int first = getnumber("First DMap", 0);

    if(cancelgetnum)
        return D_O_K;

    int last = getnumber("Last DMap", first);

    if(cancelgetnum)
        return D_O_K;

    if(!getname("Export DMaps (.zdmap)","zdmap",NULL,datapath,false))
        return D_O_K;

    std::string why;
    bool ok = zqx_export_dmaps(temppath, first, last, why);
    zq_newer_file_done(true, temppath, ok, why, "");
    return D_O_K;
}

static int zq_newer_import_dmaps()
{
    if(!getname("Import DMaps (.zdmap)","zdmap",NULL,datapath,false))
        return D_O_K;

    std::string why, note;
    int max_map = 0;
    bool ok = zqx_import_dmaps(temppath, &max_map, why, note);

    if(ok)
    {
        saved=false;

        // as 2.53 and upstream: more maps, or the DMaps on missing maps go to map 0
        if(max_map >= map_count)
        {
            int ret=jwin_alert("Not enough maps",
                               "The imported DMaps use more maps than are",
                               " currently available. Do you want to add",
                               "more maps or change the DMaps' settings?",
                               "&Add maps","&Modify DMaps",'a','m',lfont);
            if(ret==1)
                setMapCount2(max_map+1);
            else if(!zqx_dmaps_fit_maps(map_count, why))
                note += "; " + why;
        }
    }

    zq_newer_file_done(false, temppath, ok, why, note);
    refresh(rALL);
    return D_O_K;
}

static int zq_newer_export_strings()
{
    if(!getname("Export String Table (.zqs)","zqs",NULL,datapath,false))
        return D_O_K;

    std::string why;
    bool ok = zqx_export_strings(temppath, why);
    zq_newer_file_done(true, temppath, ok, why, "");
    return D_O_K;
}

static int zq_newer_import_strings()
{
    if(!getname("Import String Table (.zqs)","zqs",NULL,datapath,false))
        return D_O_K;

    std::string why, note;
    bool ok = zqx_import_strings(temppath, why, note);

    if(ok)
    {
        saved=false;
        rebuild_string_list();
    }

    zq_newer_file_done(false, temppath, ok, why, note);
    refresh(rALL);
    return D_O_K;
}

static int zq_newer_export_combos()
{
    if(!getname("Export Combos (.zcombo)","zcombo",NULL,datapath,false))
        return D_O_K;

    std::string why;
    bool ok = zqx_export_combos(temppath, why);
    zq_newer_file_done(true, temppath, ok, why, "");
    return D_O_K;
}

static int zq_newer_import_combos()
{
    int page = getnumber("Import Start Page", 0);

    if(cancelgetnum)
        return D_O_K;

    bound(page, 0, COMBO_PAGES-1);

    if(!getname("Import Combos (.zcombo)","zcombo",NULL,datapath,false))
        return D_O_K;

    std::string why, note;
    bool ok = zqx_import_combos(temppath, page*COMBOS_PER_PAGE, why, note);

    if(ok)
        saved=false;

    zq_newer_file_done(false, temppath, ok, why, note);
    refresh(rALL);
    return D_O_K;
}

static int zq_newer_export_tiles()
{
    // 3.0 exports the pages asked for; 2.55.17 every tile
    int first = 0, pages = 0;

    if(zqx_is_30())
    {
        const int all = zq_tile_limit() / TILES_PER_PAGE;
        first = getnumber("Start Page", 0);

        if(cancelgetnum)
            return D_O_K;

        bound(first, 0, all-1);
        pages = getnumber("Page Count", 1);

        if(cancelgetnum)
            return D_O_K;

        bound(pages, 1, all-first);
    }

    if(!getname("Export Tiles (.ztileset)","ztileset",NULL,datapath,false))
        return D_O_K;

    std::string why;
    bool ok = zqx_export_tiles(temppath, first, pages, why);
    zq_newer_file_done(true, temppath, ok, why, "");
    return D_O_K;
}

static int zq_newer_import_tiles()
{
    int page = getnumber(zqx_is_30() ? "Destination Page" : "Import Start Page", 0);

    if(cancelgetnum)
        return D_O_K;

    bound(page, 0, zq_tile_limit()/TILES_PER_PAGE-1);

    if(!getname("Import Tiles (.ztileset)","ztileset",NULL,datapath,false))
        return D_O_K;

    std::string why, note;
    bool ok = zqx_import_tiles(temppath, page*TILES_PER_PAGE, why, note);

    if(ok)
    {
        saved=false;
        register_blank_tiles();
    }

    zq_newer_file_done(false, temppath, ok, why, note);
    refresh(rALL);
    return D_O_K;
}

static int zq_newer_export_guys()
{
    if(!getname("Export Enemies (.guy)","guy",NULL,datapath,false))
        return D_O_K;

    std::string why;
    bool ok = zqx_export_guys(temppath, why);
    zq_newer_file_done(true, temppath, ok, why, "");
    return D_O_K;
}

static int zq_newer_import_guys()
{
    if(!getname("Import Enemies (.guy)","guy",NULL,datapath,false))
        return D_O_K;

    std::string why, note;
    bool ok = zqx_import_guys(temppath, why, note);

    if(ok)
        saved=false;

    zq_newer_file_done(false, temppath, ok, why, note);
    refresh(rALL);
    return D_O_K;
}

static int zq_newer_export_zgp()
{
    if(!getname("Export Graphics Pack (.zgp)","zgp",NULL,datapath,false))
        return D_O_K;

    std::string why;
    bool ok = zqx_export_zgp(temppath, why);
    zq_newer_file_done(true, temppath, ok, why, "");
    return D_O_K;
}

static int zq_newer_import_zgp()
{
    if(!getname("Import Graphics Pack (.zgp)","zgp",NULL,datapath,false))
        return D_O_K;

    std::string why, note;
    bool ok = zqx_import_zgp(temppath, why, note);

    if(ok)
    {
        saved=false;
        register_blank_tiles();
        loadlvlpal(Color);
        setup_combo_animations();
        setup_combo_animations2();
    }

    zq_newer_file_done(false, temppath, ok, why, note);
    refresh(rALL);
    refresh_pal();
    return D_O_K;
}

// The whole quest in its own version, unencoded: .zqt packed, .qsu not.
static int zq_newer_export_quest(bool packed)
{
    if(!getname(packed ? "Export Quest Template (.zqt)" : "Export Unencoded Quest (.qsu)", packed ? "zqt" : "qsu",NULL,datapath,false))
        return D_O_K;

    std::string why;
    bool ok = zqx_export_quest(temppath, packed, why);
    zq_newer_file_done(true, temppath, ok, why, "");
    return D_O_K;
}

// A quest template or unencoded quest in place of this quest, as 2.53 imports them (everything
// is the file's, the quest keeps its file name), when it was made in this quest's version or a
// newer one: the quest is then saved in that version, never an older one.
static int zq_newer_import_quest(bool packed)
{
    if(!getname(packed ? "Import Quest Template (.zqt)" : "Import Unencoded Quest (.qsu)", packed ? "zqt" : "qsu",NULL,datapath,false))
        return D_O_K;

    std::string why, made;

    if(!zqx_quest_not_older(temppath, packed, made, why))
    {
        zq_newer_file_done(false, temppath, false, why, "");
        return D_O_K;
    }

    saved=false;
    int ret = load_quest(temppath, packed, false);

    if(ret != qe_OK && ret != qe_cancel)
    {
        char buf[300],name[256];
        extract_name(temppath,name,FILENAMEALL,sizeof(name));
        sprintf(buf,"Unable to load %s",name);
        jwin_alert("Error",buf,NULL,NULL,"O&K",NULL,'k',0,lfont);
    }
    else if(ret == qe_OK && !packed)
    {
        // The quest keeps its own file: File > Save writes a .qst packed, as upstream always
        // does, not as the unencoded file that was imported is stored (a .qsu stays unpacked).
        const char *ext = get_extension(filepath);
        zq_quest_file.compressed = !(ext && stricmp(ext, "qsu") == 0);
    }

    register_blank_tiles();
    loadlvlpal(Color);
    setup_combo_animations();
    setup_combo_animations2();
    Map.setCurrMap(Map.getCurrMap());                         // for bound checking
    refresh(rALL);
    refresh_pal();
    return D_O_K;
}

// ---- saving in the version a quest was made in (port/zqeditor/zq_writers.h) ----------

// Saves in the version the quest was made in, unless that's 2.53.1 (ZQuest's own format,
// saved by its 2.53 writers as always) or ZQuest has no copy of the file to start from.
static bool zq_saves_own_format()
{
    if(zq_quest_file.parts.empty() || zq_quest_file.inexact)
        return false;

    return !(zq_quest_file.zelda_version == ZELDA_VERSION && zq_quest_file.build == VERSION_BUILD);
}

static std::string zq_made_in()
{
    std::string v = zc_quest_built_with(zq_quest_file.zelda_version, zq_quest_file.build);
    return v.empty() ? std::string("its own version") : v;
}

// A file of 2.53 or older holds 252 pages of tiles, and ZQuest's 2.53 writers keep tile numbers
// in 16 bits (zq_tile_refs.h). True when the quest fits such a file; otherwise says why, and
// nothing is saved. what: "saved as 2.53.1", "exported".
static bool zq_fits_253(const char *what)
{
    std::string why;

    if(zq_tiles_fit_253(why))
        return true;

    std::string line1 = std::string("This quest can't be ") + what + ":";
    why += ".";
    jwin_alert("ZQuest", line1.c_str(), why.c_str(), "Files of 2.53 and older hold tiles up to 65,519. Nothing was saved.", "O&K", NULL, 'k', 0, lfont);
    return false;
}

// 0: saved. 1: not saved, and the user was told why. 2: the user chose 2.53.1 instead.
static int zq_save_own(const char *path)
{
    std::string why;
    int r = zq_save_own_format(path, true, why);

    if(r == 0)
        return 0;

    std::string v = zq_made_in();
    std::string line1 = "This quest can't be saved as " + v + " yet.";

    // a tile past 65,519 (zq_tile_refs.h); 2.53.1 can't take it either
    if(r == 8)
    {
        std::string what = "saved as " + v;
        zq_fits_253(what.c_str());
        return 1;
    }

    if(r == 2 && !zq255_quest_is_newer())
    {
        if(jwin_alert("ZQuest", line1.c_str(), why.c_str(), "Save it as 2.53.1 instead?", "&Yes", "&No", 'y', 'n', lfont) == 1)
            return 2;

        return 1;
    }

    jwin_alert("ZQuest", line1.c_str(), why.c_str(), "Nothing was saved.", "O&K", NULL, 'k', 0, lfont);
    return 1;
}

// The versions Save As offers: the quest's own, or newer ones (never an older one).
enum { ZQV_CANCEL, ZQV_OWN, ZQV_253, ZQV_255, ZQV_30 };

// Whether a quest was made in 2.55.18 or a later 2.55 release: "As 2.55.19" isn't offered for it.
// 2.55.19 writes every section in 2.55.18's formats, so such a quest is already in them (a quest
// made in 2.55.19 or later would only be marked as older), and its own version keeps it as made.
// A quest made in 2.55.17 or before is offered it: its sections come up to 2.55.18's formats,
// and the header says 2.55.19 saved it, as 2.55.19 itself does.
static bool zq_made_in_255_release(const std::string &made, long patch)
{
    if(made.compare(0, 5, "2.55.") != 0)
        return false;

    const char *p = made.c_str() + 5;
    char *end = NULL;
    long n = strtol(p, &end, 10);
    return end != p && *end == 0 && n >= patch;
}

static int zq_choose_version()
{
    std::string made = zc_quest_built_with(header.zelda_version, header.build);
    bool is253 = header.zelda_version == ZELDA_VERSION && header.build == VERSION_BUILD;
    bool newer = zq255_quest_is_newer();
    bool is30 = newer && made.compare(0, 2, "3.") == 0;
    bool is25518 = newer && zq_made_in_255_release(made, 18);
    std::vector<int> up;    // the newer versions on offer, oldest first

    if(!newer && !is253)
        up.push_back(ZQV_253);
    if(!is30 && !is25518)
        up.push_back(ZQV_255);
    // a quest made in a 3.0 prerelease whose parts are older than 3.0 Nightly's: still newer
    if(!is30 || zq_newer_upgrades(ZQ_NEWER_30))
        up.push_back(ZQV_30);

    if(up.empty())
        return ZQV_OWN;

    // a long version (a 3.0 prerelease's "3.0.0-prerelease.211+2026-08-09") would push the other
    // buttons out of the dialog: the line above names it
    std::string own = "As " + ((made.empty() || made.size() > 20) ? std::string("its own version") : made);
    std::string line = "It was made in " + (made.empty() ? std::string("an unknown version") : made) + ".";
    int c = jwin_alert3("Save Quest As", "Which version of ZC should this file be for?", line.c_str(), NULL,
                        own.c_str(), "As a newer version", "Cancel", 0, 0, 0, lfont);

    if(c == 1)
        return ZQV_OWN;
    if(c != 2)
        return ZQV_CANCEL;

    int pick = up[0];

    if(up.size() > 1)
    {
        const char *label[4] = { "", "", "As 2.53.1", "As 2.55.19" };
        const char *b[3] = { NULL, NULL, NULL };

        for(size_t i = 0; i < up.size(); ++i)
            b[i] = up[i] == ZQV_30 ? "As 3.0 Nightly" : label[up[i] == ZQV_253 ? 2 : 3];

        int d = up.size() == 3 ? jwin_alert3("Save Quest As", "Which newer version?", NULL, NULL, b[0], b[1], b[2], 0, 0, 0, lfont)
                               : jwin_alert3("Save Quest As", "Which newer version?", NULL, NULL, b[0], b[1], "Cancel", 0, 0, 0, lfont);

        if(d < 1 || d > (int)up.size())
            return ZQV_CANCEL;

        pick = up[d - 1];
    }

    if(pick == ZQV_255 || pick == ZQV_30)
    {
        const char *t = pick == ZQV_255 ? "2.55.19" : "3.0 Nightly";
        std::string q = std::string("Older versions of ZC can't open a ") + t + " quest file.";
        std::string a = std::string("Save this quest as ") + t + "?";

        if(jwin_alert("Save Quest As", q.c_str(), a.c_str(), NULL, "&Yes", "&No", 'y', 'n', lfont) != 1)
            return ZQV_CANCEL;
    }

    return pick;
}

// After saving as a newer version: opens that file as File > Open would, so what is edited from
// now on is exactly the quest as saved.
static void zq_reopen(const char *path)
{
    char p[2048];
    strncpy(p, path, sizeof(p) - 1);
    p[sizeof(p) - 1] = 0;
    int ret = load_quest(p, true, true);

    if(ret == qe_OK)
    {
        saved = true;
        strcpy(filepath, p);
        first_save = true;
    }
    else
    {
        jwin_alert("Error", "The quest was saved, but couldn't be opened again:", qst_error[ret], NULL, "O&K", NULL, 'k', 0, lfont);
        filepath[0] = 0;
    }

    setup_combo_animations();
    setup_combo_animations2();
    Map.setCurrMap(zinit.last_map);
    Map.setCurrScr(zinit.last_screen);
    Map.setCanPaste(false);
    Map.setCanUndo(false);
    rebuild_trans_table();
    rebuild_string_list();
    onDrawingModeNormal();
    refresh(rALL);
    last_timed_save[0] = 0;
}

static void zq_saved_message(const char *path, const char *as)
{
    char buf[200], name[13];
    extract_name((char *)path, name, FILENAME8_3);
    snprintf(buf, sizeof(buf), "Saved %s as %s", name, as);
    jwin_alert("ZQuest", buf, NULL, NULL, "O&K", NULL, 'k', 0, lfont);
}

// ---- File > New in any version ZQuest saves in --------------------------------------------
//
// A new quest is made as that version's own ZQuest made one, and is from then on a quest made
// in that version: Save keeps it in that version, Save As offers it or newer ones (never older).
//   2.53.1       ZQuest's new quest (qst.dat's template, then the ruleset), as always
//   2.53.0       the same quest, its header saying 2.53.0 (build 32) made it: 2.53.0 wrote every
//                section in the formats 2.53.1 writes
//   2.55.19, 3.0 Nightly
//                ZQuest's new quest saved as that version (the header, version fields and every
//                section in its newest formats, as Save As writes them) and opened from there
//   template     the new-quest file that version's ZQuest shipped (its qst.dat's template, in
//                that version's own formats), as it is: templates\<version>.qst beside the program
enum { ZQN_OWN253, ZQN_253_0, ZQN_TEMPLATE, ZQN_255, ZQN_30 };

struct ZqNewVersion
{
    const char *label;      // what the list shows
    const char *name;       // the version, for messages
    int kind;
    const char *file;       // ZQN_TEMPLATE: the template's file
};

static const ZqNewVersion zq_new_versions[] =
{
    { "2.50.1",                  "2.50.1",      ZQN_TEMPLATE, "templates/2.50.1.qst" },
    { "2.53.0",                  "2.53.0",      ZQN_253_0,    NULL },
    { "2.53.1 (this ZQuest's)",  "2.53.1",      ZQN_OWN253,   NULL },
    { "2.55.19",                 "2.55.19",     ZQN_255,      NULL },
    { "3.0 Nightly",             "3.0 Nightly", ZQN_30,       NULL },
};
static const int zq_new_version_count = (int)(sizeof(zq_new_versions) / sizeof(zq_new_versions[0]));

// The versions on offer (a template version only when its file is there), as indices above.
static std::vector<int> zq_new_offered;

static const char *zq_new_version_list(int index, int *list_size)
{
    if(index < 0)
    {
        *list_size = (int)zq_new_offered.size();
        return NULL;
    }

    return index < (int)zq_new_offered.size() ? zq_new_versions[zq_new_offered[index]].label : "";
}

static ListData zq_new_version_list_data(zq_new_version_list, &font);

static DIALOG zq_new_version_dlg[] =
{
    // (dialog proc)     (x)   (y)   (w)   (h)   (fg)     (bg)    (key)    (flags)     (d1)           (d2)     (dp)
    { jwin_win_proc,          0,      0,    220,    150,    vc(14),                vc(1),                  0,    D_EXIT,     0,           0, (void *) "New Quest",           NULL,   NULL  },
    { jwin_text_proc,        12,     26,    196,      8,    vc(14),                vc(1),                  0,    0,          0,           0, (void *) "Which version of ZC is this quest for?", NULL, NULL },
    { jwin_list_proc,        12,     40,    196,     68,    jwin_pal[jcBOXFG],     jwin_pal[jcLIGHT],      0,    D_EXIT,     0,           0, (void *) &zq_new_version_list_data, NULL, NULL },
    { jwin_button_proc,      40,    118,     61,     21,    vc(14),                vc(1),                 13,    D_EXIT,     0,           0, (void *) "OK",                  NULL,   NULL  },
    { jwin_button_proc,     120,    118,     61,     21,    vc(14),                vc(1),                 27,    D_EXIT,     0,           0, (void *) "Cancel",              NULL,   NULL  },
    { d_timer_proc,           0,      0,      0,      0,    0,                     0,                      0,    0,          0,           0,  NULL,                             NULL,   NULL  },
    { NULL,                   0,      0,      0,      0,    0,                     0,                      0,    0,          0,           0,  NULL,                             NULL,   NULL  }
};

// An index into zq_new_versions, or -1 when the user cancelled.
static int zq_pick_new_version()
{
    zq_new_offered.clear();
    int dflt = 0;

    for(int i = 0; i < zq_new_version_count; ++i)
    {
        if(zq_new_versions[i].kind == ZQN_TEMPLATE && !exists(zq_new_versions[i].file))
            continue;

        if(zq_new_versions[i].kind == ZQN_OWN253)
            dflt = (int)zq_new_offered.size();

        zq_new_offered.push_back(i);
    }

    static bool sized = false;
    zq_new_version_dlg[0].dp2 = lfont;

    if(is_large && !sized)
    {
        large_dialog(zq_new_version_dlg);
        sized = true;
    }

    zq_new_version_dlg[2].d1 = dflt;
    zq_new_version_dlg[2].d2 = 0;
    int ret = zc_popup_dialog(zq_new_version_dlg, 2);

    if(ret != 2 && ret != 3)
        return -1;

    int pick = zq_new_version_dlg[2].d1;
    return pick >= 0 && pick < (int)zq_new_offered.size() ? zq_new_offered[pick] : -1;
}

static bool zq_new_version_uses_template(int v)
{
    return v >= 0 && v < zq_new_version_count && zq_new_versions[v].kind == ZQN_TEMPLATE;
}

// The quest just made is a new one: no file yet, so Save asks where (and in which version).
static void zq_new_untitled(const char *version)
{
    memset(filepath, 0, 255);
    first_save = false;
    saved = true;
    char buf[256];
    snprintf(buf, sizeof(buf), ZQ_WINDOW_NAME " - Untitled Quest (ZC %s)", version);
    set_window_title(buf);
    refresh(rALL);
    last_timed_save[0] = 0;
}

static void zq_new_quest_as(int v)
{
    if(v < 0 || v >= zq_new_version_count)
        return;

    const ZqNewVersion &nv = zq_new_versions[v];

    switch(nv.kind)
    {
    case ZQN_OWN253:
        return;

    case ZQN_253_0:
        if(!zq_new_quest_stamp(0x250, 32))
            jwin_alert("ZQuest", "The new quest couldn't be made for 2.53.0.", "It is a 2.53.1 quest.", NULL, "O&K", NULL, 'k', 0, lfont);
        else
            zq_new_untitled(nv.name);

        return;

    case ZQN_TEMPLATE:
    {
        char p[2048];
        strncpy(p, nv.file, sizeof(p) - 1);
        p[sizeof(p) - 1] = 0;
        int ret = load_quest(p, true, true);

        if(ret != qe_OK)
        {
            jwin_alert("Error", "The new-quest file for this version couldn't be opened:", qst_error[ret], NULL, "O&K", NULL, 'k', 0, lfont);
            NewQuestFile(0);
            return;
        }

        setup_combo_animations();
        setup_combo_animations2();
        zinit.last_map = 0;
        zinit.last_screen = 0;
        Map.setCurrMap(0);
        Map.setCurrScr(0);
        Map.setCanPaste(false);
        Map.setCanUndo(false);
        rebuild_trans_table();
        rebuild_string_list();
        onDrawingModeNormal();
        zq_new_untitled(nv.name);
        return;
    }

    case ZQN_255:
    case ZQN_30:
    {
        const int target = nv.kind == ZQN_255 ? ZQ_NEWER_255 : ZQ_NEWER_30;
        char tmp[64];
        temp_name(tmp);
        std::string path = std::string(tmp) + ".qst";
        std::string why;
        int r = zq_save_newer(path.c_str(), target, why);

        if(r != 0)
        {
            std::string line1 = std::string("The new quest couldn't be made for ") + nv.name + ":";
            jwin_alert("ZQuest", line1.c_str(), why.c_str(), "It is a 2.53.1 quest.", "O&K", NULL, 'k', 0, lfont);
            remove(path.c_str());
            return;
        }

        zq_reopen(path.c_str());
        remove(path.c_str());
        remove(zq_zinfo_path(path.c_str()).c_str());
        Map.setCurrMap(0);
        Map.setCurrScr(0);
        zq_new_untitled(nv.name);
        return;
    }
    }
}
#endif

int onSave()
{
    restore_mouse();

    if(disable_saving)
    {
        jwin_alert("ZQuest","Saving is","disabled in this version.",NULL,"O&K",NULL,'k',0,lfont);
        return D_O_K;
    }

    if(!first_save)
        return onSaveAs();
    else if(OverwriteProtection)
    {
        jwin_alert("ZQuest","Overwriting quests is disabled.","Change this in the options dialog.",NULL,"O&K",NULL,'k',0,lfont);
        return D_O_K;
    }
    
#ifdef ZC_ZQEDITOR
    // A quest is saved in the version it was made in; 2.55 and 3.0 Nightly quests never older.
    if(zq_saves_own_format())
    {
        int r = zq_save_own(filepath);

        if(r == 0)
        {
            zq_saved_message(filepath, zq_made_in().c_str());
            saved=true;
            first_save=true;
            header.dirty_password=false;
            last_timed_save[0]=0;
            return D_O_K;
        }

        if(r != 2)
            return D_O_K;
    }
    else if(zq255_quest_is_newer())
        return zq255_saving_not_ready();

    if(!zq_fits_253("saved as 2.53.1"))
        return D_O_K;
#endif
    int ret = save_quest(filepath, false);
    char buf[80],name[13];
    extract_name(filepath,name,FILENAME8_3);

    if(!ret)
    {
#ifdef ZC_ZQEDITOR
        zq_sections_forget();           // the file is a 2.53.1 quest now
        zq_set_quest_title(filepath);
#endif
        sprintf(buf,"Saved %s",name);
        jwin_alert("ZQuest",buf,NULL,NULL,"O&K",NULL,'k',0,lfont);
        saved=true;
        first_save=true;
        header.dirty_password=false;
    }
    else
    {
        sprintf(buf,"Error saving %s",name);
        jwin_alert("Error",buf,NULL,NULL,"O&K",NULL,'k',0,lfont);
    }
    
    last_timed_save[0]=0;
    return D_O_K;
}

int onSaveAs()
{
    if(disable_saving)
    {
        jwin_alert("ZQuest","Saving is","disabled in this version.",NULL,"O&K",NULL,'k',0,lfont);
        return D_O_K;
    }

#ifdef ZC_ZQEDITOR
    if(zq255_quest_is_newer() && !zq_saves_own_format())
        return zq255_saving_not_ready();
#endif

    if(!getname("Save Quest As (.qst)","qst",NULL,filepath,true))
        return D_O_K;
        
    if(exists(temppath))
    {
        if(OverwriteProtection)
        {
            jwin_alert("ZQuest","Overwriting quests is disabled.","Change this in the options dialog.",NULL,"O&K",NULL,'k',0,lfont);
            return D_O_K;
        }
        
        if(jwin_alert("Confirm Overwrite",temppath,"already exists.","Write over existing file?","&Yes","&No",'y','n',lfont)==2)
        {
            return D_O_K;
        }
    }
    
#ifdef ZC_ZQEDITOR
    // The version it was made in, or a newer one; never an older one.
    {
        int v = zq_choose_version();

        if(v == ZQV_CANCEL)
            return D_O_K;

        if(v == ZQV_255 || v == ZQV_30)
        {
            int target = v == ZQV_255 ? ZQ_NEWER_255 : ZQ_NEWER_30;
            std::string why;
            int r = zq_save_newer(temppath, target, why);

            if(r != 0)
            {
                // 8: parts newer than the target holds, which no converter will ever write older
                std::string line1 = std::string("This quest can't be saved as ") + zq_newer_target_name(target) + (r == 8 ? "." : " yet.");
                jwin_alert("ZQuest", line1.c_str(), why.c_str(), "Nothing was saved.", "O&K", NULL, 'k', 0, lfont);
                return D_O_K;
            }

            // from now on the quest is that file, in that version
            zq_reopen(temppath);
            zq_saved_message(temppath, zq_newer_target_name(target));
            return D_O_K;
        }

        if(v == ZQV_OWN && zq_saves_own_format())
        {
            int r = zq_save_own(temppath);

            if(r == 0)
            {
                strcpy(filepath,temppath);
                zq_set_quest_title(filepath);
                zq_saved_message(filepath, zq_made_in().c_str());
                saved=true;
                first_save=true;
                header.dirty_password=false;
                last_timed_save[0]=0;
                return D_O_K;
            }

            if(r != 2)
                return D_O_K;
        }
        // otherwise 2.53.1: ZQuest's own 2.53 save, below
        if(!zq_fits_253("saved as 2.53.1"))
            return D_O_K;
    }
#endif
    int ret = save_quest(temppath, false);
    char buf[1024],name[13];
    extract_name(temppath,name,FILENAME8_3);
    
    if(!ret)
    {
        strcpy(filepath,temppath);
#ifdef ZC_ZQEDITOR
        zq_sections_forget();           // the file is a 2.53.1 quest now
        zq_set_quest_title(filepath);
#else
        sprintf(buf,ZQ_WINDOW_NAME " - [%s]", get_filename(filepath));
        set_window_title(buf);
#endif
        sprintf(buf,"Saved %s",name);
        jwin_alert("ZQuest",buf,NULL,NULL,"O&K",NULL,'k',0,lfont);
        saved=true;
        first_save=true;
        header.dirty_password=false;
    }
    else
    {
        sprintf(buf,"Error saving %s",name);
        jwin_alert("Error",buf,NULL,NULL,"O&K",NULL,'k',0,lfont);
    }
    
    refresh(rMENU);
    last_timed_save[0]=0;
    return D_O_K;
}

int onOpen()
{
    bool compressed=true;
    bool encrypted=true;
    restore_mouse();
    
    if(checksave()==0)
        return D_O_K;
        
    static EXT_LIST list[] =
    {
        { (char *)"Quest Files (*.qst)", (char *)"qst"                                     },
        { (char *)"Unencrypted Quest Files (*.qsu)", (char *)"qsu"                                     },
        { (char *)"Quest Auto-backups (*.qb?)", (char *)"qb0,qb1,qb2,qb3,qb4,qb5,qb6,qb7,qb8,qb9" },
        { (char *)"Quest Timed Auto-saves (*.qt?)", (char *)"qt0,qt1,qt2,qt3,qt4,qt5,qt6,qt7,qt8,qt9" },
        { (char *)"Uncompressed Quest Timed Auto-saves (*.qu?)", (char *)"qu0,qu1,qu2,qu3,qu4,qu5,qu6,qu7,qu8,qu9" },
        { (char *)"All Files (*.*)",                             NULL                                              },
        { NULL,                                                  NULL                                              }
    };
    
    if(!getname("Load File",NULL,list,filepath,true))
        return D_O_K;
        
    char ext[2048];
    char ext2[5];
    strcpy(ext,get_extension(temppath));
    strupr(ext);
    
    for(int i=0; i<10; ++i)
    {
        sprintf(ext2,"qu%d",i);
        
        if(stricmp(ext,ext2)==0)
        {
            compressed=false;
            encrypted=false;
            break;
        }
    }
    
    if(stricmp(ext,"qsu")==0)
    {
        compressed=false;
        encrypted=false;
    }
    
    
    int ret = load_quest(temppath, compressed, encrypted);
    
    if(ret == qe_OK)
    {
        saved = true;
        strcpy(filepath, temppath);
        first_save=true;
    }
    else if(ret == qe_cancel)
    {
        saved = true;
        filepath[0]=0;
    }
    else
    {
        char buf[80],name[13];
        extract_name(temppath,name,FILENAME8_3);
        sprintf(buf,"Unable to load %s",name);
        jwin_alert("Error",buf,qst_error[ret],NULL,"O&K",NULL,'k',0,lfont);
        filepath[0]=0;
    }
    
    setup_combo_animations();
    setup_combo_animations2();
    Map.setCurrMap(zinit.last_map);
    Map.setCurrScr(zinit.last_screen);
    Map.setCanPaste(false);
    Map.setCanUndo(false);
    rebuild_trans_table();
    rebuild_string_list();
    onDrawingModeNormal();
    refresh(rALL);
    last_timed_save[0]=0;
    return D_O_K;
}

int onRevert()
{
    if(jwin_alert("Confirm Revert","Are you sure you want to lose","all changes since last save?",NULL,"Yes","No",'y','n',lfont)==2)
    {
        return D_O_K;
    }
    
    if(filepath[0]!=0)
    {
        int ret = load_quest(filepath, true, true);
        
        if(!ret)
        {
            saved = true;
        }
        else
        {
            char buf[80],name[13];
            extract_name(filepath,name,FILENAME8_3);
            sprintf(buf,"Unable to load %s",name);
            jwin_alert("Error",buf,qst_error[ret],NULL,"O&K",NULL,'k',0,lfont);
            filepath[0]=0;
        }
        
        setup_combo_animations();
        setup_combo_animations2();
        refresh(rALL);
    }
    else
    {
        NewQuestFile(0);
        
        if(RulesetDialog > 0)
            PickRuleset();
    }
    
    onDrawingModeNormal();
    last_timed_save[0]=0;
    return D_O_K;
}

static DIALOG import_map_bias_dlg[] =
{
    // (dialog proc)     (x)   (y)   (w)   (h)   (fg)     (bg)    (key)    (flags)     (d1)           (d2)     (dp)
    { jwin_win_proc, 80,   80-8,   160+1,  64+20+1,   vc(14),  vc(1),  0,       D_EXIT,          0,             0, (void *) "Select Import Bias", NULL, NULL },
    { d_timer_proc,         0,    0,     0,    0,    0,       0,       0,       0,          0,          0,         NULL, NULL, NULL },
    { jwin_button_proc,     90,   120+10,  61,   21,   vc(14),  vc(1),  13,      D_EXIT,     0,             0, (void *) "OK", NULL, NULL },
    { jwin_button_proc,     170,  120+10,  61,   21,   vc(14),  vc(1),  27,      D_EXIT,     0,             0, (void *) "Cancel", NULL, NULL },
    { jwin_radio_proc,      130, 104-8,   96+1,    8+1,   jwin_pal[jcBOXFG],  jwin_pal[jcBOX],  0,       0,          0,             0, (void *) "Total Copy", NULL, NULL },
    { jwin_radio_proc,      130, 114-8,  120+1,    8+1,   jwin_pal[jcBOXFG],  jwin_pal[jcBOX],  0,       0,          0,             0, (void *) "Prefer Original", NULL, NULL },
    { jwin_radio_proc,      130, 124-8,  104+1,    8+1,   jwin_pal[jcBOXFG],  jwin_pal[jcBOX],  0,       0,          0,             0, (void *) "Prefer Import", NULL, NULL },
    { NULL,                 0,    0,    0,    0,   0,       0,       0,       0,          0,             0,       NULL,                           NULL,  NULL }
};

int get_import_map_bias()
{
    import_map_bias_dlg[0].dp2=lfont;
    
    for(int i=0; i<3; i++)
    {
        import_map_bias_dlg[i+4].flags=0;
    }
    
    import_map_bias_dlg[ImportMapBias+4].flags=D_SELECTED;
    
    if(is_large)
        large_dialog(import_map_bias_dlg);
        
    if(zc_popup_dialog(import_map_bias_dlg,2)==2)
    {
        for(int i=0; i<3; i++)
        {
            if(import_map_bias_dlg[i+4].flags&D_SELECTED)
            {
                ImportMapBias=i;
                break;
            }
        }
        
        return 0;
    }
    
    return -1;
}

#ifdef ZC_ZQEDITOR
// A .map file of 2.55 or 3.0 (upstream's zmap::save starts it with the map section's version, 19
// to 41, where 2.53's starts with the version of the ZC that wrote it, 0x250 for 2.53) for a quest
// of 2.53 or older: its format can't hold the file's screens, so the quest is to be saved as a
// newer version first; nothing is read. True when the file was refused (and the user told why).
static bool zq_map_file_too_new(const char *path)
{
    PACKFILE *f = pack_fopen_password(path, F_READ, "");

    if(!f)
        return false;

    short version = 0;
    const bool got = p_igetw(&version, f, true) != 0;
    pack_fclose(f);

    if(!got || version < 19 || version > 41)
        return false;

    char l1[120], l2[160];
    snprintf(l1, sizeof(l1), "This map file's screens are in map format %d: ZC 2.55 or 3.0 Nightly wrote it.", version);
    snprintf(l2, sizeof(l2), "This quest was made in ZC %s, whose file format holds screens up to format %d.", zq_made_in().c_str(), V_MAPS);

    if(jwin_alert("Import", l1, l2, "Save the quest as 2.55.19 or 3.0 Nightly first, then import the file?", "&Save As...", "&Cancel", 's', 'c', lfont) == 1)
        onSaveAs();

    return true;
}
#endif

int onImport_Map()
{
    if(Map.getCurrMap()>=Map.getMapCount())
        return D_O_K;

    if(get_import_map_bias()==-1)
    {
        return D_O_K;
    }

    if(!getname("Import Map (.map)","map",NULL,datapath,false))
        return D_O_K;

    int ret;
#ifdef ZC_ZQEDITOR
    // A 2.55 / 3.0 quest takes upstream's .map files of its version (or older, converted up),
    // into its own map section's records (zq_exports.cpp); nothing changes when one is refused.
    const bool zq_newer = zq255_quest_is_newer();
    std::string zq_why, zq_note;

    if(zq_newer)
        ret = zqx_import_map(temppath, Map.getCurrMap(), ImportMapBias, zq_why, zq_note) ? 0 : -1;
    else if(zq_map_file_too_new(temppath))
        return D_O_K;
    else
#endif
    {
    saved=false;
    ret=Map.load(temppath);
    }

    if(ret)
    {
#ifdef ZC_ZQEDITOR
        if(zq_newer)
            zq_newer_file_done(false, temppath, false, zq_why, "");
        else
#endif
        {
        char buf[80],name[13];
        extract_name(temppath,name,FILENAME8_3);
        sprintf(buf,"Unable to load %s",name);
        jwin_alert("Error",buf,loaderror[ret],NULL,"O&K",NULL,'k',0,lfont);
        }

#ifndef ZC_ZQEDITOR
        if(ret>1)
            Map.clearmap(false);
#endif
        // (ZQuest: Map.load put the map back as it was; nothing was imported)
    }
    else
    {
#ifdef ZC_ZQEDITOR
        if(zq_newer)
            Map.setCurrScr(0);  // as zmap::load
#endif
        bool willaffectlayers=false;
        
        for(int i=0; i<MAPSCRS; i++)
        {
            for(int j=0; !willaffectlayers && j<6; j++)
            {
                if(TheMaps[Map.getCurrMap()*MAPSCRS+i].layermap[j]>Map.getMapCount())
                {
                    willaffectlayers=true;
                    break;
                }
            }
            
            fix_layers(&TheMaps[Map.getCurrMap()*MAPSCRS+i], false);
        }
        
        if(willaffectlayers)
        {
            jwin_alert("Layers Changed",
                       "One or more screens in the imported map had",
                       "layers on maps that do not exist. The map numbers",
                       "of the affected layers will be reset to 0.",
                       "&OK", NULL, 'o', 0, lfont);
        }
#ifdef ZC_ZQEDITOR
        if(zq_newer)
            zq_newer_file_done(false, temppath, true, "", zq_note);
#endif
    }

    refresh(rSCRMAP+rMAP+rMENU);
    return D_O_K;
}

int onExport_Map()
{
    if(Map.getCurrMap()>=Map.getMapCount())
        return D_O_K;

    if(!getname("Export Map (.map)","map",NULL,datapath,false))
        return D_O_K;

#ifdef ZC_ZQEDITOR
    // a 2.55 / 3.0 quest: upstream's .map, in the quest's own map section version
    if(zq255_quest_is_newer())
    {
        std::string why;
        const bool ok = zqx_export_map(temppath, Map.getCurrMap(), why);
        zq_newer_file_done(true, temppath, ok, why, "");
        return D_O_K;
    }
#endif

    int ret = Map.save(temppath);
    char buf[80],buf2[80],name[13];
    extract_name(temppath,name,FILENAME8_3);
    
    if(!ret)
    {
        sprintf(buf,"ZQuest");
        sprintf(buf2,"Saved %s",name);
    }
    else
    {
        sprintf(buf,"Error");
        sprintf(buf2,"Error saving %s",name);
    }
    
    jwin_alert(buf,buf2,NULL,NULL,"O&K",NULL,'k',0,lfont);
    return D_O_K;
}

#ifdef ZC_ZQEDITOR
// An import file with a section in a newer format than 2.53's loaders read (they are what reads
// these files, converting an older format up as they go): a newer ZC wrote it. A quest of 2.53 or
// older couldn't hold it in its own format either, so it is told to be saved as a newer version
// first; nothing is read. True when the file was refused (and the user told why).
static bool zq_import_too_new(const char *path)
{
    static const struct { dword id; int newest; const char *what; } known[] =
    {
        { ID_DMAPS, V_DMAPS, "DMaps" }, { ID_STRINGS, V_STRINGS, "strings" }, { ID_COMBOS, V_COMBOS, "combos" },
        { ID_TILES, V_TILES, "tiles" }, { ID_GUYS, V_GUYS, "enemies" }, { ID_COMBOALIASES, V_COMBOALIASES, "combo aliases" },
        { ID_CSETS, V_CSETS, "palettes" }, { ID_SUBSCREEN, V_SUBSCREEN, "subscreens" }, { ID_ITEMS, V_ITEMS, "items" },
        { ID_WEAPONS, V_WEAPONS, "weapon sprites" }, { ID_ICONS, V_ICONS, "game icons" }, { ID_COLORS, V_COLORS, "misc colours" },
        { ID_DOORS, V_DOORS, "door combo sets" }
    };
    PACKFILE *f = pack_fopen_password(path, F_READ, "");

    if(!f)
        return false;

    dword id = 0;
    word v = 0, cv = 0;
    const char *what = NULL;
    int file_v = 0, newest = 0;
    bool more = p_mgetl(&id, f, true);

    // 2.55 and 3.0's own DMap, combo and tile files (.zdmap, .zcombo, .ztileset) start with that
    // editor's version, 0x255, as a 32-bit number (after a -1 for DMaps), not with a section
    const bool newer_file = more && (id == 0xFFFFFFFF || id == 0x55020000);

    if(newer_file)
        more = false;

    // a graphics pack: its own version, then its sections one after another
    if(more && id == ID_GRAPHICSPACK)
        more = p_igetw(&v, f, true) && p_igetw(&cv, f, true) && p_mgetl(&id, f, true);

    while(more && !what)
    {
        dword size = 0;
        size_t k = 0;

        while(k < sizeof(known) / sizeof(known[0]) && known[k].id != id)
            ++k;

        if(k == sizeof(known) / sizeof(known[0]) || !p_igetw(&v, f, true) || !p_igetw(&cv, f, true) || !p_igetl(&size, f, true))
            break;

        if(v > known[k].newest)
        {
            what = known[k].what;
            file_v = v;
            newest = known[k].newest;
            break;
        }

        more = size > 0 && pack_fseek(f, size) == 0 && p_mgetl(&id, f, true);
    }

    pack_fclose(f);

    if(!what && !newer_file)
        return false;

    char l1[160], l2[200];

    if(newer_file)
        snprintf(l1, sizeof(l1), "This file was written by ZC 2.55 or 3.0 Nightly, in that version's format.");
    else
        snprintf(l1, sizeof(l1), "This file's %s are in format %d: a newer version of ZC wrote it.", what, file_v);

    if(zq255_quest_is_newer())
    {
        if(newer_file)
            snprintf(l2, sizeof(l2), "ZQuest reads this kind of file in 2.53's format only so far.");
        else
            snprintf(l2, sizeof(l2), "ZQuest reads this kind of file up to 2.53's format (%d) so far.", newest);

        jwin_alert("Import", l1, l2, "Nothing was imported.", "O&K", NULL, 'k', 0, lfont);
        return true;
    }

    if(newer_file)
        snprintf(l2, sizeof(l2), "This quest was made in ZC %s, whose file format can't hold it.", zq_made_in().c_str());
    else
        snprintf(l2, sizeof(l2), "This quest was made in ZC %s, whose file format holds %s up to format %d.", zq_made_in().c_str(), what, newest);

    if(jwin_alert("Import", l1, l2, "Save the quest as 2.55.19 or 3.0 Nightly first, then import the file?", "&Save As...", "&Cancel", 's', 'c', lfont) == 1)
        onSaveAs();

    return true;
}
#endif

int onImport_DMaps()
{
#ifdef ZC_ZQEDITOR
    if(zq255_quest_is_newer())
        return zq_newer_import_dmaps();
#endif
    if(!getname("Import DMaps (.dmp)","dmp",NULL,datapath,false))
        return D_O_K;
#ifdef ZC_ZQEDITOR
    if(zq_import_too_new(temppath))
        return D_O_K;
#endif
        
    saved=false;
    
    if(!load_dmaps(temppath,0))
    {
        char buf[80],name[13];
        extract_name(temppath,name,FILENAME8_3);
        sprintf(buf,"Unable to load %s",name);
        jwin_alert("Error",buf,NULL,NULL,"O&K",NULL,'k',0,lfont);
    }
    else
    {
        int maxMap=0;
        for(int i=0; i<MAXDMAPS; i++)
        {
            if(DMaps[i].map>maxMap)
                maxMap=DMaps[i].map;
        }
        
        if(maxMap>map_count)
        {
            int ret=jwin_alert("Not enough maps",
                               "The imported DMaps use more maps than are",
                               " currently available. Do you want to add",
                               "more maps or change the DMaps' settings?",
                               "&Add maps","&Modify DMaps",'a','m',lfont);
            if(ret==1)
                setMapCount2(maxMap+1);
            else
            {
                for(int i=0; i<MAXDMAPS; i++)
                {
                    if(DMaps[i].map>=map_count)
                        DMaps[i].map=0;
                }
            }
        }
    }
    
    return D_O_K;
}

int onExport_DMaps()
{
#ifdef ZC_ZQEDITOR
    if(zq255_quest_is_newer())
        return zq_newer_export_dmaps();
#endif
    if(!getname("Export DMaps (.dmp)","dmp",NULL,datapath,false))
        return D_O_K;
        
    char buf[80],buf2[80],name[13];
    extract_name(temppath,name,FILENAME8_3);
    
    if(save_dmaps(temppath))
    {
        sprintf(buf,"ZQuest");
        sprintf(buf2,"Saved %s",name);
    }
    else
    {
        sprintf(buf,"Error");
        sprintf(buf2,"Error saving %s",name);
    }
    
    jwin_alert(buf,buf2,NULL,NULL,"O&K",NULL,'k',0,lfont);
    return D_O_K;
}

int onImport_Pals()
{
    if(!getname("Import Palettes (.zpl)","zpl",NULL,datapath,false))
        return D_O_K;
#ifdef ZC_ZQEDITOR
    if(zq_import_too_new(temppath))
        return D_O_K;
#endif
        
    saved=false;
    
    if(!load_pals(temppath,0))
    {
        char buf[80],name[13];
        extract_name(temppath,name,FILENAME8_3);
        sprintf(buf,"Unable to load %s",name);
        jwin_alert("Error",buf,NULL,NULL,"O&K",NULL,'k',0,lfont);
    }
    
    return D_O_K;
}

int onExport_Pals()
{
    if(!getname("Export Palettes (.zpl)","zpl",NULL,datapath,false))
        return D_O_K;
        
    char buf[80],buf2[80],name[13];
    extract_name(temppath,name,FILENAME8_3);
    
    if(save_pals(temppath))
    {
        sprintf(buf,"ZQuest");
        sprintf(buf2,"Saved %s",name);
    }
    else
    {
        sprintf(buf,"Error");
        sprintf(buf2,"Error saving %s",name);
    }
    
    jwin_alert(buf,buf2,NULL,NULL,"O&K",NULL,'k',0,lfont);
    return D_O_K;
}

int onImport_Msgs()
{
#ifdef ZC_ZQEDITOR
    if(zq255_quest_is_newer())
        return zq_newer_import_strings();
#endif
    if(!getname("Import String Table (.zqs)","zqs",NULL,datapath,false))
        return D_O_K;
#ifdef ZC_ZQEDITOR
    if(zq_import_too_new(temppath))
        return D_O_K;
#endif
        
    saved=false;
    
    if(!load_msgstrs(temppath,0))
    {
        char buf[80],name[13];
        extract_name(temppath,name,FILENAME8_3);
        sprintf(buf,"Unable to load %s",name);
        jwin_alert("Error",buf,NULL,NULL,"O&K",NULL,'k',0,lfont);
    }
    
    return D_O_K;
}

int onExport_Msgs()
{
#ifdef ZC_ZQEDITOR
    if(zq255_quest_is_newer())
        return zq_newer_export_strings();
#endif
    if(!getname("Export String Table (.zqs)","zqs",NULL,datapath,false))
        return D_O_K;
        
    char buf[80],buf2[80],name[13];
    extract_name(temppath,name,FILENAME8_3);
    
    if(save_msgstrs(temppath))
    {
        sprintf(buf,"ZQuest");
        sprintf(buf2,"Saved %s",name);
    }
    else
    {
        sprintf(buf,"Error");
        sprintf(buf2,"Error saving %s",name);
    }
    
    jwin_alert(buf,buf2,NULL,NULL,"O&K",NULL,'k',0,lfont);
    return D_O_K;
}


int onExport_MsgsText()
{
    if(!getname("Export Text Dump (.txt)","txt",NULL,datapath,false))
        return D_O_K;
        
    char buf[80],buf2[80],name[13];
    extract_name(temppath,name,FILENAME8_3);
    
    if(save_msgstrs_text(temppath))
    {
        sprintf(buf,"ZQuest");
        sprintf(buf2,"Saved %s",name);
    }
    else
    {
        sprintf(buf,"Error");
        sprintf(buf2,"Error saving %s",name);
    }
    
    jwin_alert(buf,buf2,NULL,NULL,"O&K",NULL,'k',0,lfont);
    return D_O_K;
}

int onImport_Combos()
{
#ifdef ZC_ZQEDITOR
    if(zq255_quest_is_newer())
        return zq_newer_import_combos();
#endif
    int ret=getnumber("Import Start Page",0);
    
    if(cancelgetnum)
    {
        return D_O_K;
    }
    
    bound(ret,0,COMBO_PAGES-1);
    
    if(!getname("Import Combo Table (.cmb)","cmb",NULL,datapath,false))
        return D_O_K;
#ifdef ZC_ZQEDITOR
    if(zq_import_too_new(temppath))
        return D_O_K;
#endif
        
    if(!load_combos(temppath, ret*COMBOS_PER_PAGE))
    {
        // if(!load_combos(temppath)) {
        char buf[80],name[13];
        extract_name(temppath,name,FILENAME8_3);
        sprintf(buf,"Unable to load %s",name);
        jwin_alert("Error",buf,NULL,NULL,"O&K",NULL,'k',0,lfont);
    }
    else
        saved=false;
        
    refresh(rALL);
    return D_O_K;
}

int onExport_Combos()
{
#ifdef ZC_ZQEDITOR
    if(zq255_quest_is_newer())
        return zq_newer_export_combos();
#endif
    if(!getname("Export Combo Table (.cmb)","cmb",NULL,datapath,false))
        return D_O_K;
        
    char buf[80],buf2[80],name[13];
    extract_name(temppath,name,FILENAME8_3);
    
    if(save_combos(temppath))
    {
        sprintf(buf,"ZQuest");
        sprintf(buf2,"Saved %s",name);
    }
    else
    {
        sprintf(buf,"Error");
        sprintf(buf2,"Error saving %s",name);
    }
    
    jwin_alert(buf,buf2,NULL,NULL,"O&K",NULL,'k',0,lfont);
    return D_O_K;
}

int onImport_Tiles()
{
#ifdef ZC_ZQEDITOR
    if(zq255_quest_is_newer())
        return zq_newer_import_tiles();
#endif
    int ret=getnumber("Import Start Page",0);
    
    if(cancelgetnum)
    {
        return D_O_K;
    }
    
#ifdef ZC_ZQEDITOR
    bound(ret,0,zq_tile_limit()/TILES_PER_PAGE-1);
#else
    bound(ret,0,TILE_PAGES-1);
#endif
    
    if(!getname("Import Tiles (.til)","til",NULL,datapath,false))
        return D_O_K;
#ifdef ZC_ZQEDITOR
    if(zq_import_too_new(temppath))
        return D_O_K;
#endif
        
    saved=false;
    
    // usetiles=true;
    if(!load_tiles(temppath, ret*TILES_PER_PAGE))
    {
        char buf[80],name[13];
        extract_name(temppath,name,FILENAME8_3);
        sprintf(buf,"Unable to load %s",name);
        jwin_alert("Error",buf,NULL,NULL,"O&K",NULL,'k',0,lfont);
    }
    
    refresh(rALL);
    return D_O_K;
}

int onExport_Tiles()
{
#ifdef ZC_ZQEDITOR
    if(zq255_quest_is_newer())
        return zq_newer_export_tiles();
#endif
    if(!getname("Export Tiles (.til)","til",NULL,datapath,false))
        return D_O_K;
        
    char buf[80],buf2[80],name[13];
    extract_name(temppath,name,FILENAME8_3);
    
    if(save_tiles(temppath))
    {
        sprintf(buf,"ZQuest");
        sprintf(buf2,"Saved %s",name);
    }
    else
    {
        sprintf(buf,"Error");
        sprintf(buf2,"Error saving %s",name);
    }
    
    jwin_alert(buf,buf2,NULL,NULL,"O&K",NULL,'k',0,lfont);
    return D_O_K;
}

int onImport_Guys()
{
#ifdef ZC_ZQEDITOR
    if(zq255_quest_is_newer())
        return zq_newer_import_guys();
#endif
    if(!getname("Import Enemies (.guy)","guy",NULL,datapath,false))
        return D_O_K;
#ifdef ZC_ZQEDITOR
    if(zq_import_too_new(temppath))
        return D_O_K;
#endif
        
    if(!load_guys(temppath))
    {
        char buf[80],name[13];
        extract_name(temppath,name,FILENAME8_3);
        sprintf(buf,"Unable to load %s",name);
        jwin_alert("Error",buf,NULL,NULL,"O&K",NULL,'k',0,lfont);
    }
    
    refresh(rALL);
    return D_O_K;
}

int onExport_Guys()
{
#ifdef ZC_ZQEDITOR
    if(zq255_quest_is_newer())
        return zq_newer_export_guys();
#endif
    if(!getname("Export Enemies (.guy)","guy",NULL,datapath,false))
        return D_O_K;
        
    char buf[80],buf2[80],name[13];
    extract_name(temppath,name,FILENAME8_3);
    
    if(save_guys(temppath))
    {
        sprintf(buf,"ZQuest");
        sprintf(buf2,"Saved %s",name);
    }
    else
    {
        sprintf(buf,"Error");
        sprintf(buf2,"Error saving %s",name);
    }
    
    jwin_alert(buf,buf2,NULL,NULL,"O&K",NULL,'k',0,lfont);
    return D_O_K;
}





bool save_combo_alias(const char *path);
bool load_combo_alias(const char *path);
int onImport_ComboAlias()
{
    if(!getname("Import Combo Alias (.zca)","zca",NULL,datapath,false))
        return D_O_K;
#ifdef ZC_ZQEDITOR
    if(zq_import_too_new(temppath))
        return D_O_K;
#endif
        
    if(!load_combo_alias(temppath))
    {
        char buf[80],name[13];
        extract_name(temppath,name,FILENAME8_3);
        sprintf(buf,"Unable to load %s",name);
        jwin_alert("Error",buf,NULL,NULL,"O&K",NULL,'k',0,lfont);
    }
    
    refresh(rALL);
    return D_O_K;
}

int onExport_ComboAlias()
{
    if(!getname("Export Combo Alias (.zca)","zca",NULL,datapath,false))
        return D_O_K;
        
    char buf[80],buf2[80],name[13];
    extract_name(temppath,name,FILENAME8_3);
    
    if(save_combo_alias(temppath))
    {
        sprintf(buf,"ZQuest");
        sprintf(buf2,"Saved %s",name);
    }
    else
    {
        sprintf(buf,"Error");
        sprintf(buf2,"Error saving %s",name);
    }
    
    jwin_alert(buf,buf2,NULL,NULL,"O&K",NULL,'k',0,lfont);
    return D_O_K;
}

int onImport_ZGP()
{
#ifdef ZC_ZQEDITOR
    if(zq255_quest_is_newer())
        return zq_newer_import_zgp();
#endif
    if(!getname("Import Graphics Pack (.zgp)","zgp",NULL,datapath,false))
        return D_O_K;
#ifdef ZC_ZQEDITOR
    if(zq_import_too_new(temppath))
        return D_O_K;
#endif
        
    saved=false;
    
    // usetiles=true;
    if(!load_zgp(temppath))
    {
        char buf[80],name[13];
        extract_name(temppath,name,FILENAME8_3);
        sprintf(buf,"Unable to load %s",name);
        jwin_alert("Error",buf,NULL,NULL,"O&K",NULL,'k',0,lfont);
    }
    
    refresh(rALL);
    return D_O_K;
}

int onExport_ZGP()
{
#ifdef ZC_ZQEDITOR
    if(zq255_quest_is_newer())
        return zq_newer_export_zgp();
#endif
    if(!getname("Export Graphics Pack (.zgp)","zgp",NULL,datapath,false))
        return D_O_K;
        
    char buf[80],buf2[80],name[13];
    extract_name(temppath,name,FILENAME8_3);
    
    if(save_zgp(temppath))
    {
        sprintf(buf,"ZQuest");
        sprintf(buf2,"Saved %s",name);
    }
    else
    {
        sprintf(buf,"Error");
        sprintf(buf2,"Error saving %s",name);
    }
    
    jwin_alert(buf,buf2,NULL,NULL,"O&K",NULL,'k',0,lfont);
    return D_O_K;
}

#ifdef ZC_ZQEDITOR
// A 2.55/3.0 quest whose subscreens are in upstream's own format (SUBS 8 on): 2.53's import and
// export here work on 2.53's subscreens, so its .sub files go through the subscreen layout
// editor's Subscreen menu instead, in upstream's format. True when the user was told so.
static bool zq_subs_in_layout_editor()
{
    bool zq255_subs_record(std::vector<byte> &rec, int *version);      // zqw_init.cpp
    std::vector<byte> rec;
    int v = 0;

    if(!zq255_subs_record(rec, &v))
        return false;

    char line[80];
    sprintf(line, "upstream's own format (subscreen format %d):", v);
    jwin_alert("Subscreens", "This quest's subscreens are in", line, "use Export / Import in Quest > Misc Data > Subscreens.", "O&K", NULL, 'k', 0, lfont);
    return true;
}
#endif

int onImport_Subscreen()
{
#ifdef ZC_ZQEDITOR
    if(zq_subs_in_layout_editor())
        return D_O_K;
#endif
    if(!getname("Import Subscreen (.sub)","sub",NULL,datapath,false))
        return D_O_K;
#ifdef ZC_ZQEDITOR
    if(zq_import_too_new(temppath))
        return D_O_K;
#endif
        
    saved=false;
    
    // usetiles=true;
    if(!load_subscreen(temppath))
    {
        char buf[80],name[13];
        extract_name(temppath,name,FILENAME8_3);
        sprintf(buf,"Unable to load %s",name);
        jwin_alert("Error",buf,NULL,NULL,"O&K",NULL,'k',0,lfont);
    }
    
    refresh(rALL);
    return D_O_K;
}

int onExport_Subscreen()
{
#ifdef ZC_ZQEDITOR
    if(zq_subs_in_layout_editor())
        return D_O_K;
#endif
    if(!getname("Export Subscreen (.sub)","sub",NULL,datapath,false))
        return D_O_K;
        
    bool cancel;
    char buf[80],buf2[80],name[13];
    extract_name(temppath,name,FILENAME8_3);
    
    if(save_subscreen(temppath, &cancel))
    {
        if(!cancel)
        {
            sprintf(buf,"ZQuest");
            sprintf(buf2,"Saved %s",name);
        }
        else
        {
            sprintf(buf,"ZQuest");
            sprintf(buf2,"Did not save %s",name);
        }
    }
    else
    {
        sprintf(buf,"Error");
        sprintf(buf2,"Error saving %s",name);
    }
    
    jwin_alert(buf,buf2,NULL,NULL,"O&K",NULL,'k',0,lfont);
    return D_O_K;
}

int onImport_ZQT()
{
#ifdef ZC_ZQEDITOR
    if(zq255_quest_is_newer())
        return zq_newer_import_quest(true);
#endif
    if(!getname("Import Quest Template (.zqt)","zqt",NULL,datapath,false))
        return D_O_K;
        
    saved=false;
    // usetiles=true;
    int error = load_quest(temppath,true, false);
    
    if(error != qe_OK && error != qe_cancel)
    {
        char buf[80],name[13];
        extract_name(temppath,name,FILENAME8_3);
        sprintf(buf,"Unable to load %s",name);
        jwin_alert("Error",buf,NULL,NULL,"O&K",NULL,'k',0,lfont);
    }
    
    register_blank_tiles();
    loadlvlpal(Color);
    setup_combo_animations();
    setup_combo_animations2();
    Map.setCurrMap(Map.getCurrMap());                         // for bound checking
    refresh(rALL);
    refresh_pal();
    return D_O_K;
}

int onExport_ZQT()
{
#ifdef ZC_ZQEDITOR
    if(zq255_quest_is_newer())
        return zq_newer_export_quest(true);
#endif
    if(!getname("Export Quest Template (.zqt)","zqt",NULL,datapath,false))
        return D_O_K;
        
    char buf[80],buf2[80],name[13];
    extract_name(temppath,name,FILENAME8_3);
    
#ifdef ZC_ZQEDITOR
    if(!zq_fits_253("exported"))
        return D_O_K;
#endif
#ifdef ZC_ZQEDITOR
    // 2.53's writer stamps the open quest's header as a 2.53.1 quest's while it writes. An
    // export leaves the open quest what it was: made in its own version, and saved in it.
    const zquestheader keep_header = header;
#endif
    int zqt_result = save_unencoded_quest(temppath, true);
#ifdef ZC_ZQEDITOR
    header = keep_header;
#endif
#ifdef ZC_PORT
    box_end(false);     // see save_quest()
#endif
    if(!zqt_result)
    {
        sprintf(buf,"ZQuest");
        sprintf(buf2,"Saved %s",name);
    }
    else
    {
        sprintf(buf,"Error");
        sprintf(buf2,"Error saving %s",name);
    }
    
    jwin_alert(buf,buf2,NULL,NULL,"O&K",NULL,'k',0,lfont);
    return D_O_K;
}

int onImport_UnencodedQuest()
{
#ifdef ZC_ZQEDITOR
    if(zq255_quest_is_newer())
        return zq_newer_import_quest(false);
#endif
    if(!getname("Import Unencoded Quest (.qsu)","qsu",NULL,datapath,false))
        return D_O_K;
        
    saved=false;
    // usetiles=true;
    int ret = load_quest(temppath,false,false);
    
    if(ret != qe_OK && ret != qe_cancel)
    {
        char buf[80],name[13];
        extract_name(temppath,name,FILENAME8_3);
        sprintf(buf,"Unable to load %s",name);
        jwin_alert("Error",buf,NULL,NULL,"O&K",NULL,'k',0,lfont);
    }
    
    register_blank_tiles();
    loadlvlpal(Color);
    setup_combo_animations();
    setup_combo_animations2();
    Map.setCurrMap(Map.getCurrMap());                         // for bound checking
    refresh(rALL);
    refresh_pal();
    return D_O_K;
}

int onExport_UnencodedQuest()
{
#ifdef ZC_ZQEDITOR
    if(zq255_quest_is_newer())
        return zq_newer_export_quest(false);
#endif
    if(!getname("Export Unencoded Quest (.qsu)","qsu",NULL,datapath,false))
        return D_O_K;
        
    char buf[80],buf2[80],name[13];
    extract_name(temppath,name,FILENAME8_3);
    
#ifdef ZC_ZQEDITOR
    if(!zq_fits_253("exported"))
        return D_O_K;
#endif
#ifdef ZC_ZQEDITOR
    // 2.53's writer stamps the open quest's header as a 2.53.1 quest's while it writes. An
    // export leaves the open quest what it was: made in its own version, and saved in it.
    const zquestheader keep_header = header;
#endif
    int qsu_result = save_unencoded_quest(temppath, false);
#ifdef ZC_ZQEDITOR
    header = keep_header;
#endif
#ifdef ZC_PORT
    box_end(false);     // see save_quest()
#endif
    if(!qsu_result)
    {
        sprintf(buf,"ZQuest");
        sprintf(buf2,"Saved %s",name);
    }
    else
    {
        sprintf(buf,"Error");
        sprintf(buf2,"Error saving %s",name);
    }
    
    jwin_alert(buf,buf2,NULL,NULL,"O&K",NULL,'k',0,lfont);
    return D_O_K;
}

void center_zq_files_dialogs()
{
    jwin_center_dialog(editqt_dlg);
    jwin_center_dialog(ruleset_dlg);
    jwin_center_dialog(import_map_bias_dlg);
    jwin_center_dialog(qtlist_dlg);
}

