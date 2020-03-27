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
#include "Device.tmh"

#define BULK_READ_PIPE_REQUESTS 2

static 
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
McbaEvtDevicePrepareHardware(
    _In_
    WDFDEVICE Device,
    _In_
    WDFCMRESLIST ResourcesRaw,
    _In_
    WDFCMRESLIST ResourcesTranslated
    );

static
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
McbaEvtDeviceFileCreate(
    _In_
    WDFDEVICE Device,
    _In_
    WDFREQUEST Request,
    _In_
    WDFFILEOBJECT FileObject
    );

static
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
McbaEvtFileCleanup(
    _In_
    WDFFILEOBJECT FileObject
);


static
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
McbaEvtDeviceD0Entry(
    _In_
    WDFDEVICE Device,
    _In_
    WDF_POWER_DEVICE_STATE PreviousState
);

static
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
McbaEvtDeviceD0Exit(
    _In_
    WDFDEVICE Device,
    _In_
    WDF_POWER_DEVICE_STATE TargetState
);


static
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
McbaUsbReaderCompletionRoutine(
    _In_
    WDFUSBPIPE Pipe,
    _In_
    WDFMEMORY Buffer,
    _In_
    size_t NumBytesTransferred,
    _In_
    WDFCONTEXT Context
);

static
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
McbaProcessKeepAliveCan(
    _Inout_
    PMCBA_DEVICE_CONTEXT DeviceContext,
    _In_
    const struct mcba_usb_msg_ka_can* Msg
);

static
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
McbaProcessKeepAliveUsb(
    _Inout_
    PMCBA_DEVICE_CONTEXT DeviceContext,
    _In_
    const struct mcba_usb_msg_ka_usb* Msg
);

static 
inline 
UINT16 
Swap16(UINT16 value) {
    return RtlUshortByteSwap(value);
}

static 
inline 
UINT16 
Nop16(UINT16 value) {
    return value;
}

static
inline
UINT16
ReadUnalignedBigEndian16(_In_ PMCBA_DEVICE_CONTEXT DeviceContext, _In_ const void* ptr)
{
    UINT16 x;
    RtlCopyMemory(&x, ptr, 2);
    return DeviceContext->BigEndianToHost(x);
}

static
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS 
McbaSelectInterfaces(
    _In_ WDFDEVICE Device
);

static
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
VOID
McbaMessageBuffersFree(
    _Inout_ PMCBA_DEVICE_CONTEXT DeviceContext
);

static
_IRQL_requires_same_
VOID
McbaUrbCompletedForFirmwareRequest(
    _In_
    WDFREQUEST Request,
    _In_
    WDFIOTARGET Target,
    _In_
    PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_
    WDFCONTEXT Context
);

static
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
McbaUsbRequestsInit(
    _Inout_ PMCBA_DEVICE_USB_REQUEST_DATA UsbRequestData,
    _In_ WDFDEVICE Device,
    _In_ WDFIOTARGET IoTarget
);

static
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
VOID
McbaUsbRequestsUninit(
    _Inout_ PMCBA_DEVICE_USB_REQUEST_DATA UsbRequestData
);

static
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PCHAR
DbgDevicePowerString(
    _In_ WDF_POWER_DEVICE_STATE Type
)
{
    switch (Type)
    {
    case WdfPowerDeviceInvalid:
        return "WdfPowerDeviceInvalid";
    case WdfPowerDeviceD0:
        return "WdfPowerDeviceD0";
    case WdfPowerDeviceD1:
        return "WdfPowerDeviceD1";
    case WdfPowerDeviceD2:
        return "WdfPowerDeviceD2";
    case WdfPowerDeviceD3:
        return "WdfPowerDeviceD3";
    case WdfPowerDeviceD3Final:
        return "WdfPowerDeviceD3Final";
    case WdfPowerDevicePrepareForHibernation:
        return "WdfPowerDevicePrepareForHibernation";
    case WdfPowerDeviceMaximum:
        return "WdfPowerDeviceMaximum";
    default:
        return "UnKnown Device Power State";
    }
}


#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, McbaMessageBuffersFree)
#pragma alloc_text(PAGE, McbaCreateDevice)
#pragma alloc_text(PAGE, McbaEvtDevicePrepareHardware)
#pragma alloc_text(PAGE, McbaSelectInterfaces)
#pragma alloc_text(PAGE, McbaEvtDeviceFileCreate)
#pragma alloc_text(PAGE, McbaEvtFileCleanup)

/* This function is not marked pageable because this function is 
 * in the device power up path.  When a function is marked pagable and the code
 * section is paged out, it will generate a page fault which could impact
 * the fast resume behavior because the client driver will have to wait
 * until the system drivers can service this page fault.
 */
//#pragma alloc_text(PAGE, McbaEvtDeviceD0Entry)
#pragma alloc_text(PAGE, McbaEvtDeviceD0Exit)

#endif




_Use_decl_annotations_
static
VOID
McbaUrbCompletedForFirmwareRequest(
    WDFREQUEST Request,
    WDFIOTARGET Target,
    PWDF_REQUEST_COMPLETION_PARAMS Params,
    WDFCONTEXT Context
)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "--> %!FUNC!");

    NTSTATUS status = Params->IoStatus.Status;
    PWDF_USB_REQUEST_COMPLETION_PARAMS usbCompletionParams = Params->Parameters.Usb.Completion;
    MCBA_USB_REQUEST_INDEX_TYPE index = (MCBA_USB_REQUEST_INDEX_TYPE)PtrToUlong(Context);
    PMCBA_DEVICE_CONTEXT pDeviceContext = McbaDeviceGetContext(WdfIoTargetGetDevice(Target));

    UNREFERENCED_PARAMETER(Request);

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE,
            "%!FUNC! Write failed with status %!STATUS!, UsbdStatus 0x%x\n",
            status, usbCompletionParams->UsbdStatus);

        // FIX ME, requeue
    }

    McbaUsbRequestsReuse(&pDeviceContext->UsbRequests, 1, &index);
    McbaUsbRequestsFree(&pDeviceContext->UsbRequests, 1, &index);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "<-- %!FUNC!");
}

_Use_decl_annotations_
NTSTATUS
McbaCreateDevice(
    PWDFDEVICE_INIT DeviceInit
    )
/*++

Routine Description:

    Worker routine called to create a device and its software resources.

Arguments:

    DeviceInit - Pointer to an opaque init structure. Memory for this
                    structure will be freed by the framework when the WdfDeviceCreate
                    succeeds. So don't access the structure after that point.

Return Value:

    NTSTATUS

--*/
{
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_OBJECT_ATTRIBUTES deviceAttributes, fileAttributes;
    PMCBA_DEVICE_CONTEXT pDeviceContext = NULL;
    WDFDEVICE device;
    NTSTATUS status;
    WDF_DEVICE_PNP_CAPABILITIES pnpCaps;
    WDF_IO_TYPE_CONFIG ioTypeConfig;
    WDF_FILEOBJECT_CONFIG fileConfig;
    BOOLEAN memoryAllocated = FALSE;
    static const UINT16 Uint16 = 0xff;
    UINT8 const* const pEndianCheck = (UINT8 const* const )&Uint16;
    BOOLEAN littleEndian;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "--> %!FUNC!");

    if (0xff == *pEndianCheck) {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Little endian machine\n");
        littleEndian = TRUE;
    } else if (0 == *pEndianCheck) {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Big endian machine\n");
        littleEndian = FALSE;
    } else {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! Unsupported endian machine\n");
        status = STATUS_UNSUCCESSFUL;
        goto Error;
    }

    WDF_IO_TYPE_CONFIG_INIT(&ioTypeConfig);
    ioTypeConfig.DeviceControlIoType = WdfDeviceIoDirect;
    ioTypeConfig.ReadWriteIoType = WdfDeviceIoDirect;
    WdfDeviceInitSetIoTypeEx(DeviceInit, &ioTypeConfig);


    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    //
    // Register PNP callbacks.
    //
    pnpPowerCallbacks.EvtDevicePrepareHardware = McbaEvtDevicePrepareHardware;
    
    //pnpPowerCallbacks.EvtDevicePrepareHardware = ToasterEvtDevicePrepareHardware;
    //pnpPowerCallbacks.EvtDeviceReleaseHardware = ToasterEvtDeviceReleaseHardware;
    //pnpPowerCallbacks.EvtDeviceSelfManagedIoInit = ToasterEvtDeviceSelfManagedIoInit;

    //
    // Register Power callbacks.
    //
    pnpPowerCallbacks.EvtDeviceD0Entry = McbaEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = McbaEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    //
    // Initialize WDF_FILEOBJECT_CONFIG_INIT struct to tell the
    // framework whether you are interested in handling Create, Close and
    // Cleanup requests that gets genereate when an application or another
    // kernel component opens an handle to the device. If you don't register,
    // the framework default behaviour would be complete these requests
    // with STATUS_SUCCESS. A driver might be interested in registering these
    // events if it wants to do security validation and also wants to maintain
    // per handle (fileobject) context.
    //

    WDF_FILEOBJECT_CONFIG_INIT(
        &fileConfig,
        McbaEvtDeviceFileCreate,
        WDF_NO_EVENT_CALLBACK, // not interested in Close
        McbaEvtFileCleanup
    );

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&fileAttributes, MCBA_FILE_CONTEXT);

    WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, &fileAttributes);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, MCBA_DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfDeviceCreate failed with status code %!STATUS!\n", status);
        return status;
    }

    //
    // Tell the framework to set the SurpriseRemovalOK in the DeviceCaps so
    // that you don't get the popup in usermode when you surprise remove the device.
    //
    WDF_DEVICE_PNP_CAPABILITIES_INIT(&pnpCaps);
    pnpCaps.SurpriseRemovalOK = WdfTrue;

    WdfDeviceSetPnpCapabilities(device, &pnpCaps);

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfDeviceCreate failed with status code %!STATUS!\n", status);
        return status;
    }

    //
    // Get a pointer to the device context structure that we just associated
    // with the device object. We define this structure in the device.h
    // header file. McbaDeviceGetContext is an inline function generated by
    // using the WDF_DECLARE_CONTEXT_TYPE_WITH_NAME macro in device.h.
    // This function will do the type checking and return the device context.
    // If you pass a wrong object handle it will return NULL and assert if
    // run under framework verifier mode.
    //
    pDeviceContext = McbaDeviceGetContext(device);

    if (littleEndian) {
        pDeviceContext->BigEndianToHost = Swap16;
        pDeviceContext->LittleEndianToHost = Nop16;
    } else {
        pDeviceContext->BigEndianToHost = Nop16;
        pDeviceContext->LittleEndianToHost = Swap16;
    }


    ExInitializeSListHead(&pDeviceContext->MessageBuffersAvailable);
    KeInitializeSpinLock(&pDeviceContext->MessageBuffersLock);
    status = McbaMessageBuffersAdd(pDeviceContext);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! McbaAddMessageBuffers failed with status code %!STATUS!\n", status);
        goto Error;
    }

    memoryAllocated = TRUE;

    InitializeListHead(&pDeviceContext->FilesList);
    KeInitializeSpinLock(&pDeviceContext->FilesLock);

    ExInitializeSListHead(&pDeviceContext->BatchRequestDataListHeader);
    KeInitializeSpinLock(&pDeviceContext->BatchRequestDataLock);

    //
    // Create a device interface so that applications can find and talk
    // to us.
    //
    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_MCBA, NULL);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfDeviceCreateDeviceInterface failed with status code %!STATUS!\n", status);
        goto Error;
    }

    status = McbaQueueInitialize(device);
    if (!NT_SUCCESS(status)) {
        goto Error;
    }

Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "--> %!FUNC! completed with status=%!STATUS!\n", status);
    return status;

Error:
    if (memoryAllocated) {
        McbaMessageBuffersFree(pDeviceContext);
    }
    
    goto Exit;
}



_Use_decl_annotations_
static 
NTSTATUS
McbaEvtDevicePrepareHardware(
    WDFDEVICE Device,
    WDFCMRESLIST ResourceList,
    WDFCMRESLIST ResourceListTranslated
)
/*++

Routine Description:

    In this callback, the driver does whatever is necessary to make the
    hardware ready to use.  In the case of a USB device, this involves
    reading and selecting descriptors.

Arguments:

    Device - handle to a device

    ResourceList - handle to a resource-list object that identifies the
                   raw hardware resources that the PnP manager assigned
                   to the device

    ResourceListTranslated - handle to a resource-list object that
                             identifies the translated hardware resources
                             that the PnP manager assigned to the device

Return Value:

    NT status value

--*/
{
    NTSTATUS                            status;
    PMCBA_DEVICE_CONTEXT                     pDeviceContext;
    WDF_USB_DEVICE_INFORMATION          deviceInfo;
    ULONG                               waitWakeEnable;

    UNREFERENCED_PARAMETER(ResourceList);
    UNREFERENCED_PARAMETER(ResourceListTranslated);
    waitWakeEnable = FALSE;
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "--> %!FUNC!");

    pDeviceContext = McbaDeviceGetContext(Device);

    //
    // Create a USB device handle so that we can communicate with the
    // underlying USB stack. The WDFUSBDEVICE handle is used to query,
    // configure, and manage all aspects of the USB device.
    // These aspects include device properties, bus properties,
    // and I/O creation and synchronization. We only create device the first
    // the PrepareHardware is called. If the device is restarted by pnp manager
    // for resource rebalance, we will use the same device handle but then select
    // the interfaces again because the USB stack could reconfigure the device on
    // restart.
    //
    if (pDeviceContext->UsbDevice == NULL) {
        WDF_USB_DEVICE_CREATE_CONFIG config;

        WDF_USB_DEVICE_CREATE_CONFIG_INIT(&config, USBD_CLIENT_CONTRACT_VERSION_602);
        status = WdfUsbTargetDeviceCreateWithParameters(
            Device,
            &config,
            WDF_NO_OBJECT_ATTRIBUTES,
            &pDeviceContext->UsbDevice);

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE,
                "%!FUNC! WdfUsbTargetDeviceCreateWithParameters failed with status code %!STATUS!\n", status);
            goto Exit;
        }

        status = WdfUsbTargetDeviceResetPortSynchronously(pDeviceContext->UsbDevice);

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfUsbTargetDeviceResetPortSynchronously failed with status=%!STATUS!\n", status);
            goto Exit;
        }

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! USB port reset successfully\n");

        //
        // TODO: If you are fetching configuration descriptor from device for
        // selecting a configuration or to parse other descriptors, call OsrFxValidateConfigurationDescriptor
        // to do basic validation on the descriptors before you access them .
        //
    }

    //
    // Retrieve USBD version information, port driver capabilites and device
    // capabilites such as speed, power, etc.
    //
    WDF_USB_DEVICE_INFORMATION_INIT(&deviceInfo);    
    status = WdfUsbTargetDeviceRetrieveInformation(pDeviceContext->UsbDevice, &deviceInfo);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfUsbTargetDeviceRetrieveInformation failed with status=%!STATUS!\n", status);
        goto Exit;
    }


    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! IsDeviceHighSpeed: %s\n",
        (deviceInfo.Traits & WDF_USB_DEVICE_TRAIT_AT_HIGH_SPEED) ? "TRUE" : "FALSE");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE,
        "%!FUNC! IsDeviceSelfPowered: %s\n",
        (deviceInfo.Traits & WDF_USB_DEVICE_TRAIT_SELF_POWERED) ? "TRUE" : "FALSE");

    waitWakeEnable = deviceInfo.Traits & 
        WDF_USB_DEVICE_TRAIT_REMOTE_WAKE_CAPABLE;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, 
        "%!FUNC! IsDeviceRemoteWakeable: %s\n",
        waitWakeEnable ? "TRUE" : "FALSE");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE,
        "%!FUNC! Version: %x\n", deviceInfo.UsbdVersionInformation.Supported_USB_Version);
        
    
    status = McbaSelectInterfaces(Device);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE,  "%!FUNC! McbaSelectInterfaces failed with status=%!STATUS!\n", status);
        goto Exit;
    }

Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "<-- %!FUNC! completed with status=%!STATUS!", status);
    return status;
}


_Use_decl_annotations_
static 
NTSTATUS 
McbaSelectInterfaces(
    WDFDEVICE Device
)
{
    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS configParams;
    NTSTATUS status = STATUS_SUCCESS;
    PMCBA_DEVICE_CONTEXT pDeviceContext;
    WDFUSBPIPE pipe;
    WDF_USB_PIPE_INFORMATION            pipeInfo;
    UCHAR                               index;
    UCHAR                               numberConfiguredPipes;
    WDF_USB_CONTINUOUS_READER_CONFIG usbContinousReaderConfig;
    
    PAGED_CODE();

    
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "--> %!FUNC!\n");

    pDeviceContext = McbaDeviceGetContext(Device);

    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_SINGLE_INTERFACE(&configParams);

    status = WdfUsbTargetDeviceSelectConfig(pDeviceContext->UsbDevice, WDF_NO_OBJECT_ATTRIBUTES, &configParams);
    if (!NT_SUCCESS(status)) {
        TraceEvents(
            TRACE_LEVEL_ERROR, 
            TRACE_DEVICE, 
            "%!FUNC! WdfUsbTargetDeviceSelectConfig failed with status=%!STATUS!\n",
            status);

        goto Exit;
    }

    pDeviceContext->UsbInterface = configParams.Types.SingleInterface.ConfiguredUsbInterface;

    numberConfiguredPipes = configParams.Types.SingleInterface.NumberConfiguredPipes;

    //
    // Get pipe handles
    //
    for (index = 0; index < numberConfiguredPipes; ++index) {

        WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
        pipe = WdfUsbInterfaceGetConfiguredPipe(pDeviceContext->UsbInterface, index, &pipeInfo);
        //
        // Tell the framework that it's okay to read less than
        // MaximumPacketSize
        //
        WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pipe);

        if (WdfUsbPipeTypeBulk == pipeInfo.PipeType &&
            WdfUsbTargetPipeIsInEndpoint(pipe)) {
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE,
                "%!FUNC! BulkInput Pipe is 0x%p\n", pipe);
            pDeviceContext->BulkReadPipe = pipe;
        }

        if (WdfUsbPipeTypeBulk == pipeInfo.PipeType &&
            WdfUsbTargetPipeIsOutEndpoint(pipe)) {
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE,
                "%!FUNC! BulkOutput Pipe is 0x%p\n", pipe);
            pDeviceContext->BulkWritePipe = pipe;
        }
    }

    //
    // If we didn't find both pipes, fail the start.
    //
    if (!(pDeviceContext->BulkWritePipe
        && pDeviceContext->BulkReadPipe)) {
        status = STATUS_INVALID_DEVICE_STATE;
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, 
            "%!FUNC! device is not configured properly failing with status=%!STATUS!\n",
            status);

        goto Exit;
    }

    WDF_USB_CONTINUOUS_READER_CONFIG_INIT(
        &usbContinousReaderConfig,
        McbaUsbReaderCompletionRoutine,
        pDeviceContext,
        MCBA_USB_RX_BUFF_SIZE);

    usbContinousReaderConfig.NumPendingReads = BULK_READ_PIPE_REQUESTS;

    status = WdfUsbTargetPipeConfigContinuousReader(
        pDeviceContext->BulkReadPipe, 
        &usbContinousReaderConfig);

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE,
            "%!FUNC! WdfUsbTargetPipeConfigContinuousReader failed %!STATUS!\n", status);
        goto Exit;
    }

Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "<-- %!FUNC! completed with status=%!STATUS!\n", status);
    return status;
}

_Use_decl_annotations_
static
VOID
McbaEvtDeviceFileCreate(
    WDFDEVICE Device,
    WDFREQUEST Request,
    WDFFILEOBJECT FileObject
)
{
    PMCBA_DEVICE_CONTEXT pDeviceContext;
    PMCBA_FILE_CONTEXT pFileContext;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "--> %!FUNC! Device=0x%p Request=0x%p FileObject=0x%p\n", Device, Request, FileObject);

    pDeviceContext = McbaDeviceGetContext(Device);
    pFileContext = McbaFileGetContext(FileObject);

    InitializeListHead(&pFileContext->ReadBuffersList);
    ExInterlockedInsertTailList(&pDeviceContext->FilesList, &pFileContext->FilesList, &pDeviceContext->FilesLock);

    KeInitializeSpinLock(&pFileContext->ReadLock);

    WdfRequestComplete(Request, STATUS_SUCCESS);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "<-- %!FUNC!\n");
}

_Use_decl_annotations_
static
VOID
McbaEvtFileCleanup(
    WDFFILEOBJECT FileObject
)
{
    PMCBA_DEVICE_CONTEXT pDeviceContext;
    PMCBA_FILE_CONTEXT pFileContext;
    KIRQL irql;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "--> %!FUNC! FileObject=0x%p\n", FileObject);

    PAGED_CODE();

    pDeviceContext = McbaDeviceGetContext(WdfFileObjectGetDevice(FileObject));
    pFileContext = McbaFileGetContext(FileObject);

    NT_ASSERT(!pFileContext->PendingReadRequest);
    if (pFileContext->PendingReadRequest) {
        WdfRequestComplete(pFileContext->PendingReadRequest, STATUS_REQUEST_ABORTED);
    }

    KeAcquireSpinLock(&pDeviceContext->FilesLock, &irql);
    RemoveEntryList(&pFileContext->FilesList);
    KeReleaseSpinLock(&pDeviceContext->FilesLock, irql);

    for (PLIST_ENTRY pEntry = pFileContext->ReadBuffersList.Flink; pEntry != &pFileContext->ReadBuffersList; ) {
        PMCBA_CAN_MSG_ITEM pItem = CONTAINING_RECORD(pEntry, MCBA_CAN_MSG_ITEM, ReadBuffers);
        pEntry = pItem->ReadBuffers.Flink;
        ExInterlockedPushEntrySList(
            &pDeviceContext->MessageBuffersAvailable,
            &pItem->MessageBuffersAvailable,
            &pDeviceContext->MessageBuffersLock);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "<-- %!FUNC!\n");
}

_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
static
inline
VOID
McbaStopPipes(
    _In_
    PMCBA_DEVICE_CONTEXT DeviceContext
)
{
    WdfIoTargetStop(WdfUsbTargetPipeGetIoTarget(DeviceContext->BulkReadPipe), WdfIoTargetCancelSentIo);
    WdfIoTargetStop(WdfUsbTargetPipeGetIoTarget(DeviceContext->BulkWritePipe), WdfIoTargetCancelSentIo);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! stopped bulk read/write pipes\n");
}

_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
static 
NTSTATUS
McbaSendFirmwareRequests(
    _Inout_ PMCBA_DEVICE_CONTEXT DeviceContext
)
{
    NTSTATUS status;
    MCBA_USB_REQUEST_INDEX_TYPE indices[2];

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "--> %!FUNC! DeviceContext=0x%p\n", DeviceContext);

    NT_ASSERT(DeviceContext);

    status = McbaUsbRequestsAlloc(&DeviceContext->UsbRequests, _countof(indices), indices);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! McbaUsbRequestAlloc for fw req failed with %!STATUS!\n", status);
        goto Exit;
    }

    struct mcba_usb_msg_fw_ver* usb_msg = (struct mcba_usb_msg_fw_ver*) & DeviceContext->UsbRequests.Messages[indices[0]];
    usb_msg->cmd_id = MBCA_CMD_READ_FW_VERSION;
    usb_msg->pic = MCBA_VER_REQ_USB;

    struct mcba_usb_msg_fw_ver* can_msg = (struct mcba_usb_msg_fw_ver*) & DeviceContext->UsbRequests.Messages[indices[1]];
    can_msg->cmd_id = MBCA_CMD_READ_FW_VERSION;
    can_msg->pic = MCBA_VER_REQ_CAN;

    for (size_t i = 0; i < _countof(indices); ++i) {
        MCBA_USB_REQUEST_INDEX_TYPE index = indices[i];
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! set fw requests completion routing for index=%u\n", index);
        WdfRequestSetCompletionRoutine(DeviceContext->UsbRequests.Requests[index], McbaUrbCompletedForFirmwareRequest, UlongToPtr(index));
    }

    status = McbaUsbBulkWritePipeSend(DeviceContext, indices[0]);
    if (!NT_SUCCESS(status)) {
        McbaUsbRequestsFree(&DeviceContext->UsbRequests, _countof(indices), indices);
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! McbaUsbBulkWritePipeSend for pic usb firmware failed %!STATUS!\n", status);
        goto Exit;
    }

    status = McbaUsbBulkWritePipeSend(DeviceContext, indices[1]);
    if (!NT_SUCCESS(status)) {
        McbaUsbRequestsFree(&DeviceContext->UsbRequests, 1, &indices[1]);
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! McbaUsbBulkWritePipeSend for pic can firmware failed %!STATUS!\n", status);
        goto Exit;
    }
Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "<-- %!FUNC! completed with status=%!STATUS!\n", status);
    return status;
}

_Use_decl_annotations_
static
NTSTATUS
McbaEvtDeviceD0Entry(
    WDFDEVICE Device,
    WDF_POWER_DEVICE_STATE PreviousState
)
/*++

Routine Description:

    EvtDeviceD0Entry event callback must perform any operations that are
    necessary before the specified device is used.  It will be called every
    time the hardware needs to be (re-)initialized.

    This function is not marked pageable because this function is in the
    device power up path. When a function is marked pagable and the code
    section is paged out, it will generate a page fault which could impact
    the fast resume behavior because the client driver will have to wait
    until the system drivers can service this page fault.

    This function runs at PASSIVE_LEVEL, even though it is not paged.  A
    driver can optionally make this function pageable if DO_POWER_PAGABLE
    is set.  Even if DO_POWER_PAGABLE isn't set, this function still runs
    at PASSIVE_LEVEL.  In this case, though, the function absolutely must
    not do anything that will cause a page fault.

Arguments:

    Device - Handle to a framework device object.

    PreviousState - Device power state which the device was in most recently.
        If the device is being newly started, this will be
        PowerDeviceUnspecified.

Return Value:

    NTSTATUS

--*/
{
    PMCBA_DEVICE_CONTEXT pDeviceContext;
    NTSTATUS status = STATUS_SUCCESS;
    NTSTATUS statusPipe;
    WDFIOTARGET bulkWriteTarget;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE,
        "--> %!FUNC! Device=0x%p coming from %s\n", Device, DbgDevicePowerString(PreviousState));

    pDeviceContext = McbaDeviceGetContext(Device);

    //
    // Since continuous reader is configured for this interrupt-pipe, we must explicitly start
    // the I/O target to get the framework to post read requests.
    //
    statusPipe = WdfIoTargetStart(WdfUsbTargetPipeGetIoTarget(pDeviceContext->BulkReadPipe));
    if (NT_SUCCESS(statusPipe)) {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! started bulk read pipe\n");
    } else {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "Failed to start bulk read pipe status=%!STATUS!\n", statusPipe);
        if (NT_SUCCESS(status)) {
            status = statusPipe;
        }
    }

    bulkWriteTarget = WdfUsbTargetPipeGetIoTarget(pDeviceContext->BulkWritePipe);
    statusPipe = WdfIoTargetStart(bulkWriteTarget);
    if (NT_SUCCESS(statusPipe)) {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! started bulk write pipe\n");
    } else {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "Failed to start bulk write pipe status=%!STATUS!\n", statusPipe);
        if (NT_SUCCESS(status)) {
            status = statusPipe;
        }
    }

    if (NT_SUCCESS(status)) {
        status = McbaUsbRequestsInit(&pDeviceContext->UsbRequests, Device, bulkWriteTarget);
        if (NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! initialized USB requests for bulk write pipe\n");
            status = McbaSendFirmwareRequests(pDeviceContext);
            if (NT_SUCCESS(status)) {
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! send firmware requests\n");
            }
            else {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed to send firmware requests status=%!STATUS!\n", status);
            }
        }
        else {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed to initialized USB requests for bulk write pipe status=%!STATUS!\n", status);
        }
    }
    
    if (!NT_SUCCESS(status)) {
        McbaUsbRequestsUninit(&pDeviceContext->UsbRequests);
        McbaStopPipes(pDeviceContext);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "<-- %!FUNC! completed with status=%!STATUS!\n", status);

    return status;
}

_Use_decl_annotations_
static
NTSTATUS
McbaEvtDeviceD0Exit(
    WDFDEVICE Device,
    WDF_POWER_DEVICE_STATE TargetState
)
/*++

Routine Description:

    This routine undoes anything done in EvtDeviceD0Entry.  It is called
    whenever the device leaves the D0 state, which happens when the device is
    stopped, when it is removed, and when it is powered off.

    The device is still in D0 when this callback is invoked, which means that
    the driver can still touch hardware in this routine.


    EvtDeviceD0Exit event callback must perform any operations that are
    necessary before the specified device is moved out of the D0 state.  If the
    driver needs to save hardware state before the device is powered down, then
    that should be done here.

    This function runs at PASSIVE_LEVEL, though it is generally not paged.  A
    driver can optionally make this function pageable if DO_POWER_PAGABLE is set.

    Even if DO_POWER_PAGABLE isn't set, this function still runs at
    PASSIVE_LEVEL.  In this case, though, the function absolutely must not do
    anything that will cause a page fault.

Arguments:

    Device - Handle to a framework device object.

    TargetState - Device power state which the device will be put in once this
        callback is complete.

Return Value:

    Success implies that the device can be used.  Failure will result in the
    device stack being torn down.

--*/
{
    PMCBA_DEVICE_CONTEXT         pDeviceContext;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE,
        "--> %!FUNC! Device=0x%p moving to %s\n", Device, DbgDevicePowerString(TargetState));

    pDeviceContext = McbaDeviceGetContext(Device);

    McbaStopPipes(pDeviceContext);
    McbaUsbRequestsUninit(&pDeviceContext->UsbRequests);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "<-- %!FUNC!\n");

    return STATUS_SUCCESS;
}



static
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
McbaAddCanMessageToFileReadQueue(
    _Inout_ PMCBA_DEVICE_CONTEXT DeviceContext,
    _Inout_ PMCBA_FILE_CONTEXT FileContext,
    _In_ const MCBA_CAN_MSG_DATA* Msg
)
{
    WDFREQUEST requestToComplete = NULL;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG_PTR information = 0;
    KIRQL irql;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "--> %!FUNC! DeviceContext=0x%p FileContext=0x%p Msg=0x%p\n", DeviceContext, FileContext, Msg);

    NT_ASSERT(DeviceContext);
    NT_ASSERT(FileContext);
    NT_ASSERT(Msg);

    KeAcquireSpinLock(&FileContext->ReadLock, &irql);

    if (FileContext->PendingReadRequest) {
        NT_ASSERT(FileContext->PendingReadOffset < FileContext->PendingReadCount);
        FileContext->PendingReadBuffer[FileContext->PendingReadOffset] = *Msg;
        ++FileContext->PendingReadOffset;

        // call this here to check for cancelation
        status = WdfRequestUnmarkCancelable(FileContext->PendingReadRequest);
        if (NT_SUCCESS(status)) {
            if (FileContext->PendingReadOffset == FileContext->PendingReadCount) {
                requestToComplete = FileContext->PendingReadRequest;
                information = FileContext->PendingReadCount * sizeof(*FileContext->PendingReadBuffer);
                McbaClearPendingReadRequest(FileContext);
            }
            else {
                // re-mark cancelable
                status = WdfRequestMarkCancelableEx(FileContext->PendingReadRequest, McbaCancelPendingReadRequest);
                if (!NT_SUCCESS(status)) {
                    goto PostCancelOp;
                }
            }
        }
        else {
        PostCancelOp:
            if (STATUS_CANCELLED == status) { // cancel callback executed, we don't need to complete the request
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Pending read request=%p was cancelled\n", FileContext->PendingReadRequest);
            }
            else {
                requestToComplete = FileContext->PendingReadRequest;
            }

            McbaClearPendingReadRequest(FileContext);
        }
    }
    else {
        PMCBA_CAN_MSG_ITEM pCanMessageListItem;
        if (MCBA_MAX_READ_BUFFERS_QUEUED == FileContext->ReadBuffersQueued) {
            TraceEvents(
                TRACE_LEVEL_WARNING,
                TRACE_DEVICE,
                "%!FUNC! Dropping message from file 0x%p queue which is full (size=%d)\n",
                WdfObjectContextGetObject(FileContext),
                MCBA_MAX_READ_BUFFERS_QUEUED);
            PLIST_ENTRY pEntry = RemoveHeadList(&FileContext->ReadBuffersList);
            NT_ASSERT(pEntry);
            ++FileContext->Stats.RxLost;
            --FileContext->ReadBuffersQueued;
            pCanMessageListItem = CONTAINING_RECORD(pEntry, MCBA_CAN_MSG_ITEM, ReadBuffers);
        }
        else {
            PSLIST_ENTRY pEntry;
        AllocBuffers:
            pEntry = ExInterlockedPopEntrySList(&DeviceContext->MessageBuffersAvailable, &DeviceContext->MessageBuffersLock);
            if (!pEntry) {
                status = McbaMessageBuffersAdd(DeviceContext);
                if (!NT_SUCCESS(status)) {
                    TraceEvents(
                        TRACE_LEVEL_ERROR,
                        TRACE_DEVICE,
                        "%!FUNC! Memory allocation failed with status %!STATUS!\n",
                        status);
                    ++FileContext->Stats.RxLost;
                    goto Exit;
                }

                goto AllocBuffers;
            }

            pCanMessageListItem = CONTAINING_RECORD(pEntry, MCBA_CAN_MSG_ITEM, MessageBuffersAvailable);
        }

        pCanMessageListItem->Msg = Msg->Msg;
        pCanMessageListItem->Timestamp = Msg->SystemTimeReceived << 1;

        InsertTailList(&FileContext->ReadBuffersList, &pCanMessageListItem->ReadBuffers);
        ++FileContext->ReadBuffersQueued;
    }
Exit:
    KeReleaseSpinLock(&FileContext->ReadLock, irql);

    if (requestToComplete) {
        WdfRequestCompleteWithInformation(requestToComplete, status, information);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Pending read request=%p completed with status=%!STATUS! information=%ul\n", requestToComplete, status, (unsigned long)information);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "<-- %!FUNC!\n");
}

static
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
McbaOnCanMsgReceived(
    _Inout_ PMCBA_DEVICE_CONTEXT DeviceContext,
    _In_ const struct mcba_usb_msg_can* Msg
)   
{
    MCBA_CAN_MSG_DATA canMsg;
    UINT16 sid;

    sid = ReadUnalignedBigEndian16(DeviceContext, &Msg->sid);

    if (sid & MCBA_SIDL_EXID_MASK) {
        /* SIDH    | SIDL                 | EIDH   | EIDL
         * 28 - 21 | 20 19 18 x x x 17 16 | 15 - 8 | 7 - 0
         */
        canMsg.Msg.Id = MCBA_CAN_EFF_FLAG;

        /* store 28-18 bits */
        canMsg.Msg.Id |= (sid & 0xffe0) << 13;
        /* store 17-16 bits */
        canMsg.Msg.Id |= (sid & 3) << 16;
        /* store 15-0 bits */
        canMsg.Msg.Id |= ReadUnalignedBigEndian16(DeviceContext, &Msg->eid);
    } else {
        /* SIDH   | SIDL
         * 10 - 3 | 2 1 0 x x x x x
         */
        canMsg.Msg.Id = (sid & 0xffe0) >> 5;
    }

    if (Msg->dlc & MCBA_DLC_RTR_MASK) {
        canMsg.Msg.Id |= MCBA_CAN_RTR_FLAG;
    }

    canMsg.Msg.Dlc = Msg->dlc & MCBA_DLC_MASK;
    if (canMsg.Msg.Dlc > MCBA_CAN_MAX_DLC) {
        canMsg.Msg.Dlc = MCBA_CAN_MAX_DLC;
    }

    RtlCopyMemory(canMsg.Msg.Data, Msg->data, MCBA_CAN_MAX_DLC);
    KeQuerySystemTime(&canMsg.SystemTimeReceived);

    for (PLIST_ENTRY pFileEntry = DeviceContext->FilesList.Flink; pFileEntry != &DeviceContext->FilesList; pFileEntry = pFileEntry->Flink) {
        PMCBA_FILE_CONTEXT pFileContext = CONTAINING_RECORD(pFileEntry, MCBA_FILE_CONTEXT, FilesList);
        
        McbaAddCanMessageToFileReadQueue(DeviceContext, pFileContext, &canMsg);
    }
}

static
_Use_decl_annotations_
VOID
McbaProcessKeepAliveCan(
    PMCBA_DEVICE_CONTEXT DeviceContext,
    const struct mcba_usb_msg_ka_can* Msg
)
{
    UINT16 be, le;
    RtlCopyMemory(&be, &Msg->can_bitrate, 2);
    DeviceContext->DeviceStatus.Bitrate = DeviceContext->BigEndianToHost(be);

    if ((DeviceContext->DeviceStatus.Bitrate == 33) || (DeviceContext->DeviceStatus.Bitrate == 83)) {
        DeviceContext->DeviceStatus.Bitrate = DeviceContext->DeviceStatus.Bitrate * 1000 + 333;
    }
    else {
        DeviceContext->DeviceStatus.Bitrate *= 1000;
    }

    RtlCopyMemory(&le, &Msg->rx_lost, 2);
    DeviceContext->DeviceStatus.Stats.RxLost += DeviceContext->LittleEndianToHost(le);
    DeviceContext->DeviceStatus.Stats.RxBufferOverflow += Msg->rx_buff_ovfl;
    DeviceContext->DeviceStatus.Stats.TxErrorCount += Msg->tx_err_cnt;
    DeviceContext->DeviceStatus.Stats.RxErrorCount += Msg->rx_err_cnt;
    DeviceContext->DeviceStatus.Stats.TxBusOff += Msg->tx_bus_off;
    DeviceContext->DeviceStatus.CanSoftwareVersionMajor = Msg->soft_ver_major;
    DeviceContext->DeviceStatus.CanSoftwareVersionMinor = Msg->soft_ver_minor;
}

static
_Use_decl_annotations_
VOID
McbaProcessKeepAliveUsb(
    PMCBA_DEVICE_CONTEXT DeviceContext,
    const struct mcba_usb_msg_ka_usb* Msg
)
{
    DeviceContext->DeviceStatus.UsbSoftwareVersionMajor = Msg->soft_ver_major;
    DeviceContext->DeviceStatus.UsbSoftwareVersionMinor = Msg->soft_ver_minor;
    DeviceContext->DeviceStatus.TerminationEnabled = Msg->termination_state;
}

static
_Use_decl_annotations_
VOID
McbaUsbReaderCompletionRoutine(
    WDFUSBPIPE Pipe,
    WDFMEMORY Buffer,
    size_t NumBytesTransferred,
    WDFCONTEXT Context
)
{
    PMCBA_DEVICE_CONTEXT pDeviceContext = Context;
    size_t count;
    struct mcba_usb_msg* pMsg;
    

    UNREFERENCED_PARAMETER(Pipe);

    TraceEvents(
        TRACE_LEVEL_VERBOSE,
        TRACE_DEVICE,
        "--> %!FUNC! %u bytes transferred\n",
        (unsigned)NumBytesTransferred);

    if (NumBytesTransferred == 0) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DEVICE, "%!FUNC! zero length read\n");
        goto Exit;
    }

    count = NumBytesTransferred / sizeof(*pMsg);
    if (count * sizeof(*pMsg) != NumBytesTransferred) {
        TraceEvents(
            TRACE_LEVEL_VERBOSE,
            TRACE_DEVICE,
            "%!FUNC! payload length=%u is not a multiple of %u=sizeof(struct mcba_usb_msg)\n",
            (unsigned)NumBytesTransferred, 
            (unsigned)sizeof(*pMsg));
    }

    if (!count) {
        goto Exit;
    }

    BOOLEAN filesLocked = FALSE;
    KIRQL irql = 0;
    BOOLEAN filesAvailable = FALSE;
    

    pMsg = WdfMemoryGetBuffer(Buffer, NULL);


    for (size_t i = 0; i < count; ++i, ++pMsg) {
        switch (pMsg->cmd_id) {
        case MBCA_CMD_I_AM_ALIVE_FROM_CAN:
            McbaProcessKeepAliveCan(pDeviceContext, (const struct mcba_usb_msg_ka_can*)pMsg);
            break;

        case MBCA_CMD_I_AM_ALIVE_FROM_USB:
            McbaProcessKeepAliveUsb(pDeviceContext, (const struct mcba_usb_msg_ka_usb*)pMsg);
            break;

        case MBCA_CMD_RECEIVE_MESSAGE:
            if (!filesLocked) {
                KeAcquireSpinLock(&pDeviceContext->FilesLock, &irql);
                filesLocked = TRUE;
                filesAvailable = !IsListEmpty(&pDeviceContext->FilesList);
            }

            if (filesAvailable) {
                McbaOnCanMsgReceived(pDeviceContext, (struct mcba_usb_msg_can*)pMsg);
            }
            break;

        case MBCA_CMD_NOTHING_TO_SEND:
            /* Side effect of communication between PIC_USB and PIC_CAN.
             * PIC_CAN is telling us that it has nothing to send
             */
            break;

        case MBCA_CMD_TRANSMIT_MESSAGE_RSP:
            /* Transmission response from the device containing timestamp */
            break;

        case 0x00:
            TraceEvents(
                TRACE_LEVEL_VERBOSE,
                TRACE_DEVICE,
                "%!FUNC! message index %u, command id 0x00\n",
                (unsigned)i);
            break;

        default:
            TraceEvents(
                TRACE_LEVEL_WARNING,
                TRACE_DEVICE,
                "%!FUNC! unsupported msg (0x%02X)\n",
                pMsg->cmd_id);
            break;
        }
    }

    if (filesLocked) {
        KeReleaseSpinLock(&pDeviceContext->FilesLock, irql);
    }
Exit:
    TraceEvents(
        TRACE_LEVEL_VERBOSE,
        TRACE_DEVICE,
        "<-- %!FUNC!\n");
}

_Use_decl_annotations_
NTSTATUS
McbaMessageBuffersAdd(
    PMCBA_DEVICE_CONTEXT DeviceContext
)
{
    PMCBA_CAN_MSG_ITEM pItem = ExAllocatePoolWithTag(NonPagedPoolNx, PAGE_SIZE, POOL_TAG);
    if (!pItem) {
        return STATUS_NO_MEMORY;
    }

    // store header
    pItem->MemoryBlockOwner = 1;
    ExInterlockedPushEntrySList(&DeviceContext->MessageBuffersAvailable, &pItem->MessageBuffersAvailable, &DeviceContext->MessageBuffersLock);
    
    ++pItem;
    size_t count = PAGE_SIZE / sizeof(MCBA_CAN_MSG_ITEM);

    // store rest
    for (size_t i = 1; i < count; ++i, ++pItem) {
        pItem->MemoryBlockOwner = 0;
        ExInterlockedPushEntrySList(&DeviceContext->MessageBuffersAvailable, &pItem->MessageBuffersAvailable, &DeviceContext->MessageBuffersLock);
    }

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
static
VOID
McbaMessageBuffersFree(
    PMCBA_DEVICE_CONTEXT DeviceContext
)
{
    PMCBA_CAN_MSG_ITEM pMemoryOwnerList = NULL;

    for (PSLIST_ENTRY pEntry = ExInterlockedPopEntrySList(&DeviceContext->MessageBuffersAvailable, &DeviceContext->MessageBuffersLock); pEntry; ) {
        PMCBA_CAN_MSG_ITEM pItem = CONTAINING_RECORD(pEntry, MCBA_CAN_MSG_ITEM, ReadBuffers);
        if (pItem->MemoryBlockOwner) {
            pItem->MessageBuffersAvailable.Next = (PSLIST_ENTRY)pMemoryOwnerList;
            pMemoryOwnerList = pItem;
        }
    }

    while (pMemoryOwnerList) {
        PVOID mem = pMemoryOwnerList;
        pMemoryOwnerList = (PMCBA_CAN_MSG_ITEM)pMemoryOwnerList->MessageBuffersAvailable.Next;
        ExFreePoolWithTag(mem, POOL_TAG);
    }
}

_Use_decl_annotations_
VOID
McbaFormatCanMessage(
    PMCBA_DEVICE_CONTEXT DeviceContext,
    const MCBA_CAN_MSG* Input,
    struct mcba_usb_msg_can* Output
)
{
    UINT16 beSid, beEid;

    Output->cmd_id = MBCA_CMD_TRANSMIT_MESSAGE_EV;

    if (Input->Id & MCBA_CAN_EFF_FLAG) {
        UINT16 sid;
        /* SIDH    | SIDL                 | EIDH   | EIDL
         * 28 - 21 | 20 19 18 x x x 17 16 | 15 - 8 | 7 - 0
         */
        sid = MCBA_SIDL_EXID_MASK;
        /* store 28-18 bits */
        sid |= (Input->Id & 0x1ffc0000) >> 13;
        /* store 17-16 bits */
        sid |= (Input->Id & 0x30000) >> 16;
        beSid = DeviceContext->BigEndianToHost(sid);
        RtlCopyMemory(&Output->sid, &beSid, 2);

        /* store 15-0 bits */
        beEid = DeviceContext->BigEndianToHost(Input->Id & 0xffff);
        RtlCopyMemory(&Output->eid, &beEid, 2);
    } else {
        /* SIDH   | SIDL
         * 10 - 3 | 2 1 0 x x x x x
         */
        beSid = DeviceContext->BigEndianToHost((Input->Id & MCBA_CAN_SFF_MASK) << 5);
        beEid = 0;
        RtlCopyMemory(&Output->sid, &beSid, 2);
        RtlCopyMemory(&Output->eid, &beEid, 2);
    }

    Output->dlc = Input->Dlc;

    RtlCopyMemory(Output->data, Input->Data, sizeof(Output->data));

    if (Input->Id & MCBA_CAN_RTR_FLAG) {
        Output->dlc |= MCBA_DLC_RTR_MASK;
    }
}
#if DBG
static
BOOLEAN
McbaCheckUsbRequestIndices(
    _In_ const MCBA_DEVICE_USB_REQUEST_DATA* UsbRequestData
    )
{
    BOOLEAN result = TRUE;
    ULONG seen[_countof(UsbRequestData->FreeIndices)];
    RtlZeroMemory(seen, sizeof(seen));

    NT_ASSERT(UsbRequestData);
    if (UsbRequestData->FreeIndexEnd > UsbRequestData->Count) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "--> %!FUNC! UsbRequestData=0x%p free index end=%u count=%u\n", UsbRequestData, UsbRequestData->FreeIndexEnd, UsbRequestData->Count);
        result = FALSE;
    }
    else {
        for (MCBA_USB_REQUEST_INDEX_TYPE i = 0; i < UsbRequestData->FreeIndexEnd; ++i) {
            MCBA_USB_REQUEST_INDEX_TYPE index = UsbRequestData->FreeIndices[i];
            if (index >= _countof(UsbRequestData->FreeIndices)) {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "--> %!FUNC! UsbRequestData=0x%p index=%u out of bounds=%u\n", UsbRequestData, index, (unsigned)_countof(UsbRequestData->FreeIndices));
                result = FALSE;
            }
            else {
                ++seen[index];
                if (seen[index] > 1) {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "--> %!FUNC! UsbRequestData=0x%p index=%u count=%u\n", UsbRequestData, index, seen[index]);
                    result = FALSE;
                }
            }
        }
    }

    return result;
}
#endif

static
_Use_decl_annotations_
NTSTATUS
McbaUsbRequestsInit(
    PMCBA_DEVICE_USB_REQUEST_DATA UsbRequestData,
    WDFDEVICE Device,
    WDFIOTARGET IoTarget
)
{
    NTSTATUS status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES attrs;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "--> %!FUNC! UsbRequestData=0x%p Device=0x%p IoTarget=0x%p\n", UsbRequestData, Device, IoTarget);

    NT_ASSERT(UsbRequestData);
    NT_ASSERT(Device);
    NT_ASSERT(IoTarget);

    WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
    attrs.ParentObject = Device;

    for (MCBA_USB_REQUEST_INDEX_TYPE i = 0; i < _countof(UsbRequestData->Requests); ++i) {
        status = WdfRequestCreate(&attrs, IoTarget, &UsbRequestData->Requests[i]);
        if (!NT_SUCCESS(status)) {
            __analysis_assume(!UsbRequestData->Requests[i]);
            break;
        }

        status = WdfMemoryCreatePreallocated(&attrs, &UsbRequestData->Messages[i], sizeof(UsbRequestData->Messages[i]), &UsbRequestData->Memory[i]);
        if (!NT_SUCCESS(status)) {
            __analysis_assume(!UsbRequestData->Memory[i]);
            WdfObjectDelete(UsbRequestData->Requests[i]);
            __analysis_assume(!UsbRequestData->Requests[i]);
            break;
        }

        UsbRequestData->FreeIndices[i] = i;
        UsbRequestData->Count = i + 1;
    }

    if (NT_SUCCESS(status)) {
        UsbRequestData->FreeIndexEnd = UsbRequestData->Count;
        KeInitializeSemaphore(&UsbRequestData->Availabe, _countof(UsbRequestData->Requests), _countof(UsbRequestData->Requests));
        KeInitializeSpinLock(&UsbRequestData->Lock);
        NT_ASSERT(McbaCheckUsbRequestIndices(UsbRequestData));
    }
    else {
        UsbRequestData->FreeIndexEnd = 0;
        McbaUsbRequestsUninit(UsbRequestData);
        NT_ASSERT(McbaCheckUsbRequestIndices(UsbRequestData));

        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! failed to allocate requests/memory for %u USB requests with status=%!STATUS!\n", (unsigned)_countof(UsbRequestData->Requests), status);   
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "<-- %!FUNC! completed with status=%!STATUS!\n", status);

    return status;
}

static
_Use_decl_annotations_
VOID
McbaUsbRequestsUninit(
    _Inout_ PMCBA_DEVICE_USB_REQUEST_DATA UsbRequestData
)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "--> %!FUNC! UsbRequestData=0x%p\n", UsbRequestData);

    NT_ASSERT(UsbRequestData);
    
    if (UsbRequestData->Count) {
        NT_ASSERT(UsbRequestData->Count <= _countof(UsbRequestData->Requests));
        NT_ASSERT(UsbRequestData->Count <= _countof(UsbRequestData->Memory));

        for (MCBA_USB_REQUEST_INDEX_TYPE j = 0; j < UsbRequestData->Count; ++j) {
            NT_ASSERT(UsbRequestData->Requests[j]);
            WdfObjectDelete(UsbRequestData->Requests[j]);
            __analysis_assume(!UsbRequestData->Requests[j]);

            NT_ASSERT(UsbRequestData->Memory[j]);
            WdfObjectDelete(UsbRequestData->Memory[j]);
            __analysis_assume(!UsbRequestData->Memory[j]);
        }

        UsbRequestData->Count = 0;
        UsbRequestData->FreeIndexEnd = 0;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "<-- %!FUNC!\n");
}


_Use_decl_annotations_
NTSTATUS
McbaUsbRequestsAlloc(
    PMCBA_DEVICE_USB_REQUEST_DATA UsbRequestData,
    MCBA_USB_REQUEST_INDEX_TYPE Count,
    MCBA_USB_REQUEST_INDEX_TYPE* Indices
)
{
    KIRQL irql;
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "--> %!FUNC! UsbRequestData=0x%p Count=%u Indices=0x%p\n", UsbRequestData, (unsigned)Count, Indices);

    NT_ASSERT(UsbRequestData);
    NT_ASSERT(Count);
    NT_ASSERT(Indices);
    

    if (Count > UsbRequestData->Count) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    
    for (MCBA_USB_REQUEST_INDEX_TYPE i = 0; i < Count;  ) {
        status = KeWaitForSingleObject(&UsbRequestData->Availabe, Executive, KernelMode, FALSE, NULL);
        if (NT_SUCCESS(status)) {
            ++i;
        }
    }
    
    KeAcquireSpinLock(&UsbRequestData->Lock, &irql);
    NT_ASSERT(McbaCheckUsbRequestIndices(UsbRequestData));
    NT_ASSERT(Count <= UsbRequestData->FreeIndexEnd);
    UsbRequestData->FreeIndexEnd -= Count;
    RtlCopyMemory(Indices, &UsbRequestData->FreeIndices[UsbRequestData->FreeIndexEnd], sizeof(*Indices) * Count);
    NT_ASSERT(McbaCheckUsbRequestIndices(UsbRequestData));

    KeReleaseSpinLock(&UsbRequestData->Lock, irql);
Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "--> %!FUNC! completed with status=%!STATUS!\n", status);
    return status;
}

_Use_decl_annotations_
VOID
McbaUsbRequestsFree(
    PMCBA_DEVICE_USB_REQUEST_DATA UsbRequestData,
    MCBA_USB_REQUEST_INDEX_TYPE Count,
    const MCBA_USB_REQUEST_INDEX_TYPE* Indices
)
{
    KIRQL irql;
    LONG available;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "--> %!FUNC! UsbRequestData=0x%p Count=%u Indices=0x%p\n", UsbRequestData, (unsigned)Count, Indices);

    NT_ASSERT(UsbRequestData);
    NT_ASSERT(Count);
    NT_ASSERT(Indices);


    KeAcquireSpinLock(&UsbRequestData->Lock, &irql);

    NT_ASSERT(McbaCheckUsbRequestIndices(UsbRequestData));
    NT_ASSERT(UsbRequestData->FreeIndexEnd + Count <= _countof(UsbRequestData->FreeIndices));

    RtlCopyMemory(&UsbRequestData->FreeIndices[UsbRequestData->FreeIndexEnd], Indices, sizeof(*Indices) * Count);
    UsbRequestData->FreeIndexEnd += Count;
    NT_ASSERT(McbaCheckUsbRequestIndices(UsbRequestData));
    
    KeReleaseSpinLock(&UsbRequestData->Lock, irql);

    available = KeReleaseSemaphore(&UsbRequestData->Availabe, 0, Count, FALSE);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "<-- %!FUNC! available %ld\n", available + Count);
}


_Use_decl_annotations_
VOID
McbaUsbRequestsReuse(
    PMCBA_DEVICE_USB_REQUEST_DATA UsbRequestData,
    MCBA_USB_REQUEST_INDEX_TYPE Count,
    const MCBA_USB_REQUEST_INDEX_TYPE* Indices
)
{
    WDF_REQUEST_REUSE_PARAMS reuseParams;

    UNREFERENCED_PARAMETER(Indices);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "--> %!FUNC! UsbRequestData=0x%p Count=%u Indices=0x%p\n", UsbRequestData, (unsigned)Count, Indices);
    
    NT_ASSERT(UsbRequestData);
    NT_ASSERT(Count);
    NT_ASSERT(Indices);
    NT_ASSERT(Count <= _countof(UsbRequestData->Requests));

    WDF_REQUEST_REUSE_PARAMS_INIT(&reuseParams, WDF_REQUEST_REUSE_NO_FLAGS, STATUS_SUCCESS);
    for (MCBA_USB_REQUEST_INDEX_TYPE i = 0; i < Count; ++i) {
        MCBA_USB_REQUEST_INDEX_TYPE index = Indices[i];        
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "--> %!FUNC! reuse Request=0x%p index=%u\n", UsbRequestData->Requests[index], index);
        NTSTATUS status = WdfRequestReuse(UsbRequestData->Requests[index], &reuseParams);
        NT_ASSERT(NT_SUCCESS(status));
        (void)status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "<-- %!FUNC!\n");
}


_Use_decl_annotations_
NTSTATUS
McbaUsbBulkWritePipeSend(
    PMCBA_DEVICE_CONTEXT DeviceContext,
    MCBA_USB_REQUEST_INDEX_TYPE Index
)
{
    NTSTATUS status;
    WDFREQUEST request;
    WDFMEMORY memory;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "--> %!FUNC! DeviceContext=0x%p Index=%u\n", DeviceContext, (unsigned)Index);

    NT_ASSERT(DeviceContext);
    NT_ASSERT(DeviceContext->BulkWritePipe);
    NT_ASSERT(Index < MCBA_MAX_WRITES);
    NT_ASSERT(Index < DeviceContext->UsbRequests.Count);

    request = DeviceContext->UsbRequests.Requests[Index];
    memory = DeviceContext->UsbRequests.Memory[Index];

    status = WdfUsbTargetPipeFormatRequestForWrite(DeviceContext->BulkWritePipe, request, memory, NULL);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfUsbTargetPipeFormatRequestForWrite failed with status=%!STATUS!\n", status);
        goto Exit;
    }

    if (WdfRequestSend(request, WdfUsbTargetPipeGetIoTarget(DeviceContext->BulkWritePipe), WDF_NO_SEND_OPTIONS) == FALSE) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfRequestSend failed\n");
        status = WdfRequestGetStatus(request);
        NT_ASSERT(!NT_SUCCESS(status));
        goto Exit;
    }
Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "<-- %!FUNC! completed with status=%!STATUS!\n", status);
    return status;
}


_Use_decl_annotations_
VOID
McbaCancelPendingReadRequest(
    WDFREQUEST Request
)
{
    KIRQL irql;
    PMCBA_FILE_CONTEXT pFileContext;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "--> %!FUNC! Request=%p\n", Request);

    pFileContext = McbaFileGetContext(WdfRequestGetFileObject(Request));

    KeAcquireSpinLock(&pFileContext->ReadLock, &irql);

    NT_ASSERT(Request == pFileContext->PendingReadRequest);

    McbaClearPendingReadRequest(pFileContext);

    KeReleaseSpinLock(&pFileContext->ReadLock, irql);

    WdfRequestComplete(Request, STATUS_CANCELLED);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "<-- %!FUNC!\n");
}