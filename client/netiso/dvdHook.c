#include "common.h"
#include "dvdHook.h"
#include "irpCompare.h"
#include "isoio.h"

enum {
	DVD_TYPE_NONE = 0,
	DVD_TYPE_X360 = 1,
	DVD_TYPE_XBOX = 2,
	DVD_TYPE_UNKNOWN = 3,
	DVD_TYPE_AUDIO = 4,
	DVD_TYPE_MOVIE = 5,
	DVD_TYPE_VIDEO_CD = 6,
	DVD_TYPE_AUDIO_CD = 7,
	DVD_TYPE_DATA_CD = 8,
	DVD_TYPE_HYBRID_GAME_MOVIE = 9,
	DVD_TYPE_HD_DVD = 10,
};

#define DUMMY_AUTH_TICK_COUNT	(0x00000c40)


NTSTATUS CdRomRead(void* pDeviceObject, void* pirp);
VOID CdRomStartIo(void* pDeviceObject, void* pirp);
NTSTATUS CdRomDeviceControl(void* pDeviceObject, void* pirp);

static CACHED_MEDIA_INFO mInfo;
BOOL g_HasDvd;
static DVD_SPINDLE_SPEED_INFO g_curSpindleSpeed =  {0,3,4,0};
static LARGE_INTEGER g_maxDiskSize;

typedef NTSTATUS (*XEKEYSGETMEDIAID)(PBYTE pMediaID, BOOL fCheckHvAuth);
#define XEKEYS_GET_MEDIAID_ORD		0x2B2
XEKEYSGETMEDIAID XeKeysGetMediaIdOrg;

DRIVER_OBJECT CdRomOrigObj;
DRIVER_OBJECT CdRomHookObj = {
	CdRomStartIo, // DriverStartIo
	NULL, // DriverDeleteDevice
	NULL, // DriverDismountVolume
	(DRIVERLONG)IoInvalidDeviceRequest,
	(DRIVERLONG)IoInvalidDeviceRequest,
	CdRomRead,
	(DRIVERLONG)IoInvalidDeviceRequest,
	(DRIVERLONG)IoInvalidDeviceRequest,
	(DRIVERLONG)IoInvalidDeviceRequest,
	(DRIVERLONG)IoInvalidDeviceRequest,
	(DRIVERLONG)IoInvalidDeviceRequest,
	(DRIVERLONG)IoInvalidDeviceRequest,
	CdRomDeviceControl, // DobF.DObDeviceControl
	(DRIVERLONG)IoInvalidDeviceRequest, // DobF.DObCleanup
};

NTSTATUS dvdHandleXGD2GetTestInfo(PIRP pvirp, PIO_STACK_LOCATION pios)
{
	NTSTATUS ret = STATUS_BUFFER_TOO_SMALL;
	sdbgPrint("XGD2GetTestInfo buf: %x", pios->Parameters.DeviceIoControl.OutputBufferLength);
	if(pios->Parameters.DeviceIoControl.OutputBufferLength >= 8)
	{
		PXGD2_GET_DRIVE_INFO pinf = (PXGD2_GET_DRIVE_INFO)pvirp->UserBuffer;
		pinf->HashTableIndexLBA = mInfo.HashTableIndexLBA;
		pinf->BCADescriptor = 0;
		pinf->CacheFlags = 3;
		if(pios->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(XGD2_GET_TEST_INFO))
		{
			pinf->LastDiscAuthTime = DUMMY_AUTH_TICK_COUNT;
			pvirp->IoStatus.Information = sizeof(XGD2_GET_TEST_INFO);
		}
		if(pios->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(XGD2_GET_DRIVE_INFO))
		{
			pinf->Layer0Length = ((mInfo.dvdLayerInfo[2])-(mInfo.dvdLayerInfo[0]))+1;
			pinf->Layer1Length = (mInfo.dvdLayerInfo[1])-((~(mInfo.dvdLayerInfo[2]+1))&0xFFFFFF);
			memcpy(pinf->HashValueOfTable, mInfo.HashTableIndexHash, 0x14);
			pvirp->IoStatus.Information = sizeof(XGD2_GET_DRIVE_INFO);
		}
		else
			pvirp->IoStatus.Information = 8;
		ret = STATUS_SUCCESS;
	}
	if(pvirp->UserIosb != NULL)
	{
		pvirp->UserIosb->st.Status = ret;
	}
	if(pvirp->UserEvent != NULL)
	{
		KeSetEvent(pvirp->UserEvent, 0, FALSE);
	}
	pvirp->IoStatus.st.Status = ret;
	IoCompleteRequest(pvirp, IO_NO_INCREMENT);
	return ret;
}

NTSTATUS dvdReadNetisoIrp(void* pDeviceObject, void* pirp)
{
	PIRP pvirp = (PIRP)pirp;
	PIO_STACK_LOCATION sloc = pvirp->Tail.Overlay.CurrentStackLocation;
	QWORD offset;
	// 	irpShow(pirp);
	//sdbgPrint("\ndvdReadNetisoIrp called ob: %08x irp: %08x cache: 0x%08x\n", pDeviceObject, pirp, sloc->Parameters.Read.CacheBuffer);
	//sdbgPrint("IRP flags    : 0x%x\n", pvirp->Flags);
	//sdbgPrint("UserBuffer   : 0x%x\n", pvirp->UserBuffer);
	//sdbgPrint("Cancel       : %d\n", pvirp->Cancel);
	//sdbgPrint("MajorFunction: 0x%02x\n", sloc->MajorFunction);
	//sdbgPrint("MinorFunction: 0x%02x\n", sloc->MinorFunction);
	//sdbgPrint("Flags        : 0x%02x\n", sloc->Flags);
	//sdbgPrint("Control      : 0x%02x\n", sloc->Control);
	//sdbgPrint("Length       : 0x%08x\n", sloc->Parameters.Read.Length);
	//sdbgPrint("BufferOffset : 0x%08x\n", sloc->Parameters.Read.BufferOffset);
	//sdbgPrint("ByteOffset   : 0x%I64x\n", sloc->Parameters.Read.ByteOffset.QuadPart);

	if(((sloc->Flags & IRP_NOCACHE) == 0)) // RequestorMode? if this bit is set skip these checks? maybe its continue previous op?
	{
		if((((DWORD)pvirp->UserBuffer & 1) != 0) || // check args
			((sloc->Parameters.Read.BufferOffset & 1) != 0) ||
			((sloc->Parameters.Read.Length & 0x7FF) != 0) ||
			((sloc->Parameters.Read.ByteOffset.LowPart & 0x7FF) != 0) ||
			((sloc->Parameters.Read.ByteOffset.QuadPart + sloc->Parameters.Read.Length) > isoIoGetPartitionSize()))
		{
			sdbgPrint("\nFsdIrpRead invalid parameter!\n");
			sdbgPrint("Flags       : 0x%x\n", sloc->Flags);
			sdbgPrint("UserBuffer  : 0x%x\n", pvirp->UserBuffer);
			sdbgPrint("BufferOffset: 0x%x\n", sloc->Parameters.Read.BufferOffset);
			sdbgPrint("Length      : 0x%x\n", sloc->Parameters.Read.Length);
			sdbgPrint("ByteOffset  : 0x%I64x\n", sloc->Parameters.Read.ByteOffset.QuadPart);
			sdbgPrint("Max Size    : 0x%I64x\n", isoIoGetPartitionSize());

			pvirp->IoStatus.st.Status = STATUS_INVALID_PARAMETER;
			IoCompleteRequest(pirp, IO_NO_INCREMENT);
			return STATUS_INVALID_PARAMETER;
		}
	}
	if(sloc->Parameters.Read.Length == 0)
	{
		pvirp->IoStatus.Information = 0;
		pvirp->IoStatus.st.Status = 0;
// 		sdbgPrint("\nFsdIrpRead no read length!!\n");
		IoCompleteRequest(pirp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
	}
	offset = sloc->Parameters.Read.ByteOffset.QuadPart >> 11;
	sloc->Parameters.Read.ByteOffset.HighPart = (DWORD)(offset&0xFFFFFFFF); // sector is stored in highpart
	if(sloc->Flags & IRP_NOCACHE)
		sloc->Parameters.Read.ByteOffset.LowPart = 0;
	else
	{
		sloc->Parameters.Read.ByteOffset.LowPart = sloc->Parameters.Read.BufferOffset;
		sloc->Parameters.Read.BufferOffset = (DWORD)pvirp->UserBuffer;
	}
	sloc->Flags = 0;
	pvirp->IoStatus.Information = 0;
	//return isoIoQueueRead(pvirp);
	sloc->Control |= SL_PENDING_RETURNED;
	return isoIoQueueRead(pvirp);
}

NTSTATUS dvdDeviceControlNetisoIrp(void* pDeviceObject, void* pirp)
{
	DWORD Code = 0;
	NTSTATUS ret = STATUS_INVALID_DEVICE_REQUEST;
	PIRP pvirp = (PIRP)pirp;
	PIO_STACK_LOCATION pios;
	pios = pvirp->Tail.Overlay.CurrentStackLocation;
	Code = pios->Parameters.DeviceIoControl.IoControlCode;
	//sdbgPrint("\ndvdDeviceControlNetisoIrp called ob: %08x irp: %08x\n", pDeviceObject, pirp);
	//sdbgPrint("IoCtl: 0x%x obl: 0x%x ib: 0x%x ibl: 0x%x\n", Code, pios->Parameters.DeviceIoControl.OutputBufferLength, pios->Parameters.DeviceIoControl.InputBuffer, pios->Parameters.DeviceIoControl.InputBufferLength);
	//sdbgPrint("Dev: 0x%x Func: 0x%x Meth: 0x%x Accs: 0x%x\n", CTL_CODE_DEVTYPE(Code), CTL_CODE_FUNCTION(Code), CTL_CODE_METHOD(Code), CTL_CODE_ACCESS(Code));
	if(Code == 0x24800) // IOCTL_CDROM_CHECK_VERIFY aka SENSE
	{
// 		storeIrp(pirp);
		ret = isoIoGetSense(pvirp);
// 		compareIrp(pirp);
	}
	else if(Code == 0x2404c) // IOCTL_CDROM_GET_DRIVE_GEOMETRY
	{
		ret = isoIoGetGeometry(pvirp);
	}
	else if(Code == 0x335142) // IOCTL_DVD_READ_STRUCTURE obl: 0x18 ib: 0x7a09f950 ibl: 0x18
	{
		PDVD_READ_STRUCTURE drs = (PDVD_READ_STRUCTURE)pios->Parameters.DeviceIoControl.InputBuffer;
		PDVD_DESCRIPTOR_HEADER ddh = (PDVD_DESCRIPTOR_HEADER)pvirp->UserBuffer;
		//sdbgLockPush();
		//sdbgPrintL("read structure\n");
		//sdbgPrintL("BlockByteOffset: 0x%I64x\n", drs->BlockByteOffset.QuadPart);
		//sdbgPrintL("Format         : %d\n", drs->Format);
		//sdbgPrintL("SessionId      : 0x%08x\n", drs->SessionId);
		//sdbgPrintL("LayerNumber    : 0x%02x\n", drs->LayerNumber);
		//sdbgPrintL("userbuf        : 0x%x\n", pvirp->UserBuffer);
		//drs = (PDVD_READ_STRUCTURE)pvirp->UserBuffer;
		//sdbgPrintL("BlockByteOffset: 0x%I64x\n", drs->BlockByteOffset.QuadPart);
		//sdbgPrintL("Format         : %d\n", drs->Format);
		//sdbgPrintL("SessionId      : 0x%08x\n", drs->SessionId);
		//sdbgPrintL("LayerNumber    : 0x%02x\n", drs->LayerNumber);
		//sdbgLockPop();
		if(drs->Format == DvdPhysicalDescriptor) // for XexpComputeImageMediaTypes aka: PFI
		{
			PDVD_LAYER_DESCRIPTOR layer = (PDVD_LAYER_DESCRIPTOR)ddh->Data;
// 			unsigned char hexData[16] = {0x01, 0x02, 0x31, 0x10, 0x00, 0x03, 0x00, 0x00, 0x00, 0xFC, 0xFF, 0xE6, 0x00, 0x03, 0x30, 0xFF};

			BYTE* pbuf = (BYTE*)pvirp->UserBuffer;
// 			memcpy(pbuf, hexData, 16);
			pbuf[0] = 0x08;
			pbuf[1] = 0x02;
			pbuf[6] = 0x02; // important that this &0xF is 1, XGD2 seem to have 0x31 here, data has 0x02
			pvirp->IoStatus.st.Status = STATUS_SUCCESS;
			if(pvirp->UserIosb != NULL)
			{
				pvirp->UserIosb->st.Status = pvirp->IoStatus.st.Status;
			}
			if(pvirp->UserEvent != NULL)
			{
				KeSetEvent(pvirp->UserEvent, 0, FALSE);
			}
			IoCompleteRequest(pirp, IO_NO_INCREMENT); // IO_NO_INCREMENT IO_CD_ROM_INCREMENT
			ret = STATUS_PENDING;
		}
		//else
		//{
		//	sdbgLockPush();
		//	sdbgPrintL("\ndvdDeviceControlNetisoIrp (unhandled structure) called ob: %08x irp: %08x\n", pDeviceObject, pirp);
		//	sdbgPrintL("IoCtl: 0x%x obl: 0x%x ib: 0x%x ibl: 0x%x\n", Code, pios->Parameters.DeviceIoControl.OutputBufferLength, pios->Parameters.DeviceIoControl.InputBuffer, pios->Parameters.DeviceIoControl.InputBufferLength);
		//	sdbgPrintL("Dev: 0x%x Func: 0x%x Meth: 0x%x Accs: 0x%x\n", CTL_CODE_DEVTYPE(Code), CTL_CODE_FUNCTION(Code), CTL_CODE_METHOD(Code), CTL_CODE_ACCESS(Code));
		//	sdbgPrintL("read structure\n");
		//	sdbgPrintL("BlockByteOffset: 0x%I64x\n", drs->BlockByteOffset.QuadPart);
		//	sdbgPrintL("Format         : %d\n", drs->Format);
		//	sdbgPrintL("SessionId      : 0x%08x\n", drs->SessionId);
		//	sdbgPrintL("LayerNumber    : 0x%02x\n", drs->LayerNumber);
		//	sdbgPrintL("userbuf        : 0x%x\n", pvirp->UserBuffer);
		//	sdbgLockPop();
		//}
	}
	else if (Code == 0x24084) // SataCdRomStartSetSpindleSpeed
	{
		if(pios->Parameters.DeviceIoControl.InputBufferLength == 4)
		{
			if(isoIoHasDisk())
			{
				DWORD setSpeed = *((DWORD*)pios->Parameters.DeviceIoControl.InputBuffer);
				if(pios->Parameters.DeviceIoControl.OutputBufferLength >= 4)
				{
					DWORD* retSpeed = (DWORD*)pvirp->UserBuffer;
					*retSpeed = g_curSpindleSpeed.FastestSpeed;
					pvirp->IoStatus.Information = 4;
				}
				else
					pvirp->IoStatus.Information = 0;
				if(setSpeed > 3)
					ret = STATUS_INVALID_PARAMETER;
				else
				{
					g_curSpindleSpeed.CurrentSpeed = setSpeed;
					g_curSpindleSpeed.DesiredSpeed = setSpeed;
					ret = STATUS_SUCCESS;
				}
			}
			else
			{
				ret = STATUS_NO_MEDIA_IN_DEVICE;
			}
		}
		else
			ret = STATUS_INVALID_PARAMETER;
		if(pvirp->UserIosb != NULL)
		{
			pvirp->UserIosb->st.Status = ret;
		}
		if(pvirp->UserEvent != NULL)
		{
			KeSetEvent(pvirp->UserEvent, 0, FALSE);
		}
	}
	else if(Code == 0x240A8) // SataCdromGetSpindleSpeed
	{
		if(pios->Parameters.DeviceIoControl.OutputBufferLength != 0x10)
			ret = STATUS_BUFFER_TOO_SMALL;
		else
		{
			memcpy(pvirp->UserBuffer, &g_curSpindleSpeed, sizeof(DVD_SPINDLE_SPEED_INFO));
			ret = STATUS_SUCCESS;
		}
		if(pvirp->UserIosb != NULL)
		{
			pvirp->UserIosb->st.Status = ret;
		}
		if(pvirp->UserEvent != NULL)
		{
			KeSetEvent(pvirp->UserEvent, 0, FALSE);
		}
	}
	// SataCdRomDoUninterruptableReads || CdRomSetBootPerfStat || SataCdRomClearAuthenticationState || SataCdRomStartStandby
	else if((Code == 0x240c4)||(Code == 0x240b8)||(Code == 0x24098)||(Code == 0x240e0))
	{
		ret = STATUS_SUCCESS;
		// 		irpShowBuf(pvirp->UserBuffer, pios->Parameters.DeviceIoControl.OutputBufferLength, pios->Parameters.DeviceIoControl.InputBuffer, pios->Parameters.DeviceIoControl.InputBufferLength);
		if(pvirp->UserIosb != NULL)
		{
			pvirp->UserIosb->st.Status = ret;
		}
		if(pvirp->UserEvent != NULL)
		{
			KeSetEvent(pvirp->UserEvent, 0, FALSE);
		}
	}
	else if(Code == 0x240D0) // SataCdRomFwcr
	{
		//	0x240D0		Dev: 0x2 Func: 0x34 Meth: 0x0 Accs: 0x1			SataCdRomFwcr							obl: 0x0 ib: 0x3a162a30 ibl: 0x28
		ret = STATUS_NOT_SUPPORTED; // maybe STATUS_NOT_SUPPORTED
		// 		irpShowBuf(pvirp->UserBuffer, pios->Parameters.DeviceIoControl.OutputBufferLength, pios->Parameters.DeviceIoControl.InputBuffer, pios->Parameters.DeviceIoControl.InputBufferLength);
		if(pvirp->UserIosb != NULL)
		{
			pvirp->UserIosb->st.Status = ret;
		}
		if(pvirp->UserEvent != NULL)
		{
			KeSetEvent(pvirp->UserEvent, 0, FALSE);
		}
	}
	else if(Code == 0x240E4) // SataCdRomStartCheckVerify SCSI_CMD_TEST_UNIT_READY
	{
		//	0x240E4 	Dev: 0x2  Func: 0x39  Meth: 0x0 Accs: 0x1		SataCdRomStartCheckVerify				obl: 0x12 ib: 0x0 ibl: 0x0			SCSI_CMD_TEST_UNIT_READY
		// 		irpShowBuf(pvirp->UserBuffer, pios->Parameters.DeviceIoControl.OutputBufferLength, pios->Parameters.DeviceIoControl.InputBuffer, pios->Parameters.DeviceIoControl.InputBufferLength);
		pios->Parameters.DeviceIoControl.OutputBufferLength = 0;
		ret = STATUS_SUCCESS;
		if(pvirp->UserIosb != NULL)
		{
			pvirp->UserIosb->st.Status = ret;
		}
		if(pvirp->UserEvent != NULL)
		{
			KeSetEvent(pvirp->UserEvent, 0, FALSE);
		}
	}
	else if(Code == 0x240cc) // SataCdRomX360Media < DWORD 3 on real disk, ?1? on partition only?
	{
		DWORD* SataCdRomX360Media = (DWORD*)pvirp->UserBuffer;
		*SataCdRomX360Media = 0x3;
		pvirp->IoStatus.Information = 4;
		if(pvirp->UserEvent != NULL)
		{
			KeSetEvent(pvirp->UserEvent, 0, FALSE);
		}
		ret = STATUS_SUCCESS;
	}
	else if((Code == 0x24080)||(Code == 0x24094)) // SataCdRomAuthenticationSequence || SataCdRomReauthenticationSequence
	{
// 		ret = STATUS_UNRECOGNIZED_MEDIA; // SataCdRomAuthenticationSequence data disks return STATUS_UNRECOGNIZED_MEDIA, real disks STATUS_SUCCESS
		if(isoIoHasDisk())
		{
			DWORD lastAuthTicks = DUMMY_AUTH_TICK_COUNT; // 00000c40 dummy real value for auth time
			XeKeysSetKey(0x101C, &lastAuthTicks, 4);
			ret = STATUS_SUCCESS;
		}
		else
		{
			ret = STATUS_NO_MEDIA_IN_DEVICE;
		}
		if(pvirp->UserIosb != NULL)
		{
			pvirp->UserIosb->st.Status = ret;
		}
		if(pvirp->UserEvent != NULL)
		{
			KeSetEvent(pvirp->UserEvent, 0, FALSE);
		}
	}
	else if(Code == 0x24090) // SataCdRomXGD2GetTestInfo
	{
		ret = dvdHandleXGD2GetTestInfo(pvirp, pios);
		return ret;
	}
	else
	{
		BYTE irql =	KeGetCurrentIrql();
		KfLowerIrql(0);
		sdbgPrint("\ndvdDeviceControlNetisoIrp (unhandled) called ob: %08x irp: %08x IoCtl: 0x%x\n", pDeviceObject, pirp, Code);
		KfRaiseIrql(irql);
		//sdbgPrint("\ndvdDeviceControlNetisoIrp (unhandled) called ob: %08x irp: %08x\n", pDeviceObject, pirp);
		//sdbgPrint("IoCtl: 0x%x obl: 0x%x ib: 0x%x ibl: 0x%x\n", Code, pios->Parameters.DeviceIoControl.OutputBufferLength, pios->Parameters.DeviceIoControl.InputBuffer, pios->Parameters.DeviceIoControl.InputBufferLength);
		//sdbgPrint("Dev: 0x%x Func: 0x%x Meth: 0x%x Accs: 0x%x\n", CTL_CODE_DEVTYPE(Code), CTL_CODE_FUNCTION(Code), CTL_CODE_METHOD(Code), CTL_CODE_ACCESS(Code));
	}

	if(ret != STATUS_PENDING)
	{
		pvirp->IoStatus.st.Status = ret;
		IoCompleteRequest(pirp, IO_NO_INCREMENT);
	}
	return ret;
}

VOID CdRomStartIo(void* pDeviceObject, void* pirp)
{
	//PIRP pvirp = (PIRP)pirp;
	//pvirp->IoStatus.st.Status = STATUS_INVALID_DEVICE_REQUEST;
	//IoCompleteRequest(pirp, IO_NO_INCREMENT);
// 	sdbgPrint("\nCdRomStartIo called\n");
	if(g_HasDvd == FALSE)
	{
		PIRP pvirp = (PIRP)pirp;
		pvirp->IoStatus.st.Status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(pirp, IO_NO_INCREMENT);
	}
	else
	{
		//if(isoIoHasDisk())
		//{
		//	BYTE irql =	KeGetCurrentIrql();
		//	KfLowerIrql(0);
		//	showStackTrace();
		//	sdbgPrint("******** unexpected call to CdRomStartIo *****\n");
		//	KfRaiseIrql(irql);
		//}

		CdRomOrigObj.DriverStartIo(pDeviceObject, pirp);
	}
}

typedef NTSTATUS (*DRIVERCOMPLETION)(PDEVICE_OBJECT pDeviceObject, PIRP pIrp, PVOID Context);

static const BYTE msMagic[] = "MICROSOFT*XBOX*MEDIA"; // 20
static const QWORD offsets[3] = {0xFD90000LL, 0x2080000LL, 0x0ULL}; // actual offsets-0x10000 on disk for msMagic
static const QWORD ssOffsets[3] = {0xFD8F000LL, 0x2077000LL, 0x0ULL}; // actual offsets-0x10000 on disk for msMagic
static int currOff = -1;
static KEVENT dvdComEvt;
static IO_STACK_LOCATION iosSave;
// DRIVERCOMPLETION dvdOrgComplete;

NTSTATUS dvdCompletion(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context) // context is PKEVENT
{
	NTSTATUS ret = STATUS_MORE_PROCESSING_REQUIRED;
// 	sdbgPrint("dvdCompletion\n");
	KeSetEvent(&dvdComEvt, TRUE, FALSE);
	return ret;
}

NTSTATUS dvdTryRead(PDEVICE_OBJECT pDeviceObject, PIRP pirp)
{
	NTSTATUS ret;
	if(isoIoHasDisk())
		ret = dvdReadNetisoIrp(pDeviceObject, pirp);
	else
		ret = CdRomOrigObj.dObFuns.DObRead(pDeviceObject, pirp);
	KeWaitForSingleObject(&dvdComEvt, WrFsCacheIn, PROC_IDLE, FALSE, NULL);
	KeResetEvent(&dvdComEvt);
// 	sdbgPrint("event signaled\n");
// 	sdbgPrint("stackcount %d currloc %d\n", pvirp->StackCount, pvirp->CurrentLocation);
	return ret;
}

VOID dvdRestoreStackLoc(PIRP pirp, PIO_STACK_LOCATION pios)
{
	pirp->Tail.Overlay.CurrentStackLocation = pios;
	memcpy(pios, &iosSave, sizeof(IO_STACK_LOCATION));
	pirp->IoStatus.Information = 0;
	pirp->CurrentLocation = 1;
	pirp->PendingReturned = 0;
}

void dvdProcessDmiSS(BYTE* buf)
{
	BOOL hasData = FALSE;
	int i = 0;
	for(; i < 0x10; i++)
	{
		BYTE cb = (buf[(i+0x20)]&0xFF);
		if((cb != 0x0)&&(cb != 0xFF))
		{
			i = 0x10;
			hasData = TRUE;
		}
	}
	// media ID is 0x10 bytes at 0x460 in ss and 0x20 in dmi - DMI is at 0x0, SS is at 0x800
	if(hasData)
	{
		if(memcmp(&buf[0x20], &buf[0x800+0x460], 0x10) == 0) // if these compare, we have a good (hopefully) DMI and SS pair
		{
			memcpy(mInfo.mediaId, &buf[0x20], 0x10); // media ID
			memcpy(&mInfo.dvdLayerInfo, &buf[0x804], 0xC); // layer information
			memcpy(&mInfo.HashTableIndexLBA, &buf[0x800+0x100], 4);
			memcpy(mInfo.HashTableIndexHash, &buf[0x800+0x108], 0x14);
			mInfo.hasDiskInfo = TRUE;
// 			sdbgPrint("dvdProcessDmiSS valid SS/DMI found!\n");
		}
// 		else
// 			sdbgPrint("dvdProcessDmiSS media IDs not compareable!\n");
	}
// 	else
// 		sdbgPrint("dvdProcessDmiSS no valid data found!\n");
}

NTSTATUS dvdCheckDmiSS(PDEVICE_OBJECT pDeviceObject, PIRP pvirp, PIO_STACK_LOCATION sloc, BYTE* dest)
{
	NTSTATUS ret;
	sloc->Parameters.Read.ByteOffset.QuadPart = ssOffsets[currOff];
	ret = dvdTryRead((PDEVICE_OBJECT)pDeviceObject, pvirp);
	// userbuffer will have DMI and SS in it now
	dvdProcessDmiSS(dest);
	dvdRestoreStackLoc(pvirp, sloc);
	// re-read the MS magic header
	sloc->Parameters.Read.ByteOffset.QuadPart = offsets[currOff]+0x10000LL;
	ret = dvdTryRead((PDEVICE_OBJECT)pDeviceObject, pvirp);
	return ret;
}

NTSTATUS detectXGD3head(void* pDeviceObject, void* pirp)
{
	int i;
	NTSTATUS ret;
	PIRP pvirp = (PIRP)pirp;
	PIO_STACK_LOCATION sloc = pvirp->Tail.Overlay.CurrentStackLocation;
	PKEVENT dvdOrgEvt;
	BYTE* dest; 
	dvdOrgEvt = NULL;
	currOff = -1;
	mInfo.hasDiskInfo = FALSE;

	if(sloc->Flags&IRP_NOCACHE)
		dest = (BYTE*)(sloc->Parameters.Read.BufferOffset);
	else
		dest = (BYTE*)((DWORD)pvirp->UserBuffer + sloc->Parameters.Read.BufferOffset);
	KeInitializeEvent(&dvdComEvt, 0, FALSE);
// 	dvdOrgComplete = (DRIVERCOMPLETION)sloc->CompletionRoutine;
	sloc->CompletionRoutine = &dvdCompletion;
	dvdOrgEvt = (PKEVENT)sloc->Context;

	memcpy(&iosSave, sloc, sizeof(IO_STACK_LOCATION));

	//storeIrp(pirp);
// 		sdbgPrint("detectXGD3head: UserEvent is NULL!!\n");
	for(i = 0; i < 3; i++)
	{
		LONGLONG offset = offsets[i]+0x10000LL;
		sloc->Parameters.Read.ByteOffset.QuadPart = offset;
		offset += ((LONGLONG)(sloc->Parameters.Read.Length&0xFFFFFFFF));
		if(g_maxDiskSize.QuadPart >= offset)
		{
			sdbgPrint("trying: 0x%I64x\n", sloc->Parameters.Read.ByteOffset.QuadPart);
			ret = dvdTryRead((PDEVICE_OBJECT)pDeviceObject, pvirp);

			if(memcmp(dest, msMagic, 0x14) == 0)
			{
				sdbgPrint("msmagic found OK at 0x%I64x!\n", offsets[i]+0x10000LL);
				currOff = i;
				i = 3;
				if(currOff != 2) // if its not a direct game partition, XGDtool or real authed disk
				{
					dvdRestoreStackLoc(pvirp, sloc);
					dvdCheckDmiSS((PDEVICE_OBJECT)pDeviceObject, pvirp, sloc, dest);
				}
			}
			else
			{
				sdbgPrint("msmagic not found at 0x%I64x!\n", offsets[i]+0x10000LL);
				if(i != 2) // we don't want to restore the stack location on the last try, which should be the original request anyway
					dvdRestoreStackLoc(pvirp, sloc);
			}
		}
		//else
		//{
		//	sdbgPrint("skipping (too large): 0x%I64x\n", sloc->Parameters.Read.ByteOffset.QuadPart);
		//	sdbgPrint("max size is: 0x%I64x\n", g_maxDiskSize.QuadPart);
		//}
	}
	KeSetEvent(dvdOrgEvt, 1, FALSE);
	return ret;
}

// this is just a rough way of detecting whether the IRP is doing a read to get the partition header from XGD3
BOOL isDetectXGD3Head(PIRP pirp, PIO_STACK_LOCATION pios)
{
	if(pirp->UserEvent != NULL)
	{
		sdbgPrint("UserEvent is not null\n");
		return FALSE;
	}
	if(pirp->UserIosb != NULL)
	{
		sdbgPrint("UserIosb is not null\n");
		return FALSE;
	}
	if(pios->CompletionRoutine == NULL)
	{
		sdbgPrint("CompletionRoutine is null\n");
		return FALSE;
	}
	if(pios->Context == NULL)
	{
		sdbgPrint("Context is null\n");
		return FALSE;
	}
	if((pios->Control&(SL_INVOKE_ON_CANCEL|SL_INVOKE_ON_SUCCESS|SL_INVOKE_ON_ERROR)) != (SL_INVOKE_ON_CANCEL|SL_INVOKE_ON_SUCCESS|SL_INVOKE_ON_ERROR))
	{
		sdbgPrint("Control is not E0\n");
		return FALSE;
	}
	if((g_maxDiskSize.QuadPart < 0x11000ULL))
	{
		sdbgPrint("disk size is too small\n");
		return FALSE;
	}
	return TRUE;
}

NTSTATUS CdRomRead(void* pDeviceObject, void* pirp)
{
	NTSTATUS ret;
// 	sdbgPrint("\nCdRomRead called\n");
	PIRP pvirp = (PIRP)pirp;
	PIO_STACK_LOCATION sloc = pvirp->Tail.Overlay.CurrentStackLocation;
// 	sdbgPrint("\nCdRomRead called ob: %08x irp: %08x cache: 0x%08x\n", pDeviceObject, pirp, sloc->Parameters.Read.CacheBuffer);
//	storeIrp(pirp);
// 	sdbgPrint("IRP flags    : 0x%x\n", pvirp->Flags);
// 	sdbgPrint("UserBuffer   : 0x%x\n", pvirp->UserBuffer);
//	sdbgPrint("Cancel       : %d\n", pvirp->Cancel);
// 	sdbgPrint("MajorFunction: 0x%02x\n", sloc->MajorFunction);
// 	sdbgPrint("MinorFunction: 0x%02x\n", sloc->MinorFunction);
// 	sdbgPrint("Flags        : 0x%02x\n", sloc->Flags);
// 	sdbgPrint("Control      : 0x%02x\n", sloc->Control);
// 	sdbgPrint("Length       : 0x%08x\n", sloc->Parameters.Read.Length);
// 	sdbgPrint("BufferOffset : 0x%08x\n", sloc->Parameters.Read.BufferOffset);
// 	sdbgPrint("ByteOffset   : 0x%I64x\n", sloc->Parameters.Read.ByteOffset.QuadPart);
	if((isoIoHasDisk() == FALSE)&&(g_HasDvd == FALSE))
	{
		pvirp->IoStatus.st.Status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(pirp, IO_NO_INCREMENT);
		ret = STATUS_INVALID_DEVICE_REQUEST;
	}
	else if((sloc->Parameters.Read.ByteOffset.QuadPart == 0x10000LL)&&(isDetectXGD3Head(pvirp, sloc)))
	{
		ret = detectXGD3head(pDeviceObject, pirp);
	}
	else
	{
		if((currOff >= 0)&&(currOff <= 3))
		{
			sloc->Parameters.Read.ByteOffset.QuadPart += offsets[currOff];
		}
		if(isoIoHasDisk())
			ret = dvdReadNetisoIrp(pDeviceObject, pirp);
		else
		{
// 			irpShow(pirp);
			ret = CdRomOrigObj.dObFuns.DObRead(pDeviceObject, pirp);
// 			sdbgPrint("cdrom read returns 0x%x\n", ret);
// 			Sleep(100);
// 			irpShow(pirp);

		}
	}

//	sdbgPrint("\nCdRomRead done ob: %08x irp: %08x cache: 0x%08x ret: 0x%08x\n", pDeviceObject, pirp, sloc->Parameters.Read.CacheBuffer, ret);
//	compareIrp(pirp);

	return ret;
}

NTSTATUS CdRomDeviceControl(void* pDeviceObject, void* pirp)
{
	PIRP pvirp = (PIRP)pirp;
	NTSTATUS ret;
	DWORD Code;
	PIO_STACK_LOCATION pios;
	pios = pvirp->Tail.Overlay.CurrentStackLocation;
	Code = pios->Parameters.DeviceIoControl.IoControlCode;
// 	if(Code != 0x240E4)
// 	{
// 		sdbgPrint("\nCdRomDeviceControl called ob: %08x irp: %08x\n", pDeviceObject, pirp);
// 		sdbgPrint("IoCtl: 0x%x obl: 0x%x ib: 0x%x ibl: 0x%x\n", Code, pios->Parameters.DeviceIoControl.OutputBufferLength, pios->Parameters.DeviceIoControl.InputBuffer, pios->Parameters.DeviceIoControl.InputBufferLength);
// 		sdbgPrint("Dev: 0x%x Func: 0x%x Meth: 0x%x Accs: 0x%x\n", CTL_CODE_DEVTYPE(Code), CTL_CODE_FUNCTION(Code), CTL_CODE_METHOD(Code), CTL_CODE_ACCESS(Code));
// 		//if(Code == 0x24090)
// 		//	showStackTrace();
// 	}

	if(isoIoHasDisk())
		ret = dvdDeviceControlNetisoIrp(pDeviceObject, pirp);
	else if(g_HasDvd)
	{
		if((Code == 0x24090)&&(mInfo.hasDiskInfo))
			ret = dvdHandleXGD2GetTestInfo(pvirp, pios);
		else
			ret = CdRomOrigObj.dObFuns.DObDeviceControl(pDeviceObject, pirp);
	}
	else
	{
		if(Code == 0x24800) // IOCTL_CDROM_CHECK_VERIFY aka SENSE
		{
			ret = isoIoGetSense(pvirp);
		}
		else
		{
			pvirp->IoStatus.st.Status = STATUS_INVALID_DEVICE_REQUEST;
			IoCompleteRequest(pirp, IO_NO_INCREMENT);
			ret = STATUS_INVALID_DEVICE_REQUEST;
		}
	}
	if(Code == 0x2404c) // GET_GEOMETRY for our little 'seek MS*MAGIC'
	{
		if(pvirp->UserEvent != NULL)
		{
			PDISK_GEOMETRY geo = (PDISK_GEOMETRY)pvirp->UserBuffer;
			KeWaitForSingleObject(pvirp->UserEvent, UserRequest, PROC_USER, FALSE, NULL);
			g_maxDiskSize.QuadPart = ((QWORD)(geo->Sectors&0xFFFFFFFF))*0x800ULL;
			sdbgPrint("geometry: sectors 0x%x bps 0x%x max: 0x%I64x\n", geo->Sectors, geo->BytesPerSector, g_maxDiskSize.QuadPart);
		}
	}
	//else if(Code == 0x24090) // SataCdRomXGD2GetTestInfo
	//{
	//	if(pvirp->UserEvent != NULL)
	//		KeWaitForSingleObject(pvirp->UserEvent, UserRequest, PROC_USER, FALSE, NULL);
	//	if(pios->Parameters.DeviceIoControl.OutputBufferLength >= 8)
	//	{
	//		PXGD2_GET_DRIVE_INFO pinf = (PXGD2_GET_DRIVE_INFO)pvirp->UserBuffer;
	//		sdbgPrint("ioctl 0x24090 returns 0x%x\n", ret);
	//		sdbgPrint("HashTableIndexLBA: 0x%08x\n", pinf->HashTableIndexLBA);
	//		sdbgPrint("BCADescriptor    : 0x%02x\n", pinf->BCADescriptor);
	//		sdbgPrint("CacheFlags       : 0x%02x\n", pinf->CacheFlags);
	//		if(pios->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(XGD2_GET_TEST_INFO))
	//		{
	//			sdbgPrint("LastDiscAuthTime : 0x%08x\n", pinf->LastDiscAuthTime);
	//		}
	//		if(pios->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(XGD2_GET_DRIVE_INFO))
	//		{
	//			sdbgPrint("Layer0Length     : 0x%08x\n", pinf->Layer0Length);
	//			sdbgPrint("Layer1Length     : 0x%08x\n", pinf->Layer1Length);
	//		}
	//	}
	//	else
	//		sdbgPrint("outbuffer unknown size! %x out %x\n", pios->Parameters.DeviceIoControl.OutputBufferLength, pvirp->IoStatus.Information);
	//}
//	sdbgPrint("\nCdRomDeviceControl called ob: %08x irp: %08x\n", pDeviceObject, pirp);
// 	ret = dvdDeviceControlNetisoIrp(pDeviceObject, pirp);
	//ret = CdRomOrigObj.dObFuns.DObDeviceControl(pDeviceObject, pirp);

	return ret;
}

NTSTATUS XeKeysGetMediaIdHook(PBYTE pMediaID, BOOL fCheckHvAuth)
{
	NTSTATUS ret;
	if(mInfo.hasDiskInfo)
	{
		memcpy(pMediaID, mInfo.mediaId, 0x10);
		ret = STATUS_SUCCESS;
	}
	else
		ret = XeKeysGetMediaIdOrg(pMediaID, fCheckHvAuth);
	return ret;
}

// this only works if the drive object is flagged as existing, otherwise it returns STATUS_NO_SUCH_DEVICE
//PVOID DvdGetObjectDevice(VOID)
//{
//	NTSTATUS ret;
//	PVOID obj = NULL;
//	STRING nCdRom;
//	RtlInitAnsiString(&nCdRom, "\\Device\\Cdrom0");
//	ret = ObReferenceObjectByName(&nCdRom, 0, IoDeviceObjectType, 0, &obj); // reference device "\\Network\\RealSmb"
//	if(NT_SUCCESS(ret))
//	{
//		ObDereferenceObject(obj);
//		return obj;
//	}
//	return NULL;
//}

// this works whether or not the driver completed its loading
PVOID DvdGetObjectDeviceFromLink(VOID)
{
	PVOID oret = NULL;
	if(MountPath("isoio:", "\\Device\\Cdrom0", FALSE) >= 0)
	{
		PVOID obj = NULL;
		NTSTATUS ret;
		STRING nCdRom;
// 		sdbgPrint("mounted isoio: OK\n");
		RtlInitAnsiString(&nCdRom, "\\System??\\isoio:");
		ret = ObReferenceObjectByName(&nCdRom, 0, ObSymbolicLinkObjectType, 0, &obj);
		if(NT_SUCCESS(ret))
		{
			POBJECT_SYMBOLIC_LINK psl = (POBJECT_SYMBOLIC_LINK)obj;
// 			sdbgPrint("\\System??\\isoio: found 0x%08x!\n", obj);
// 			sdbgPrint("LinkTargetObject: %08x\n", psl->LinkTargetObject);
			oret = psl->LinkTargetObject;
			ObDereferenceObject(obj);
		}
		else
		{
			sdbgPrint("failed to reference \\Device\\Cdrom0 sta: 0x%x obj: %08x\n", ret, obj);
		}
		deleteLink("isoio:", FALSE);
	}
	return oret;
}

long DvdHookStartup(void)
{
	PDEVICE_OBJECT pdevob;
	g_HasDvd = FALSE;
	currOff = -1;

	pdevob = (PDEVICE_OBJECT)DvdGetObjectDeviceFromLink();
	if(pdevob != NULL)
	{
		BYTE irql = IoAcquireDeviceObjectLock();
		//sdbgPrint("object gotten at 0x%x!\n", pdevob);
		memcpy(&CdRomOrigObj, pdevob->DriverObject, sizeof(DRIVER_OBJECT));
		pdevob->DriverObject = &CdRomHookObj;
		if((pdevob->Flags&0x10) != 0) // this flag is set when the device is present, otherwise STATUS_NO_SUCH_DEVICE
		{
			//sdbgPrint("dvd flags invalid 0x%x\n", pdevob->Flags);
			pdevob->Flags = pdevob->Flags&~0x10;
		}
		else
		{
			//sdbgPrint("dvd flags valid 0x%x\n", pdevob->Flags);
			g_HasDvd = TRUE;
			doSync(&g_HasDvd);
		}
		IoReleaseDeviceObjectLock(irql);
		XeKeysGetMediaIdOrg = (XEKEYSGETMEDIAID)hookExportOrd(MODULE_KERNEL, XEKEYS_GET_MEDIAID_ORD, (DWORD)&XeKeysGetMediaIdHook);

	}

	sdbgPrint("DvdHookStartup completed!\n");
	return 0;
}

// NTSTATUS __stdcall XamLoaderGetMediaInfoEx(PDWORD pdwMediaType, PDWORD pdwTitleId, PDWORD pdwTypeExt) export 483
// NTSTATUS __stdcall XamLoaderGetMediaInfo(PDWORD pdwMediaType, PDWORD pdwTitleId) export 419
// XeKeysGetMediaID
