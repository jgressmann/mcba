/* The MIT License (MIT)
 *
 * Copyright (c) 2020 Jean Gressmann <jean@0x42.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#pragma once

#include <ntddk.h>
#include <wdf.h>

#include "Mcba.h"
#include "Device.h"

EXTERN_C_START


_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
McbaQueueInitialize(
    _In_ WDFDEVICE Device
    );


EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL McbaEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_STOP McbaEvtIoStop;
#if 0
EVT_WDF_IO_QUEUE_IO_RESUME McbaEvtIoResume;
#endif
EVT_WDF_IO_QUEUE_IO_READ McbaEvtIoRead;
EVT_WDF_IO_QUEUE_IO_WRITE McbaEvtIoWrite;



EXTERN_C_END
