#include "common.h"
#include "fsdDriver.h"
#include "isoio.h"
#include "irpCompare.h"

typedef struct _DIRECTORY_ENUM_CONTEXT{
	WORD curIndex;
	WORD maxsize;
	WORD curDepth;
	char* baseName;
}DIRECTORY_ENUM_CONTEXT, *PDIRECTORY_ENUM_CONTEXT;

VOID FsdDriverStartIo(void* pDeviceObject, void* pirp);
NTSTATUS FsdIrpCreate(void* pDeviceObject, void* pirp);
NTSTATUS FsdIrpClose(void* pDeviceObject, void* pirp);
NTSTATUS FsdIrpRead(void* pDeviceObject, void* pirp);
NTSTATUS FsdIrpWrite(void* pDeviceObject, void* pirp);
NTSTATUS FsdIrpQueryInformation(void* pDeviceObject, void* pirp);
NTSTATUS FsdIrpSetInformation(void* pDeviceObject, void* pirp);
NTSTATUS FsdIrpFlushBuffers(void* pDeviceObject, void* pirp);
NTSTATUS FsdIrpQueryVolumeInformation(void* pDeviceObject, void* pirp);
NTSTATUS FsdIrpDirectoryControl(void* pDeviceObject, void* pirp);
NTSTATUS FsdIrpDeviceControl(void* pDeviceObject, void* pirp);

const char NetworkRealSmbString[] = "\\Device\\NetIso";

OBJECT_STRING g_NetworkRealString = CMAKE_STRING(NetworkRealSmbString);

DRIVER_OBJECT g_FsdDriverObject = {
	FsdDriverStartIo, // DriverStartIo
	NULL, // DriverDeleteDevice
	NULL, // DriverDismountVolume
	FsdIrpCreate,
	FsdIrpClose,
	FsdIrpRead,
	FsdIrpWrite,
	FsdIrpQueryInformation,
	FsdIrpSetInformation,
	FsdIrpFlushBuffers,
	FsdIrpQueryVolumeInformation,
	FsdIrpDirectoryControl,
	FsdIrpDeviceControl, // DobF.DObDeviceControl
	(DRIVERLONG)IoInvalidDeviceRequest, // DobF.DObCleanup
};

long FsdStartup(void)
{
	long ret;
	DEVICE_OBJECT* dob;
	// "\\Network\\RealIso"
	ret = IoCreateDevice(&g_FsdDriverObject, 0, &g_NetworkRealString, FILE_DEVICE_NETWORK_FILE_SYSTEM, 0, &dob);
	if(NT_SUCCESS(ret))
	{
		dob->Flags |= DO_DIRECT_IO;
		dob->AlignmentRequirement = 1; // cdrom requires alignment, connectx does not
		dob->SectorSize = 0x800; // sector size 0x800 is the same between both
		dob->Flags = dob->Flags&~DO_DEVICE_INITIALIZING; // mark the device as present
	}
	sdbgPrint("FsdStartup returns %08x\n", ret);
	return ret;
}

NTSTATUS FsdCompleteRequest(PIRP pirp, NTSTATUS status)
{
	pirp->IoStatus.st.Status = status;
	IoCompleteRequest(pirp, IO_NO_INCREMENT);
	return status;
}

VOID FsdDriverStartIo(void* pDeviceObject, void* pirp)
{
	NTSTATUS ret = STATUS_INVALID_DEVICE_REQUEST;
	PIRP pvirp = (PIRP)pirp;
// 	sdbgPrint("\nFsdDriverStartIo called ob: %08x irp: %08x\n", pDeviceObject, pirp);
	FsdCompleteRequest(pvirp, ret);
}

NTSTATUS FsdCreateDirectoryEnumContext(PFILE_OBJECT fobj, STRING* fname, PDIRECTORY_ENUM_CONTEXT* ctx)
{
	PDIRECTORY_ENUM_CONTEXT cenum;
	WORD strSz = 0;
	if(fname != NULL)
	{
		if((fname->Length != 0)&&(fname->Buffer[0] != '*'))
		{
			strSz = fname->Length+1;
		}
	}
	if(ObIsTitleObject(fobj) == FALSE)
		cenum = (PDIRECTORY_ENUM_CONTEXT)ExAllocatePoolTypeWithTag(sizeof(DIRECTORY_ENUM_CONTEXT)+strSz, 0x65446F49, PoolTypeSystem); // 'eDoI'
	else
		cenum = (PDIRECTORY_ENUM_CONTEXT)ExAllocatePoolTypeWithTag(sizeof(DIRECTORY_ENUM_CONTEXT)+strSz, 0x65446F49, PoolTypeTitle); // 'eDoI'
	if(cenum == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;
	memset(cenum, 0, sizeof(DIRECTORY_ENUM_CONTEXT)+strSz);
	cenum->maxsize = strSz;
	if(strSz != 0)
	{
		int i;
		cenum->baseName = &((char*)cenum)[sizeof(DIRECTORY_ENUM_CONTEXT)];
		// copy the string but without a trailing *
		for(i = 0; i < fname->Length; i++)
		{
			if(fname->Buffer[i] == '\\')
				cenum->curDepth++;
			if((fname->Buffer[i] != '*')&&(fname->Buffer[i] != '?'))
				cenum->baseName[i] = fname->Buffer[i];
			else
				i = fname->Length;
		}
	}
// 	sdbgPrint("enum context created depth %d '%s' from '%s'\n", cenum->curDepth, cenum->baseName, fname->Buffer);
	*ctx = cenum;
	return STATUS_SUCCESS;
}

NTSTATUS FsdIrpCreate(void* pDeviceObject, void* pirp)
{
	NTSTATUS ret = STATUS_ACCESS_DENIED;
	PIRP pvirp = (PIRP)pirp;
	PIO_STACK_LOCATION pios = pvirp->Tail.Overlay.CurrentStackLocation;
	PFILE_OBJECT FileObject = pios->FileObject;
	PSTRING fname = pios->Parameters.Create.RemainingName;

// 	sdbgPrint("\nFsdIrpCreate called ob: %08x irp: %08x\n", pDeviceObject, pirp);
// 	irpShow(pirp);
	if(((pios->Parameters.Create.Options >> 24) & 0xff) == FILE_OPEN) // CreateDisposition we are read only after all...
	{
		FileObject->FsContext2 = NULL;
		if((pios->Flags&SL_OPEN_TARGET_DIRECTORY) == 0)
		{
			FileObject->FsContext = NULL;
			if(fname != NULL)
			{
				// fileName: \ << disk
				// fileName: \*.* or fileName: \* << directory
				// fileName: \test1_iso\*.* << subdirectory
// 				if(fname->Length != 0)
// 					sdbgPrint("fileName: %s\n", pios->Parameters.Create.RemainingName->Buffer);// initial call is "\\*"
				ret = FsdCreateDirectoryEnumContext(FileObject, pios->Parameters.Create.RemainingName, (PDIRECTORY_ENUM_CONTEXT *)&FileObject->FsContext2);
				if(!NT_SUCCESS(ret))
					return FsdCompleteRequest(pvirp, ret);

				//ret = STATUS_SUCCESS;
				//return FsdCompleteRequest(pvirp, STATUS_INVALID_PARAMETER);
			}
			ret = STATUS_SUCCESS;
		}
	}

	return FsdCompleteRequest(pvirp, ret);
}

NTSTATUS FsdIrpClose(void* pDeviceObject, void* pirp)
{
	NTSTATUS ret = STATUS_SUCCESS;
	PIRP pvirp = (PIRP)pirp;
	PIO_STACK_LOCATION pios = pvirp->Tail.Overlay.CurrentStackLocation;
	PFILE_OBJECT FileObject = pios->FileObject;
// 	sdbgPrint("\nFsdIrpClose called ob: %08x irp: %08x\n", pDeviceObject, pirp);
	if(FileObject->FsContext2 != NULL)
		ExFreePool(FileObject->FsContext2);
// 	irpShow(pirp);
	return FsdCompleteRequest(pvirp, ret);
}

NTSTATUS FsdIrpRead(void* pDeviceObject, void* pirp)
{
	NTSTATUS ret = STATUS_INVALID_DEVICE_REQUEST;
	PIRP pvirp = (PIRP)pirp;
// 	sdbgPrint("\nFsdIrpRead called ob: %08x irp: %08x\n", pDeviceObject, pirp);
	return FsdCompleteRequest(pvirp, ret);
}

NTSTATUS FsdIrpWrite(void* pDeviceObject, void* pirp)
{
	NTSTATUS ret = STATUS_INVALID_DEVICE_REQUEST;
	PIRP pvirp = (PIRP)pirp;
// 	sdbgPrint("\nFsdIrpWrite called ob: %08x irp: %08x\n", pDeviceObject, pirp);
	return FsdCompleteRequest(pvirp, ret);
}

NTSTATUS FsdIrpQueryInformation(void* pDeviceObject, void* pirp)
{
	NTSTATUS ret = STATUS_SUCCESS;
	PIRP pvirp = (PIRP)pirp;
	PIO_STACK_LOCATION pios = pvirp->Tail.Overlay.CurrentStackLocation;
	PFILE_OBJECT FileObject = pios->FileObject;
// 	sdbgPrint("\nFsdIrpQueryInformation called ob: %08x irp: %08x\n", pDeviceObject, pirp);
	//irpShow(pirp);
	if((FileObject->Flags & 0x10) == 0)
	{
		DWORD inf = 0;
		memset(pvirp->UserBuffer, 0, pios->Parameters.QueryFile.Length);
		if(pios->Parameters.QueryFile.FileInformationClass == FileNetworkOpenInformation)
		{
			//LARGE_INTEGER ptime;
			//PFILE_NETWORK_OPEN_INFORMATION fnet = (PFILE_NETWORK_OPEN_INFORMATION)pvirp->UserBuffer;
			//KeQuerySystemTime((PFILETIME)&ptime);
			// disk queries have all 0's
			inf = sizeof(FILE_NETWORK_OPEN_INFORMATION);
		}
		else
			ret = STATUS_INVALID_PARAMETER;
		pvirp->IoStatus.Information = inf;
	}
	else
		ret = STATUS_FILE_CLOSED;
	return FsdCompleteRequest(pvirp, ret);
}

NTSTATUS FsdIrpSetInformation(void* pDeviceObject, void* pirp)
{
	NTSTATUS ret = STATUS_INVALID_DEVICE_REQUEST;
	PIRP pvirp = (PIRP)pirp;
// 	sdbgPrint("\nFsdIrpSetInformation called ob: %08x irp: %08x\n", pDeviceObject, pirp);
	return FsdCompleteRequest(pvirp, ret);
}

NTSTATUS FsdIrpFlushBuffers(void* pDeviceObject, void* pirp)
{
	NTSTATUS ret = STATUS_INVALID_DEVICE_REQUEST;
	PIRP pvirp = (PIRP)pirp;
// 	sdbgPrint("\nFsdIrpFlushBuffers called ob: %08x irp: %08x\n", pDeviceObject, pirp);
	return FsdCompleteRequest(pvirp, ret);
}

NTSTATUS FsdIrpQueryVolumeInformation(void* pDeviceObject, void* pirp)
{
	NTSTATUS ret = STATUS_INVALID_DEVICE_REQUEST;
	PIRP pvirp = (PIRP)pirp;
// 	sdbgPrint("\nFsdIrpQueryVolumeInformation called ob: %08x irp: %08x\n", pDeviceObject, pirp);
	return FsdCompleteRequest(pvirp, ret);
}

NTSTATUS FsdEnumFireMount(PDIRECTORY_ENUM_CONTEXT ctx)
{
	IRP tirp;
	memset(&tirp, 0, sizeof(IRP));
	tirp.Type = IRP_FAKE_MOUNT_ISO;
	tirp.UserBuffer = ctx->baseName;
	tirp.Flags = ctx->maxsize;
	isoIoQueueOther(&tirp);
	return STATUS_SUCCESS;
}

NTSTATUS FsdEnumGetNextIndexItem(PDIRECTORY_ENUM_CONTEXT ctx, char* dest, int maxlen, DWORD* nameLenOut)
{
	NTSTATUS ret = STATUS_SUCCESS;
	if(ctx->curDepth == 1)
	{
		if((ctx->curIndex == 0)&&(isoIoHasDisk()))
		{
			char fakeUnItem[] = "[Disable Current ISO]";
			if(maxlen > (int)strlen(fakeUnItem))
			{
				strcpy(dest, fakeUnItem);
				*nameLenOut = strlen(fakeUnItem);
			}
			else
				ret = STATUS_BUFFER_OVERFLOW;
			ctx->curIndex++;
		}
		else
		{
			IRP tirp;
			memset(&tirp, 0, sizeof(IRP));
			tirp.Type = IRP_FAKE_GET_INDEX;
			tirp.UserBuffer = dest;
			tirp.Flags = maxlen;
			if(isoIoHasDisk())
				tirp.Size = ctx->curIndex-1;
			else
				tirp.Size = ctx->curIndex;
			isoIoQueueOther(&tirp);
			if((tirp.IoStatus.st.Status != 1)||(dest[0] == 0))
				return STATUS_NO_MORE_FILES;
			ctx->curIndex++;
			*nameLenOut = strlen(dest);
		}
	}
	else if(ctx->curDepth == 2) // fake mount item
	{
		if(ctx->curIndex < 1)
		{
			char fakeMountItem[] = "Mount";
			if(maxlen > (int)strlen(fakeMountItem))
			{
				strcpy(dest, fakeMountItem);
				*nameLenOut = strlen(fakeMountItem);
			}
			else
				ret = STATUS_BUFFER_OVERFLOW;
			ctx->curIndex++;
		}
		else
			ret = STATUS_NO_MORE_FILES;
	}
	else // otherwise its empty and should fire a mount event
	{
// 		sdbgPrint("*** FIRE MOUNT '%s' ***\n", ctx->baseName);
		FsdEnumFireMount(ctx);
//		XamGetExecutingXamApp
		XamAppUnloadSelf(); // closes the HUD
		ret = STATUS_NO_MORE_FILES;
	}

	return ret;
}

// https://msdn.microsoft.com/en-us/library/windows/hardware/ff548658%28v=vs.85%29.aspx	IRP_MJ_DIRECTORY_CONTROL
NTSTATUS FsdIrpDirectoryControl(void* pDeviceObject, void* pirp)
{
	NTSTATUS ret = STATUS_INVALID_DEVICE_REQUEST;
	PIRP pvirp = (PIRP)pirp;
	PIO_STACK_LOCATION pios = pvirp->Tail.Overlay.CurrentStackLocation;
	PFILE_OBJECT FileObject = pios->FileObject;
	PFILE_DIRECTORY_INFORMATION dirinf = (PFILE_DIRECTORY_INFORMATION)pvirp->UserBuffer;
	LARGE_INTEGER ptime;
	DWORD maxFnameLen;
	PDIRECTORY_ENUM_CONTEXT cenum;
// 	sdbgPrint("\nFsdIrpDirectoryControl called ob: %08x irp: %08x\n", pDeviceObject, pirp);
	//irpShow(pirp);
	if(FileObject->Flags & 0x10)
	{
		return FsdCompleteRequest(pvirp, STATUS_FILE_CLOSED);
	}
	cenum = (PDIRECTORY_ENUM_CONTEXT)FileObject->FsContext2;
	if(cenum == NULL)
	{
		return FsdCompleteRequest(pvirp, STATUS_ACCESS_DENIED);
	}

	KeQuerySystemTime((PFILETIME)&ptime);
	dirinf->EndOfFile.QuadPart = 0;
	dirinf->AllocationSize.QuadPart = 0;
	dirinf->FileAttributes = FILE_ATTRIBUTE_DIRECTORY;

	dirinf->CreationTime.QuadPart = ptime.QuadPart;
	dirinf->LastWriteTime.QuadPart = ptime.QuadPart;
	dirinf->LastAccessTime.QuadPart = ptime.QuadPart;
	dirinf->ChangeTime.QuadPart = ptime.QuadPart;

	maxFnameLen = pios->Parameters.QueryDirectory.Length - 0x40;
	ret = FsdEnumGetNextIndexItem(cenum, dirinf->FileName, maxFnameLen, &dirinf->FileNameLength);


	//showStackTrace();
	//irpShow(pirp);
	//DebugBreak();
	return FsdCompleteRequest(pvirp, ret);

}

NTSTATUS FsdIrpDeviceControl(void* pDeviceObject, void* pirp)
{
	NTSTATUS ret = STATUS_INVALID_DEVICE_REQUEST;
	PIRP pvirp = (PIRP)pirp;
// 	sdbgPrint("\nFsdIrpDeviceControl called ob: %08x irp: %08x\n", pDeviceObject, pirp);
	return FsdCompleteRequest(pvirp, ret);
}


