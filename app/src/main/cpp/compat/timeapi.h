#pragma once
#include <Windows.h>
inline DWORD timeGetTime() { return GetTickCount(); }
