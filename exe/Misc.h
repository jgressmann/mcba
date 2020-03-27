#pragma once

#include "Windows.h"

EXTERN_C_START

HANDLE
OpenDevice(
    _In_ const GUID* Guid,
    _In_ BOOL Synchronous
);

EXTERN_C_END

