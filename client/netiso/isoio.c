#include "common.h"
#include "client.h"
#include "vtray.h"
#include "irpCompare.h"
#include "dlist.h"
#include "isoio.h"

typedef struct _ISOIO_WORKER_CONTEXT {
	HANDLE hThread; // 0x0 PKTHREAD thread;
	DWORD spinLock; // 0x4
	LIST_ENTRY queueList; // 0x8 sz 0x8
	KEVENT waitEvent; // 0x10 sz 0x10
}ISOIO_WORKER_CONTEXT, *PISOIO_WORKER_CONTEXT;
C_ASSERT(sizeof(ISOIO_WORKER_CONTEXT) == 0x20);

typedef struct _DISK_INFO{
	BOOL isAvailable; // when this is FALSE redirects will go to normal DVD...
	DISK_GEOMETRY geo;
	DWORD type;
	LARGE_INTEGER partitionLen;
}DISK_INFO, *PDISK_INFO;

static ISOIO_WORKER_CONTEXT isoIoCtx;
static DISK_INFO g_diskInfo;
static BOOL isRunning = TRUE;

static KEVENT g_workDoneEvent;
static int clientState = -2;

void isoIoMakeDiskUnavail(BOOL change)
{
	g_diskInfo.isAvailable = FALSE;
	doSync(&g_diskInfo.isAvailable);
	if(change)
	{
		clientState = -1;
		doSync(&clientState);
		vtrayFakeTrayCycle();
	}
}

NTSTATUS isoIoClientGetGeometry(BOOL diskSwap)
{
	DISK_GEOMETRY geo;
	if(clientGetGeometry((BYTE*)&geo))
	{
		if(geo.Sectors != 0)
		{
			memcpy(&g_diskInfo.geo, &geo, sizeof(DISK_GEOMETRY));
			if(clientGetDiskType(&g_diskInfo.type))
			{
				g_diskInfo.partitionLen.QuadPart = ((QWORD)(g_diskInfo.geo.Sectors&0xFFFFFFFF))*0x800ULL;
				sdbgPrint("geometry(sense): 0x%x bps 0x%x sectors\n", g_diskInfo.geo.BytesPerSector, g_diskInfo.geo.Sectors);
				sdbgPrint("maxsize : 0x%I64x\n", g_diskInfo.partitionLen.QuadPart);
				g_diskInfo.isAvailable = TRUE;
				doSync(&g_diskInfo.isAvailable);
				if(diskSwap)
					vtrayFakeTrayCycle();
				return STATUS_SUCCESS;
			}
		}
		else
		{
			g_diskInfo.isAvailable = FALSE;
			doSync(&g_diskInfo.isAvailable);
			if(diskSwap)
				vtrayFakeTrayCycle();
			return STATUS_SUCCESS;
		}
	}
	else
		isoIoMakeDiskUnavail(TRUE);
	//sdbgPrint("no disk is available!\n");
	return STATUS_UNSUCCESSFUL;
}

// DueTime.QuadPart = (LONGLONG)( UInt32x32To64 ( 1000 /*ms*/ , 1 )* -10000 /*100-ns-intervals*/ );
QWORD updateClientStateOrPing(void)
{
	QWORD ret = -10000000LL;// 1 second in relative 100 nanoseconds units
	clientState = clientStartup();
	doSync(&clientState);
	// returns 1 on existing connection, 0 on new connection, -1 on no connection, -2 on no network
	switch(clientState)
	{
		case 0: // new connection
			isoIoClientGetGeometry(FALSE);
			break;
		case 1: // existing connection
			if(clientGetPing() == FALSE)
			{
				isoIoMakeDiskUnavail(TRUE);
			}
			break;
		case -1: // no connection
		case -2: // no network
			ret = -5000000LL;// 1/2 second in relative 100 nanoseconds units
		default:
			break;
	}
	return ret;
}

NTSTATUS isoIoRead(PIRP pirp)
{
	NTSTATUS ret = STATUS_IO_DEVICE_ERROR;
	pirp->IoStatus.Information = 0;
	if(g_diskInfo.isAvailable)
	{
		BYTE* dest;
		DWORD bRead = 0;
		PIO_STACK_LOCATION sloc = pirp->Tail.Overlay.CurrentStackLocation;
		// dispatch reads in multiples of 2 sectors 0x1000
		while(sloc->Parameters.Read.Length > 0)
		{
			dest = (BYTE*)(sloc->Parameters.Read.BufferOffset+sloc->Parameters.Read.ByteOffset.LowPart);
			if(sloc->Parameters.Read.Length >= 0x1000)
				bRead = clientReadDisk(sloc->Parameters.Read.ByteOffset.HighPart, 0x1000, dest);
			else
				bRead = clientReadDisk(sloc->Parameters.Read.ByteOffset.HighPart, sloc->Parameters.Read.Length, dest);
			if(bRead != 0)
			{
				sloc->Parameters.Read.ByteOffset.HighPart += (bRead>>11); // increment sector
				sloc->Parameters.Read.ByteOffset.LowPart += bRead; // increment buffer offset/userbuffer
				sloc->Parameters.Read.Length -= bRead; // decrease remaining bytes to be read

				pirp->IoStatus.Information += bRead; // set status bytes
				// 			sdbgPrint("set read 0x%x bytes\n", pirp->IoStatus.Information);
				ret = STATUS_SUCCESS;
			}
			else
			{
				// 			sdbgPrint("disk read failed!!??!!\n");
				isoIoMakeDiskUnavail(TRUE);
				pirp->IoStatus.st.Status = STATUS_IO_DEVICE_ERROR;
				IoCompleteRequest(pirp, IO_CD_ROM_INCREMENT); // IO_NO_INCREMENT IO_CD_ROM_INCREMENT
				return STATUS_IO_DEVICE_ERROR;
			}
		}
	}
/*		// this was the old method, single read of all requested data
		// sloc->Parameters.Read.ByteOffset.HighPart == current sector
		// sloc->Parameters.Read.BufferOffset == current buffer
		// sloc->Parameters.Read.ByteOffset.LowPart == current buffer offset
		dest = (BYTE*)(sloc->Parameters.Read.BufferOffset+sloc->Parameters.Read.ByteOffset.LowPart);

		bRead = clientReadDisk(sloc->Parameters.Read.ByteOffset.HighPart, sloc->Parameters.Read.Length, dest);
		if(bRead != 0)
		{
			sloc->Parameters.Read.ByteOffset.HighPart += (bRead>>11); // increment sector
			sloc->Parameters.Read.ByteOffset.LowPart += bRead; // increment buffer offset/userbuffer
			sloc->Parameters.Read.Length -= bRead; // decrease remaining bytes to be read

			pirp->IoStatus.Information += bRead; // set status bytes
// 			sdbgPrint("set read 0x%x bytes\n", pirp->IoStatus.Information);
			ret = STATUS_SUCCESS;
		}
		else
		{
// 			sdbgPrint("disk read failed!!??!!\n");
			isoIoMakeDiskUnavail(TRUE);
			ret = STATUS_IO_DEVICE_ERROR;
		}
	}
*/
	pirp->IoStatus.st.Status = ret;
	//if(pirp->UserIosb != NULL)
	//{
	//	pirp->UserIosb->st.Status = ret;
	//	pirp->UserIosb->Information = pirp->IoStatus.Information;
	//}
	//if(pirp->UserEvent != NULL)
	//{
	//	KeSetEvent(pirp->UserEvent, 0, FALSE);
	//}
	IoCompleteRequest(pirp, IO_CD_ROM_INCREMENT); // IO_NO_INCREMENT IO_CD_ROM_INCREMENT
	return ret;
}

void isoIoWorkerThread(PISOIO_WORKER_CONTEXT ctx)
{
	NTSTATUS waitSt;
	LARGE_INTEGER wTime;
	wTime.QuadPart = updateClientStateOrPing();
	//sdbgPrint("hello from isoIoWorkerThread!\n");
	while(isRunning)
	{
		waitSt = KeWaitForSingleObject(&ctx->waitEvent, Executive, UserMode, TRUE, &wTime);
		if(waitSt == STATUS_TIMEOUT) // timer has expired, lets check to see if the server is there
		{
			wTime.QuadPart = updateClientStateOrPing();
		}
		else if(waitSt != STATUS_USER_APC)
		{
			PLIST_ENTRY pli = NULL;
			while(pli != &ctx->queueList)
			{
				BYTE oldIrql;
				oldIrql = KfAcquireSpinLock(&ctx->spinLock);
				pli = RemoveHeadList(&ctx->queueList);
				KfReleaseSpinLock(&ctx->spinLock, oldIrql);
				if(pli != &ctx->queueList)
				{
					PIRP tirp = CONTAINING_RECORD(pli, IRP, Tail.Overlay.DeviceListEntry);
					// do the work here
					if(tirp->Type == IO_TYPE_IRP)
					{
						isoIoRead(tirp);
					}
					else if(tirp->Type == IRP_FAKE_GET_INDEX)
					{
						if(clientState != 1)
						{
							if(tirp->Size == 0)
							{
								strcpy((char*)tirp->UserBuffer, "No Server!");
								tirp->IoStatus.st.Status = 1;
							}
						}
						else
						{
							if(clientGetIndex(tirp->Size, tirp->Flags, (BYTE*)tirp->UserBuffer))
								tirp->IoStatus.st.Status = 1;
						}
						KeSetEvent(tirp->UserEvent, 1, FALSE);
					}
					else if(tirp->Type == IRP_FAKE_MOUNT_ISO)
					{
// 						vtrayFakeTrayOpen();
						if(clientMountIso(tirp->Flags, (BYTE*)tirp->UserBuffer))
						{
							tirp->IoStatus.st.Status = 1;
						}
						KeSetEvent(tirp->UserEvent, 1, FALSE);
						//Sleep(1000);
// 						vtrayFakeTrayClose();
						isoIoClientGetGeometry(TRUE);
					}
				}
			}
		}
	}
}

// 0x200006AA = EX_CREATE_FLAG_CORE5|EX_CREATE_FLAG_SYSTEM|EX_CREATE_FLAG_HIDDEN|EX_CREATE_FLAG_RETURN_KTHREAD|EX_CREATE_FLAG_PRIORITY1|0x208
// 0x100000A2= EX_CREATE_FLAG_SYSTEM|EX_CREATE_FLAG_PRIORITY1|EX_CREATE_FLAG_RETURN_KTHREAD|EX_CREATE_FLAG_CORE4
VOID IsoIoInitializeWorkerThreadContext(PISOIO_WORKER_CONTEXT ctx)
{
	//DWORD flags = (EX_CREATE_FLAG_SYSTEM | EX_CREATE_FLAG_PRIORITY1 | EX_CREATE_FLAG_RETURN_KTHREAD | EX_CREATE_FLAG_CORE4); // priority 14 - 9MB/s or so
	DWORD flags = 0x200002AA; // priority 14 - 10MB/s or so ~ p 13 about 7MB/s ithd
	flags |= EX_CREATE_FLAG_HIDDEN;
	InitializeListHead(&ctx->queueList);
// 	ctx->waitEvent.Header.Type = EventSynchronizationObject;
// 	InitializeListHead(&ctx->waitEvent.Header.WaitListHead);
	KeInitializeEvent(&ctx->waitEvent, EventSynchronizationObject, FALSE);
	if(NT_SUCCESS(ExCreateThread(&ctx->hThread, 0x4000, NULL, NULL, (LPTHREAD_START_ROUTINE)isoIoWorkerThread, ctx, flags)))
	{
		KeSetBasePriorityThread((PKTHREAD)ctx->hThread, 0x10);
	}
}

void isoIoStartup(void)
{
	g_diskInfo.isAvailable = FALSE;
	g_diskInfo.partitionLen.QuadPart = 0;
	g_diskInfo.geo.BytesPerSector = 0x800;
	g_diskInfo.geo.Sectors = 0;
	g_diskInfo.type = 0;
	// initialize worker thread
	KeInitializeEvent(&g_workDoneEvent, EventSynchronizationObject, TRUE);
	// create worker thread
	IsoIoInitializeWorkerThreadContext(&isoIoCtx);
	vtrayStartup();
	sdbgPrint("isoIoStartup complete\n");
}

// this is a really dirty way of seeing if we should return status 0x103...
// BOOL IoIsOperationSynchronous(PIRP pirp)
// {
// 	PIO_STACK_LOCATION sloc = pirp->Tail.Overlay.CurrentStackLocation;
// 	//if((pirp->Overlay.AsynchronousParameters.UserApcRoutine == 0)&&(pirp->Overlay.AsynchronousParameters.UserApcContext != 0))
// 	//{
// 	//	if(sloc != NULL)
// 	//	{
// 	//		if(sloc->FileObject != NULL)
// 	//			return FALSE;
// 	//	}
// 	//}
// 	if((pirp->UserEvent != NULL)&&(sloc->CompletionRoutine == NULL))
// 	{
// 		if(pirp->UserIosb != NULL)
// 		{
// 			if(pirp->UserIosb->st.Status == STATUS_PENDING)
// 			{
// 				if((pirp->Overlay.AsynchronousParameters.UserApcRoutine == 0)&&(pirp->Overlay.AsynchronousParameters.UserApcContext != 0))
// 				{
// 					if(sloc->FileObject != NULL)
// 						return FALSE;
// 				}
// 			}
// 		}
// 	}
// 	return TRUE;
// }

NTSTATUS isoIoQueueRead(PIRP irp)
{
	//PIO_STACK_LOCATION sloc = irp->Tail.Overlay.CurrentStackLocation;
	if(g_diskInfo.isAvailable)
	{
		BYTE oldIrql;
		oldIrql = KfAcquireSpinLock(&isoIoCtx.spinLock);
		InsertTailList(&isoIoCtx.queueList, (PLIST_ENTRY)&irp->Tail.Overlay.DeviceListEntry);
		KfReleaseSpinLock(&isoIoCtx.spinLock, oldIrql);

		KeSetEvent(&isoIoCtx.waitEvent, 1, FALSE); // toggle the worker thread to go

		return STATUS_PENDING;
	}
// 	sdbgPrint("unexpected nodisk!\n");
	irp->IoStatus.Information = 0;
	irp->IoStatus.st.Status = STATUS_IO_DEVICE_ERROR;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_IO_DEVICE_ERROR;
}

VOID isoIoQueueOther(PIRP irp)
{
	BYTE oldIrql;
	KEVENT kev;
	KeInitializeEvent(&kev, EventSynchronizationObject, FALSE);
	irp->UserEvent = &kev;
	oldIrql = KfAcquireSpinLock(&isoIoCtx.spinLock);
	InsertTailList(&isoIoCtx.queueList, (PLIST_ENTRY)&irp->Tail.Overlay.DeviceListEntry);
	KfReleaseSpinLock(&isoIoCtx.spinLock, oldIrql);

	KeSetEvent(&isoIoCtx.waitEvent, 1, FALSE); // toggle the worker thread to go
	KeWaitForSingleObject(&kev, Executive, UserMode, FALSE, 0);
}

NTSTATUS isoIoGetSense(PIRP irp)
{
	if(clientState >= 0)
	{
		if(g_diskInfo.isAvailable)
		{
			irp->IoStatus.st.Status = 0;
		}
		else
		{
			irp->IoStatus.st.Status = STATUS_DEVICE_NOT_READY; // STATUS_NO_MEDIA_IN_DEVICE STATUS_DEVICE_NOT_READY
		}
	}
	else
	{
		irp->IoStatus.st.Status = STATUS_NO_MEDIA_IN_DEVICE; // STATUS_NO_MEDIA_IN_DEVICE STATUS_DEVICE_NOT_READY
	}
	if(irp->UserIosb != NULL)
	{
		irp->UserIosb->st.Status = irp->IoStatus.st.Status;
	}
	if(irp->UserEvent != NULL)
	{
		KeSetEvent(irp->UserEvent, 0, FALSE);
	}
	IoCompleteRequest(irp, IO_NO_INCREMENT); // IO_NO_INCREMENT IO_CD_ROM_INCREMENT
	// 		compareIrp(pirp);
	return STATUS_PENDING;
}

NTSTATUS isoIoGetGeometry(PIRP irp)
{
	PDISK_GEOMETRY geo = (PDISK_GEOMETRY)irp->UserBuffer;
	// 		sdbgPrint("userbuf: 0x%x\n", pvirp->UserBuffer);
	// 		storeIrp(pirp);
	geo->Sectors = g_diskInfo.geo.Sectors;// 0x220500;
	geo->BytesPerSector = g_diskInfo.geo.BytesPerSector;// 0x800;
	irp->UserIosb->Information = 8;
	if(irp->UserEvent != NULL)
	{
		KeSetEvent(irp->UserEvent, 0, FALSE);
	}
	//		pvirp->UserEvent->Header.SignalState = 1;
	IoCompleteRequest(irp, IO_NO_INCREMENT); // IO_NO_INCREMENT IO_CD_ROM_INCREMENT
	return STATUS_PENDING;
	// 		compareIrp(pirp);
}

LONGLONG isoIoGetPartitionSize(void)
{
	return g_diskInfo.partitionLen.QuadPart;
}

BOOL isoIoHasDisk(void)
{
	if(clientState >= 0)
	{
		return g_diskInfo.isAvailable;
	}
	return FALSE;
}
