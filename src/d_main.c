/*
====================================================================

DOOM RETRO
The classic, refined DOOM source port. For Windows PC.

Copyright (C) 1993-1996 id Software LLC, a ZeniMax Media company.
Copyright (C) 2005-2014 Simon Howard.
Copyright (C) 2013-2014 Brad Harding.

This file is part of DOOM RETRO.

DOOM RETRO is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

DOOM RETRO is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with DOOM RETRO. If not, see http://www.gnu.org/licenses/.

====================================================================
*/

#pragma comment(lib, "winmm.lib")

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <Commdlg.h>
#include <MMSystem.h>

#include "am_map.h"
#include "d_iwad.h"
#include "d_main.h"
#include "doomstat.h"
#include "f_finale.h"
#include "f_wipe.h"
#include "g_game.h"
#include "hu_stuff.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "m_argv.h"
#include "m_config.h"
#include "m_menu.h"
#include "m_misc.h"
#include "p_local.h"
#include "p_saveg.h"
#include "p_setup.h"
#include "s_sound.h"
#include "SDL.h"
#include "st_stuff.h"
#include "v_video.h"
#include "w_merge.h"
#include "w_wad.h"
#include "wi_stuff.h"
#include "z_zone.h"

//
// D-DoomLoop()
// Not a globally visible function,
//  just included for source reference,
//  called by D_DoomMain, never exits.
// Manages timing and IO,
//  calls all ?_Responder, ?_Ticker, and ?_Drawer,
//  calls I_GetTime, I_StartFrame, and I_StartTic
//
void D_DoomLoop(void);

// Location where savegames are stored

char           *savegamedir;

// location of IWAD and WAD files

char           *iwadfile;

char           *wadfolder = ".";

boolean        devparm;        // started game with -devparm
boolean        nomonsters;     // checkparm of -nomonsters
boolean        respawnparm;    // checkparm of -respawn
boolean        fastparm;       // checkparm of -fast

boolean        runcount = 0;

skill_t        startskill;
int            startepisode;
int            startmap;
boolean        autostart;
int            startloadgame;

boolean        advancedemo;
boolean        forcewipe = false;

extern int screenwidth;
extern int screenheight;
extern int windowwidth;
extern int windowheight;
extern int pixelwidth;
extern int pixelheight;
extern int selectedexpansion;

void D_CheckNetGame(void);

//
// EVENT HANDLING
//
// Events are asynchronous inputs generally generated by the game user.
// Events can be discarded if no responder claims them
//
#define MAXEVENTS 64

static event_t    events[MAXEVENTS];
static int        eventhead;
static int        eventtail;

//
// D_PostEvent
// Called by the I/O functions when input is detected
//
void D_PostEvent(event_t *ev)
{
    events[eventhead] = *ev;
    eventhead = (eventhead + 1) % MAXEVENTS;
}

//
// D_PopEvent
// Read an event from the queue
//
event_t *D_PopEvent(void)
{
    event_t *result;

    if (eventtail == eventhead)
        return NULL;

    result = &events[eventtail];

    eventtail = (eventtail + 1) % MAXEVENTS;

    return result;
}

boolean wipe = true;

//
// D_ProcessEvents
// Send all the events of the given timestamp down the responder chain
//
void D_ProcessEvents(void)
{
    event_t *ev;

    while ((ev = D_PopEvent()) != NULL)
    {
        if (wipe && ev->type == ev_mouse)
            continue;
        if (M_Responder(ev))
            continue;           // menu ate the event
        G_Responder(ev);
    }
}

//
// D_Display
//  draw current display, possibly wiping it from the previous
//

// wipegamestate can be set to -1 to force a wipe on the next draw
gamestate_t     wipegamestate = GS_DEMOSCREEN;
extern  boolean setsizeneeded;
extern  boolean message_on;
extern  int     graphicdetail;
extern  int     viewheight2;

void R_ExecuteSetViewSize(void);

void D_Display(void)
{
    static boolean     viewactivestate = false;
    static boolean     menuactivestate = false;
    static boolean     pausedstate = false;
    static gamestate_t oldgamestate = (gamestate_t)(-1);
    static int         borderdrawcount;
    int                nowtime;
    int                tics;
    int                wipestart;
    boolean            done;

    // change the view size if needed
    if (setsizeneeded)
    {
        R_ExecuteSetViewSize();
        oldgamestate = (gamestate_t)(-1);         // force background redraw
        borderdrawcount = 3;
    }

    // save the current screen if about to wipe
    if ((wipe = (gamestate != wipegamestate || forcewipe)))
    {
        wipe_StartScreen();
        if (forcewipe)
            forcewipe = false;
        else
            menuactive = false;
    }

    if (gamestate != GS_LEVEL)
    {
        if (gamestate != oldgamestate)
            I_SetPalette((byte *)W_CacheLumpName("PLAYPAL", PU_CACHE));

        switch (gamestate)
        {
            case GS_INTERMISSION:
                WI_Drawer();
                break;

            case GS_FINALE:
                F_Drawer();
                break;

            case GS_DEMOSCREEN:
                D_PageDrawer();
                break;
        }
    }
    else if (gametic)
    {
        HU_Erase();

        ST_Drawer(viewheight == SCREENHEIGHT, true);

        // draw the view directly
        R_RenderPlayerView(&players[displayplayer]);

        if (automapactive)
            AM_Drawer();

        // see if the border needs to be initially drawn
        if (oldgamestate != GS_LEVEL)
        {
            viewactivestate = false;    // view was not active
            R_FillBackScreen();         // draw the pattern into the back screen
        }

        // see if the border needs to be updated to the screen
        if (!automapactive)
        {
            if (scaledviewwidth != SCREENWIDTH)
            {
                if (menuactive || menuactivestate || !viewactivestate || paused || pausedstate || message_on)
                    borderdrawcount = 3;
                if (borderdrawcount)
                {
                    R_DrawViewBorder();     // erase old menu stuff
                    borderdrawcount--;
                }
            }
            if (graphicdetail == LOW)
                V_LowGraphicDetail(0, viewheight2);
        }
        HU_Drawer();
    }

    menuactivestate = menuactive;
    viewactivestate = viewactive;
    oldgamestate = wipegamestate = gamestate;

    // draw pause pic
    if ((pausedstate = paused))
    {
        M_DarkBackground();
        if (M_PAUSE)
        {
            patch_t     *patch = W_CacheLumpName("M_PAUSE", PU_CACHE);

            if (widescreen)
                V_DrawPatchWithShadow((ORIGINALWIDTH - patch->width) / 2,
                                      viewwindowy / 2 + (viewheight / 2 - patch->height) / 2,
                                      0, patch, false);
            else
                V_DrawPatchWithShadow((ORIGINALWIDTH - patch->width) / 2,
                                      (ORIGINALHEIGHT - patch->height) / 2,
                                      0, patch, false);
        }
        else
        {
            if (widescreen)
                M_DrawCenteredString(viewwindowy / 2 + (viewheight / 2 - 16) / 2, "Paused");
            else
                M_DrawCenteredString((ORIGINALHEIGHT - 16) / 2, "Paused");
        }
    }

    // menus go directly to the screen
    M_Drawer();                 // menu is drawn even on top of everything

    // normal update
    if (!wipe)
    {
        I_FinishUpdate();       // page flip or blit buffer
        return;
    }

    // wipe update
    wipe_EndScreen();

    wipestart = I_GetTime() - 1;

    do
    {
        do
        {
            nowtime = I_GetTime();
            tics = nowtime - wipestart;
            I_Sleep(1);
        }
        while (tics <= 0);

        wipestart = nowtime;
        done = wipe_ScreenWipe(tics);
        blurred = false;
        M_Drawer();             // menu is drawn even on top of wipes
        I_FinishUpdate();       // page flip or blit buffer
    }
    while (!done);
}

//
//  D_DoomLoop
//
void D_DoomLoop(void)
{
    TryRunTics();

    I_InitGraphics();

    R_ExecuteSetViewSize();

    D_StartGameLoop();

    while (1)
    {
        TryRunTics(); // will run at least one tic

        S_UpdateSounds(players[consoleplayer].mo); // move positional sounds

        // Update display, next frame, with current state.
        if (screenvisible)
            D_Display();
    }
}

//
//  DEMO LOOP
//
static int  demosequence;
static int  pagetic;
static char *pagename;

//
// D_PageTicker
// Handles timing for warped projection
//
void D_PageTicker(void)
{
    if (!menuactive)
    {
        if (--pagetic < 0)
            D_AdvanceDemo();
        if (!TITLEPIC)
            M_StartControlPanel();
    }
}

//
// D_PageDrawer
//
void D_PageDrawer(void)
{
    V_DrawPatch(0, 0, 0, W_CacheLumpName(pagename, PU_CACHE));
}

//
// D_AdvanceDemo
// Called after each demo or intro demosequence finishes
//
void D_AdvanceDemo(void)
{
    advancedemo = true;
}

//
// This cycles through the demo sequences.
//
void D_DoAdvanceDemo(void)
{
    static boolean starttitle = true;

    players[consoleplayer].playerstate = PST_LIVE;      // not reborn
    advancedemo = false;
    usergame = false;                                   // no save / end game here
    paused = false;
    gameaction = ga_nothing;
    gamestate = GS_DEMOSCREEN;
    blurred = false;

    if ((demosequence = !demosequence))
    {
        pagename = (TITLEPIC ? "TITLEPIC" : (DMENUPIC ? "DMENUPIC" : "INTERPIC"));
        pagetic = 20 * TICRATE;
        S_StartMusic(gamemode == commercial ? mus_dm2ttl : mus_intro);
    }
    else
    {
        pagename = "CREDIT";
        pagetic = 10 * TICRATE;
    }
    if (starttitle)
        starttitle = false;
    else
        forcewipe = true;
}

//
// D_StartTitle
//
void D_StartTitle(void)
{
    gameaction = ga_nothing;
    demosequence = 0;
    SDL_WM_SetCaption(gamedescription, NULL);
    D_AdvanceDemo();
}

static boolean D_AddFile(char *filename)
{
    wad_file_t *handle;

    handle = W_AddFile(filename);

    FREEDOOM = IsFreedoom(filename);

    return (handle != NULL);
}

char *uppercase(char *str)
{
    char *newstr;
    char *p;

    p = newstr = strdup(str);
    while (*p++ = toupper(*p));

    return newstr;
}

// Initialize the game version
static void InitGameVersion(void)
{
    // Determine automatically
        if (gamemode == shareware || gamemode == registered)
        // original
        gameversion = exe_doom_1_9;
    else if (gamemode == retail)
        gameversion = exe_ultimate;
    else if (gamemode == commercial)
    {
        if (gamemission == doom2)
            gameversion = exe_doom_1_9;
        else
            // Final Doom: tnt or plutonia
            gameversion = exe_final;
    }

    // The original exe does not support retail - 4th episode not supported
    if (gameversion < exe_ultimate && gamemode == retail)
        gamemode = registered;

    // EXEs prior to the Final Doom exes do not support Final Doom.
    if (gameversion < exe_final && gamemode == commercial)
        gamemission = doom2;
}

static void D_FirstUse(void)
{
    LPCWSTR msg = L"Thank you for downloading DOOM RETRO!\n\n"
                  L"Please note that, as with all DOOM source ports, no actual map data is "
                  L"distributed with DOOM RETRO.\n\n"
                  L"In the dialog box that follows, please navigate to where an official "
                  L"release of DOOM or DOOM II has been installed and select an \u201cIWAD "
                  L"file\u201d that DOOM RETRO requires (such as DOOM.WAD or "
                  L"DOOM2.WAD). Additional \u201cPWAD files\u201d may also be selected by "
                  L"CTRL-clicking on them.";

    if (MessageBoxW(NULL, msg, L"DOOM RETRO", MB_ICONINFORMATION | MB_OKCANCEL) == IDCANCEL)
        I_Quit(false);
}

static boolean D_IsDOOMIWAD(char *filename)
{
    return (D_CheckFilename(filename, "DOOM.WAD")
            || D_CheckFilename(filename, "DOOM1.WAD")
            || D_CheckFilename(filename, "DOOM2.WAD")
            || D_CheckFilename(filename, "PLUTONIA.WAD")
            || D_CheckFilename(filename, "TNT.WAD"));
}

static boolean D_IsUnsupportedIWAD(char *filename)
{
    return (D_CheckFilename(filename, "HERETIC1.WAD")
            || D_CheckFilename(filename, "HERETIC.WAD")
            || D_CheckFilename(filename, "HEXEN.WAD")
            || D_CheckFilename(filename, "HEXDD.WAD")
            || D_CheckFilename(filename, "STRIFE0.WAD")
            || D_CheckFilename(filename, "STRIFE1.WAD")
            || D_CheckFilename(filename, "CHEX.WAD")
            || D_CheckFilename(filename, "HACX.WAD"));
}

static boolean D_IsUnsupportedPWAD(char *filename)
{
    return (D_CheckFilename(filename, "VOICES.WAD")
            || D_CheckFilename(filename, "CHEX3.WAD"));
}

static int D_ChooseIWAD(void)
{
    OPENFILENAME        ofn;
    char                szFile[4096];
    int                 iwadfound = -1;
    boolean             sharewareiwad = false;

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "IWAD/PWAD Files (*.wad)\0*.WAD\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = wadfolder;
    ofn.Flags = OFN_HIDEREADONLY | OFN_NOCHANGEDIR | OFN_ALLOWMULTISELECT
                | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrTitle = "Where\u2019s All the Data?\0";

    if (GetOpenFileName(&ofn))
    {
        iwadfound = 0;

        // only one file was selected
        if (!ofn.lpstrFile[strlen(ofn.lpstrFile) + 1])
        {
            LPSTR       file = ofn.lpstrFile;

            wadfolder = strdup(M_ExtractFolder(file));

            // check if it's a valid and supported IWAD
            if (D_IsDOOMIWAD(file)
                || (W_WadType(file) == IWAD
                    && !D_IsUnsupportedIWAD(file)))
            {
                IdentifyIWADByContents(file, &gamemode, &gamemission);
                if (D_AddFile(file))
                    iwadfound = 1;
            }

            // if it's a PWAD, determine the IWAD required and try loading that as well
            else if (!D_CheckFilename(file, "DOOMRETRO.WAD")
                     && W_WadType(file) == PWAD
                     && !D_IsUnsupportedPWAD(file))
            {
                int             iwadrequired = IWADRequiredByPWAD(file);
                static char     fullpath[MAX_PATH];

                if (iwadrequired == indetermined)
                    return 0;

                sprintf(fullpath, "%s\\%s", wadfolder,
                        iwadrequired == doom ? "DOOM.WAD" : "DOOM2.WAD");
                IdentifyIWADByName(fullpath);
                if (D_AddFile(fullpath))
                {
                    iwadfound = 1;
                    if (W_MergeFile(file))
                    {
                        modifiedgame = true;
                        if (D_CheckFilename(file, "NERVE.WAD"))
                            nerve = true;
                    }
                }
            }
        }

        // more than one file was selected
        else
        {
            LPSTR       iwad = ofn.lpstrFile;
            LPSTR       pwad = ofn.lpstrFile;

            wadfolder = strdup(szFile);

            // find and add IWAD first
            while (iwad[0])
            {
                static char     fullpath[MAX_PATH];

                iwad += lstrlen(iwad) + 1;
                sprintf(fullpath, "%s\\%s", wadfolder, iwad);

                if (D_IsDOOMIWAD(fullpath)
                    || (W_WadType(fullpath) == IWAD
                        && !D_IsUnsupportedIWAD(fullpath)))
                {
                    if (!iwadfound)
                    {
                        IdentifyIWADByContents(fullpath, &gamemode, &gamemission);
                        if (D_AddFile(fullpath))
                        {
                            iwadfound = 1;
                            sharewareiwad = !strcasecmp(iwad, "DOOM1.WAD");
                            break;
                        }
                    }
                }

                // if it's NERVE.WAD, try to open DOOM2.WAD with it
                else if (!strcasecmp(iwad, "NERVE.WAD"))
                {
                    static char     fullpath2[MAX_PATH];

                    sprintf(fullpath2, "%s\\DOOM2.WAD", wadfolder);
                    IdentifyIWADByName(fullpath2);
                    if (D_AddFile(fullpath2))
                    {
                        iwadfound = 1;
                        if (W_MergeFile(fullpath))
                        {
                            modifiedgame = true;
                            nerve = true;
                        }
                        break;
                    }
                }
            }

            // merge any pwads
            if (iwadfound && !sharewareiwad)
                while (pwad[0])
                {
                    static char     fullpath[MAX_PATH];

                    pwad += lstrlen(pwad) + 1;
                    sprintf(fullpath, "%s\\%s", wadfolder, pwad);

                    if (!D_CheckFilename(pwad, "DOOMRETRO.WAD")
                        && W_WadType(fullpath) == PWAD
                        && !D_IsUnsupportedPWAD(fullpath))
                        if (W_MergeFile(fullpath))
                        {
                            modifiedgame = true;
                            if (!strcasecmp(pwad, "NERVE.WAD"))
                                nerve = true;
                        }
                }
        }
    }
    return iwadfound;
}

void (*bloodSplatSpawner)(fixed_t, fixed_t, int, void (*)(void));

//
// D_DoomMainSetup
//
// CPhipps - the old contents of D_DoomMain, but moved out of the main
//  line of execution so its stack space can be freed
static void D_DoomMainSetup(void)
{
    int     p;
    char    file[256];
    int     temp;
    int     choseniwad;

    SDL_Init(0);

    M_FindResponseFile();

    iwadfile = D_FindIWAD();

    modifiedgame = false;

    nomonsters = M_CheckParm("-nomonsters");
    respawnparm = M_CheckParm("-respawn");
    fastparm = M_CheckParm("-fast");
    devparm = M_CheckParm("-devparm");
    if (M_CheckParm("-altdeath"))
        deathmatch = 2;
    else if (M_CheckParm("-deathmatch"))
        deathmatch = 1;

    M_SetConfigDir();

    // turbo option
    p = M_CheckParm("-turbo");
    if (p)
    {
        int        scale = 200;
        extern int forwardmove[2];
        extern int sidemove[2];

        if (p < myargc - 1)
            scale = atoi(myargv[p + 1]);
        if (scale < 10)
            scale = 10;
        if (scale > 400)
            scale = 400;
        forwardmove[0] = forwardmove[0] * scale / 100;
        forwardmove[1] = forwardmove[1] * scale / 100;
        sidemove[0] = sidemove[0] * scale / 100;
        sidemove[1] = sidemove[1] * scale / 100;
    }

    // init subsystems
    V_Init();

    // Load configuration files before initialising other subsystems.
    M_LoadDefaults();

    if (!M_FileExists("doomretro.wad"))
        if (!M_FileExists("doomretro.wad.temp"))
            I_Error("Can't find doomretro.wad.");

    if (iwadfile)
    {
        if (D_AddFile(iwadfile))
            if (runcount < RUNCOUNT_MAX)
                runcount++;
    }
    else 
    {
        if (!runcount)
            D_FirstUse();

        rename("doomretro.wad", "doomretro.wad.temp");

        do
        {
            choseniwad = D_ChooseIWAD();

            if (choseniwad == -1)
            {
                rename("doomretro.wad.temp", "doomretro.wad");
                I_Quit(false);
            }
            else if (!choseniwad)
                PlaySound((LPCTSTR)SND_ALIAS_SYSTEMHAND, NULL, SND_ALIAS_ID | SND_ASYNC);

        } while (!choseniwad);

        rename("doomretro.wad.temp", "doomretro.wad");

        if (runcount < RUNCOUNT_MAX)
            runcount++;
    }
    M_SaveDefaults();

    if (!W_MergeFile("doomretro.wad"))
        if (!W_MergeFile("doomretro.wad.temp"))
            I_Error("Can't find doomretro.wad.");

    if (W_CheckNumForName("BLD2A0") < 0 ||
        W_CheckNumForName("MEDBA0") < 0 ||
        W_CheckNumForName("STBAR2") < 0)
        I_Error("Wrong version of doomretro.wad.");

    p = M_CheckParmsWithArgs("-file", "-pwad", 1);
    if (p > 0)
    {
        for (p = p + 1; p < myargc && myargv[p][0] != '-'; ++p)
        {
            char *filename = uppercase(D_TryFindWADByName(myargv[p]));

            if (W_MergeFile(filename))
            {
                modifiedgame = true;
                if (D_CheckFilename(filename, "NERVE.WAD"))
                    nerve = true;
            }
        }
    }

    if (FREEDOOM && W_CheckNumForName("FREEDM") < 0 && !modifiedgame)
        I_Error("FREEDOOM requires a BOOM-compatible source port, and is therefore"
                "unable to be opened by DOOM RETRO.");

    DMENUPIC = (W_CheckNumForName("DMENUPIC") >= 0);
    M_DOOM = (W_CheckMultipleLumps("M_DOOM") > 1);
    M_EPISOD = (W_CheckMultipleLumps("M_EPISOD") > 1);
    M_GDHIGH = (W_CheckMultipleLumps("M_GDHIGH") > 1);
    M_GDLOW = (W_CheckMultipleLumps("M_GDLOW") > 1);
    M_LOADG = (W_CheckMultipleLumps("M_LOADG") > 1);
    M_LSCNTR = (W_CheckMultipleLumps("M_LSCNTR") > 1);
    M_MSENS = (W_CheckMultipleLumps("M_MSENS") > 1);
    M_MSGOFF = (W_CheckMultipleLumps("M_MSGOFF") > 1);
    M_MSGON = (W_CheckMultipleLumps("M_MSGON") > 1);
    M_NEWG = (W_CheckMultipleLumps("M_NEWG") > 1);
    M_NMARE = (W_CheckMultipleLumps("M_NMARE") > 1);
    M_OPTTTL = (W_CheckMultipleLumps("M_OPTTTL") > 1);
    M_PAUSE = (W_CheckMultipleLumps("M_PAUSE") > 1);
    M_SAVEG = (W_CheckMultipleLumps("M_SAVEG") > 1);
    M_SKILL = (W_CheckMultipleLumps("M_SKILL") > 1);
    M_SKULL1 = (W_CheckMultipleLumps("M_SKULL1") > 1);
    M_SVOL = (W_CheckMultipleLumps("M_SVOL") > 1);
    STARMS = (W_CheckMultipleLumps("STARMS") > 2);
    STBAR = (W_CheckMultipleLumps("STBAR") > 2);
    STCFN034 = (W_CheckMultipleLumps("STCFN034") > 1);
    STCFN039 = (W_CheckMultipleLumps("STCFN039") > 1);
    STCFN121 = (W_CheckMultipleLumps("STCFN121") > 1);
    STYSNUM0 = (W_CheckMultipleLumps("STYSNUM0") > 1);
    TITLEPIC = (W_CheckNumForName("TITLEPIC") >= 0);
    WISCRT2 = (W_CheckMultipleLumps("WISCRT2") > 1);

    bfgedition = (DMENUPIC && W_CheckNumForName("M_ACPT") >= 0);

    // Generate the WAD hash table. Speed things up a bit.
    W_GenerateHashTable();

    D_IdentifyVersion();
    InitGameVersion();
    D_SetGameDescription();
    D_SetSaveGameDir();

    // Check for -file in shareware
    if (modifiedgame)
    {
        // These are the lumps that will be checked in IWAD,
        // if any one is not present, execution will be aborted.
        char name[23][9] =
        {
            "e2m1", "e2m2", "e2m3", "e2m4", "e2m5", "e2m6", "e2m7", "e2m8", "e2m9",
            "e3m1", "e3m3", "e3m3", "e3m4", "e3m5", "e3m6", "e3m7", "e3m8", "e3m9",
            "dphoof", "bfgga0", "heada1", "cybra1", "spida1d1"
        };
        int i;

        if (gamemode == shareware)
            I_Error("You cannot use -FILE with the shareware version.\n"
                    "Please purchase the full version.");

        // Check for fake IWAD with right name,
        // but w/o all the lumps of the registered version.
        if (gamemode == registered)
            for (i = 0; i < 23; i++)
                if (W_CheckNumForName(name[i]) < 0)
                    I_Error("This is not the registered version.");
    }

    // get skill / episode / map from parms
    startskill = sk_medium;
    startepisode = 1;
    startmap = 1;
    autostart = false;

    p = M_CheckParmWithArgs("-skill", 1);
    if (p)
    {
        temp = myargv[p + 1][0] - '1';
        if (temp >= sk_baby && temp <= sk_nightmare)
        {
            startskill = (skill_t)temp;
            autostart = true;
        }
    }

    p = M_CheckParmWithArgs("-episode", 1);
    if (p)
    {
        temp = myargv[p + 1][0] - '0';
        if ((gamemode == shareware && temp == 1)
            || (temp >= 1
                && ((gamemode == registered && temp <= 3)
                    || (gamemode == retail && temp <= 4))))
        {
            startepisode = temp;
            startmap = 1;
            autostart = true;
        }
    }

    p = M_CheckParmWithArgs("-expansion", 1);
    if (p)
    {
        temp = myargv[p + 1][0] - '0';
        if (gamemode == commercial && temp <= (nerve ? 2 : 1))
        {
            gamemission = (temp == 1 ? doom2 : pack_nerve);
            selectedexpansion = temp - 1;
            startepisode = 1;
            startmap = 1;
            autostart = true;
        }
    }

    timelimit = 0;

    p = M_CheckParmWithArgs("-timer", 1);
    if (p)
        timelimit = atoi(myargv[p + 1]);

    p = M_CheckParm("-avg");
    if (p)
        timelimit = 20;

    p = M_CheckParmWithArgs("-warp", 1);
    if (p)
    {
        static char lumpname[6];

        if (gamemode == commercial)
        {
            if (strlen(myargv[p + 1]) == 5 &&
                toupper(myargv[p + 1][0]) == 'M' &&
                toupper(myargv[p + 1][1]) == 'A' &&
                toupper(myargv[p + 1][2]) == 'P')
                startmap = (myargv[p + 1][3] - '0') * 10 + myargv[p + 1][4] - '0';
            else
                startmap = atoi(myargv[p + 1]);

            sprintf(lumpname, "MAP%02i", startmap);
        }
        else
        {
            if (strlen(myargv[p + 1]) == 4 &&
                toupper(myargv[p + 1][0]) == 'E' &&
                toupper(myargv[p + 1][2]) == 'M')
            {
                startepisode = myargv[p + 1][1] - '0';
                startmap = myargv[p + 1][3] - '0';
            }
            else
            {
                startepisode = myargv[p + 1][0] - '0';

                if (p + 2 < myargc)
                    startmap = myargv[p + 2][0] - '0';
                else
                    startmap = 1;
            }

            sprintf(lumpname, "E%iM%i", startepisode, startmap);
        }

        if ((W_CheckNumForName(lumpname) >= 0 && !FREEDOOM)
            || (W_CheckMultipleLumps(lumpname) > 1 && FREEDOOM))
            autostart = true;
    }

    p = M_CheckParmWithArgs("-loadgame", 1);
    if (p)
        startloadgame = atoi(myargv[p + 1]);
    else
        startloadgame = -1;

    if (mouseSensitivity < MOUSESENSITIVITY_MIN || mouseSensitivity > MOUSESENSITIVITY_MAX)
        mouseSensitivity = MOUSESENSITIVITY_DEFAULT;
    if (mouseSensitivity == MOUSESENSITIVITY_MIN)
        mouseSensitivity = -5;
    gamepadSensitivity = (!mouseSensitivity ? 0.0f :
                          (2.0f + mouseSensitivity / (float)MOUSESENSITIVITY_MAX));

    if (sfxVolume < SFXVOLUME_MIN || sfxVolume > SFXVOLUME_MAX)
        sfxVolume = SFXVOLUME_DEFAULT;

    if (musicVolume < MUSICVOLUME_MIN || musicVolume > MUSICVOLUME_MAX)
        musicVolume = MUSICVOLUME_DEFAULT;

    if (screensize < SCREENSIZE_MIN || screensize > SCREENSIZE_MAX)
        screensize = SCREENSIZE_DEFAULT;
    if (widescreen && !fullscreen)
    {
        widescreen = false;
        screensize = SCREENSIZE_MAX;
    }
    if (!widescreen)
        hud = true;
    if (fullscreen && screensize == SCREENSIZE_MAX)
    {
        widescreen = true;
        screensize = SCREENSIZE_MAX - 1;
    }
    if (widescreen)
    {
        returntowidescreen = true;
        widescreen = false;
    }

    if (screenwidth && screenheight
        && (screenwidth < SCREENWIDTH || screenheight < SCREENHEIGHT * 3 / 4))
    {
        screenwidth = SCREENWIDTH;
        screenheight = SCREENWIDTH * 3 / 4;
    }

    if (windowwidth < SCREENWIDTH || windowheight < SCREENWIDTH * 3 / 4)
    {
        windowwidth = SCREENWIDTH;
        windowheight = SCREENWIDTH * 3 / 4;
    }

    if (gammalevel < GAMMALEVEL_MIN || gammalevel > GAMMALEVEL_MAX)
        gammalevel = GAMMALEVEL_DEFAULT;
    gammalevelindex = 0;
    while (gammalevelindex < GAMMALEVELS)
        if (gammalevels[gammalevelindex++] == gammalevel)
            break;
    if (gammalevelindex == GAMMALEVELS)
    {
        gammalevelindex = 0;
        while (gammalevels[gammalevelindex++] != GAMMALEVEL_DEFAULT);
    }
    gammalevelindex--;

    if (bloodsplats < BLOODSPLATS_MIN || bloodsplats > BLOODSPLATS_MAX)
        bloodsplats = BLOODSPLATS_DEFAULT;
    bloodSplatSpawner = (bloodsplats == UNLIMITED ? P_SpawnBloodSplat : P_SpawnBloodSplat2);

    if (pixelwidth < PIXELWIDTH_MIN || pixelwidth > PIXELWIDTH_MAX)
        pixelwidth = PIXELWIDTH_DEFAULT;
    while (SCREENWIDTH % pixelwidth)
        pixelwidth--;
    if (pixelheight < PIXELHEIGHT_MIN || pixelheight > PIXELHEIGHT_MAX)
        pixelheight = PIXELHEIGHT_DEFAULT;
    while (SCREENHEIGHT % pixelheight)
        pixelheight--;

    M_Init();

    R_Init();

    P_Init();

    I_Init();

    S_Init((int)(sfxVolume * (127.0f / 15.0f)), (int)(musicVolume * (127.0f / 15.0f)));

    D_CheckNetGame();

    HU_Init();

    ST_Init();

    AM_Init();

    if (startloadgame >= 0)
    {
        M_StringCopy(file, P_SaveGameFile(startloadgame), sizeof(file));
        G_LoadGame(file);
    }

    if (gameaction != ga_loadgame)
    {
        if (autostart || netgame)
            G_DeferredInitNew(startskill, startepisode, startmap);
        else
            D_StartTitle();                     // start up intro loop
    }
}

//
// D_DoomMain
//
void D_DoomMain(void)
{
    D_DoomMainSetup(); // CPhipps - setup out of main execution stack

    D_DoomLoop();                               // never returns
}
