#include "common.h"
#include <stdio.h>

BOOL hassKv = FALSE;
KEVENT sKv; // UserEvent
BOOL hassIos = FALSE;
IO_STATUS_BLOCK sIos; // UserIosb
IRP sIrp;

void storeIrp(void* irp)
{
	PIRP pIrp = (PIRP)irp;
	hassKv = FALSE;
	hassIos = FALSE;
	memcpy(&sIrp, pIrp, sizeof(IRP));
	sdbgPrint("irp stored.");
	if(pIrp->UserIosb != NULL)
	{
		hassIos = TRUE;
		memcpy(&sIos, pIrp->UserIosb, sizeof(IO_STATUS_BLOCK));
		sdbgPrint(" irp uios stored.");
	}
	if(pIrp->UserEvent != NULL)
	{
		hassKv = TRUE;
		memcpy(&sKv, pIrp->UserEvent, sizeof(KEVENT));
		sdbgPrint(" irp uevent stored.");
	}
	sdbgPrint("\n");
}

void showIoStatusDiffs(char* baseName, PIO_STATUS_BLOCK ns, PIO_STATUS_BLOCK os)
{
	if((ns != NULL)&&(os != NULL))
	{
		if(ns->st.Status != os->st.Status)
			sdbgPrint("%s.Status n:0x%x o:0x%x\n", baseName, ns->st.Status, os->st.Status);
		if(ns->Information != os->Information)
			sdbgPrint("%s.Info   n:0x%x o:0x%x\n", baseName, ns->Information, os->Information);
	}
	else
	{
		if(os != NULL)
		{
			sdbgPrint("%s.Status n:NULL o:0x%x\n", baseName, os->st.Status);
			sdbgPrint("%s.Info   n:NULL o:0x%x\n", baseName, os->Information);
		}
		if(ns != NULL)
		{
			sdbgPrint("%s.Status n:0x%x o:NULL\n", baseName, ns->st.Status);
			sdbgPrint("%s.Info   n:0x%x o:NULL\n", baseName, ns->Information);
		}
	}
}

void showIrpStructDiffs(PIRP ni, PIRP oi)
{
	if(ni->Type != oi->Type)
		sdbgPrint("IRP.Type  n:0x%x o:0x%x\n", ni->Type, oi->Type);
	if(ni->Size != oi->Size)
		sdbgPrint("IRP.Size  n:0x%x o:0x%x\n", ni->Size, oi->Size);
	if(ni->Flags != oi->Flags)
		sdbgPrint("IRP.Flags n:0x%x o:0x%x\n", ni->Flags, oi->Flags);
	// LIST_ENTRY ThreadListEntry
	showIoStatusDiffs("IRP.IoStatus", &ni->IoStatus, &oi->IoStatus); // IO_STATUS_BLOCK IoStatus
	if(ni->StackCount != oi->StackCount)
		sdbgPrint("IRP.StaCn n:0x%x o:0x%x\n", ni->StackCount, oi->StackCount);
	if(ni->CurrentLocation != oi->CurrentLocation)
		sdbgPrint("IRP.CurLo n:0x%x o:0x%x\n", ni->CurrentLocation, oi->CurrentLocation);
	if(ni->PendingReturned != oi->PendingReturned)
		sdbgPrint("IRP.PendR n:0x%x o:0x%x\n", ni->PendingReturned, oi->PendingReturned);
	if(ni->Cancel != oi->Cancel)
		sdbgPrint("IRP.Cancl n:0x%x o:0x%x\n", ni->Cancel, oi->Cancel);
	if(ni->UserBuffer != oi->UserBuffer)
		sdbgPrint("IRP.UsrB n:0x%x o:0x%x\n", ni->UserBuffer, oi->UserBuffer);
	// PIO_STATUS_BLOCK UserIosb already in main compare
	// PKEVENT UserEvent  already in main compare
	if(ni->Overlay.AsynchronousParameters.UserApcRoutine != oi->Overlay.AsynchronousParameters.UserApcRoutine)
		sdbgPrint("IRP.Overlay.ApcR   n:0x%x o:0x%x\n", ni->Overlay.AsynchronousParameters.UserApcRoutine, oi->Overlay.AsynchronousParameters.UserApcRoutine);
	if(ni->Overlay.AsynchronousParameters.UserApcContext != oi->Overlay.AsynchronousParameters.UserApcContext)
		sdbgPrint("IRP.Overlay.ApcC   n:0x%x o:0x%x\n", ni->Overlay.AsynchronousParameters.UserApcContext, oi->Overlay.AsynchronousParameters.UserApcContext);
	if(ni->Overlay.AllocationSize.QuadPart != oi->Overlay.AllocationSize.QuadPart)
		sdbgPrint("IRP.Overlay.AllocSz n:0x%I64x o:0x%I64x\n", ni->Overlay.AllocationSize.QuadPart, oi->Overlay.AllocationSize.QuadPart);
	// UIRP_TAIL Tail
	if(ni->CancelRoutine != oi->CancelRoutine)
		sdbgPrint("IRP.canR n:0x%x o:0x%x\n", ni->CancelRoutine, oi->CancelRoutine);
}

//typedef union _UIRP_TAIL { 
//	IRP_OVERLAY Overlay; // 0x0 sz:0x28
//	KAPC Apc; // 0x0 sz:0x28
//	void * CompletionKey; // 0x0 sz:0x4
//} UIRP_TAIL, *PUIRP_TAIL; // size 40

void showKevDiffs(PKEVENT ne, PKEVENT oe)
{
	if(ne->Header.Type != oe->Header.Type)
		sdbgPrint("IRP.UserEvent.Type   n:0x%x o:0x%x\n", ne->Header.Type, oe->Header.Type);
	if(ne->Header.Absolute != oe->Header.Absolute)
		sdbgPrint("IRP.UserEvent.Abs    n:0x%x o:0x%x\n", ne->Header.Absolute, oe->Header.Absolute);
	if(ne->Header.ProcessType != oe->Header.ProcessType)
		sdbgPrint("IRP.UserEvent.Ptype  n:0x%x o:0x%x\n", ne->Header.ProcessType, oe->Header.ProcessType);
	if(ne->Header.Inserted != oe->Header.Inserted)
		sdbgPrint("IRP.UserEvent.Insrt  n:0x%x o:0x%x\n", ne->Header.Inserted, oe->Header.Inserted);
	if(ne->Header.SignalState != oe->Header.SignalState)
		sdbgPrint("IRP.UserEvent.Signl  n:0x%x o:0x%x\n", ne->Header.SignalState, oe->Header.SignalState);
	// 	LIST_ENTRY WaitListHead;
}

void compareIrp(void* irp)
{
	PIRP pIrp = (PIRP)irp;
	if(memcmp(&sIrp, pIrp, sizeof(IRP)) != 0)
	{
		sdbgPrint("+++IRP struct modified!\n");
		showIrpStructDiffs(pIrp, &sIrp);
	}
	else
		sdbgPrint("---IRP struct exact\n");

	if((pIrp->UserIosb != NULL)&&(hassIos))
	{
		if(memcmp(&sIos, pIrp->UserIosb, sizeof(IO_STATUS_BLOCK)))
		{
			sdbgPrint("+++IRP UserIosb struct modified!\n");
			showIoStatusDiffs("IRP.UserIosb", pIrp->UserIosb, &sIos);
		}
		else
			sdbgPrint("---IRP UserIosb struct exact\n");
	}
	else
		sdbgPrint("!!!IRP UserIosb struct not comparable (s:%d c:0x%x)\n", hassIos, pIrp->UserIosb);

	if((pIrp->UserEvent != NULL)&&(hassKv))
	{
		if(memcmp(&sKv, pIrp->UserEvent, sizeof(KEVENT)))
		{
			sdbgPrint("+++IRP UserEvent struct modified!\n");
			showKevDiffs(pIrp->UserEvent, &sKv);
		}
		else
			sdbgPrint("---IRP UserEvent struct exact\n");
	}
	else
		sdbgPrint("!!!IRP UserEvent struct not comparable (s:%d c:0x%x)\n", hassKv, pIrp->UserEvent);
}

char* majorNames[] = {
	"Create",
	"Close",
	"Read",
	"Write",
	"QueryInfo",
	"SetInfo",
	"FlushBuf",
	"QueryVolInfo",
	"DirCtrl",
	"DevCtrl",
	"Cleanup"
};

void irpSlocShowInfo(PIO_STACK_LOCATION pios, char cur)
{

	if(pios != NULL)
	{
		sdbgPrintL("{pIrp->Tail.Overlay.CurrentStackLocation %d:0x%x}\n", cur, pios);
		if((pios->MajorFunction >= 0)&&(pios->MajorFunction < 11))
			sdbgPrintL("  MajorFunction    : 0x%02x (%s)\n", pios->MajorFunction, majorNames[pios->MajorFunction]);
		else
			sdbgPrintL("  MajorFunction    : 0x%02x (erm what!?)\n", pios->MajorFunction);
		sdbgPrintL("  MinorFunction    : 0x%02x\n", pios->MinorFunction);
		sdbgPrintL("  Flags            : 0x%02x\n", pios->Flags);
		sdbgPrintL("  Control          : 0x%02x\n", pios->Control);
		//Parameters
		switch(pios->MajorFunction)
		{
		case IOS_MJ_READ:
			sdbgPrintL("  Parameters.Read.Length      : 0x%08x\n", pios->Parameters.Read.Length);
			sdbgPrintL("  Parameters.Read.BufferOffset: 0x%08x\n", pios->Parameters.Read.BufferOffset);
			sdbgPrintL("  Parameters.Read.ByteOffset  : 0x%I64x\n", pios->Parameters.Read.ByteOffset.QuadPart);
			break;
		case IOS_MJ_DEVICECTRL:
			sdbgPrintL("  Parameters.DeviceIoControl.OutputBufferLength: 0x%08x\n", pios->Parameters.DeviceIoControl.OutputBufferLength);
			sdbgPrintL("  Parameters.DeviceIoControl.InputBuffer       : 0x%08x\n", pios->Parameters.DeviceIoControl.InputBuffer);
			sdbgPrintL("  Parameters.DeviceIoControl.InputBufferLength : 0x%08x\n", pios->Parameters.DeviceIoControl.InputBufferLength);
			sdbgPrintL("  Parameters.DeviceIoControl.IoControlCode     : 0x%08x\n", pios->Parameters.DeviceIoControl.IoControlCode);
			break;
		case IOS_MJ_CREATE:
			sdbgPrintL("  Parameters.Create.DesiredAccess : 0x%08x\n", pios->Parameters.Create.DesiredAccess);
			sdbgPrintL("  Parameters.Create.Options       : 0x%08x\n", pios->Parameters.Create.Options);
			sdbgPrintL("  Parameters.Create.FileAttributes: 0x%04x\n", pios->Parameters.Create.FileAttributes);
			sdbgPrintL("  Parameters.Create.ShareAccess   : 0x%04x\n", pios->Parameters.Create.ShareAccess);
			sdbgPrintL("  Parameters.Create.RemainingName*: 0x%08x\n", pios->Parameters.Create.RemainingName);
			break;
		case IOS_MJ_WRITE:
			sdbgPrintL("  Parameters.Write.Length      : 0x%08x\n", pios->Parameters.Write.Length);
			sdbgPrintL("  Parameters.Write.BufferOffset: 0x%08x\n", pios->Parameters.Write.BufferOffset);
			sdbgPrintL("  Parameters.Write.ByteOffset  : 0x%I64x\n", pios->Parameters.Write.ByteOffset.QuadPart);
			break;
		case IOS_MJ_DIRECTORYCTRL:
			sdbgPrintL("  Parameters.QueryDirectory.Length  : 0x%08x\n", pios->Parameters.QueryDirectory.Length);
			sdbgPrintL("  Parameters.QueryDirectory.FileName: 0x%08x\n", pios->Parameters.QueryDirectory.FileName);
//			sdbgPrintL("  Parameters.QueryDirectory.FileName: 0x%08x (%s)\n", pios->Parameters.QueryDirectory.FileName, pios->Parameters.QueryDirectory.FileName->Buffer);
			sdbgPrintL("  Parameters.Others.Argument3       : 0x%08x\n", pios->Parameters.Others.Argument3);
			sdbgPrintL("  Parameters.Others.Argument4       : 0x%08x\n", pios->Parameters.Others.Argument4);
			break;
		case IOS_MJ_QUERYINFO:
			sdbgPrintL("  Parameters.QueryFile.Length              : 0x%08x\n", pios->Parameters.QueryFile.Length);
			sdbgPrintL("  Parameters.QueryFile.FileInformationClass: 0x%08x\n", pios->Parameters.QueryFile.FileInformationClass);
			sdbgPrintL("  Parameters.Others.Argument3              : 0x%08x\n", pios->Parameters.Others.Argument3);
			sdbgPrintL("  Parameters.Others.Argument4              : 0x%08x\n", pios->Parameters.Others.Argument4);
			break;
		case IOS_MJ_QUERYVOLINFO:
			sdbgPrintL("  Parameters.QueryVolume.Length            : 0x%08x\n", pios->Parameters.QueryVolume.Length);
			sdbgPrintL("  Parameters.QueryVolume.FsInformationClass: 0x%08x\n", pios->Parameters.QueryVolume.FsInformationClass);
			sdbgPrintL("  Parameters.Others.Argument3              : 0x%08x\n", pios->Parameters.Others.Argument3);
			sdbgPrintL("  Parameters.Others.Argument4              : 0x%08x\n", pios->Parameters.Others.Argument4);
			break;
		case IOS_MJ_FLUSHBUF:
		case IOS_MJ_CLEANUP:
		case IOS_MJ_CLOSE:
		default:
			sdbgPrintL("  Parameters.Others.Argument1  : 0x%08x\n", pios->Parameters.Others.Argument1);
			sdbgPrintL("  Parameters.Others.Argument2  : 0x%08x\n", pios->Parameters.Others.Argument2);
			sdbgPrintL("  Parameters.Others.Argument3  : 0x%08x\n", pios->Parameters.Others.Argument3);
			sdbgPrintL("  Parameters.Others.Argument4  : 0x%08x\n", pios->Parameters.Others.Argument4);
			break;
		}
		if(pios->FileObject != NULL)
		{
			PFILE_OBJECT pfo = pios->FileObject;
			sdbgPrintL("  {pIrp->Tail.Overlay.CurrentStackLocation->FileObject 0x%x}\n", pios->FileObject);
			sdbgPrintL("    Type             : 0x%04x\n", pfo->Type);
			sdbgPrintL("    Flags            : 0x%02x\n", pfo->Flags);
			sdbgPrintL("    Flags2           : 0x%02x\n", pfo->Flags2);
			sdbgPrintL("    FsContext        : 0x%08x\n", pfo->FsContext);
			sdbgPrintL("    FsContext2       : 0x%08x\n", pfo->FsContext2);
			sdbgPrintL("    FinalStatus      : 0x%08x\n", pfo->FinalStatus);
			sdbgPrintL("    RelatedFileObject: 0x%08x\n", pfo->RelatedFileObject);
			sdbgPrintL("    CurrentByteOffset: 0x%I64x\n", pfo->CurrentByteOffset.QuadPart);
			sdbgPrintL("    CompletionContext: 0x%08x\n", pfo->CompletionContext);
			sdbgPrintL("    LockCount        : 0x%08x\n", pfo->LockCount);
			sdbgPrintL("    Lock.Header.Type        : 0x%02x\n", pfo->Lock.Header.Type);
			sdbgPrintL("    Lock.Header.Absolute    : 0x%02x\n", pfo->Lock.Header.Absolute);
			sdbgPrintL("    Lock.Header.ProcessType : 0x%02x\n", pfo->Lock.Header.ProcessType);
			sdbgPrintL("    Lock.Header.Inserted    : 0x%02x\n", pfo->Lock.Header.Inserted);
			sdbgPrintL("    Lock.Header.SignalState : 0x%08x\n", pfo->Lock.Header.SignalState);
			sdbgPrintL("    Lock.Header.WaitListHead.Blink: 0x%08x\n", pfo->Lock.Header.WaitListHead.Blink);
			sdbgPrintL("    Lock.Header.WaitListHead.Flink: 0x%08x\n", pfo->Lock.Header.WaitListHead.Flink);
			sdbgPrintL("    Event.Header.Type       : 0x%02x\n", pfo->Event.Header.Type);
			sdbgPrintL("    Event.Header.Absolute   : 0x%02x\n", pfo->Event.Header.Absolute);
			sdbgPrintL("    Event.Header.ProcessType: 0x%02x\n", pfo->Event.Header.ProcessType);
			sdbgPrintL("    Event.Header.Inserted   : 0x%02x\n", pfo->Event.Header.Inserted);
			sdbgPrintL("    Event.Header.SignalState: 0x%08x\n", pfo->Event.Header.SignalState);
			sdbgPrintL("    Event.Header.WaitListHead.Blink: 0x%08x\n", pfo->Event.Header.WaitListHead.Blink);
			sdbgPrintL("    Event.Header.WaitListHead.Flink: 0x%08x\n", pfo->Event.Header.WaitListHead.Flink);
			sdbgPrintL("    ProcessListEntry.Blink   : 0x%08x\n", pfo->ProcessListEntry.Blink);
			sdbgPrintL("    ProcessListEntry.Flink   : 0x%08x\n", pfo->ProcessListEntry.Flink);
			sdbgPrintL("    FileSystemListEntry.Blink: 0x%08x\n", pfo->FileSystemListEntry.Blink);
			sdbgPrintL("    FileSystemListEntry.Flink: 0x%08x\n", pfo->FileSystemListEntry.Flink);
			sdbgPrintL("    IoPriority       : 0x%02x\n", pfo->IoPriority);
		}
		else
			sdbgPrintL("  *FileObject      : 0x(NULL)\n");

		sdbgPrintL("  CompletionRoutine: 0x%08x\n", pios->CompletionRoutine);
		sdbgPrintL("  Context          : 0x%08x\n", pios->Context);
	}
	else
		sdbgPrintL("{pIrp->Tail.Overlay.CurrentStackLocation 0x(NULL)}\n");
}

void irpShow(void* irp)
{
	PIRP pIrp = (PIRP)irp;
	sdbgLockPush();
	sdbgPrintL("\nShowing IRP 0x%08x\n", irp);
	sdbgPrintL("Type : 0x%04x\n", pIrp->Type);
	sdbgPrintL("Size : 0x%04x\n", pIrp->Size);
	sdbgPrintL("Flags: 0x%08x\n", pIrp->Flags);
	sdbgPrintL("ThreadListEntry.Blink: 0x%08x\n", pIrp->ThreadListEntry.Blink);
	sdbgPrintL("ThreadListEntry.Flink: 0x%08x\n", pIrp->ThreadListEntry.Flink);
	sdbgPrintL("IoStatus.st.Status  : 0x%08x\n", pIrp->IoStatus.st.Status);
	sdbgPrintL("IoStatus.Information: 0x%08x\n", pIrp->IoStatus.Information);
	sdbgPrintL("StackCount     : 0x%02x\n", pIrp->StackCount);
	sdbgPrintL("CurrentLocation: 0x%02x\n", pIrp->CurrentLocation);
	sdbgPrintL("PendingReturned: 0x%02x\n", pIrp->PendingReturned);
	sdbgPrintL("Cancel         : 0x%02x\n", pIrp->Cancel);
	sdbgPrintL("UserBuffer     : 0x%08x\n", pIrp->UserBuffer);

	if(pIrp->UserIosb != NULL)
	{
		sdbgPrintL("UserIosb             : 0x%08x\n", pIrp->UserIosb);
		sdbgPrintL("UserIosb->st.Status  : 0x%08x\n", pIrp->UserIosb->st.Status);
		sdbgPrintL("UserIosb->Information: 0x%08x\n", pIrp->UserIosb->Information);
	}
	else
		sdbgPrintL("*UserIosb: 0x(NULL)\n");
	if(pIrp->UserEvent != NULL)
	{
		sdbgPrintL("UserEvent                    : 0x%08x\n", pIrp->UserEvent);
		sdbgPrintL("UserEvent->Header.Type       : 0x%02x\n", pIrp->UserEvent->Header.Type);
		sdbgPrintL("UserEvent->Header.Absolute   : 0x%02x\n", pIrp->UserEvent->Header.Absolute);
		sdbgPrintL("UserEvent->Header.ProcessType: 0x%02x\n", pIrp->UserEvent->Header.ProcessType);
		sdbgPrintL("UserEvent->Header.Inserted   : 0x%02x\n", pIrp->UserEvent->Header.Inserted);
		sdbgPrintL("UserEvent->Header.SignalState: 0x%08x\n", pIrp->UserEvent->Header.SignalState);
		sdbgPrintL("UserEvent->Header.WaitListHead.Blink: 0x%08x\n", pIrp->UserEvent->Header.WaitListHead.Blink);
		sdbgPrintL("UserEvent->Header.WaitListHead.Flink: 0x%08x\n", pIrp->UserEvent->Header.WaitListHead.Flink);
	}
	else
		sdbgPrintL("*UserEvent: 0x(NULL)\n");
	sdbgPrintL("Overlay.AsynchronousParameters.UserApcRoutine: 0x%08x\n", pIrp->Overlay.AsynchronousParameters.UserApcRoutine);
	sdbgPrintL("Overlay.AsynchronousParameters.UserApcContext: 0x%08x\n", pIrp->Overlay.AsynchronousParameters.UserApcContext);
	sdbgPrintL("Overlay.AllocationSize.QuadPart: 0x%I64x\n", pIrp->Overlay.AllocationSize.QuadPart);

	sdbgPrintL("pIrp->Tail.Overlay.DriverContext[0]  : 0x%08x\n", pIrp->Tail.Overlay.DriverContext[0]);
	sdbgPrintL("pIrp->Tail.Overlay.DriverContext[1]  : 0x%08x\n", pIrp->Tail.Overlay.DriverContext[1]);
	sdbgPrintL("pIrp->Tail.Overlay.DriverContext[2]  : 0x%08x\n", pIrp->Tail.Overlay.DriverContext[2]);
	sdbgPrintL("pIrp->Tail.Overlay.DriverContext[3]  : 0x%08x\n", pIrp->Tail.Overlay.DriverContext[3]);
	sdbgPrintL("pIrp->Tail.Overlay.LockedBufferLength: 0x%08x\n", pIrp->Tail.Overlay.LockedBufferLength);
	sdbgPrintL("pIrp->Tail.Overlay.Thread            : 0x%08x\n", pIrp->Tail.Overlay.Thread);
	sdbgPrintL("pIrp->Tail.Overlay.ListEntry.Blink   : 0x%08x\n", pIrp->Tail.Overlay.ListEntry.Blink);
	sdbgPrintL("pIrp->Tail.Overlay.ListEntry.Flink   : 0x%08x\n", pIrp->Tail.Overlay.ListEntry.Flink);

	// 	UIRP_TAIL Tail; // 0x30 sz:0x28
	if(pIrp->Tail.Overlay.CurrentStackLocation != NULL)
	{
		char cnt = pIrp->StackCount;
		char cur = pIrp->CurrentLocation;
		if(cur > cnt)
			sdbgPrintL("{pIrp->Tail.Overlay.CurrentStackLocation 0x(WHOOPS!) it appears this stack has been completed?}\n");
		else
		{
			PIO_STACK_LOCATION pios = pIrp->Tail.Overlay.CurrentStackLocation; // stack location grows in a negative direction
			while(cur <= cnt)
			{
				irpSlocShowInfo(pios, cur);
				pios = (PIO_STACK_LOCATION)((DWORD)pios+sizeof(IO_STACK_LOCATION));
				cur++;
			}
		}
	}
	else
		sdbgPrintL("{pIrp->Tail.Overlay.CurrentStackLocation 0x(NULL)}\n");



	sdbgPrintL("pIrp->Tail.Overlay.OriginalFileObject: 0x%08x\n", pIrp->Tail.Overlay.OriginalFileObject);


	sdbgPrintL("CancelRoutine  : 0x%08x\n", pIrp->CancelRoutine);
	sdbgLockPop();
}

void irpShowBufLockedUnaligned(BYTE* buf, DWORD len)
{
	DWORD i;
	for(i = 0; i < len; i++)
		sdbgPrintL("%02x ", buf[i]);
	sdbgPrintL("\n");
}

void irpShowBuf(void* outBuffer, DWORD dwOutLen, void* inputBuffer, DWORD dwInLen)
{
	sdbgLockPush();

	sdbgPrintL("in : 0x%08x len: 0x%x\n", inputBuffer, dwInLen);
	if(inputBuffer != NULL)
		irpShowBufLockedUnaligned((BYTE*)inputBuffer, dwInLen);
	sdbgPrintL("out: 0x%08x len: 0x%x\n", outBuffer, dwOutLen);
	if(outBuffer != NULL)
		irpShowBufLockedUnaligned((BYTE*)outBuffer, dwOutLen);
	sdbgLockPop();
}

#define TOTAL_TYPES	4
DWORD obtypes[] = {
	OBJ_TYP_SYMBLINK,
	OBJ_TYP_DEVICE,
	0x0,
	OBJ_TYP_DIRECTORY
};

static int iLevel = 0;
VOID DumpAllObjects(char* pszDir)
{
	int i, typeCount = 0;
	STRING UName;
	BOOL restart = FALSE;
	HANDLE hObj, hLink;
	NTSTATUS ntStatus, ntStatusTmp;
	OBJECT_ATTRIBUTES ObjectAttributes;
	POBJDIR_INFORMATION DirObjInformation;
	CHAR szData[256];
	CHAR szLinkName[256];
	CHAR tabs[32];
	CHAR dirTabs[32];
	DWORD dw, index;
	DirObjInformation = (POBJDIR_INFORMATION)szData;
	ZeroMemory(tabs, 32);
	ZeroMemory(dirTabs, 32);
	if(iLevel == 0)
	{
		strcat(tabs, "    ");
	}
	else
	{
		for(i=0; i<iLevel; i++)
			strcat(tabs, "    ");
		for(i=0; i<(iLevel-1); i++)
			strcat(dirTabs, "    ");
	}
	// open directory object
	RtlInitAnsiString(&UName, pszDir);
	InitializeObjectAttributes (&ObjectAttributes, &UName, OBJ_CASE_INSENSITIVE, NULL);

	ntStatus = NtOpenDirectoryObject(&hObj, &ObjectAttributes);

	if(NT_SUCCESS(ntStatus))
	{
		sdbgPrint("%s'%s' (directory)\n", dirTabs, pszDir);
		index = 0; // start index

		while(NT_SUCCESS(ntStatus))
		{
			ZeroMemory(szData, sizeof(szData));
			DirObjInformation = (POBJDIR_INFORMATION)&szData;
			ntStatus = NtQueryDirectoryObject(hObj, szData, sizeof(szData), restart, &index, &dw);
			restart = FALSE;
			//DbgPrint("status %08x\n", ntStatus);
			if((ntStatus == 0x8000001A)) // STATUS_NO_MORE_ENTRIES
			{
				if(typeCount < (TOTAL_TYPES-1))
				{
					index = 0;
					ntStatus = 0;
					typeCount++;
					restart = TRUE;
				}
			}
			else if(NT_SUCCESS(ntStatus))
			{
				//DbgPrint("index %08x\n", index);
				if(pszDir[strlen(pszDir)-1] != '\\')
					sprintf_s(szLinkName, 256, "%s\\%s", pszDir, DirObjInformation->Name.Buffer);
				else
					sprintf_s(szLinkName, 256, "%s%s", pszDir, DirObjInformation->Name.Buffer);
				//DbgPrint("checking type %x index %x typecount %d\n", obtypes[typeCount], index, typeCount);

				if((DirObjInformation->Type == obtypes[typeCount])&&(obtypes[typeCount] == OBJ_TYP_SYMBLINK))
				{
					STRING symb;
					sdbgPrint("%s'%s' ", tabs, szLinkName);
					RtlInitAnsiString(&symb, szLinkName);
					InitializeObjectAttributes(&ObjectAttributes, &symb, OBJ_CASE_INSENSITIVE, NULL);
					ntStatusTmp = NtOpenSymbolicLinkObject(&hLink, &ObjectAttributes);
					if(NT_SUCCESS(ntStatusTmp))
					{
						STRING LName;
						char outstr[256];
						LName.Buffer = outstr;
						LName.Length = 0;
						LName.MaximumLength = 256;
						memset(outstr, 0x0, 256);
						ntStatusTmp = NtQuerySymbolicLinkObject(hLink, &LName, &dw);
						if(NT_SUCCESS(ntStatusTmp))
							sdbgPrint("linked to: '%s' (SymbolicLink)\n", outstr);
						else
							sdbgPrint("\n    NtQuerySymbolicLinkObject fail = 0x%lX\n\n", ntStatusTmp);
						NtClose(hLink);
					}
					else
						sdbgPrint("\n    NtOpenSymboliclinkObject fail = 0x%lX\n\n", ntStatusTmp);
				}
				else if((DirObjInformation->Type == obtypes[typeCount])&&(obtypes[typeCount] == OBJ_TYP_DEVICE))
				{
					sdbgPrint("%s'%s' (Device)\n", tabs, szLinkName);
				}
				else if((DirObjInformation->Type == obtypes[typeCount])&&(obtypes[typeCount] == OBJ_TYP_DIRECTORY))
				{
					iLevel++;
					DumpAllObjects(szLinkName);
					iLevel--;
				}
				else if(obtypes[typeCount] == 0x0)
				{
					DWORD tt = DirObjInformation->Type;
					if((tt != OBJ_TYP_DIRECTORY)&&(tt != OBJ_TYP_DEVICE)&&(tt != OBJ_TYP_SYMBLINK))
					{
						if(DirObjInformation->Type == OBJ_TYP_EVENT)
							sdbgPrint("%s'%s' (Event)\n", tabs, szLinkName);
						else if(DirObjInformation->Type == OBJ_TYP_DEBUG)
							sdbgPrint("%s'%s' (Debug)\n", tabs, szLinkName);
						else
						{
							sdbgPrint("%s**** '%s' (unknown %08x-'%c%c%c%c')\n", tabs, szLinkName, tt, tt&0xFF, (tt>>8)&0xFF, (tt>>16)&0xFF, (tt>>24)&0xFF);
						}
					}
				}
			}
			else
			{
				sdbgPrint("NtQueryDirectoryObject = 0x%lX (%S)\n", ntStatus, pszDir);
			}
		}

		NtClose(hObj);
	}
	else
	{
		sdbgPrint("NtOpenDirectoryObject = 0x%lX (%S)\n", ntStatus, pszDir);
	}
}

VOID tfShowObjs(VOID)
{
	iLevel = 0;
	DumpAllObjects("\\Device\\");
}
