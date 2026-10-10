// stubs.cpp - Windows-only entry points the shared code still references.
#include <stdio.h>

#include "Globals.h"

int ShowMessageBox(HWND hWnd, LPCSTR lpMsg, LPCSTR lpCaption, UINT uType)
{
    (void)hWnd; (void)uType;
    printf("[MSGBOX] %s: %s\n", lpCaption ? lpCaption : "RESIDENT EVIL",
           lpMsg ? lpMsg : "");
    return 1;   // IDOK
}
