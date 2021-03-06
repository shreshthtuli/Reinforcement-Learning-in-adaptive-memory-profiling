#include "AlleriaDriver.h"

//
// The trace message header file must be included in a source file
// before any WPP macro calls and after defining a WPP_CONTROL_GUIDS
// macro. During the compilation, WPP scans the source files for
// TraceEvents() calls and builds a .tmh file which stores a unique
// data GUID for each message, the text resource string for each message,
// and the data types of the variables passed in for each message.
// This file is automatically generated and used during post-processing.
//
//#include "AlleriaDriver.tmh"


#ifdef ALLOC_PRAGMA
#pragma alloc_text( INIT, DriverEntry )
#pragma alloc_text( PAGE, AlleriaDriverDeviceAdd)
#pragma alloc_text( PAGE, AlleriaDriverEvtDriverContextCleanup)
#pragma alloc_text( PAGE, AlleriaDriverEvtDriverUnload)
#pragma alloc_text( PAGE, AlleriaDriverEvtDeviceIoInCallerContext)
#pragma alloc_text( PAGE, FileEvtIoDeviceControl)
#endif // ALLOC_PRAGMA


NTSTATUS
DriverEntry(
IN OUT PDRIVER_OBJECT   DriverObject,
IN PUNICODE_STRING      RegistryPath
)
/*++

Routine Description:
This routine is called by the Operating System to initialize the driver.

It creates the device object, fills in the dispatch entry points and
completes the initialization.

Arguments:
DriverObject - a pointer to the object that represents this device
driver.

RegistryPath - a pointer to our Services key in the registry.

Return Value:
STATUS_SUCCESS if initialized; an error otherwise.

--*/
{
	NTSTATUS                       status;
	WDF_DRIVER_CONFIG              config;
	WDFDRIVER                      hDriver;
	PWDFDEVICE_INIT                pInit = NULL;
	WDF_OBJECT_ATTRIBUTES          attributes;


	WDF_DRIVER_CONFIG_INIT(
		&config,
		WDF_NO_EVENT_CALLBACK // This is a non-pnp driver.
		);

	//
	// Tell the framework that this is non-pnp driver so that it doesn't
	// set the default AddDevice routine.
	//
	config.DriverInitFlags |= WdfDriverInitNonPnpDriver;

	//
	// AlleriaDriver driver must explicitly register an unload routine for
	// the driver to be unloaded.
	//
	config.EvtDriverUnload = AlleriaDriverEvtDriverUnload;

	//
	// Register a cleanup callback so that we can call WPP_CLEANUP when
	// the framework driver object is deleted during driver unload.
	//
	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.EvtCleanupCallback = AlleriaDriverEvtDriverContextCleanup;

	//
	// Create a framework driver object to represent our driver.
	//
	status = WdfDriverCreate(DriverObject,
		RegistryPath,
		&attributes,
		&config,
		&hDriver);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	//
	// Since we are calling WPP_CLEANUP in the DriverContextCleanup
	// callback we should initialize WPP Tracing after WDFDRIVER
	// object is created to ensure that we cleanup WPP properly
	// if we return failure status from DriverEntry. This
	// eliminates the need to call WPP_CLEANUP in every path
	// of DriverEntry.
	//
	//WPP_INIT_TRACING(DriverObject, RegistryPath);

	//
	// On Win2K system,  you will experience some delay in getting trace events
	// due to the way the ETW is activated to accept trace messages.
	//

	//
	//
	// In order to create a control device, we first need to allocate a
	// WDFDEVICE_INIT structure and set all properties.
	//
	pInit = WdfControlDeviceInitAllocate(
		hDriver,
		&SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R
		);

	if (pInit == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		return status;
	}

	//
	// Call AlleriaDriverDeviceAdd to create a deviceobject to represent our
	// software device.
	//
	status = AlleriaDriverDeviceAdd(hDriver, pInit);

	return status;
}

NTSTATUS
AlleriaDriverDeviceAdd(
IN WDFDRIVER Driver,
IN PWDFDEVICE_INIT DeviceInit
)
/*++

Routine Description:

Called by the DriverEntry to create a control-device. This call is
responsible for freeing the memory for DeviceInit.

Arguments:

DriverObject - a pointer to the object that represents this device
driver.

DeviceInit - Pointer to a driver-allocated WDFDEVICE_INIT structure.

Return Value:

STATUS_SUCCESS if initialized; an error otherwise.

--*/
{
	NTSTATUS                       status;
	WDF_OBJECT_ATTRIBUTES           attributes;
	WDF_IO_QUEUE_CONFIG      ioQueueConfig;
	//WDF_FILEOBJECT_CONFIG fileConfig;
	WDFQUEUE                            queue;
	WDFDEVICE   controlDevice;
	DECLARE_CONST_UNICODE_STRING(ntDeviceName, NTDEVICE_NAME_STRING);
	DECLARE_CONST_UNICODE_STRING(symbolicLinkName, SYMBOLIC_NAME_STRING);

	UNREFERENCED_PARAMETER(Driver);

	PAGED_CODE();

	WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoNeither);
	WdfDeviceInitSetDeviceType(DeviceInit, ALLERIA_DEVICE_TYPE);
	WdfDeviceInitSetExclusive(DeviceInit, FALSE);

	status = WdfDeviceInitAssignName(DeviceInit, &ntDeviceName);

	if (!NT_SUCCESS(status)) {
		goto End;
	}

	WdfControlDeviceInitSetShutdownNotification(DeviceInit,
		AlleriaDriverShutdown,
		WdfDeviceShutdown);

	//
	// In order to support METHOD_NEITHER Device controls, or
	// NEITHER device I/O type, we need to register for the
	// EvtDeviceIoInProcessContext callback so that we can handle the request
	// in the calling threads context.
	//
	WdfDeviceInitSetIoInCallerContextCallback(DeviceInit,
		AlleriaDriverEvtDeviceIoInCallerContext);

	//
	// Specify the size of device context
	//
	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

	status = WdfDeviceCreate(&DeviceInit,
		&attributes,
		&controlDevice);
	if (!NT_SUCCESS(status)) {
		goto End;
	}

	//
	// Create a symbolic link for the control object so that usermode can open
	// the device.
	//


	status = WdfDeviceCreateSymbolicLink(controlDevice,
		&symbolicLinkName);

	if (!NT_SUCCESS(status)) {
		//
		// Control device will be deleted automatically by the framework.
		//
		goto End;
	}

	//
	// Configure a default queue so that requests that are not
	// configure-fowarded using WdfDeviceConfigureRequestDispatching to goto
	// other queues get dispatched here.
	//
	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig,
		WdfIoQueueDispatchSequential);

	ioQueueConfig.EvtIoDeviceControl = FileEvtIoDeviceControl;

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	//
	// Since we are using Zw function set execution level to passive so that
	// framework ensures that our Io callbacks called at only passive-level
	// even if the request came in at DISPATCH_LEVEL from another driver.
	//
	//attributes.ExecutionLevel = WdfExecutionLevelPassive;

	//
	// By default, Static Driver Verifier (SDV) displays a warning if it 
	// doesn't find the EvtIoStop callback on a power-managed queue. 
	// The 'assume' below causes SDV to suppress this warning. If the driver 
	// has not explicitly set PowerManaged to WdfFalse, the framework creates
	// power-managed queues when the device is not a filter driver.  Normally 
	// the EvtIoStop is required for power-managed queues, but for this driver
	// it is not needed b/c the driver doesn't hold on to the requests or 
	// forward them to other drivers. This driver completes the requests 
	// directly in the queue's handlers. If the EvtIoStop callback is not 
	// implemented, the framework waits for all driver-owned requests to be
	// done before moving in the Dx/sleep states or before removing the 
	// device, which is the correct behavior for this type of driver.
	// If the requests were taking an indeterminate amount of time to complete,
	// or if the driver forwarded the requests to a lower driver/another stack,
	// the queue should have an EvtIoStop/EvtIoResume.
	//
	__analysis_assume(ioQueueConfig.EvtIoStop != 0);
	status = WdfIoQueueCreate(controlDevice,
		&ioQueueConfig,
		&attributes,
		&queue // pointer to default queue
		);
	__analysis_assume(ioQueueConfig.EvtIoStop == 0);
	if (!NT_SUCCESS(status)) {
		goto End;
	}

	//
	// Control devices must notify WDF when they are done initializing.   I/O is
	// rejected until this call is made.
	//
	WdfControlFinishInitializing(controlDevice);

End:
	//
	// If the device is created successfully, framework would clear the
	// DeviceInit value. Otherwise device create must have failed so we
	// should free the memory ourself.
	//
	if (DeviceInit != NULL) {
		WdfDeviceInitFree(DeviceInit);
	}

	return status;

}

VOID
AlleriaDriverEvtDriverContextCleanup(
IN WDFOBJECT Driver
)
/*++
Routine Description:

Called when the driver object is deleted during driver unload.
You can free all the resources created in DriverEntry that are
not automatically freed by the framework.

Arguments:

Driver - Handle to a framework driver object created in DriverEntry

Return Value:

NTSTATUS

--*/
{
	UNREFERENCED_PARAMETER(Driver);
	PAGED_CODE();

	//
	// No need to free the controldevice object explicitly because it will
	// be deleted when the Driver object is deleted due to the default parent
	// child relationship between Driver and ControlDevice.
	//
	//WPP_CLEANUP(WdfDriverWdmGetDriverObject((WDFDRIVER)Driver));

}

VOID
FileEvtIoDeviceControl(
IN WDFQUEUE         Queue,
IN WDFREQUEST       Request,
IN size_t           OutputBufferLength,
IN size_t           InputBufferLength,
IN ULONG            IoControlCode
)
/*++
Routine Description:

This event is called when the framework receives IRP_MJ_DEVICE_CONTROL
requests from the system.

Arguments:

Queue - Handle to the framework queue object that is associated
with the I/O request.
Request - Handle to a framework request object.

OutputBufferLength - length of the request's output buffer,
if an output buffer is available.
InputBufferLength - length of the request's input buffer,
if an input buffer is available.

IoControlCode - the driver-defined or system-defined I/O control code
(IOCTL) that is associated with the request.

Return Value:

VOID

--*/
{
	NTSTATUS            status = STATUS_SUCCESS;// Assume success

	UNREFERENCED_PARAMETER(Queue);
	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);
	UNREFERENCED_PARAMETER(IoControlCode);

	PAGED_CODE();

	WdfRequestComplete(Request, status);

}

VOID
AlleriaDriverEvtDeviceIoInCallerContext(
IN WDFDEVICE  Device,
IN WDFREQUEST Request
)
/*++
Routine Description:

This I/O in-process callback is called in the calling threads context/address
space before the request is subjected to any framework locking or queueing
scheme based on the device pnp/power or locking attributes set by the
driver. The process context of the calling app is guaranteed as long as
this driver is a top-level driver and no other filter driver is attached
to it.

This callback is only required if you are handling method-neither IOCTLs,
or want to process requests in the context of the calling process.

Driver developers should avoid defining neither IOCTLs and access user
buffers, and use much safer I/O tranfer methods such as buffered I/O
or direct I/O.

Arguments:

Device - Handle to a framework device object.

Request - Handle to a framework request object. Framework calls
PreProcess callback only for Read/Write/ioctls and internal
ioctl requests.

Return Value:

VOID

--*/
{
	NTSTATUS                   status = STATUS_SUCCESS;
	PREQUEST_CONTEXT           reqContext = NULL;
	WDF_OBJECT_ATTRIBUTES      attributes;
	WDF_REQUEST_PARAMETERS     params;
	size_t                     inBufLen, outBufLen;
	PVOID               *inBuf = NULL; // pointer to input buffer
	PULONGLONG          outBuf = NULL; // pointer to output buffer

	UNREFERENCED_PARAMETER(Device);
	PAGED_CODE();

	WDF_REQUEST_PARAMETERS_INIT(&params);

	WdfRequestGetParameters(Request, &params);

	//
	// Check to see whether we have recevied a METHOD_NEITHER IOCTL. if not
	// just send the request back to framework because we aren't doing
	// any pre-processing in the context of the calling thread process.
	//
	if (!(params.Type == WdfRequestTypeDeviceControl &&
		params.Parameters.DeviceIoControl.IoControlCode ==
		IOCTL_ALLERIA_V2P)) {
		//
		// Forward it for processing by the I/O package
		//
		status = WdfDeviceEnqueueRequest(Device, Request);
		if (!NT_SUCCESS(status)) {
			goto End;
		}

		return;
	}

	//
	// In this type of transfer, the I/O manager assigns the user input
	// to Type3InputBuffer and the output buffer to UserBuffer of the Irp.
	// The I/O manager doesn't copy or map the buffers to the kernel
	// buffers.
	//
	status = WdfRequestRetrieveUnsafeUserInputBuffer(Request, 0, &(PVOID)(inBuf), &inBufLen);
	if (!NT_SUCCESS(status)) {
		goto End;
	}

	status = WdfRequestRetrieveUnsafeUserOutputBuffer(Request, 0, &(PVOID)outBuf, &outBufLen);
	if (!NT_SUCCESS(status)) {
		goto End;
	}

	//
	// Allocate a context for this request so that we can store the memory
	// objects created for input and output buffer.
	//
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, REQUEST_CONTEXT);

	status = WdfObjectAllocateContext(Request, &attributes, &reqContext);
	if (!NT_SUCCESS(status)) {
		goto End;
	}

	//
	// WdfRequestProbleAndLockForRead/Write function checks to see
	// whether the caller in the right thread context, creates an MDL,
	// probe and locks the pages, and map the MDL to system address
	// space and finally creates a WDFMEMORY object representing this
	// system buffer address. This memory object is associated with the
	// request. So it will be freed when the request is completed. If we
	// are accessing this memory buffer else where, we should store these
	// pointers in the request context.
	//

#pragma prefast(suppress:6387, "If inBuf==NULL at this point, then inBufLen==0")    
	status = WdfRequestProbeAndLockUserBufferForRead(Request,
		inBuf,
		inBufLen,
		&reqContext->InputMemoryBuffer);

	if (!NT_SUCCESS(status)) {
		goto End;
	}

#pragma prefast(suppress:6387, "If outBuf==NULL at this point, then outBufLen==0") 
	status = WdfRequestProbeAndLockUserBufferForWrite(Request,
		outBuf,
		outBufLen,
		&reqContext->OutputMemoryBuffer);
	if (!NT_SUCCESS(status)) {
		goto End;
	}

	size_t inBufLength, outBufLength;

	//
	// The AlleriaDriverEvtDeviceIoInCallerContext has already probe and locked the
	// pages and mapped the user buffer into system address space and
	// stored memory buffer pointers in the request context. We can get the
	// buffer pointer by calling WdfMemoryGetBuffer.
	//

	inBuf = WdfMemoryGetBuffer(reqContext->InputMemoryBuffer, &inBufLength);
	outBuf = WdfMemoryGetBuffer(reqContext->OutputMemoryBuffer, &outBufLength);

	inBufLength = inBufLength / sizeof(void*);
	outBufLength = outBufLength / sizeof(ULONGLONG);

	if (inBuf == NULL || outBuf == NULL || inBufLength != outBufLength){
		status = STATUS_INVALID_PARAMETER;
		goto End;
	} 

	//
	// Now you can safely read and write data in any arbitrary
	// context.
	//

	size_t outSize = 0;
	for (size_t i = 0; i < inBufLength; ++i)
	{
		if (MmIsAddressValid(inBuf[i]))
		{
			outBuf[i] = MmGetPhysicalAddress(inBuf[i]).QuadPart;
			++outSize;
		}
		else
		{
			outBuf[i] = (ULONGLONG)-1;
		}
	}

	//
	// Assign the length of the data copied to IoStatus.Information
	// of the Irp and complete the Irp.
	//

	WdfRequestSetInformation(Request, outSize);

End:

	WdfRequestComplete(Request, status);
	return;
}

VOID
AlleriaDriverShutdown(
WDFDEVICE Device
)
/*++

Routine Description:
Callback invoked when the machine is shutting down.  If you register for
a last chance shutdown notification you cannot do the following:
o Call any pageable routines
o Access pageable memory
o Perform any file I/O operations

If you register for a normal shutdown notification, all of these are
available to you.

This function implementation does nothing, but if you had any outstanding
file handles open, this is where you would close them.

Arguments:
Device - The device which registered the notification during init

Return Value:
None

--*/

{
	UNREFERENCED_PARAMETER(Device);
	return;
}


VOID
AlleriaDriverEvtDriverUnload(
IN WDFDRIVER Driver
)
/*++
Routine Description:

Called by the I/O subsystem just before unloading the driver.
You can free the resources created in the DriverEntry either
in this routine or in the EvtDriverContextCleanup callback.

Arguments:

Driver - Handle to a framework driver object created in DriverEntry

Return Value:

NTSTATUS

--*/
{
	UNREFERENCED_PARAMETER(Driver);

	PAGED_CODE();

	return;
}