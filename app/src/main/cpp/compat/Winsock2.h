#pragma once
// Sockets are not used on Android (single player); just the types Fog headers name.
#include <Windows.h>
typedef uintptr_t SOCKET;
#define INVALID_SOCKET ((SOCKET)~0)
