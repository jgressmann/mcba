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

#include "Pch.h"
#include "Queue.tmh"



static
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
McbaStartBatchWrite(
    _Inout_ PMCBA_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_reads_(Count) const MCBA_CAN_MSG* Msg,
    _In_ MCBA_USB_REQUEST_INDEX_TYPE Count
);

static
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
McbaStandaloneUsbRequest(
    _Inout_ PMCBA_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ const struct mcba_usb_msg* Msg
);


#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, McbaQueueInitialize)
#endif



_Use_decl_annotations_
NTSTATUS
McbaQueueInitialize(
    _In_ WDFDEVICE Device
    )
{
    WDFQUEUE queue;
    NTSTATUS status;
    WDF_IO_QUEUE_CONFIG queueConfig;
    PMCBA_DEVICE_CONTEXT pDeviceContext;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "--> %!FUNC!\n");

    pDeviceContext = McbaDeviceGetContext(Device);
    
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);

    queueConfig.EvtIoDeviceControl = McbaEvtIoDeviceControl;
    queueConfig.EvtIoWrite = McbaEvtIoWrite;

    queueConfig.EvtIoStop = McbaEvtIoStop;
#if 0
    queueConfig.EvtIoResume = McbaEvtIoResume;
#endif
    
    

    //__analysis_assume(queueConfig.EvtIoStop != NULL);
    status = WdfIoQueueCreate(
                 Device,
                 &queueConfig,
                 WDF_NO_OBJECT_ATTRIBUTES,
                 &queue
                 );
    //__analysis_assume(queueConfig.EvtIoStop == NULL);

    if( !NT_SUCCESS(status) ) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "%!FUNC! WdfIoQueueCreate failed with %!STATUS!", status);
        goto Exit;
    }

    // Create a non-power managed queue for read
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoRead = McbaEvtIoRead;
    queueConfig.PowerManaged = WdfFalse;

    status = WdfIoQueueCreate(
        Device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &queue
    );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "%!FUNC! WdfIoQueueCreate failed %!STATUS!", status);
        goto Exit;
    }

    status = WdfDeviceConfigureRequestDispatching(Device, queue, WdfRequestTypeRead);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "%!FUNC! WdfDeviceConfigureRequestDispatching failed with %!STATUS!\n", status);
        goto Exit;
    }
    
Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "<-- %!FUNC! completed with status=%!STATUS!\n", status);
    return status;
}

#if 0    
_IRQL_requires_same_
static
inline
NTSTATUS
McbaRetrievePendingRequest(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _Out_ WDFREQUEST* Result
)
{
    NTSTATUS status;

    for (WDFREQUEST marker = NULL, found; ; ) {
        status = WdfIoQueueFindRequest(Queue, marker, NULL, NULL, &found);

        if (marker) { // release previous marker
            WdfObjectDereference(marker);
            marker = NULL;
        }

        if (!NT_SUCCESS(status)) {
            break;
        }

        if (found == Request) {
            status = WdfIoQueueRetrieveFoundRequest(Queue, found, Result);
            WdfObjectDereference(found);
            break;
        }

        marker = found;
    }

    return status;
}
#endif


static
_IRQL_requires_same_
VOID
McbaUrbCompleted(
    _In_
    WDFREQUEST Request,
    _In_
    WDFIOTARGET Target,
    _In_
    PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_
    WDFCONTEXT Context
)
{
    NTSTATUS status = Params->IoStatus.Status;
    PWDF_USB_REQUEST_COMPLETION_PARAMS usbCompletionParams = Params->Parameters.Usb.Completion;
    size_t bytesWritten = usbCompletionParams->Parameters.PipeWrite.Length;
    MCBA_USB_REQUEST_INDEX_TYPE index = (MCBA_USB_REQUEST_INDEX_TYPE)PtrToUlong(Context);
    PMCBA_DEVICE_CONTEXT pDeviceContext = McbaDeviceGetContext(WdfIoTargetGetDevice(Target));
    WDFREQUEST userRequest = pDeviceContext->UsbRequests.Contexts[index];

    UNREFERENCED_PARAMETER(Request);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "--> %!FUNC! Request 0x%p finished on index=%u\n", userRequest, index);


    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE,
            "%!FUNC! write failed with status=%!STATUS!, UsbdStatus=0x%x\n",
            status, usbCompletionParams->UsbdStatus);
    }

    bytesWritten = 0;
    WdfRequestCompleteWithInformation(userRequest, status, bytesWritten);

    McbaUsbRequestsReuse(&pDeviceContext->UsbRequests, 1, &index);
    McbaUsbRequestsFree(&pDeviceContext->UsbRequests, 1, &index);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "<-- %!FUNC! Request 0x%p index=%u completed with status=%!STATUS! bytes transferred=%u\n", userRequest, index, status, (unsigned)bytesWritten);    
}

static
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
McbaRead(
    WDFREQUEST Request,
    size_t Length,
    BOOLEAN readAll,
    BOOLEAN nonBlocking
)
{
    NTSTATUS status;
    PMCBA_FILE_CONTEXT pFileContext;
    size_t count;
    ULONG_PTR transferred = 0;
    PMCBA_CAN_MSG_DATA pData;
    WDFFILEOBJECT fileObject;
    PMCBA_DEVICE_CONTEXT pDeviceContext;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "--> %!FUNC! Request=0x%p bytes=%u all=%d, nonblocking=%d\n", Request, (unsigned)Length, readAll, nonBlocking);

    count = Length / sizeof(MCBA_CAN_MSG_DATA);
    if (Length != count * sizeof(MCBA_CAN_MSG_DATA)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE,
            "%!FUNC! payload of length=%u is not a multiple of %u=sizeof(MCBA_CAN_MSG_DATA)\n",
            (unsigned)Length, (unsigned)sizeof(MCBA_CAN_MSG_DATA));
        status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }

    status = WdfRequestRetrieveOutputBuffer(Request, Length, &pData, NULL);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE,
            "%!FUNC! WdfRequestRetrieveOutputBuffer failed with status=%!STATUS!\n", status);
        goto Complete;
    }

    if (count) {

        fileObject = WdfRequestGetFileObject(Request);
        pDeviceContext = McbaDeviceGetContext(WdfFileObjectGetDevice(fileObject));
        pFileContext = McbaFileGetContext(fileObject);
        KIRQL irql;
        BOOLEAN completeRequest = TRUE;

        KeAcquireSpinLock(&pFileContext->ReadLock, &irql);

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! %u buffers queued, want to read %u\n", (unsigned)pFileContext->ReadBuffersQueued, (unsigned)count);

        for (size_t i = 0; i < count; ++i, ++pData, transferred += sizeof(*pData)) {
            if (0 == pFileContext->ReadBuffersQueued) {
                
                if (nonBlocking) {
                    break;
                }

                if (!readAll && i) {
                    break;
                }

                if (pFileContext->PendingReadRequest) {
                    status = STATUS_NOT_IMPLEMENTED;
                }
                else {
                    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Making Request=0x%p cancelable\n", Request);
                    status = WdfRequestMarkCancelableEx(Request, McbaCancelPendingReadRequest);
                    if (NT_SUCCESS(status)) {
                        pFileContext->PendingReadRequest = Request;
                        pFileContext->PendingReadBuffer = pData;
                        pFileContext->PendingReadOffset = i;
                        pFileContext->PendingReadCount = readAll ? count : i + 1;
                        completeRequest = FALSE;
                    }                    
                }
                break;
            }

            NT_ASSERT(pFileContext->ReadBuffersQueued > 0);
            --pFileContext->ReadBuffersQueued;

            PLIST_ENTRY pEntry = RemoveHeadList(&pFileContext->ReadBuffersList);
            NT_ASSERT(pEntry);
            PMCBA_CAN_MSG_ITEM pItem = CONTAINING_RECORD(pEntry, MCBA_CAN_MSG_ITEM, ReadBuffers);
            pData->Msg = pItem->Msg;
            pData->SystemTimeReceived = pItem->Timestamp >> 1;

            ExInterlockedPushEntrySList(&pDeviceContext->MessageBuffersAvailable, &pItem->MessageBuffersAvailable, &pDeviceContext->MessageBuffersLock);
        }

        KeReleaseSpinLock(&pFileContext->ReadLock, irql);

        if (!completeRequest) {
            goto Exit;
        }
    }
Complete:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! completed Request=0x%p with status=%!STATUS! can frames read=%u\n", Request, status, (unsigned)(transferred / sizeof(*pData)));
    WdfRequestCompleteWithInformation(Request, status, transferred);
Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "<-- %!FUNC!\n");
}

_Use_decl_annotations_
VOID
McbaEvtIoDeviceControl(
    WDFQUEUE Queue,
    WDFREQUEST Request,
    size_t OutputBufferLength,
    size_t InputBufferLength,
    ULONG IoControlCode
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    size_t bufferSize;
    BOOLEAN pending = FALSE;
    ULONG_PTR information = 0;    
    PMCBA_DEVICE_CONTEXT pDeviceContext;

    TraceEvents(TRACE_LEVEL_INFORMATION, 
                TRACE_QUEUE, 
                "--> %!FUNC! Request=0x%p IoControlCode=0x%x OutputBufferLength=%ul InputBufferLength=%ul\n", 
                Request, (unsigned long)IoControlCode, (unsigned long) OutputBufferLength, (unsigned long) InputBufferLength);


    pDeviceContext = McbaDeviceGetContext(WdfIoQueueGetDevice(Queue));

    switch (IoControlCode) {
    case MCBA_IOCTL_DEVICE_BITRATE_SET: {
        PMCBA_BITRATE pBitrate;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pBitrate), &pBitrate, &bufferSize);
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE,
                "%!FUNC! WdfRequestRetrieveInputBuffer failed %!STATUS!\n", status);
            break;
        }

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! MCBA_IOCTL_DEVICE_BITRATE_SET bitrate=%d\n", *pBitrate);

        struct mcba_usb_msg_change_bitrate usb_msg = {
            .cmd_id = MBCA_CMD_CHANGE_BIT_RATE
        };

        const UINT16 bitrateKbpsBe = pDeviceContext->BigEndianToHost((UINT16)(*pBitrate / 1000));
        RtlCopyMemory(&usb_msg.bitrate, &bitrateKbpsBe, 2);
        McbaStandaloneUsbRequest(pDeviceContext, Request, (const struct mcba_usb_msg*)&usb_msg);
        pending = TRUE;
    } break;
    case MCBA_IOCTL_DEVICE_BITRATE_GET: {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! MCBA_IOCTL_DEVICE_BITRATE_GET\n");
        PMCBA_BITRATE pBitrate;
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*pBitrate), &pBitrate, &bufferSize);
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE,
                "%!FUNC! WdfRequestRetrieveOutputBuffer failed %!STATUS!\n", status);
            break;
        }
        *pBitrate = pDeviceContext->DeviceStatus.Bitrate;
        information = sizeof(*pBitrate);
    } break;
    case MCBA_IOCTL_DEVICE_RESET: {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! MCBA_IOCTL_DEVICE_RESET\n");
        status = STATUS_NOT_IMPLEMENTED;
    } break;
    case MCBA_IOCTL_DEVICE_STATS_CLEAR: {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! MCBA_IOCTL_DEVICE_STATS_CLEAR\n");
        RtlZeroMemory(&pDeviceContext->DeviceStatus.Stats, sizeof(pDeviceContext->DeviceStatus.Stats));
        status = STATUS_SUCCESS;
    } break;
    case MCBA_IOCTL_DEVICE_STATS_GET: {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! MCBA_IOCTL_DEVICE_STATS_GET\n");
        PMCBA_DEVICE_STATS pStats;
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*pStats), &pStats, &bufferSize);
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE,
                "%!FUNC! WdfRequestRetrieveOutputBuffer failed %!STATUS!\n", status);
            break;
        }
        *pStats = pDeviceContext->DeviceStatus.Stats;
        information = sizeof(*pStats);
    } break;
    case MCBA_IOCTL_DEVICE_STATUS_GET: {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! MCBA_IOCTL_DEVICE_STATUS_GET\n");
        PMCBA_DEVICE_STATUS pStatus;
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*pStatus), &pStatus, &bufferSize);
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE,
                "%!FUNC! WdfRequestRetrieveOutputBuffer failed %!STATUS!\n", status);
            break;
        }
        *pStatus = pDeviceContext->DeviceStatus;
        information = sizeof(*pStatus);
    } break;
    case MCBA_IOCTL_DEVICE_TERMINATION_ENABLE: 
    case MCBA_IOCTL_DEVICE_TERMINATION_DISABLE: {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! MCBA_IOCTL_DEVICE_TERMINATION_ENABLE/DISABLE\n");
        struct mcba_usb_msg_termination usb_msg = {
            .cmd_id = MBCA_CMD_SETUP_TERMINATION_RESISTANCE
        };

        usb_msg.termination = IoControlCode == MCBA_IOCTL_DEVICE_TERMINATION_ENABLE ? 1 : 0;

        McbaStandaloneUsbRequest(pDeviceContext, Request, (const struct mcba_usb_msg*)&usb_msg);
        pending = TRUE;
    } break;
    case MCBA_IOCTL_HOST_CAN_FRAME_READ_AT_LEAST_ONE: {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! MCBA_IOCTL_HOST_CAN_FRAME_READ_AT_LEAST_ONE\n");
        pending = TRUE;
        McbaRead(Request, OutputBufferLength, FALSE, FALSE);
    } break;
    case MCBA_IOCTL_HOST_CAN_FRAME_READ_NON_BLOCKING: {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! MCBA_IOCTL_HOST_CAN_FRAME_READ_NON_BLOCKING\n");
        pending = TRUE;
        McbaRead(Request, OutputBufferLength, TRUE, TRUE);
    } break;
    case MCBA_IOCTL_HOST_CAN_FRAME_WRITE_AT_LEAST_ONE: {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! MCBA_IOCTL_HOST_CAN_FRAME_WRITE_AT_LEAST_ONE\n");
        status = STATUS_NOT_IMPLEMENTED;
    } break;
    case MCBA_IOCTL_HOST_CAN_FRAME_WRITE_NON_BLOCKING: {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! MCBA_IOCTL_HOST_CAN_FRAME_WRITE_NON_BLOCKING\n");
        status = STATUS_NOT_IMPLEMENTED;
    } break;
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    if (!pending) {
        WdfRequestCompleteWithInformation(Request, status, information);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Request=0x%p completed with status=%!STATUS! information=%ul\n", Request, status, (unsigned long)information);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "<-- %!FUNC!\n");
}


_Use_decl_annotations_
VOID
McbaEvtIoStop(
    WDFQUEUE Queue,
    WDFREQUEST Request,
    ULONG ActionFlags
)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, 
                TRACE_QUEUE, 
                "--> %!FUNC! Queue=0x%p, Request=0x%p ActionFlags=0x%x", 
                Queue, Request, ActionFlags);

    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(ActionFlags);

    WdfRequestCancelSentRequest(Request);

    /*
    if (ActionFlags & WdfRequestStopActionSuspend) {
        WdfRequestStopAcknowledge(Request, TRUE);
    } else if (ActionFlags & WdfRequestStopActionPurge) {
        WdfRequestCancelSentRequest(Request);
    }
    */

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "<-- %!FUNC!\n");
}

#if 0
_Use_decl_annotations_
VOID
McbaEvtIoResume(
    WDFQUEUE Queue,
    WDFREQUEST Request
)
{
    TraceEvents(TRACE_LEVEL_INFORMATION,
        TRACE_QUEUE,
        "--> %!FUNC! Queue=0x%p, Request=0x%p\n",
        Queue, Request);

    WdfRequestStopAcknowledge(Request, TRUE);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "<-- %!FUNC!\n");
}
#endif


_Use_decl_annotations_
VOID
McbaEvtIoRead(
    WDFQUEUE Queue,
    WDFREQUEST Request,
    size_t Length
    ) 
{
    UNREFERENCED_PARAMETER(Queue);

    McbaRead(Request, Length, TRUE, FALSE);
}
#if 0
static
_IRQL_requires_same_
VOID
McbaUrbCompletedForSingleCanFrame(
    _In_
    WDFREQUEST Request,
    _In_
    WDFIOTARGET Target,
    _In_
    PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_
    WDFCONTEXT Context
)
{
    NTSTATUS status = Params->IoStatus.Status;
    PWDF_USB_REQUEST_COMPLETION_PARAMS usbCompletionParams = Params->Parameters.Usb.Completion;
    size_t bytesWritten = usbCompletionParams->Parameters.PipeWrite.Length;

    UNREFERENCED_PARAMETER(Target);
    

    if (NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE,
            "%!FUNC! Bytes written: %u\n", (unsigned)bytesWritten);
    } else {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE,
            "%!FUNC! Write failed with status %!STATUS!, UsbdStatus 0x%x\n",
            status, usbCompletionParams->UsbdStatus);
    }

    WdfRequestCompleteWithInformation(Context, status, sizeof(MCBA_CAN_MSG));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Request 0x%p completed with status=%!STATUS! bytes transferred=%u\n", Context, status, (unsigned)sizeof(MCBA_CAN_MSG));

    WdfObjectDelete(Request);
}

_Use_decl_annotations_
NTSTATUS
McbaSendWriteUrb(
    PMCBA_DEVICE_CONTEXT DeviceContext,
    const struct mcba_usb_msg* Msg,
    _In_ WDFOBJECT Parent,
    PFN_WDF_REQUEST_COMPLETION_ROUTINE CompletionRoutine,
    WDFCONTEXT CompletionContext
)
{
    WDF_OBJECT_ATTRIBUTES attrs;
    WDFIOTARGET ioTarget;
    WDFREQUEST request = NULL;
    NTSTATUS status;
    WDFMEMORY memory;
    PVOID buffer;
    (void)Parent;

    
    ioTarget = WdfUsbTargetPipeGetIoTarget(DeviceContext->BulkWritePipe);

    WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
    
    status = WdfRequestCreate(&attrs, ioTarget, &request);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfRequestCreate failed %!STATUS!\n", status);
        goto Exit;
    }

    WdfRequestSetCompletionRoutine(request, CompletionRoutine, CompletionContext);

    WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
    attrs.ParentObject = request;

    status = WdfMemoryCreate(&attrs, NonPagedPoolNx, POOL_TAG, sizeof(*Msg), &memory, &buffer);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfMemoryCreate failed %!STATUS!\n", status);
        goto Exit;
    }

    RtlCopyMemory(buffer, Msg, sizeof(*Msg));

    status = WdfUsbTargetPipeFormatRequestForWrite(DeviceContext->BulkWritePipe, request, memory, NULL);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfUsbTargetPipeFormatRequestForWrite failed %!STATUS!\n", status);
        goto Exit;
    }

    if (WdfRequestSend(request, ioTarget, WDF_NO_SEND_OPTIONS) == FALSE) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfRequestSend failed\n");
        status = WdfRequestGetStatus(request);
        __analysis_assert(!NT_SUCCESS(status));
        goto Exit;
    }

Exit:
    if (!NT_SUCCESS(status)) {
        if (request) {
            WdfObjectDelete(request);
        }
    }

    return status;
}
#endif


static
_IRQL_requires_same_
VOID
McbaFinalizeBatchWrite(
    _Inout_ PMCBA_DEVICE_BATCH_REQUEST_DATA Header
)
{
    NTSTATUS requestStatus = STATUS_SUCCESS;  
    ULONG_PTR transferred = 0;
    PMCBA_DEVICE_CONTEXT pDeviceContext;
    WDFREQUEST request = Header->Request;
    
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "--> %!FUNC! Batch request=0x%p\n", Header);
    pDeviceContext = McbaDeviceGetContext(WdfFileObjectGetDevice(WdfRequestGetFileObject(Header->Request)));

    // collect status, bytes transfered of batch write
    for (size_t i = 0; i < Header->Count; ++i) {
        requestStatus = Header->UsbRequestStatuses[i];
        if (!NT_SUCCESS(requestStatus)) {
            break;
        }

        transferred += sizeof(MCBA_CAN_MSG);
    }

    WdfRequestCompleteWithInformation(request, requestStatus, transferred);

    McbaUsbRequestsReuse(&pDeviceContext->UsbRequests, (MCBA_USB_REQUEST_INDEX_TYPE)Header->Count, Header->UsbRequestIndices);
    McbaUsbRequestsFree(&pDeviceContext->UsbRequests, (MCBA_USB_REQUEST_INDEX_TYPE)Header->Count, Header->UsbRequestIndices);

    ExInterlockedPushEntrySList(&pDeviceContext->BatchRequestDataListHeader, &Header->Next, &pDeviceContext->BatchRequestDataLock);

    TraceEvents(
        TRACE_LEVEL_INFORMATION, 
        TRACE_QUEUE, 
        "<-- %!FUNC! Request=%p completed on batch=%p with status=%!STATUS! information=%ul\n",
        request,
        Header,
        requestStatus,
        (unsigned long)transferred);
}


static
_IRQL_requires_same_
VOID
McbaUrbCompletedForCanFrameInBatch(
    _In_
    WDFREQUEST Request,
    _In_
    WDFIOTARGET Target,
    _In_
    PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_
    WDFCONTEXT Context
)
{
    NTSTATUS status = Params->IoStatus.Status;
    PWDF_USB_REQUEST_COMPLETION_PARAMS usbCompletionParams = Params->Parameters.Usb.Completion;
    size_t bytesWritten = usbCompletionParams->Parameters.PipeWrite.Length;
    PMCBA_USB_REQUEST_INDEX_TYPE pIndex = (PMCBA_USB_REQUEST_INDEX_TYPE)Context;
    const MCBA_USB_REQUEST_INDEX_TYPE index = *pIndex;
    PMCBA_USB_REQUEST_INDEX_TYPE pFirst = pIndex - index;
    PMCBA_DEVICE_BATCH_REQUEST_DATA pHeader = CONTAINING_RECORD(pFirst, MCBA_DEVICE_BATCH_REQUEST_DATA, BatchIndices);

    UNREFERENCED_PARAMETER(Request);
    UNREFERENCED_PARAMETER(Target);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "--> %!FUNC! header=0x%p index=%u written=%u status=%!STATUS!\n", pHeader, index, (unsigned)bytesWritten, status);

    if (NT_SUCCESS(status)) {
        if (bytesWritten != sizeof(struct mcba_usb_msg)) {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_QUEUE,
                "%!FUNC! Incomplete write, expected %u=sizeof(struct mcba_usb_msg)\n", (unsigned)sizeof(struct mcba_usb_msg));
            status = STATUS_DATA_ERROR;
        }
    }
    else {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE,
            "%!FUNC! Write failed with status %!STATUS!, UsbdStatus 0x%x\n",
            status, usbCompletionParams->UsbdStatus);
    }

    pHeader->UsbRequestStatuses[index] = status;

    LONG result = InterlockedDecrement(&pHeader->Pending);
    if (0 == result) {
        McbaFinalizeBatchWrite(pHeader);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "<-- %!FUNC!\n");
}

_Use_decl_annotations_
static
VOID
McbaStartBatchWrite(
    PMCBA_DEVICE_CONTEXT DeviceContext,
    WDFREQUEST Request,
    const MCBA_CAN_MSG* Msg,
    MCBA_USB_REQUEST_INDEX_TYPE Count
)
{
    NTSTATUS status;
    PMCBA_DEVICE_BATCH_REQUEST_DATA pData;
    MCBA_USB_REQUEST_INDEX_TYPE sent = 0;

    __analysis_assert(Count <= MCBA_MAX_WRITES);

    TraceEvents(
        TRACE_LEVEL_INFORMATION,
        TRACE_QUEUE,
        "--> %!FUNC! Request=0x%p Msg=0x%p Count=%u\n",
        Request, Msg, (unsigned)Count);

    PSLIST_ENTRY pBatchRequestEntryLink = ExInterlockedPopEntrySList(&DeviceContext->BatchRequestDataListHeader, &DeviceContext->BatchRequestDataLock);
    if (!pBatchRequestEntryLink) {
        pData = (PMCBA_DEVICE_BATCH_REQUEST_DATA)ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(MCBA_DEVICE_BATCH_REQUEST_DATA), POOL_TAG);
        if (!pData) {
            WdfRequestCompleteWithInformation(Request, STATUS_NO_MEMORY, 0);
            return;
        }
        
        for (MCBA_USB_REQUEST_INDEX_TYPE i = 0; i < _countof(pData->BatchIndices); ++i) {
            pData->BatchIndices[i] = i;
        }

        pBatchRequestEntryLink = &pData->Next;
    }

    pData = CONTAINING_RECORD(pBatchRequestEntryLink, MCBA_DEVICE_BATCH_REQUEST_DATA, Next);
    
    status = McbaUsbRequestsAlloc(&DeviceContext->UsbRequests, Count, pData->UsbRequestIndices);
    if (!NT_SUCCESS(status)) {
        ExInterlockedPushEntrySList(&DeviceContext->BatchRequestDataListHeader, &pData->Next, &DeviceContext->BatchRequestDataLock);
        WdfRequestCompleteWithInformation(Request, status, 0);
        return;
    }

    pData->Request = Request;
    pData->Count = (LONG)Count;
    pData->Pending = (LONG)Count;


    for (MCBA_USB_REQUEST_INDEX_TYPE i = 0; i < Count; ++i, ++Msg, ++sent) {
        MCBA_USB_REQUEST_INDEX_TYPE usbIndex = pData->UsbRequestIndices[i];
        WDFREQUEST usbRequest = DeviceContext->UsbRequests.Requests[usbIndex];
        WdfRequestSetCompletionRoutine(usbRequest, McbaUrbCompletedForCanFrameInBatch, &pData->BatchIndices[i]);
        struct mcba_usb_msg_can* pCanMsg = (struct mcba_usb_msg_can*)&DeviceContext->UsbRequests.Messages[usbIndex];
        McbaFormatCanMessage(DeviceContext, Msg, pCanMsg);
        pData->UsbRequestStatuses[i] = McbaUsbBulkWritePipeSend(DeviceContext, usbIndex);
        if (!NT_SUCCESS(pData->UsbRequestStatuses[i])) {
            break;
        }
    }

    if (sent) {
        if (sent < Count) {
            LONG result = InterlockedAdd(&pData->Pending, (LONG)sent - (LONG)Count);
            if (0 == result) {
                McbaFinalizeBatchWrite(pData);
            }
        }
    }
    else {
        McbaFinalizeBatchWrite(pData);
    }

}

_Use_decl_annotations_
VOID
McbaEvtIoWrite(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t Length)
{
    UNREFERENCED_PARAMETER(Queue);

    NTSTATUS status;
    PMCBA_DEVICE_CONTEXT pDeviceContext;
    PMCBA_CAN_MSG pMsg;

    TraceEvents(
        TRACE_LEVEL_INFORMATION, 
        TRACE_QUEUE, 
        "--> %!FUNC! Request=0x%p Length=%u", Request, (unsigned)Length);

    size_t count = Length / sizeof(*pMsg);
    if (Length != count * sizeof(*pMsg)) {
        TraceEvents(
            TRACE_LEVEL_ERROR,
            TRACE_QUEUE,
            "%!FUNC! Input buffer size %u is not a multiple of %u=sizeof(MCBA_CAN_MSG)\n",
            (unsigned)Length, (unsigned)sizeof(MCBA_CAN_MSG));
        status = STATUS_INVALID_PARAMETER;
        goto Error;
    }

    if (count > MCBA_MAX_WRITES) {
        TraceEvents(
            TRACE_LEVEL_ERROR,
            TRACE_QUEUE,
            "%!FUNC! batch write size %u exceeds max batch write size %u\n",
            (unsigned)Length, (unsigned)MCBA_MAX_WRITES);
        status = STATUS_INVALID_PARAMETER;
        goto Error;
    }

    

    status = WdfRequestRetrieveInputBuffer(Request, Length, &pMsg, NULL);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfRequestRetrieveInputBuffer failed with status %!STATUS!\n", status);
        goto Error;
    }

    pDeviceContext = McbaDeviceGetContext(WdfIoQueueGetDevice(Queue));

    switch (count) {
    case 0:
        goto Error;
    case 1: {
        struct mcba_usb_msg_can c;
        McbaFormatCanMessage(pDeviceContext, pMsg, &c);
        McbaStandaloneUsbRequest(pDeviceContext, Request, (const struct mcba_usb_msg*) &c);
    } break;
    default: {
        McbaStartBatchWrite(pDeviceContext, Request, pMsg, (MCBA_USB_REQUEST_INDEX_TYPE)count);
    } break;
    }

Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "<-- %!FUNC!\n");
    return;

Error:
    WdfRequestCompleteWithInformation(Request, status, 0);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Request 0x%p completed with status=%!STATUS! bytes transferred=%u\n", Request, status, 0u);

    goto Exit;
}

static
_Use_decl_annotations_
VOID
McbaStandaloneUsbRequest(
    PMCBA_DEVICE_CONTEXT DeviceContext,
    WDFREQUEST Request,
    const struct mcba_usb_msg* Msg
)
{
    MCBA_USB_REQUEST_INDEX_TYPE index;
    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "--> %!FUNC! Request 0x%p\n", Request);
    
    status = McbaUsbRequestsAlloc(&DeviceContext->UsbRequests, 1, &index);
    if (!NT_SUCCESS(status)) {
        goto Error;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "--> %!FUNC! index=%u\n", index);
    
    // set context so we can complete the user request in McbaUrbCompleted
    DeviceContext->UsbRequests.Contexts[index] = Request;
    // copy the usb message for this request into the usb request memory
    RtlCopyMemory(&DeviceContext->UsbRequests.Messages[index], Msg, sizeof(*Msg));
    // set completion routine that will complete the user request and return the allocated usb request
    WdfRequestSetCompletionRoutine(DeviceContext->UsbRequests.Requests[index], McbaUrbCompleted, ULongToPtr(index));

    status = McbaUsbBulkWritePipeSend(DeviceContext, index);
    if (!NT_SUCCESS(status)) {
        McbaUsbRequestsFree(&DeviceContext->UsbRequests, 1, &index);
        goto Error;
    }
Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "<-- %!FUNC! Request 0x%p pending on index %u\n", Request, index);
    return;

Error:
    WdfRequestCompleteWithInformation(Request, status, 0);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Request 0x%p completed with status=%!STATUS! bytes transferred=%u\n", Request, status, 0u);

    goto Exit;
}