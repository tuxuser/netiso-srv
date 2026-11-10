#ifndef __DVDHOOK_H
#define __DVDHOOK_H

typedef struct _CACHED_MEDIA_INFO {
	BOOL hasDiskInfo;
	BYTE mediaId[0x10];
	DWORD dvdLayerInfo[3];
	DWORD HashTableIndexLBA;
	BYTE  HashTableIndexHash[0x14];
}CACHED_MEDIA_INFO, *PCACHED_MEDIA_INFO;

long DvdHookStartup(void);

#endif // __DVDHOOK_H

// below are some of the hooks done to gather info
// 	if(Code == 0x24800) // sense
// 	{
// 		sdbgPrint("IoS bef inf: 0x%x st: 0x%x\n", pvirp->IoStatus.Information, pvirp->IoStatus);
// 		storeIrp(pirp);
// 	}
// 	if(Code == 0x2404c) // get geometry
// 	{
// 		PDISK_GEOMETRY geo = (PDISK_GEOMETRY)pvirp->UserBuffer;
// 		sdbgPrint("IoS bef inf: 0x%x st: 0x%x\n", pvirp->IoStatus.Information, pvirp->IoStatus);
// 		sdbgPrint("userbuf: 0x%x\n", pvirp->UserBuffer);
// 		if(geo != NULL)
// 		{
// 			sdbgPrint("Sectors: 0x%x BytesPerSector: 0x%x\n", geo->Sectors, geo->BytesPerSector);
// 		}
// 		sdbgPrint("UserIosb->Information: 0x%x\n", pvirp->UserIosb->Information);
// 		sdbgPrint("UserIosb->st.Pointer : 0x%x\n", pvirp->UserIosb->st.Pointer);
// 		sdbgPrint("UserIosb->st.Status  : 0x%x\n", pvirp->UserIosb->st.Status);
// 		storeIrp(pirp);
// 	}
// 	if(Code == 0x335142) // IOCTL_DVD_READ_STRUCTURE obl: 0x18 ib: 0x7a09f950 ibl: 0x18
// 	{
// 		PDVD_READ_STRUCTURE drs = (PDVD_READ_STRUCTURE)pios->Parameters.DeviceIoControl.InputBuffer;
// 		sdbgLockPush();
// 		sdbgPrintL("read structure\n");
// 		sdbgPrint("IoS bef inf: 0x%x st: 0x%x\n", pvirp->IoStatus.Information, pvirp->IoStatus);
// 		sdbgPrintL("BlockByteOffset: 0x%I64x\n", drs->BlockByteOffset.QuadPart);
// 		sdbgPrintL("Format         : %d\n", drs->Format);
// 		sdbgPrintL("SessionId      : 0x%08x\n", drs->SessionId);
// 		sdbgPrintL("LayerNumber    : 0x%02x\n", drs->LayerNumber);
// 		sdbgPrintL("userbuf        : 0x%x\n", pvirp->UserBuffer);
// 		sdbgLockPop();
// 	}

// 	if(Code == 0x335142) // IOCTL_DVD_READ_STRUCTURE obl: 0x18 ib: 0x7a09f950 ibl: 0x18
// 	{
// 		DWORD i;
// 		DWORD* ub = (DWORD*)pvirp->UserBuffer;
// 		PDVD_READ_STRUCTURE drs = (PDVD_READ_STRUCTURE)pios->Parameters.DeviceIoControl.InputBuffer;
// 		sdbgLockPush();
// 		sdbgPrintL("IoS aft inf: 0x%x st: 0x%x ret: 0x%x\n", pvirp->IoStatus.Information, pvirp->IoStatus, ret);
// 		sdbgPrintL("userbuf        : 0x%x\n", pvirp->UserBuffer);
// 		for(i = 0; i < 6; i++)
// 			sdbgPrintL("[%d]%08x\n", i, ub[i]);		
// 
// 		sdbgLockPop();
// 	}
// 	if(Code == 0x240cc) // SataCdRomX360Media	obl: 0x4 ib: 0x0 ibl: 0x0
// 	{
// 		DWORD* ub = (DWORD*)pvirp->UserBuffer;
// 		sdbgPrint("IoS aft inf: 0x%x st: 0x%x ret: 0x%x\n", pvirp->IoStatus.Information, pvirp->IoStatus, ret);
// 		sdbgPrint("SataCdRomX360Media: 0x%08x\n", *ub);
// 
// 	}
// 	if(Code == 0x24800)
// 	{
// 		sdbgPrint("IoS aft inf: 0x%x st: 0x%x ret: 0x%x\n", pvirp->IoStatus.Information, pvirp->IoStatus, ret);
// 		compareIrp(pirp);
// 	}
// 	if(Code == 0x2404c)
// 	{
// 		PDISK_GEOMETRY geo = (PDISK_GEOMETRY)pvirp->UserBuffer;
// 		sdbgPrint("IoS aft inf: 0x%x st: 0x%x ret: 0x%x\n", pvirp->IoStatus.Information, pvirp->IoStatus, ret);
// 		sdbgPrint("userbuf: 0x%x\n", pvirp->UserBuffer);
// 		if(geo != NULL)
// 		{
// 			sdbgPrint("Sectors: 0x%x BytesPerSector: 0x%x\n", geo->Sectors, geo->BytesPerSector);
// 		}
// 		sdbgPrint("UserIosb->Information: 0x%x\n", pvirp->UserIosb->Information);
// 		sdbgPrint("UserIosb->st.Pointer : 0x%x\n", pvirp->UserIosb->st.Pointer);
// 		sdbgPrint("UserIosb->st.Status  : 0x%x\n", pvirp->UserIosb->st.Status);
// 		compareIrp(pirp);
// 	}

// ROUND2
//sdbgPrint("IoCtl: 0x%x obl: 0x%x ib: 0x%x ibl: 0x%x\n", Code, pios->Parameters.DeviceIoControl.OutputBufferLength, pios->Parameters.DeviceIoControl.InputBuffer, pios->Parameters.DeviceIoControl.InputBufferLength);
//sdbgPrint("Dev: 0x%x Func: 0x%x Meth: 0x%x Accs: 0x%x\n", CTL_CODE_DEVTYPE(Code), CTL_CODE_FUNCTION(Code), CTL_CODE_METHOD(Code), CTL_CODE_ACCESS(Code));
//if((Code == 0x24084)||(Code == 0x240c4)) // SataCdRomStartSetSpindleSpeed || SataCdRomDoUninterruptableReads
//{
//	irpShowBuf(pvirp->UserBuffer, pios->Parameters.DeviceIoControl.OutputBufferLength, pios->Parameters.DeviceIoControl.InputBuffer, pios->Parameters.DeviceIoControl.InputBufferLength);
//}
//else if(Code == 0x240D0)
//{
//	//	0x240D0		Dev: 0x2 Func: 0x34 Meth: 0x0 Accs: 0x1			SataCdRomFwcr							obl: 0x0 ib: 0x3a162a30 ibl: 0x28
//	irpShowBuf(pvirp->UserBuffer, pios->Parameters.DeviceIoControl.OutputBufferLength, pios->Parameters.DeviceIoControl.InputBuffer, pios->Parameters.DeviceIoControl.InputBufferLength);
//}
//else if(Code == 0x240E4)
//{
//	//	0x240E4 	Dev: 0x2  Func: 0x39  Meth: 0x0 Accs: 0x1		SataCdRomStartCheckVerify				obl: 0x12 ib: 0x0 ibl: 0x0			SCSI_CMD_TEST_UNIT_READY
//	irpShowBuf(pvirp->UserBuffer, pios->Parameters.DeviceIoControl.OutputBufferLength, pios->Parameters.DeviceIoControl.InputBuffer, pios->Parameters.DeviceIoControl.InputBufferLength);
//}

//if((Code == 0x24084)||(Code == 0x240c4)) // SataCdRomStartSetSpindleSpeed || SataCdRomDoUninterruptableReads
//{
//	sdbgPrint("IoCtl: 0x%x after inf: 0x%x st: 0x%x ret: 0x%x\n", Code, pvirp->IoStatus.Information, pvirp->IoStatus, ret);
//	irpShowBuf(pvirp->UserBuffer, pios->Parameters.DeviceIoControl.OutputBufferLength, pios->Parameters.DeviceIoControl.InputBuffer, pios->Parameters.DeviceIoControl.InputBufferLength);
//}
//else if(Code == 0x240D0)
//{
//	//	0x240D0		Dev: 0x2 Func: 0x34 Meth: 0x0 Accs: 0x1			SataCdRomFwcr							obl: 0x0 ib: 0x3a162a30 ibl: 0x28
//	sdbgPrint("IoCtl: 0x%x after inf: 0x%x st: 0x%x ret: 0x%x\n", Code, pvirp->IoStatus.Information, pvirp->IoStatus, ret);
//	irpShowBuf(pvirp->UserBuffer, pios->Parameters.DeviceIoControl.OutputBufferLength, pios->Parameters.DeviceIoControl.InputBuffer, pios->Parameters.DeviceIoControl.InputBufferLength);
//}
//else if(Code == 0x240E4)
//{
//	//	0x240E4 	Dev: 0x2  Func: 0x39  Meth: 0x0 Accs: 0x1		SataCdRomStartCheckVerify				obl: 0x12 ib: 0x0 ibl: 0x0			SCSI_CMD_TEST_UNIT_READY
//	sdbgPrint("IoCtl: 0x%x after inf: 0x%x st: 0x%x ret: 0x%x\n", Code, pvirp->IoStatus.Information, pvirp->IoStatus, ret);
//	irpShowBuf(pvirp->UserBuffer, pios->Parameters.DeviceIoControl.OutputBufferLength, pios->Parameters.DeviceIoControl.InputBuffer, pios->Parameters.DeviceIoControl.InputBufferLength);
//}
//else if(Code == 0x240cc) // SataCdRomX360Media	obl: 0x4 ib: 0x0 ibl: 0x0
//{
// 	DWORD* ub = (DWORD*)pvirp->UserBuffer;
// 	sdbgPrint("IoS aft inf: 0x%x st: 0x%x ret: 0x%x\n", pvirp->IoStatus.Information, pvirp->IoStatus, ret);
// 	sdbgPrint("SataCdRomX360Media: 0x%08x\n", *ub);
//}
