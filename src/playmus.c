/*
  A Demo player for MixerX, based on code of MUSPLAY from SDL_mixer
  Copyright (C) 1997-2020 Sam Lantinga <slouken@libsdl.org>
  Copyright (C) 2024 Vitaly Novichkov <admin@wohlnet.ru>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/* Quiet windows compiler warnings */
#define _CRT_SECURE_NO_WARNINGS

// #define SUPER_DEBUG

#include <SDL2/SDL_stdinc.h>

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef __wii__
#   include <stdio.h>
#   include <fat.h>

#   include <gccore.h>
#   include <wiiuse/wpad.h>
#   define MIXER_ROOT
#elif defined(__WIIU__)
#   include <coreinit/thread.h>
#   include <coreinit/time.h>
#   include <coreinit/systeminfo.h>
#   include <vpad/input.h>
#   include <nn/ac.h>
#   include <whb/proc.h>
#   include <whb/log.h>
#   include <whb/log_console.h>
#   define MIXER_ROOT "fs:/vol/external01"
#   define printf WHBLogPrintf
#   define fflush(x)
#else
#   include <stdio.h>
#   include <ncurses.h>
static int kbhit(void);
#   define MIXER_ROOT "/home/VMs2/Wii"
#endif

#ifdef unix
#   include <unistd.h>
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer_ext.h>

#ifdef HAVE_SIGNAL_H
#   include <signal.h>
#endif

#include "fx/spc_echo.h"

static int audio_open = 0;
static Mix_Music *music = NULL;
static Mix_Music *music_prev = NULL;
static Mix_Music *multi_music[20] = {NULL};
static int multi_music_count = 0;
static int next_track = 0;
static const int musicListSize = 255;
static const int musicListStrLenSize = 64;
static char musicList[255][64];
static char curMusic[255];
static char curMusicPrint[255] = "";
static int musicListTotal = 0;
static int menuCursor = 0;
static int menuOffset = 0;
const int menuLength = 7;
static int doStop = 0;
static int fx_on = 0;
static SDL_bool rwops_on = SDL_FALSE;

static Mix_Chunk *m_recorg = NULL;
static Mix_Chunk *m_spotyeah = NULL;

enum MixKey
{
    MIX_KEY_UP      = 0x01,
    MIX_KEY_DOWN    = 0x02,
    MIX_KEY_LEFT    = 0x04,
    MIX_KEY_RIGHT   = 0x08,
    MIX_KEY_PLAY_SND1   = 0x10,
    MIX_KEY_PLAY_SND2   = 0x20,
    MIX_KEY_STOP        = 0x40,
    MIX_KEY_PLAY        = 0x80,
    MIX_KEY_TOGGLE_ECHO = 0x100,
    MIX_KEY_QUIT        = 0x200,
    MIX_KEY_TOGGLE_TYPE = 0x400,
};

static Uint32 getKey();

static void playmusVideoUpdate();
static void playmusVideoInit();

int printLine(const char *fmt, ...)
{
    const size_t line_len = 70;
    char buff[line_len];
    int len = 0;
#if !defined(__WIIU__)
    printf("\r");
    fflush(stdout);
#endif
    va_list args;
    va_start(args, fmt);
    len = vsnprintf(buff, line_len, fmt, args);
    SDL_memset(buff + len, ' ', line_len - len);
    buff[line_len - 1] = '\0';
    va_end(args);
    printf("%s\n", buff);
    fflush(stdout);

    return len;
}

void crLine()
{
#if !defined(__WIIU__)
    int i;
    printf("\r");
    fflush(stdout);
    for(i = 0; i < 70; ++i)
        printf(" ");
    printf("\r");
    fflush(stdout);
#endif
}

void printMenu(int cursor)
{
    int i;
#if defined(__WIIU__)
    for(i = 0; i < 16; ++i)
        printf("\n");

    if(curMusicPrint[0] != 0)
    {
        printLine("-- Playing: %s\n", curMusicPrint);
        printLine("\n");
    }

    printLine("  A - play sel.   B - toggle FX [%s]     X - Stop", (fx_on ? "x" : " "));
    printLine("  Y - quit        L - RWops [%s]", (rwops_on ? "x" : " "));
#else
    printf("\x1b[0;0H");
    printLine("  A - play sel.   B - toggle FX [%s]     1 - Stop", (fx_on ? "x" : " "));
    printLine("  HOME - quit");
#endif

    if(m_recorg && m_spotyeah)
        printLine("  + play SFX1   - play SFX2");
    else
        printLine(" ");
    printLine(" ");

#if defined(__WIIU__)
    printLine("Select song from the list:");
#else
    printLine("\033[1;31mSelect song from the list:\033[0m");
#endif

    if(cursor - menuOffset > menuLength - 2)
        menuOffset = cursor - (menuLength - 2);
    else if(cursor - menuOffset < 2)
        menuOffset = cursor - 2;

    if(menuOffset + menuLength >= musicListTotal)
        menuOffset = musicListTotal - menuLength;

    if(menuOffset < 0)
        menuOffset = 0;

    for(i = menuOffset; i < menuOffset + menuLength && i < musicListTotal; ++i)
        printLine("%s %d) %s", (i == cursor ? "->" : "  "), i, musicList[i]);

    crLine();
    printf("Selected song: %d\r", cursor);
    fflush(stdout);
}

void swapItems(char *one, char*second)
{
    char temp[musicListStrLenSize];
    SDL_strlcpy(temp, one, musicListStrLenSize);
    SDL_strlcpy(one, second, musicListStrLenSize);
    SDL_strlcpy(second, temp, musicListStrLenSize);
}

int split(char a[255][64], int start_index, int end_index)
{
    char *x = a[end_index];
    int i = start_index - 1;

    for(int j = start_index; j < end_index; j++) {
        if (SDL_strncasecmp(a[j], x, musicListStrLenSize) <= 0) {
            i++;
            swapItems(a[i], a[j]);
        }
    }

    swapItems(a[i + 1], a[end_index]);

    return i + 1;
}

void qsortRecursive(char a[255][64], int start_index, int end_index)
{
    if(start_index < end_index) {
        int mid_index = split(a, start_index, end_index);
        qsortRecursive(a, start_index, mid_index - 1);
        qsortRecursive(a, mid_index + 1, end_index);
    }
}

void listDir(const char* path)
{
    DIR *srcdir;
    struct dirent *dent;
    struct stat st;
    char tempPath[PATH_MAX];

    SDL_memset(curMusic, 0, sizeof(curMusic));
    SDL_memset(curMusicPrint, 0, sizeof(curMusicPrint));
    SDL_memset(musicList, 0, sizeof(musicList));
    musicListTotal = 0;

    srcdir = opendir(path);

    if (!srcdir){
        printf ("opendir() failure; terminating\n");
        return;
    }

    while((dent = readdir(srcdir)) != NULL)
    {
        if(SDL_strcmp(".", dent->d_name) == 0 || SDL_strcmp("..", dent->d_name) == 0)
            continue;

        SDL_memset(tempPath, 0, PATH_MAX);
        SDL_snprintf(tempPath, PATH_MAX, "%s%s", path, dent->d_name);

        if(stat(tempPath, &st) < 0)
            continue;

        if(S_ISREG(st.st_mode))
        {
            SDL_snprintf(musicList[musicListTotal], 64, "%s", dent->d_name);
            musicListTotal++;

            if(musicListTotal >= musicListSize)
                break; // Reached maximum!
        }
//        else if(S_ISDIR(st.st_mode))
//            printf("%s <dir>\n", dent->d_name);
    }

    qsortRecursive(musicList, 0, musicListTotal - 1);

    closedir(srcdir);
}

void waitForHome()
{
#ifdef __wii__
    printf("Press HOME to continue...");
    while(1)
    {
        // Call WPAD_ScanPads each loop, this reads the latest controller states
        WPAD_ScanPads();

        // WPAD_ButtonsDown tells us which buttons were pressed in this loop
        // this is a "one shot" state which will not fire again until the button has been released
        u32 pressed = WPAD_ButtonsDown(0);

        playmusVideoUpdate();

        if(pressed & WPAD_BUTTON_HOME)
            break;
    }
    printf("OK!\n");
#endif
}

void waitForAnyKey()
{
    printLine("Press any key to continue...");
    playmusVideoUpdate();

    while(getKey() == 0)
        SDL_Delay(10);
}

int ifHomePressed()
{
#ifdef __wii__
    // Call WPAD_ScanPads each loop, this reads the latest controller states
    WPAD_ScanPads();

    // WPAD_ButtonsDown tells us which buttons were pressed in this loop
    // this is a "one shot" state which will not fire again until the button has been released
    u32 pressed = WPAD_ButtonsDown(0);
    return (pressed & WPAD_BUTTON_HOME);
#else
    return 0;
#endif
}

void CleanUp(int exitcode)
{
    playmusVideoUpdate();

    crLine();
    SDL_Log("IsPlaying...\n");
    playmusVideoUpdate();

    if(Mix_PlayingMusic()) {
        crLine();
        SDL_Log("Fade Out...\n");
        playmusVideoUpdate();
        Mix_FadeOutMusic(1500);
        crLine();
        SDL_Log("Delay...\n");
        playmusVideoUpdate();
        SDL_Delay(1500);
    }
    if (music) {
        crLine();
        SDL_Log("FreeMusic...\n");
        playmusVideoUpdate();
        Mix_FreeMusic(music);
        music = NULL;
    }
    if (audio_open) {
        crLine();
        SDL_Log("Close Audio...\n");
        playmusVideoUpdate();
        Mix_CloseAudio();
        audio_open = 0;
    }

    crLine();
    SDL_Log("SDL Quit...\n");
    playmusVideoUpdate();

    SDL_Quit();
    playmusVideoUpdate();

#if defined(__WIIU__)
    VPADShutdown();
    WHBLogConsoleFree();
    WHBProcShutdown();

    ACFinalize();
#endif

    exit(exitcode);
}

void Usage(char *argv0)
{
    SDL_Log("Usage: %s [-i] [-l] [-8] [-f32] [-r rate] [-c channels] [-b buffers] [-v N] [-rwops] <musicfile>\n", argv0);
}


void loadChunks()
{
    // 8-bit VOCs
//    m_recorg = Mix_LoadWAV(MIXER_ROOT "/music/sfx/RECORG.VOC");
//    m_spotyeah = Mix_LoadWAV(MIXER_ROOT "/music/sfx/Spotyeah.voc");

    // 8-bit WAVs
//    m_recorg = Mix_LoadWAV(MIXER_ROOT "/music/sfx/sndIncFile.wav");
//    m_spotyeah = Mix_LoadWAV(MIXER_ROOT "/music/sfx/sndOnline.wav");

    // 16-bit OGGs
    m_recorg = Mix_LoadWAV(MIXER_ROOT "/music/sfx/sndIncFile.wav");
//    m_recorg = Mix_LoadWAV(MIXER_ROOT "/music/sfx/sndIncFile.aiff");
//    m_recorg = Mix_LoadWAV(MIXER_ROOT "/music/sfx/S_Chirps.ogg");
//    m_recorg = Mix_LoadWAV(MIXER_ROOT "/music/sfx/drmapan.wav");
    if(!m_recorg)
        SDL_Log("Couldn't load m_recorg: %s\n", Mix_GetError());

    m_spotyeah = Mix_LoadWAV(MIXER_ROOT "/music/sfx/sndOnline.wav");
//    m_spotyeah = Mix_LoadWAV(MIXER_ROOT "/music/sfx/sndOnline.aiff");
//    m_spotyeah = Mix_LoadWAV(MIXER_ROOT "/music/sfx/S_Whistle.ogg");
//    m_spotyeah = Mix_LoadWAV(MIXER_ROOT "/music/sfx/GLASS.WAV");
    if(!m_spotyeah)
        SDL_Log("Couldn't load m_spotyeah: %s\n", Mix_GetError());

    playmusVideoUpdate();
    if(!m_recorg || !m_spotyeah)
        SDL_Delay(2000);
}

void closeChunks()
{
    Mix_FreeChunk(m_recorg);
    m_recorg = NULL;
    Mix_FreeChunk(m_spotyeah);
    m_spotyeah = NULL;
}

static SDL_bool enableEffectEcho = SDL_FALSE;
static SpcEcho *effectEcho = NULL;
static Uint16 audio_format;
static int audio_rate;
static int audio_channels;

static void echoEffectDone(int x, void *context)
{
    SpcEcho *out = (SpcEcho *)(context);
    (void)x;
    if(out == effectEcho)
    {
        echoEffectFree(effectEcho);
        effectEcho = NULL;
        enableEffectEcho = SDL_FALSE;
    }
}

void SoundFX_Clear()
{
    if(effectEcho)
    {
        Mix_UnregisterEffect(MIX_CHANNEL_POST, spcEchoEffect);
        if(effectEcho)
        {
            echoEffectFree(effectEcho);
            effectEcho = NULL;
        }
        enableEffectEcho = SDL_FALSE;
    }
}

void SoundFX_SetEcho()
{
    SDL_bool isNew = SDL_FALSE;

    // Clear previously installed effects first
    if(!effectEcho)
    {
        SoundFX_Clear();

        effectEcho = echoEffectInit(audio_rate,
                                    audio_format,
                                    audio_channels);
        isNew = SDL_TRUE;
    }

    if(effectEcho)
    {
/**
fx = echo
echo-on = 1
delay = 4
feedback = 108
main-volume-left = 127
main-volume-right = 127
echo-volume-left = 21
echo-volume-right = 21
fir-0 = -1
fir-1 = 8
fir-2 = 23
fir-3 = 36
fir-4 = 36
fir-5 = 23
fir-6 = 8
fir-7 = -1
*/
        echoEffectSetReg(effectEcho, ECHO_EON, 1);
        echoEffectSetReg(effectEcho, ECHO_EDL, 4);
        echoEffectSetReg(effectEcho, ECHO_EFB, 108);

        echoEffectSetReg(effectEcho, ECHO_MVOLL, 127);
        echoEffectSetReg(effectEcho, ECHO_MVOLR, 127);
        echoEffectSetReg(effectEcho, ECHO_EVOLL, 21);
        echoEffectSetReg(effectEcho, ECHO_EVOLR, 21);

        echoEffectSetReg(effectEcho, ECHO_FIR0, -1);
        echoEffectSetReg(effectEcho, ECHO_FIR1, 8);
        echoEffectSetReg(effectEcho, ECHO_FIR2, 23);
        echoEffectSetReg(effectEcho, ECHO_FIR3, 36);
        echoEffectSetReg(effectEcho, ECHO_FIR4, 36);
        echoEffectSetReg(effectEcho, ECHO_FIR5, 23);
        echoEffectSetReg(effectEcho, ECHO_FIR6, 8);
        echoEffectSetReg(effectEcho, ECHO_FIR7, -1);
        if(isNew)
            Mix_RegisterEffect(MIX_CHANNEL_POST, spcEchoEffect, echoEffectDone, effectEcho);

        enableEffectEcho = SDL_TRUE;
    }
}

static Uint32 getKey()
{
    Uint32 ret = 0;

#if __wii__
    u32 pressed;

    // Call WPAD_ScanPads each loop, this reads the latest controller states
    WPAD_ScanPads();

    // WPAD_ButtonsDown tells us which buttons were pressed in this loop
    // this is a "one shot" state which will not fire again until the button has been released
    pressed = WPAD_ButtonsDown(0);

    if(pressed & WPAD_BUTTON_UP)
        ret |= MIX_KEY_UP;

    if(pressed & WPAD_BUTTON_DOWN)
        ret |= MIX_KEY_DOWN;

    if(pressed & WPAD_BUTTON_LEFT)
        ret |= MIX_KEY_LEFT;

    if(pressed & WPAD_BUTTON_RIGHT)
        ret |= MIX_KEY_RIGHT;

    if(pressed & WPAD_BUTTON_MINUS)
        ret |= MIX_KEY_PLAY_SND1;

    if(pressed & WPAD_BUTTON_PLUS)
        ret |= MIX_KEY_PLAY_SND2;

    if(pressed & WPAD_BUTTON_1)
        ret |= MIX_KEY_STOP;

    if(pressed & WPAD_BUTTON_A)
        ret |= MIX_KEY_PLAY;

    if(pressed & WPAD_BUTTON_B)
        ret |= MIX_KEY_TOGGLE_ECHO;

    if(pressed & WPAD_BUTTON_HOME)
        ret |= MIX_KEY_QUIT;

#elif defined(__WIIU__)

    VPADStatus vpadStatus;
    VPADRead(VPAD_CHAN_0, &vpadStatus, 1, NULL);

    if(vpadStatus.trigger & VPAD_BUTTON_UP)
        ret |= MIX_KEY_UP;

    if(vpadStatus.trigger & VPAD_BUTTON_DOWN)
        ret |= MIX_KEY_DOWN;

    if(vpadStatus.trigger & VPAD_BUTTON_LEFT)
        ret |= MIX_KEY_LEFT;

    if(vpadStatus.trigger & VPAD_BUTTON_RIGHT)
        ret |= MIX_KEY_RIGHT;

    if(vpadStatus.trigger & VPAD_BUTTON_MINUS)
        ret |= MIX_KEY_PLAY_SND1;

    if(vpadStatus.trigger & VPAD_BUTTON_PLUS)
        ret |= MIX_KEY_PLAY_SND2;

    if(vpadStatus.trigger & VPAD_BUTTON_X)
        ret |= MIX_KEY_STOP;

    if(vpadStatus.trigger & VPAD_BUTTON_A)
        ret |= MIX_KEY_PLAY;

    if(vpadStatus.trigger & VPAD_BUTTON_B)
        ret |= MIX_KEY_TOGGLE_ECHO;

    if(vpadStatus.trigger & VPAD_BUTTON_Y)
        ret |= MIX_KEY_QUIT;

    if(vpadStatus.trigger & VPAD_BUTTON_L)
        ret |= MIX_KEY_TOGGLE_TYPE;

#else // Linux

    if(kbhit())
    {
        int key = getch();

        if(key == KEY_UP)
            ret |= MIX_KEY_UP;

        if(key == KEY_DOWN)
            ret |= MIX_KEY_DOWN;

        if(key == KEY_LEFT)
            ret |= MIX_KEY_LEFT;

        if(key == KEY_RIGHT)
            ret |= MIX_KEY_RIGHT;

        if(key == '\n')
            ret |= MIX_KEY_PLAY;

        if(key == KEY_BACKSPACE)
            ret |= MIX_KEY_STOP;

        if(key == '1')
            ret |= MIX_KEY_PLAY_SND1;

        if(key == '2')
            ret |= MIX_KEY_PLAY_SND2;

        if(key == 'e')
            ret |= MIX_KEY_TOGGLE_ECHO;

        if(key == 'r')
            ret |= MIX_KEY_TOGGLE_TYPE;

        if(key == '\x1b')
            ret |= MIX_KEY_QUIT;
    }
#endif

    return ret;
}

void playListMenu()
{
    int cur = -1;
    int cursor = menuCursor;
    Uint32 pressed;

#ifdef SUPER_DEBUG
    SDL_Log("NEXT: printMenu\n");
    playmusVideoUpdate();
    SDL_Delay(1500);
#endif

    printMenu(cursor);

    playmusVideoUpdate();

    while(1)
    {
        pressed = getKey();

        if(cur != cursor)
        {
            cur = cursor;
            menuCursor = cursor;
            printMenu(cur);
        }

        if(pressed & MIX_KEY_UP)
        {
            if(cursor > 0)
                cursor--;
        }
        else if(pressed & MIX_KEY_DOWN)
        {
            if(cursor < musicListTotal - 1)
                cursor++;
        }
        else if(pressed & MIX_KEY_PLAY_SND1)
        {
            if(m_recorg)
                Mix_PlayChannelVol(-1, m_recorg, 0, MIX_MAX_VOLUME);
        }
        else if(pressed & MIX_KEY_PLAY_SND2)
        {
            if(m_spotyeah)
                Mix_PlayChannelVol(-1, m_spotyeah, 0, MIX_MAX_VOLUME);
        }
        else if(pressed & MIX_KEY_STOP)
        {
            printf("\n\n");
            crLine();
            printf("Stopping...\n\n");
            doStop = 1;
            curMusicPrint[0] = 0;
            playmusVideoUpdate();
            break;
        }
        else if(pressed & MIX_KEY_PLAY)
        {
            printf("\n\n");
            crLine();
#if !defined(__WIIU__)
            printf("\33[J");
            fflush(stdout);
#endif
            SDL_snprintf(curMusic, 255, MIXER_ROOT "/music/%s", musicList[cursor]);
            SDL_strlcpy(curMusicPrint, musicList[cursor], sizeof(curMusicPrint));
            printf("Selected song: %s\n\n", curMusic);
            playmusVideoUpdate();
            break;
        }
        else if(pressed & MIX_KEY_TOGGLE_ECHO)
        {
            fx_on = !fx_on;
            if(fx_on)
                SoundFX_SetEcho();
            else
                SoundFX_Clear();
            printMenu(cur);
        }
        else if(pressed & MIX_KEY_TOGGLE_TYPE)
        {
            rwops_on = !rwops_on;
            printMenu(cur);
        }
        else if(pressed & MIX_KEY_QUIT)
        {
            closeChunks();

            printf("\n\n");
            crLine();
#if !defined(__WIIU__)
            printf("\33[J");
            fflush(stdout);
#endif
            printf("Quitting...\n\n");

            playmusVideoUpdate();

            if (music) {
                Mix_FadeOutMusicStream(music, 5000);
                Mix_FreeMusic(music);
                music = NULL;
            }

            CleanUp(0);
        }

        playmusVideoUpdate();
    }
}

void Menu(void)
{
    char buf[10];

    printf("Available commands: (p)ause (r)esume (h)alt volume(v#) > ");
    fflush(stdin);
    if (scanf("%s",buf) == 1) {
        switch(buf[0]){
        case 'p': case 'P':
            Mix_PauseMusicStream(music);
            break;
        case 'r': case 'R':
            Mix_ResumeMusicStream(music);
            break;
        case 'h': case 'H':
            Mix_HaltMusicStream(music);
            break;
        case 'v': case 'V':
            Mix_VolumeMusicStream(music, atoi(buf+1));
            break;
        case 'n': case 'N':
            next_track++;
            break;
        }
    }
    printf("Music playing: %s Paused: %s\n", Mix_PlayingMusicStream(music) ? "yes" : "no",
           Mix_PausedMusicStream(music) ? "yes" : "no");
}

#ifdef HAVE_SIGNAL_H

void IntHandler(int sig)
{
    switch (sig) {
    case SIGINT:
        next_track++;
        break;
    }
}

#endif

#ifdef __wii__
static void* xfb = NULL;
static GXRModeObj* rmode = NULL;

static void playmusVideoInit()
{
    // Initialise the video system
    VIDEO_Init();

    // Initialise the attached controllers
    WPAD_Init();
    // Obtain the preferred video mode from the system
    // This will correspond to the settings in the Wii menu
    rmode = VIDEO_GetPreferredMode(NULL);

    // Allocate memory for the display in the uncached region
    xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

    // Initialise the console, required for printf
    console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);

    // Set up the video registers with the chosen mode
    VIDEO_Configure(rmode);

    // Tell the video hardware where our display memory is
    VIDEO_SetNextFramebuffer(xfb);

    // Make the display visible
    VIDEO_SetBlack(FALSE);

    // Flush the video register changes to the hardware
    VIDEO_Flush();

    // Wait for Video setup to complete
    VIDEO_WaitVSync();

    if (rmode->viTVMode & VI_NON_INTERLACE) {
        VIDEO_WaitVSync();
    }

    // The console understands VT terminal escape codes
    // This positions the cursor on row 2, column 0
    // we can use variables for this with format codes too
    // e.g. printf ("\x1b[%d;%dH", row, column );
    printf("\x1b[2;0H");

    if (!fatInitDefault()) {
        printf("fatInitDefault failure: terminating\n");
        CleanUp(2);
    }
}

static void playmusVideoUpdate()
{
    VIDEO_WaitVSync();
}

#elif defined(__WIIU__)

static void wiiuLogFunction(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
    (void)userdata;
    (void)category;

    switch(priority)
    {
    case SDL_LOG_PRIORITY_DEBUG:
        printf("DEBUG: %s\n", message);
        break;
    case SDL_LOG_PRIORITY_INFO:
        printf("INFO: %s\n", message);
        break;
    case SDL_LOG_PRIORITY_WARN:
        printf("WARN: %s\n", message);
        break;
    case SDL_LOG_PRIORITY_ERROR:
        printf("ERR: %s\n", message);
        break;
    case SDL_LOG_PRIORITY_CRITICAL:
        printf("CRIT: %s\n", message);
        break;
    default:
        printf("LOG:%s\n", message);
        break;
    }

    WHBLogConsoleDraw();
}

static void playmusVideoInit()
{
    ACConfigId configId;

    ACInitialize();
    ACGetStartupId(&configId);
    ACConnect();

    WHBProcInit();
    WHBLogConsoleInit();

    VPADInit();
}

static void playmusVideoUpdate()
{
    WHBLogConsoleDraw();
    WHBProcIsRunning();
}

#else

static int kbhit(void)
{
    int ch = getch();

    if (ch != ERR) {
        ungetch(ch);
        return 1;
    } else {
        return 0;
    }
}

static void playmusVideoInit()
{
    SDL_setenv("TERMINFO", "/usr/share/terminfo", 1);
    SDL_setenv("TERM", "linux", 1);
    initscr();

    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    scrollok(stdscr, TRUE);
}

static void playmusVideoUpdate()
{
    fflush(stdout);
    refresh();
}
#endif

int main(int argc, char *argv[])
{
    Mix_Music* new_music;
    int audio_buffers;
    int audio_volume = MIX_MAX_VOLUME;
    int looping = 0;
    int interactive = 0;
    int rwops = 0;
    int multimusic = 0;
    int multimusic_actives = 0;
    int playListMode = 0;
    int crossfade = 0;
    int i = 1;
    const char *typ;
    const char *tag_title = NULL;
    const char *tag_artist = NULL;
    const char *tag_album = NULL;
    const char *tag_copyright = NULL;
    double loop_start, loop_end, loop_length, current_position;

    (void) argc;
    SDL_memset(multi_music, 0, sizeof(Mix_Music*));

    /* Initialize variables */
    audio_rate = MIX_DEFAULT_FREQUENCY;
    audio_format = MIX_DEFAULT_FORMAT;
    audio_channels = MIX_DEFAULT_CHANNELS;
    audio_buffers = 4096;

    /* Check command line usage */
    if(argc > 1)
    {
        for (i=1; argv[i] && (*argv[i] == '-'); ++i) {
            if ((strcmp(argv[i], "-r") == 0) && argv[i+1]) {
                ++i;
                audio_rate = atoi(argv[i]);
            } else
            if (strcmp(argv[i], "-m") == 0) {
                audio_channels = 1;
            } else
            if ((strcmp(argv[i], "-c") == 0) && argv[i+1]) {
                ++i;
                audio_channels = atoi(argv[i]);
            } else
            if ((strcmp(argv[i], "-b") == 0) && argv[i+1]) {
                ++i;
                audio_buffers = atoi(argv[i]);
            } else
            if ((strcmp(argv[i], "-v") == 0) && argv[i+1]) {
                ++i;
                audio_volume = atoi(argv[i]);
            } else
            if (strcmp(argv[i], "-l") == 0) {
                looping = -1;
            } else
            if (strcmp(argv[i], "-i") == 0) {
                interactive = 1;
            } else
            if (strcmp(argv[i], "-8") == 0) {
                audio_format = AUDIO_U8;
            } else
            if (strcmp(argv[i], "-f32") == 0) {
                audio_format = AUDIO_F32;
            } else
            if (strcmp(argv[i], "-rwops") == 0) {
                rwops = 1;
            } else
            if (strcmp(argv[i], "-mm") == 0) {
                multimusic = 1;
            } else
            if (strcmp(argv[i], "-cf") == 0) {
                crossfade = 1;
            } else {
                Usage(argv[0]);
                return(1);
            }
        }
    }
    else
    {
        playListMode = 1;
        i = 1;
    }

    playmusVideoInit();

//    listDir("/");

//    listDir(MIXER_ROOT "/music/");

    if (!playListMode && !argv[i]) {
        Usage(argv[0]);
        playmusVideoUpdate();
        waitForHome();
        return(1);
    }


    /* Initialize the SDL library */
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        SDL_Log("Couldn't initialize SDL: %s\n",SDL_GetError());
        playmusVideoUpdate();
        waitForHome();
        return(255);
    }

#ifdef __WIIU__
    SDL_LogSetOutputFunction(&wiiuLogFunction, NULL);
#endif

#ifdef HAVE_SIGNAL_H
    signal(SIGINT, IntHandler);
    signal(SIGTERM, CleanUp);
#endif

    /* Open the audio device */
    if (Mix_OpenAudio(audio_rate, audio_format, audio_channels, audio_buffers) < 0) {
        SDL_Log("Couldn't open audio: %s\n", SDL_GetError());
        playmusVideoUpdate();
        waitForHome();
        return(2);
    } else {
        Mix_QuerySpec(&audio_rate, &audio_format, &audio_channels);
        SDL_Log("Opened audio at %d Hz %d bit%s %s %d bytes audio buffer\n", audio_rate,
            (audio_format&0xFF),
            (SDL_AUDIO_ISFLOAT(audio_format) ? " (float)" : ""),
            (audio_channels > 2) ? "surround" : (audio_channels > 1) ? "stereo" : "mono",
            audio_buffers);
    }
    audio_open = 1;

#ifdef SUPER_DEBUG
    SDL_Log("NEXT: Mix_VolumeMusic\n");
    playmusVideoUpdate();
    SDL_Delay(1500);
#endif

    /* Set the music volume */
    Mix_VolumeMusic(audio_volume);

    /* Set the external music player, if any */
    Mix_SetMusicCMD(SDL_getenv("MUSIC_CMD"));

    SDL_Delay(1000);

    if (playListMode)
    {
        crossfade = 1;
        looping = -1;

#ifdef SUPER_DEBUG
        SDL_Log("NEXT: Load chunks\n");
        playmusVideoUpdate();
        SDL_Delay(1500);
#endif

        loadChunks();

#ifdef SUPER_DEBUG
        SDL_Log("NEXT: listDir\n");
        playmusVideoUpdate();
        SDL_Delay(1500);
#endif

        listDir(MIXER_ROOT "/music/");

        while(1)
        {
#ifdef SUPER_DEBUG
            SDL_Log("NEXT: playList Menu\n");
            playmusVideoUpdate();
            SDL_Delay(1500);
#endif

            playListMenu();

            if (doStop) {
                doStop = 0;
                if (music) {
                    Mix_HaltMusicStream(music);
                    Mix_FreeMusic(music);
                    music = NULL;
                }
                music_prev = NULL;
                SDL_Delay(1);
                continue;
            }

#ifdef __WIIU__
            printf("Loading %s...", curMusicPrint);
            playmusVideoUpdate();
#endif

            if(rwops_on)
                new_music = Mix_LoadMUS_RW(SDL_RWFromFile(curMusic, "rb"), SDL_TRUE);
            else
                new_music = Mix_LoadMUS(curMusic);

            if (new_music == NULL) {
                printf("                                                                       \r");

#ifdef __WIIU__
                SDL_Log("Couldn't load %s\n", curMusicPrint);
                SDL_Log("%s\n", SDL_GetError());
                waitForAnyKey();
#else
                SDL_Log("Couldn't load %s: %s\n", curMusicPrint, SDL_GetError());
#endif
                continue;
            }

            music = new_music;

            switch (Mix_GetMusicType(music)) {
            case MUS_CMD:
                typ = "CMD";
                break;
            case MUS_WAV:
                typ = "WAV";
                break;
            case MUS_MOD:
            case MUS_MODPLUG_UNUSED:
                typ = "MOD";
                break;
            case MUS_FLAC:
                typ = "FLAC";
                break;
            case MUS_MID:
                typ = "MIDI";
                break;
            case MUS_OGG:
                typ = "OGG Vorbis";
                break;
            case MUS_MP3:
            case MUS_MP3_MAD_UNUSED:
                typ = "MP3";
                break;
            case MUS_OPUS:
                typ = "OPUS";
                break;
            case MUS_WAVPACK:
                typ = "WAVPACK";
                break;
            case MUS_GME:
                typ = "GME";
                break;
            case MUS_ADLMIDI:
                typ = "IMF/MUS/XMI";
                break;
            case MUS_FFMPEG:
                typ = "FFMPEG";
                break;
            case MUS_PXTONE:
                typ = "PXTONE";
                break;
            case MUS_NONE:
            default:
                typ = "NONE";
                break;
            }
            printf("                                                                       \r");
            SDL_Log("Detected music type: %s", typ);

            tag_title = Mix_GetMusicTitleTag(music);
            if (tag_title && SDL_strlen(tag_title) > 0) {
                printf("                                                                       \r");
                SDL_Log("Title: %s", tag_title);
            }

            tag_artist = Mix_GetMusicArtistTag(music);
            if (tag_artist && SDL_strlen(tag_artist) > 0) {
                printf("                                                                       \r");
                SDL_Log("Artist: %s", tag_artist);
            }

            tag_album = Mix_GetMusicAlbumTag(music);
            if (tag_album && SDL_strlen(tag_album) > 0) {
                printf("                                                                       \r");
                SDL_Log("Album: %s", tag_album);
            }

            tag_copyright = Mix_GetMusicCopyrightTag(music);
            if (tag_copyright && SDL_strlen(tag_copyright) > 0) {
                printf("                                                                       \r");
                SDL_Log("Copyright: %s", tag_copyright);
            }

            loop_start = Mix_GetMusicLoopStartTime(music);
            loop_end = Mix_GetMusicLoopEndTime(music);
            loop_length = Mix_GetMusicLoopLengthTime(music);

            /* Play and then exit */
            printf("                                                                       \r");
            SDL_Log("Playing %s, duration %f\n", curMusic, Mix_MusicDuration(music));
            if (loop_start > 0.0 && loop_end > 0.0 && loop_length > 0.0) {
                printf("                                                                       \r");
                SDL_Log("Loop points: start %g s, end %g s, length %g s\n", loop_start, loop_end, loop_length);
            }

            if (music_prev) {
                Mix_CrossFadeMusicStream(music_prev, music, looping, 5000, 1);
            } else {
                Mix_FadeInMusicStream(music, looping, 0);
            }
            music_prev = music;

            SDL_Delay(1);
        }
    }

    else if (multimusic) { /* Play multiple streams at once */
        while (argv[i]) {
            /* Load the requested music file */
            if (rwops) {
                multi_music[multi_music_count] = Mix_LoadMUS_RW(SDL_RWFromFile(argv[i], "rb"), SDL_TRUE);
            } else {
                multi_music[multi_music_count] = Mix_LoadMUS(argv[i]);
            }

            if (multi_music[multi_music_count] == NULL) {
                SDL_Log("Couldn't load %s: %s\n",
                    argv[i], SDL_GetError());
                CleanUp(2);
            }
            Mix_FadeInMusicStream(multi_music[multi_music_count], looping, 2000);
            multi_music_count++;
            i++;
        }

        do
        {
            printf("Positions: ");
            for(i = 0; i < multi_music_count; ++i) {
                current_position = Mix_GetMusicPosition(multi_music[i]);
                if (current_position >= 0.0) {
                    printf("%5.1f;", current_position);
                }
            }

            multimusic_actives = 0;
            for(i = 0; i < multi_music_count; ++i) {
                multimusic_actives += Mix_PlayingMusicStream(multi_music[i]) || Mix_PausedMusicStream(multi_music[i]) ? 1 : 0;
            }

            printf("--(%d) \r", multimusic_actives);
            fflush(stdout);
            SDL_Delay(100);

        } while(!next_track && multimusic_actives > 0);

        for (i = 0; i < multi_music_count; ++i) {
            Mix_FreeMusic(multi_music[i]);
            multi_music_count = 0;
        }

    } else
    while (argv[i]) {
        next_track = 0;

        /* Load the requested music file */
        if (rwops) {
            music = Mix_LoadMUS_RW(SDL_RWFromFile(argv[i], "rb"), SDL_TRUE);
        } else {
            music = Mix_LoadMUS(argv[i]);
        }
        if (music == NULL) {
            SDL_Log("Couldn't load %s: %s\n",
                argv[i], SDL_GetError());
            CleanUp(2);
            playmusVideoUpdate();
        }

        switch (Mix_GetMusicType(music)) {
        case MUS_CMD:
            typ = "CMD";
            break;
        case MUS_WAV:
            typ = "WAV";
            break;
        case MUS_MOD:
        case MUS_MODPLUG_UNUSED:
            typ = "MOD";
            break;
        case MUS_FLAC:
            typ = "FLAC";
            break;
        case MUS_MID:
            typ = "MIDI";
            break;
        case MUS_OGG:
            typ = "OGG Vorbis";
            break;
        case MUS_MP3:
        case MUS_MP3_MAD_UNUSED:
            typ = "MP3";
            break;
        case MUS_OPUS:
            typ = "OPUS";
            break;
        case MUS_GME:
            typ = "GME";
            break;
        case MUS_ADLMIDI:
            typ = "IMF/MUS/XMI";
            break;
        case MUS_FFMPEG:
            typ = "FFMPEG";
            break;
        case MUS_PXTONE:
            typ = "PXTONE";
            break;
        case MUS_NONE:
        default:
            typ = "NONE";
            break;
        }
        SDL_Log("Detected music type: %s", typ);

        tag_title = Mix_GetMusicTitleTag(music);
        if (tag_title && SDL_strlen(tag_title) > 0) {
            SDL_Log("Title: %s", tag_title);
        }

        tag_artist = Mix_GetMusicArtistTag(music);
        if (tag_artist && SDL_strlen(tag_artist) > 0) {
            SDL_Log("Artist: %s", tag_artist);
        }

        tag_album = Mix_GetMusicAlbumTag(music);
        if (tag_album && SDL_strlen(tag_album) > 0) {
            SDL_Log("Album: %s", tag_album);
        }

        tag_copyright = Mix_GetMusicCopyrightTag(music);
        if (tag_copyright && SDL_strlen(tag_copyright) > 0) {
            SDL_Log("Copyright: %s", tag_copyright);
        }

        loop_start = Mix_GetMusicLoopStartTime(music);
        loop_end = Mix_GetMusicLoopEndTime(music);
        loop_length = Mix_GetMusicLoopLengthTime(music);

        /* Play and then exit */
        SDL_Log("Playing %s, duration %f\n", argv[i], Mix_MusicDuration(music));
        if (loop_start > 0.0 && loop_end > 0.0 && loop_length > 0.0) {
            SDL_Log("Loop points: start %g s, end %g s, length %g s\n", loop_start, loop_end, loop_length);
        }
        if (crossfade) {
            if (music_prev) {
                Mix_CrossFadeMusicStream(music_prev, music, looping, 5000, 1);
            } else {
                Mix_FadeInMusicStream(music, looping, 2000);
            }
            music_prev = music;
        } else {
            Mix_FadeInMusic(music,looping,2000);
        }
        while (!next_track && (Mix_PlayingMusicStream(music) || Mix_PausedMusicStream(music))) {
            if(interactive)
                Menu();
            else {
                current_position = Mix_GetMusicPosition(music);
                if (current_position >= 0.0) {
                    printf("Position: %g seconds             \r", current_position);
                    fflush(stdout);
                }
                SDL_Delay(100);
            }

            if(ifHomePressed())
                next_track++;
        }
        if (!crossfade) {
            Mix_FreeMusic(music);
            music = NULL;
        }

        if(ifHomePressed())
        {
            next_track++;
        }

        /* If the user presses Ctrl-C more than once, exit. */
        // SDL_Delay(500);

        // Wait for the next frame
        playmusVideoUpdate();

        if(next_track > 1)
            break;

        i++;
    }

    if (crossfade) {
        Mix_FadeOutMusicStream(music, 5000);
        Mix_FreeMusic(music);
        music = NULL;
    }

    playmusVideoUpdate();
    CleanUp(0);

    /* Not reached, but fixes compiler warnings */
    return 0;
}

/* vi: set ts=4 sw=4 expandtab: */
