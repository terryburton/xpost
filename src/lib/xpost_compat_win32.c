/*
 * Xpost - a Level-2 Postscript interpreter
 * Copyright (C) 2013-2016, Michael Joshua Ryan
 * Copyright (C) 2013-2016, Vincent Torri
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the Xpost software product nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>  /* FILE, TMP_MAX */
#include <stdlib.h> /* free, getenv, malloc, */
#include <string.h> /* strlen, memcpy */

#include <windows.h>
#include <io.h> /* _open */
#include <fcntl.h> /* O_CREAT, etc... */
#include <sys/stat.h> /* S_IREAD, S_IWRITE */

#include "xpost_compat.h"


/*============================================================================*
 *                                  Local                                     *
 *============================================================================*/

static long long _xpost_time_freq;
static long long _xpost_time_start;
/* the execution the process had already had when the interpreter
   started. usertime counts from there, so that it answers what this
   interpreter has done rather than what the process did before it. */
static long long _xpost_cpu_start;
static BCRYPT_ALG_HANDLE _xpost_bcrypt_provider;

/* The execution the process has had, in milliseconds: the time it spent
   running its own instructions and the time the system spent running on
   its behalf, which together are what it has consumed. Both are counted
   in hundreds of nanoseconds. */
static long long
_xpost_cpu_ms(void)
{
    FILETIME created;
    FILETIME exited;
    FILETIME kernel;
    FILETIME user;
    ULARGE_INTEGER k;
    ULARGE_INTEGER u;

    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited,
                         &kernel, &user))
        return 0;

    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;

    return (long long)((k.QuadPart + u.QuadPart) / 10000ULL);
}

static int
_xpost_mkstemp_fill(char *template)
{
    char *buf;

    buf = template;
    while (*buf)
    {
        unsigned char val;

        if (*buf != 'X')
            return 0;

        /*
         * Only characters from 'a' to 'z' and '0' to '9' are considered
         * because on Windows, file system is case insensitive. That means
         * 36 possible values.
         * To increase randomness, we consider the greatest multiple of 36
         * within 255 : 7*36 = 252, that is, values from 0 to 251 and choose
         * a random value in this interval.
         */
        do {
            BCryptGenRandom(_xpost_bcrypt_provider, &val, sizeof(UCHAR), 0);
        } while (val > 251);

        val = '0' + val % 36;
        if (val > '9')
            val += 'a' - '9' - 1;

        *buf = val;
        buf++;
    }

    return 1;
}

/*============================================================================*
 *                                 Global                                     *
 *============================================================================*/

int
xpost_compat_init(void)
{
    LARGE_INTEGER freq;
    LARGE_INTEGER count;
    WSADATA wsa_data;

    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
        return 0;

    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&_xpost_bcrypt_provider,
                                                    BCRYPT_RNG_ALGORITHM,
                                                    NULL, 0)))
        return 0;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    _xpost_time_freq = (long long)freq.QuadPart;
    _xpost_time_start = (long long)count.QuadPart;
    _xpost_cpu_start = _xpost_cpu_ms();

    return 1;
}

void
xpost_compat_quit(void)
{
    BCryptCloseAlgorithmProvider(_xpost_bcrypt_provider, 0);
    WSACleanup();
}

void
xpost_fpurge(FILE *f)
{
    /* Windows has no fpurge()/__fpurge(). On the MSVCRT runtime the mingw
       toolchains build against, fflush() on a stream open for input discards
       the buffered but unread characters -- the same discard-and-skip effect
       __fpurge() has on POSIX, on both regular files and pipes (measured
       identical on the two runtimes). resetfile purges the input side
       (currentfile), which is what this serves. Two boundaries, both harmless
       here: on an output stream fflush() writes the buffer rather than
       discarding it, but the disk files xpost opens for writing (the device
       writers) are never reset; and under the UCRT, fflush() on input is a
       conformant no-op, which is exactly the behaviour this replaces. */
    fflush(f);
}

/* The clock's origin is arbitrary (PLRM 8.2 realtime), and it takes the
   interpreter's own start for it. Counting from there rather than from
   the counter's own zero divides by the frequency once, so an interval
   is not lengthened or shortened by the millisecond a second division
   would truncate away. */
long long
xpost_get_realtime_ms(void)
{
    LARGE_INTEGER count;

    QueryPerformanceCounter(&count);
    return ((count.QuadPart - _xpost_time_start) * 1000LL) / _xpost_time_freq;
}

/* PLRM 8.2 usertime counts the execution the interpreter has done, one
   for every millisecond of it, which is the processor time the process
   has been given and not the time that has passed. */
long long
xpost_get_usertime_ms(void)
{
    long long now = _xpost_cpu_ms();

    /* the clock counts up from the interpreter's start, so a reading
       below that start is no reading at all */
    if (now < _xpost_cpu_start)
        return 0;

    return now - _xpost_cpu_start;
}

int
xpost_mkstemp(char *template, int *fd)
{
    char *tmpdir;
    char *filename = NULL;
    char *iter;
    size_t len;
    size_t len_tmp;
    int f = -1;
    int count = TMP_MAX;

    if (!template || !*template)
        return 0;

    len = strlen(template);

    if ((tmpdir = getenv("TEMP")) || (tmpdir = getenv("TMP")))
    {
        len_tmp = strlen(tmpdir);
        /* path is $(tmpdir)\xpost_$(template) */
        filename = (char *)malloc(len_tmp + 7 /* \xpost_ */ + len + 1);
        if (filename)
        {
            iter = filename;
            memcpy(iter, tmpdir, len_tmp);
            iter += len_tmp;
            memcpy(iter, "\\xpost_", 7);
            iter += 7;
            memcpy(iter, template, len + 1);
        }
    }
    else if ((tmpdir = getenv("LOCALAPPDATA")))
    {
        len_tmp = strlen(tmpdir);
        /* path is $(tmpdir)\Temp\xpost_$(template) */
        filename = (char *)malloc(len_tmp + 12 /* \Temp\xpost_ */ + len + 1);
        if (filename)
        {
            iter = filename;
            memcpy(iter, tmpdir, len_tmp);
            iter += len_tmp;
            memcpy(iter, "\\Temp\\xpost_", 12);
            iter += 12;
            memcpy(iter, template, len + 1);
        }
    }
    else if ((tmpdir = getenv("USERPROFILE")))
    {
        len_tmp = strlen(tmpdir);
        /* path is $(tmpdir)\xpost_$(template) */
        filename = (char *)malloc(len_tmp + 7 /* \xpost_ */ + len + 1);
        if (filename)
        {
            iter = filename;
            memcpy(iter, tmpdir, len_tmp);
            iter += len_tmp;
            memcpy(iter, "\\xpost_", 7);
            iter += 7;
            memcpy(iter, template, len + 1);
        }
    }

    if (!filename)
        return 0;

    while ((f < 0) && (count-- > 0))
    {
        char *trail;

        CopyMemory(iter, template, len + 1);
        trail = iter + len - 6;

        if (!_xpost_mkstemp_fill(trail))
            break;

        f = _open(filename,
                   O_CREAT | O_EXCL | O_RDWR | O_BINARY,
                   S_IREAD | S_IWRITE);
        if (f != -1)
            memcpy(template, iter, len + 1);
        else
        {
            if (errno != EEXIST)
                count = 0;
        }
    }

    free(filename);

    if (f == -1)
        return 0;

    *fd = f;

    return 1;
}

char *
xpost_realpath(const char *path)
{
    char *resolved_path;
    DWORD sz = 0UL;

    if (!path || !*path)
        return NULL;

    sz = GetFullPathName(path, 0UL, NULL, NULL);
    if (sz == 0UL)
        return NULL;

    resolved_path = malloc(sz * sizeof(char));
    if (!resolved_path)
        return NULL;

    sz = GetFullPathName(path, sz, resolved_path, NULL);
    if (sz == 0UL)
    {
        free(resolved_path);
        return NULL;
    }

    return resolved_path;
}

FILE *
xpost_open_beneath(const char *root, const char *rel)
{
    char full[XPOST_PATH_MAX];
    char root_final[XPOST_PATH_MAX];
    char file_final[XPOST_PATH_MAX];
    HANDLE rh;
    HANDLE h;
    DWORD n;
    int fd;
    FILE *fp;
    size_t rl;

    if (!root || !rel || !*rel)
    {
        errno = ENOENT;
        return NULL;
    }

    /* canonical form of root, resolved via a handle (FILE_FLAG_BACKUP_SEMANTICS
       is required to open a directory handle) */
    rh = CreateFile(root, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (rh == INVALID_HANDLE_VALUE)
    {
        errno = ENOENT;
        return NULL;
    }
    n = GetFinalPathNameByHandle(rh, root_final, sizeof root_final, FILE_NAME_NORMALIZED);
    CloseHandle(rh);
    if (n == 0UL || n >= sizeof root_final)
    {
        errno = ENOENT;
        return NULL;
    }

    if (_snprintf(full, sizeof full, "%s\\%s", root, rel) < 0)
    {
        errno = ENAMETOOLONG;
        return NULL;
    }
    full[sizeof full - 1] = '\0';

    /* open the reparse point itself rather than following it; the final-path
       check below rejects anything that resolves outside root */
    h = CreateFile(full, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                   FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        errno = ENOENT;
        return NULL;
    }
    n = GetFinalPathNameByHandle(h, file_final, sizeof file_final, FILE_NAME_NORMALIZED);
    if (n == 0UL || n >= sizeof file_final)
    {
        CloseHandle(h);
        errno = EACCES;
        return NULL;
    }

    /* the resolved file must sit strictly beneath the resolved root */
    rl = strlen(root_final);
    if (_strnicmp(file_final, root_final, rl) != 0 ||
        (file_final[rl] != '\\' && file_final[rl] != '/'))
    {
        CloseHandle(h);
        errno = EACCES;
        return NULL;
    }

    fd = _open_osfhandle((intptr_t)h, _O_RDONLY);
    if (fd < 0)
    {
        CloseHandle(h);
        errno = EACCES;
        return NULL;
    }
    fp = _fdopen(fd, "rb"); /* takes ownership of fd, and of the handle */
    if (!fp)
    {
        _close(fd);
        errno = EACCES;
        return NULL;
    }
    return fp;
}

/* The atomic beneath-root primitives are Linux-specific (openat2). On Windows
   they report themselves unsupported so the file layer applies its portable
   name-based check instead. */

FILE *
xpost_openat2_beneath(const char *root, const char *rel, const char *mode,
                      int access, int *supported)
{
    (void)root; (void)rel; (void)mode; (void)access;
    *supported = 0;
    errno = ENOSYS;
    return NULL;
}

int
xpost_fd_realpath(int fd, char *buf, size_t buflen)
{
    HANDLE h;
    char tmp[XPOST_PATH_MAX];
    DWORD n;

    h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    n = GetFinalPathNameByHandle(h, tmp, sizeof tmp, FILE_NAME_NORMALIZED);
    if (n == 0UL || n >= sizeof tmp)
        return 0;
    /* GetFinalPathNameByHandle yields the \\?\ (or \\?\UNC\) prefixed form;
       strip it so the result matches xpost_realpath (GetFullPathName), the
       form the permit set is stored in */
    if (_strnicmp(tmp, "\\\\?\\UNC\\", 8) == 0) /* \\?\UNC\server\share -> \\server\share */
    {
        if ((size_t)(2 + strlen(tmp + 8)) >= buflen)
            return 0;
        buf[0] = '\\';
        buf[1] = '\\';
        strcpy(buf + 2, tmp + 8);
        return 1;
    }
    if (_strnicmp(tmp, "\\\\?\\", 4) == 0)
    {
        if (strlen(tmp + 4) >= buflen)
            return 0;
        strcpy(buf, tmp + 4);
        return 1;
    }
    if (strlen(tmp) >= buflen)
        return 0;
    strcpy(buf, tmp);
    return 1;
}

int
xpost_unlinkat_beneath(const char *root, const char *rel, int *supported)
{
    (void)root; (void)rel;
    *supported = 0;
    errno = ENOSYS;
    return -1;
}

int
xpost_renameat_beneath(const char *oldroot, const char *oldrel,
                       const char *newroot, const char *newrel,
                       int *supported)
{
    (void)oldroot; (void)oldrel; (void)newroot; (void)newrel;
    *supported = 0;
    errno = ENOSYS;
    return -1;
}

/*============================================================================*
 *                                   API                                      *
 *============================================================================*/
