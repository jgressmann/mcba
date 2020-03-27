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

#include "McbaDriverInterface.h"
#include "Mcba.h"


EXTERN_C_START


#pragma warning(push)
#pragma warning(disable: 4214 4201)

typedef struct _MCBA_CAN_MSG_ITEM {
    union {
        SLIST_ENTRY MessageBuffersAvailable;
        struct {
            LIST_ENTRY ReadBuffers;
            MCBA_CAN_MSG Msg;
        };
    };
    
    ULONGLONG Timestamp : sizeof(ULONGLONG) * 8 - 1; // contains system time << 1
    ULONGLONG MemoryBlockOwner : 1;
} MCBA_CAN_MSG_ITEM, *PMCBA_CAN_MSG_ITEM;

#pragma warning(pop)

#define MCBA_MAX_READ_BUFFERS_QUEUED 128

typedef struct _MCBA_FILE_CONTEXT {
    LIST_ENTRY ReadBuffersList;
    LIST_ENTRY FilesList;
    LONG ReadBuffersQueued;
    MCBA_FILE_STATS Stats;
    KSPIN_LOCK ReadLock;
    WDFREQUEST PendingReadRequest;
    PMCBA_CAN_MSG_DATA PendingReadBuffer;
    size_t PendingReadCount;
    size_t PendingReadOffset;
} MCBA_FILE_CONTEXT, *PMCBA_FILE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(MCBA_FILE_CONTEXT, McbaFileGetContext)

#define MCBA_MAX_WRITES MCBA_BATCH_WRITE_MAX_SIZE
typedef UINT8 MCBA_USB_REQUEST_INDEX_TYPE, * PMCBA_USB_REQUEST_INDEX_TYPE;

typedef struct _MCBA_DEVICE_CONTEXT MCBA_DEVICE_CONTEXT, *PMCBA_DEVICE_CONTEXT;
typedef struct _MCBA_DEVICE_BATCH_REQUEST_DATA {
    SLIST_ENTRY Next;
    WDFREQUEST Request;
    volatile LONG Pending;
    LONG Count;
    NTSTATUS UsbRequestStatuses[MCBA_MAX_WRITES];
    MCBA_USB_REQUEST_INDEX_TYPE UsbRequestIndices[MCBA_MAX_WRITES];
    MCBA_USB_REQUEST_INDEX_TYPE BatchIndices[MCBA_MAX_WRITES];
} MCBA_DEVICE_BATCH_REQUEST_DATA, * PMCBA_DEVICE_BATCH_REQUEST_DATA;

typedef struct _MCBA_ALIGNED_USB_MESSAGE {
    struct mcba_usb_msg Msg;
} MCBA_ALIGNED_USB_MESSAGE, * PMCBA_ALIGNED_USB_MESSAGE;

typedef struct _MCBA_DEVICE_USB_REQUEST_DATA {
    WDFCONTEXT Contexts[MCBA_MAX_WRITES];
    WDFREQUEST Requests[MCBA_MAX_WRITES];
    WDFMEMORY Memory[MCBA_MAX_WRITES];    
    MCBA_ALIGNED_USB_MESSAGE Messages[MCBA_MAX_WRITES];
    MCBA_USB_REQUEST_INDEX_TYPE FreeIndices[MCBA_MAX_WRITES];
    KSEMAPHORE Availabe;
    KSPIN_LOCK Lock;
    MCBA_USB_REQUEST_INDEX_TYPE FreeIndexEnd;
    MCBA_USB_REQUEST_INDEX_TYPE Count;
} MCBA_DEVICE_USB_REQUEST_DATA, * PMCBA_DEVICE_USB_REQUEST_DATA;

typedef struct _MCBA_DEVICE_CONTEXT {
    WDFUSBDEVICE UsbDevice;
	WDFUSBINTERFACE UsbInterface;
	WDFUSBPIPE BulkReadPipe;
	WDFUSBPIPE BulkWritePipe;
    SLIST_HEADER MessageBuffersAvailable;
    KSPIN_LOCK MessageBuffersLock;
    SLIST_HEADER BatchRequestDataListHeader;
    KSPIN_LOCK BatchRequestDataLock;
    LIST_ENTRY FilesList;
    KSPIN_LOCK FilesLock;
    MCBA_DEVICE_USB_REQUEST_DATA UsbRequests;
    
    UINT16 (*BigEndianToHost)(UINT16);
    UINT16 (*LittleEndianToHost)(UINT16);
    
    MCBA_DEVICE_STATUS DeviceStatus;

} MCBA_DEVICE_CONTEXT, *PMCBA_DEVICE_CONTEXT;

//
// This macro will generate an inline function called McbaDeviceGetContext
// which will be used to get a pointer to the device context memory
// in a type safe manner.
//
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(MCBA_DEVICE_CONTEXT, McbaDeviceGetContext)

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS 
McbaCreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit
    );


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
McbaMessageBuffersAdd(
    _Inout_ PMCBA_DEVICE_CONTEXT DeviceContext
);

VOID
McbaFormatCanMessage(
    _In_ PMCBA_DEVICE_CONTEXT DeviceContext,
    _In_ const MCBA_CAN_MSG* Input,
    _Out_ struct mcba_usb_msg_can* Output
);



_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
McbaUsbRequestsAlloc(
    _Inout_ PMCBA_DEVICE_USB_REQUEST_DATA UsbRequestData,
    _In_ MCBA_USB_REQUEST_INDEX_TYPE Count,
    _Out_writes_(Count) MCBA_USB_REQUEST_INDEX_TYPE* Indices
);

_IRQL_requires_same_
VOID
McbaUsbRequestsFree(
    _Inout_ PMCBA_DEVICE_USB_REQUEST_DATA UsbRequestData,
    _In_ MCBA_USB_REQUEST_INDEX_TYPE Count,
    _In_reads_(Count) const MCBA_USB_REQUEST_INDEX_TYPE* Indices
);

_IRQL_requires_same_
VOID
McbaUsbRequestsReuse(
    _Inout_ PMCBA_DEVICE_USB_REQUEST_DATA UsbRequestData,
    _In_ MCBA_USB_REQUEST_INDEX_TYPE Count,
    _In_reads_(Count) const MCBA_USB_REQUEST_INDEX_TYPE* Indices
);


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
McbaUsbBulkWritePipeSend(
    _In_ PMCBA_DEVICE_CONTEXT DeviceContext,
    _In_ MCBA_USB_REQUEST_INDEX_TYPE Index
);

EVT_WDF_REQUEST_CANCEL McbaCancelPendingReadRequest;


_Requires_lock_held_(FileContext->ReadLock)
static
inline
VOID
McbaClearPendingReadRequest(
    _Inout_ PMCBA_FILE_CONTEXT FileContext
)
{
    NT_ASSERT(FileContext);

    FileContext->PendingReadRequest = NULL;
    FileContext->PendingReadBuffer = NULL;
    FileContext->PendingReadOffset = 0;
    FileContext->PendingReadCount = 0;
}


EXTERN_C_END
