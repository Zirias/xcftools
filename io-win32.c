/* OS-specific IO functions for xcftools
 *
 * This file was contributed by Felix Palmen <felix@palmen-it.de>
 * It is released by the same terms stated on the other files of xcftools as
 * public domain.
 */

#include "xcftools.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

static char cmdline[MAX_PATH];

void
free_or_close_xcf(void)
{
    free(xcf_file);
    xcf_file = 0;
}

void
read_or_mmap_xcf(const char *filename, const char *unzipper)
{
    free_or_close_xcf();

    if (!unzipper)
    {
        const char *pc = filename + strlen(filename);
        if (pc-filename > 2 && !strcmp(pc-2, "gz"))
            unzipper = "zcat";
        else if (pc-filename > 3 && !strcmp(pc-3, "bz2"))
            unzipper = "bzcat";
        else
            unzipper = "";
    }
    else if (!strcmp(unzipper, "cat"))
        unzipper = "";

    FILE *xcfstream = 0;
    HANDLE piperd = 0;
    HANDLE pipewr = 0;

    if (*unzipper)
    {
        SECURITY_ATTRIBUTES sa;

        sa.nLength = sizeof(sa);
        sa.bInheritHandle = 1;
        sa.lpSecurityDescriptor = 0;

        if (!CreatePipe(&piperd, &pipewr, &sa, 0))
            FatalUnexpected("!Cannot create pipe for %s", unzipper);
        SetHandleInformation(piperd, HANDLE_FLAG_INHERIT, 0);

        PROCESS_INFORMATION pi;
        STARTUPINFO si;

        ZeroMemory(&pi, sizeof(pi));
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = pipewr;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        si.dwFlags |= STARTF_USESTDHANDLES;

        if (strlen(filename) + strlen(unzipper) + 3 > MAX_PATH)
            FatalUnexpected("!command line too long for %s \"%s\"",
                    unzipper, filename);

        strcpy(cmdline, unzipper);
        strcat(cmdline, " \"");
        strcat(cmdline, filename);
        strcat(cmdline, "\"");

        if (!CreateProcess(0, cmdline, 0, 0, 1, 0, 0, 0, &si, &pi))
            FatalUnexpected("!Cannot execute %s \"%s\"", unzipper, filename);

        int pipefd = _open_osfhandle((intptr_t)piperd, _O_RDONLY);
        xcfstream = _fdopen(pipefd, "rb");
    }
    else if (!strcmp(filename, "-"))
    {
        HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
        int infd = _open_osfhandle((intptr_t)in, _O_RDONLY);
        xcfstream = _fdopen(infd, "rb");
    }
    else
    {
        xcfstream = fopen(filename, "rb");
    }

    struct stat st;
    if (fstat(fileno(xcfstream), &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG)
    {
        xcf_length = st.st_size;
        xcf_file = malloc(xcf_length);
        if (!xcf_file)
            FatalUnexpected(_("Out of memory for xcf data"));
        if (fread(xcf_file, 1, xcf_length, xcfstream) != xcf_length)
        {
            if (feof(xcfstream))
                FatalUnexpected(_("XCF file shrunk while reading it"));
            else
                FatalUnexpected(_("!Could not read xcf data"));
        }
    }
    else
    {
        size_t blocksize = 0x80000;
        xcf_length = 0;
        xcf_file = 0;
        while (1)
        {
            void *tmp = realloc(xcf_file, blocksize);
            if (!tmp)
            {
                free(xcf_file);
                FatalUnexpected(_("Out of memory for xcf data"));
            }
            xcf_file = tmp;
            size_t actual = fread(xcf_file + xcf_length, 1,
                    blocksize - xcf_length, xcfstream);
            xcf_length += actual;
            if (feof(xcfstream)) break;
            if (xcf_length < blocksize)
                FatalUnexpected(_("!Could not read xcf data"));
            blocksize += (blocksize >> 1) & ~(size_t)0x3FFF;
        }
    }

    fclose(xcfstream);
    if (piperd) CloseHandle(piperd);
    if (pipewr) CloseHandle(pipewr);
}

