/*
    Copyright 2016-2020 Arisotura

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SDL2/SDL.h"
#include "../Platform.h"
#include "PlatformConfig.h"
#include "LAN_Socket.h"
#include "LAN_PCap.h"
#include "libui/ui.h"
#include <string>

#ifdef __WIN32__
    #define NTDDI_VERSION		NTDDI_WIN6 // GROSS FUCKING HACK
    #define _WIN32_WINNT _WIN32_WINNT_WIN6
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    //#include <knownfolders.h> // FUCK THAT SHIT
    extern "C" const GUID DECLSPEC_SELECTANY FOLDERID_RoamingAppData = {0x3eb685db, 0x65f9, 0x4cf6, {0xa0, 0x3a, 0xe3, 0xef, 0x65, 0x72, 0x9f, 0x3d}};
    #include <shlobj.h>
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#define socket_t    SOCKET
	#define sockaddr_t  SOCKADDR
#else
    #include <glib.h>
	#include <unistd.h>
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/select.h>
	#include <sys/socket.h>
	#define socket_t    int
	#define sockaddr_t  struct sockaddr
	#define closesocket close
#endif

#ifndef INVALID_SOCKET
#define INVALID_SOCKET  (socket_t)-1
#endif


extern char* EmuDirectory;

void Stop(bool internal);


namespace Platform
{


typedef struct
{
    SDL_Thread* ID;
    void (*Func)();

} ThreadData;

int ThreadEntry(void* data)
{
    ThreadData* thread = (ThreadData*)data;
    thread->Func();
    return 0;
}


socket_t MPSocket;
sockaddr_t MPSendAddr;
u8 PacketBuffer[2048];

#define NIFI_VER 1


void StopEmu()
{
    Stop(true);
}


FILE* OpenFile(const char* path, const char* mode, bool mustexist)
{
    FILE* ret;

#ifdef __WIN32__

    int len = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (len < 1) return NULL;
    WCHAR* fatpath = new WCHAR[len];
    int res = MultiByteToWideChar(CP_UTF8, 0, path, -1, fatpath, len);
    if (res != len) { delete[] fatpath; return NULL; } // checkme?

    // this will be more than enough
    WCHAR fatmode[4];
    fatmode[0] = mode[0];
    fatmode[1] = mode[1];
    fatmode[2] = mode[2];
    fatmode[3] = 0;

    if (mustexist)
    {
        ret = _wfopen(fatpath, L"rb");
        if (ret) ret = _wfreopen(fatpath, fatmode, ret);
    }
    else
        ret = _wfopen(fatpath, fatmode);

    delete[] fatpath;

#else

    if (mustexist)
    {
        ret = fopen(path, "rb");
        if (ret) ret = freopen(path, mode, ret);
    }
    else
        ret = fopen(path, mode);

#endif

    return ret;
}

#if !defined(UNIX_PORTABLE) && !defined(__WIN32__)

FILE* OpenLocalFile(const char* path, const char* mode)
{
    std::string fullpath;
    if (path[0] == '/')
    {
        // If it's an absolute path, just open that.
        fullpath = std::string(path);
    }
    else
    {
        // Check user configuration directory
        std::string confpath = std::string(g_get_user_config_dir()) + "/melonDS/";
        g_mkdir_with_parents(confpath.c_str(), 0755);
        fullpath = confpath + path;
    }

    return OpenFile(fullpath.c_str(), mode, mode[0] != 'w');
}

FILE* OpenDataFile(const char* path)
{
    const char* melondir = "melonDS";
    const char* const* sys_dirs = g_get_system_data_dirs();
    const char* user_dir = g_get_user_data_dir();

    // First check the user's data directory
    char* fullpath = g_build_path("/", user_dir, melondir, path, NULL);
    if (access(fullpath, R_OK) == 0)
    {
        FILE* f = fopen(fullpath, "r");
        g_free(fullpath);
        return f;
    }
    free(fullpath);

    // Then check the system data directories
    for (size_t i = 0; sys_dirs[i] != NULL; i++)
    {
        const char* dir = sys_dirs[i];
        char* fullpath = g_build_path("/", dir, melondir, path, NULL);

        if (access(fullpath, R_OK) == 0)
        {
            FILE* f = fopen(fullpath, "r");
            g_free(fullpath);
            return f;
        }
        free(fullpath);
    }

	FILE* f = fopen(path, "rb");
	if (f) return f;

    return NULL;
}

#else

FILE* OpenLocalFile(const char* path, const char* mode)
{
    bool relpath = false;
    int pathlen = strlen(path);

#ifdef __WIN32__
    if (pathlen > 3)
    {
        if (path[1] == ':' && path[2] == '\\')
            return OpenFile(path, mode);
    }
#else
    if (pathlen > 1)
    {
        if (path[0] == '/')
            return OpenFile(path, mode);
    }
#endif

    if (pathlen >= 3)
    {
        if (path[0] == '.' && path[1] == '.' && (path[2] == '/' || path[2] == '\\'))
            relpath = true;
    }

    int emudirlen = strlen(EmuDirectory);
    char* emudirpath;
    if (emudirlen)
    {
        int len = emudirlen + 1 + pathlen + 1;
        emudirpath = new char[len];
        strncpy(&emudirpath[0], EmuDirectory, emudirlen);
        emudirpath[emudirlen] = '/';
        strncpy(&emudirpath[emudirlen+1], path, pathlen);
        emudirpath[emudirlen+1+pathlen] = '\0';
    }
    else
    {
        emudirpath = new char[pathlen+1];
        strncpy(&emudirpath[0], path, pathlen);
        emudirpath[pathlen] = '\0';
    }

    // Locations are application directory, and AppData/melonDS on Windows or XDG_CONFIG_HOME/melonDS on Linux

    FILE* f;

    // First check current working directory
    f = OpenFile(path, mode, true);
    if (f) { delete[] emudirpath; return f; }

    // then emu directory
    f = OpenFile(emudirpath, mode, true);
    if (f) { delete[] emudirpath; return f; }

#ifdef __WIN32__

    // a path relative to AppData wouldn't make much sense
    if (!relpath)
    {
        // Now check AppData
        PWSTR appDataPath = NULL;
        SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &appDataPath);
        if (!appDataPath)
        {
            delete[] emudirpath;
            return NULL;
        }

        // this will be more than enough
        WCHAR fatperm[4];
        fatperm[0] = mode[0];
        fatperm[1] = mode[1];
        fatperm[2] = mode[2];
        fatperm[3] = 0;

        int fnlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
        if (fnlen < 1) { delete[] emudirpath; return NULL; }
        WCHAR* wfileName = new WCHAR[fnlen];
        int res = MultiByteToWideChar(CP_UTF8, 0, path, -1, wfileName, fnlen);
        if (res != fnlen) { delete[] wfileName; delete[] emudirpath; return NULL; } // checkme?

        const WCHAR* appdir = L"\\melonDS\\";

        int pos = wcslen(appDataPath);
        void* ptr = CoTaskMemRealloc(appDataPath, (pos+wcslen(appdir)+fnlen+1)*sizeof(WCHAR));
        if (!ptr) { delete[] wfileName; delete[] emudirpath; return NULL; } // oh well
        appDataPath = (PWSTR)ptr;

        wcscpy(&appDataPath[pos], appdir); pos += wcslen(appdir);
        wcscpy(&appDataPath[pos], wfileName);

        f = _wfopen(appDataPath, L"rb");
        if (f) f = _wfreopen(appDataPath, fatperm, f);
        CoTaskMemFree(appDataPath);
        delete[] wfileName;
        if (f) { delete[] emudirpath; return f; }
    }

#else

    if (!relpath)
    {
        // Now check XDG_CONFIG_HOME
        // TODO: check for memory leak there
        std::string fullpath = std::string(g_get_user_config_dir()) + "/melonDS/" + path;
        f = OpenFile(fullpath.c_str(), mode, true);
        if (f) { delete[] emudirpath; return f; }
    }

#endif

    if (mode[0] != 'r')
    {
        f = OpenFile(emudirpath, mode);
        if (f) { delete[] emudirpath; return f; }
    }

    delete[] emudirpath;
    return NULL;
}

FILE* OpenDataFile(const char* path)
{
	return OpenLocalFile(path, "rb");
}

#endif


void* Thread_Create(void (*func)())
{
    ThreadData* data = new ThreadData;
    data->Func = func;
    data->ID = SDL_CreateThread(ThreadEntry, "melonDS core thread", data);
    return data;
}

void Thread_Free(void* thread)
{
    delete (ThreadData*)thread;
}

void Thread_Wait(void* thread)
{
    SDL_WaitThread((SDL_Thread*)((ThreadData*)thread)->ID, NULL);
}


void* Semaphore_Create()
{
    return SDL_CreateSemaphore(0);
}

void Semaphore_Free(void* sema)
{
    SDL_DestroySemaphore((SDL_sem*)sema);
}

void Semaphore_Reset(void* sema)
{
    while (SDL_SemTryWait((SDL_sem*)sema) == 0);
}

void Semaphore_Wait(void* sema)
{
    SDL_SemWait((SDL_sem*)sema);
}

void Semaphore_Post(void* sema)
{
    SDL_SemPost((SDL_sem*)sema);
}


void* GL_GetProcAddress(const char* proc)
{
    return uiGLGetProcAddress(proc);
}


bool MP_Init()
{
    int opt_true = 1;
    int res;

#ifdef __WIN32__
    WSADATA wsadata;
    if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)
    {
        return false;
    }
#endif // __WIN32__

    MPSocket = socket(AF_INET, SOCK_DGRAM, 0);
	if (MPSocket < 0)
	{
		return false;
	}

	res = setsockopt(MPSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt_true, sizeof(int));
	if (res < 0)
	{
		closesocket(MPSocket);
		MPSocket = INVALID_SOCKET;
		return false;
	}

	sockaddr_t saddr;
	saddr.sa_family = AF_INET;
	*(u32*)&saddr.sa_data[2] = htonl(Config::SocketBindAnyAddr ? INADDR_ANY : INADDR_LOOPBACK);
	*(u16*)&saddr.sa_data[0] = htons(7064);
	res = bind(MPSocket, &saddr, sizeof(sockaddr_t));
	if (res < 0)
	{
		closesocket(MPSocket);
		MPSocket = INVALID_SOCKET;
		return false;
	}

	res = setsockopt(MPSocket, SOL_SOCKET, SO_BROADCAST, (const char*)&opt_true, sizeof(int));
	if (res < 0)
	{
		closesocket(MPSocket);
		MPSocket = INVALID_SOCKET;
		return false;
	}

	MPSendAddr.sa_family = AF_INET;
	*(u32*)&MPSendAddr.sa_data[2] = htonl(INADDR_BROADCAST);
	*(u16*)&MPSendAddr.sa_data[0] = htons(7064);

	return true;
}

void MP_DeInit()
{
    if (MPSocket >= 0)
        closesocket(MPSocket);

#ifdef __WIN32__
    WSACleanup();
#endif // __WIN32__
}

int MP_SendPacket(u8* data, int len)
{
    if (MPSocket < 0)
        return 0;

    if (len > 2048-8)
    {
        printf("MP_SendPacket: error: packet too long (%d)\n", len);
        return 0;
    }

    *(u32*)&PacketBuffer[0] = htonl(0x4946494E); // NIFI
    PacketBuffer[4] = NIFI_VER;
    PacketBuffer[5] = 0;
    *(u16*)&PacketBuffer[6] = htons(len);
    memcpy(&PacketBuffer[8], data, len);

    int slen = sendto(MPSocket, (const char*)PacketBuffer, len+8, 0, &MPSendAddr, sizeof(sockaddr_t));
    if (slen < 8) return 0;
    return slen - 8;
}

int MP_RecvPacket(u8* data, bool block)
{
    if (MPSocket < 0)
        return 0;

    fd_set fd;
	struct timeval tv;

	FD_ZERO(&fd);
	FD_SET(MPSocket, &fd);
	tv.tv_sec = 0;
	tv.tv_usec = block ? 5000 : 0;

	if (!select(MPSocket+1, &fd, 0, 0, &tv))
    {
        return 0;
    }

    sockaddr_t fromAddr;
    socklen_t fromLen = sizeof(sockaddr_t);
    int rlen = recvfrom(MPSocket, (char*)PacketBuffer, 2048, 0, &fromAddr, &fromLen);
    if (rlen < 8+24)
    {
        return 0;
    }
    rlen -= 8;

    if (ntohl(*(u32*)&PacketBuffer[0]) != 0x4946494E)
    {
        return 0;
    }

    if (PacketBuffer[4] != NIFI_VER)
    {
        return 0;
    }

    if (ntohs(*(u16*)&PacketBuffer[6]) != rlen)
    {
        return 0;
    }

    memcpy(data, &PacketBuffer[8], rlen);
    return rlen;
}



bool LAN_Init()
{
    if (Config::DirectLAN)
    {
        if (!LAN_PCap::Init(true))
            return false;
    }
    else
    {
        if (!LAN_Socket::Init())
            return false;
    }

    return true;
}

void LAN_DeInit()
{
    // checkme. blarg
    //if (Config::DirectLAN)
    //    LAN_PCap::DeInit();
    //else
    //    LAN_Socket::DeInit();
    LAN_PCap::DeInit();
    LAN_Socket::DeInit();
}

int LAN_SendPacket(u8* data, int len)
{
    if (Config::DirectLAN)
        return LAN_PCap::SendPacket(data, len);
    else
        return LAN_Socket::SendPacket(data, len);
}

int LAN_RecvPacket(u8* data)
{
    if (Config::DirectLAN)
        return LAN_PCap::RecvPacket(data);
    else
        return LAN_Socket::RecvPacket(data);
}


}
