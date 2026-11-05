#include "platform/windows/auto_start.hpp"

#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        // No waiting or C++ initialization here. The new thread starts after
        // the loader releases its lock.
        conduit::windows::scheduleAutoStart(module);
    } else if (reason == DLL_PROCESS_DETACH && reserved != nullptr) {
        // Process exit: signal only. Never join under the loader lock.
        conduit::windows::signalProcessExit();
    }
    return TRUE;
}
