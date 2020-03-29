/*
    $Id$

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/
#include "console.h"

#ifdef COLOR_OUTPUT
bool print_use_color = false;
bool print_use_bold = false;

#ifdef _WIN32
#include <windows.h>

static BOOL utf8_console;
static UINT old_consoleoutputcp;
static UINT old_consolecp;
static HANDLE console_handle;
static WORD old_attributes, current_attributes;

void console_init(void) {
    utf8_console = IsValidCodePage(CP_UTF8);
    if (utf8_console) {
        old_consoleoutputcp = GetConsoleOutputCP();
        old_consolecp = GetConsoleCP();
        SetConsoleCP(CP_UTF8);
        SetConsoleOutputCP(CP_UTF8);
    }
}

void console_destroy(void) {
    if (utf8_console) {
        SetConsoleCP(old_consolecp);
        SetConsoleOutputCP(old_consoleoutputcp);
    }
}

void console_use(FILE *f) {
    CONSOLE_SCREEN_BUFFER_INFO console_info;
    DWORD handle;
    if (f == stderr) {
        handle = STD_ERROR_HANDLE;
    } else if (f == stdout) {
        handle = STD_OUTPUT_HANDLE;
    } else {
        print_use_color = false;
        return;
    }
    console_handle = GetStdHandle(handle);
    if (console_handle == INVALID_HANDLE_VALUE) {
        print_use_color = false;
        return;
    }
    GetConsoleScreenBufferInfo(console_handle, &console_info);
    old_attributes = current_attributes = console_info.wAttributes;
    print_use_color = true;
}

void console_attribute(int c, FILE *f) {
    fflush(f);
    switch (c) {
    case 0: current_attributes |= FOREGROUND_INTENSITY; break;
    case 1: current_attributes = old_attributes | FOREGROUND_INTENSITY; break;
    case 2: 
        if (!(current_attributes & FOREGROUND_BLUE) != !(current_attributes & BACKGROUND_BLUE)) current_attributes ^= FOREGROUND_BLUE | BACKGROUND_BLUE; 
        if (!(current_attributes & FOREGROUND_GREEN) != !(current_attributes & BACKGROUND_GREEN)) current_attributes ^= FOREGROUND_GREEN | BACKGROUND_GREEN; 
        if (!(current_attributes & FOREGROUND_RED) != !(current_attributes & BACKGROUND_RED)) current_attributes ^= FOREGROUND_RED | BACKGROUND_RED; 
        if (!(current_attributes & FOREGROUND_INTENSITY) != !(current_attributes & BACKGROUND_INTENSITY)) current_attributes ^= FOREGROUND_INTENSITY | BACKGROUND_INTENSITY; 
        break;
    case 3: current_attributes = old_attributes; break;
    case 4: current_attributes &= ~(FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED); break;
    case 5: current_attributes = FOREGROUND_RED | (current_attributes & ~(FOREGROUND_BLUE | FOREGROUND_GREEN)); break;
    case 6: current_attributes = FOREGROUND_GREEN | FOREGROUND_INTENSITY | (current_attributes & ~(FOREGROUND_BLUE | FOREGROUND_RED)); break;
    case 7: current_attributes = FOREGROUND_RED | FOREGROUND_BLUE | (current_attributes & ~FOREGROUND_GREEN); break;
    default: break;
    }
    SetConsoleTextAttribute(console_handle, current_attributes);
}
#else
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void console_use(FILE *f) {
    static int terminal;
    if (terminal == 0) {
        char const *term = getenv("TERM");
        terminal = (term != NULL && strcmp(term, "dumb") != 0) ? 1 : 2;
    }
    print_use_color = terminal == 1 && isatty(fileno(f)) == 1;
}
#endif

#endif
