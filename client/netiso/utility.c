#include <xtl.h>
#include <stdio.h>
#include "xkelib.h"
#include "utility.h"

// #define DISABLE_DBG		1

DWORD __declspec(naked) HvxSetState(DWORD key, DWORD mode)
{ 
	__asm {
		li      r0, 0x0
		sc
		blr
	}
}


BYTE __declspec(naked) KeGetCurrentIrql(VOID)
{ 
	__asm {
		lbz r3, 0x18(r13)
			blr
	}
}

void dbgPut(DWORD ch)
{
	while((__loadvolatilewordbytereverse(0, DBG_SERIAL_STS)&0xFFFFFFFC) != 0)
	{
		YieldProcessor(); // db16cyc
		KeStallExecutionProcessor(0xA);
	}
	while((__loadvolatilewordbytereverse(0, DBG_SERIAL_STS)&2) == 0)
	{
		YieldProcessor(); // db16cyc
		KeStallExecutionProcessor(0xA);
	}
	__storevolatilewordbytereverse(ch, 0, DBG_SERIAL_XMIT);
	__eieio();
}

void dbgOutputString(PBYTE data, WORD len)
{
	WORD i;
	for(i = 0; i < len; i++)
	{
		if((data[i]&0xFF)== 0xA) // add in cr for missing linefeeds
			dbgPut(0xD);
		dbgPut((data[i]&0xFF));
	}
}

DUMMY_MUTEX g_dbgMutex = {
	EventSynchronizationObject, // BYTE Type
	0, // BYTE SpinCount
	4, // BYTE Size
	0, // BYTE Inserted
	0, // DWORD SignalState
	(LIST_ENTRY*)(((DWORD)&g_dbgMutex)+8), // LIST_ENTRY WaitListHead
	(LIST_ENTRY*)(((DWORD)&g_dbgMutex)+8),
	(DWORD)-1, // LONG LockCount
	0, // LONG RecursionCount
	0 // HANDLE OwningThread
};

void sdbgLockPush(void)
{
#ifndef DISABLE_DBG
	KeEnterCriticalRegion();
	RtlEnterCriticalSection((PRTL_CRITICAL_SECTION)&g_dbgMutex);
#endif
}

void sdbgLockPop(void)
{
#ifndef DISABLE_DBG
	RtlLeaveCriticalSection((PRTL_CRITICAL_SECTION)&g_dbgMutex);
	KeLeaveCriticalRegion();
#endif
}

void sdbgPrintL(const char* s, ...)
{
#ifndef DISABLE_DBG
	va_list argp;
	WORD len;
	char temp[512];
	// 	BYTE irqSave;

	va_start(argp, s);
	RtlVsnprintf(temp, 512, s, argp);
	va_end(argp);
	len = (WORD)(strlen(temp)&0xFFFF);
	dbgOutputString((PBYTE)temp, len);
#endif
}

void sdbgPrint(const char* s, ...)
{
#ifndef DISABLE_DBG
	va_list argp;
	WORD len;
	char temp[512];
// 	BYTE irqSave;

	va_start(argp, s);
	RtlVsnprintf(temp, 512, s, argp);
	va_end(argp);
	len = (WORD)(strlen(temp)&0xFFFF);
// 	irqSave = KfRaiseIrql(0x7C);
// 	KeAcquireSpinLockAtRaisedIrql(&g_DbgLock);
	sdbgLockPush();
	dbgOutputString((PBYTE)temp, len);
	sdbgLockPop();
// 	KeReleaseSpinLockFromRaisedIrql(&g_DbgLock);
// 	KfLowerIrql(irqSave);
#endif
}

DWORD patchInNop(PDWORD addr)
{
	DWORD ret = 0;
	if(addr != NULL)
	{
		ret = addr[0];
		addr[0] = ASM_NOP;
		doSync(addr);
	}
	return ret;
}

DWORD patchInDword(PDWORD addr, DWORD val)
{
	DWORD ret = 0;
	if((addr != NULL) && (((DWORD)addr & 3) == 0))
	{
		ret = addr[0];
		addr[0] = val;
		doSync(addr);
	}
	return ret;
}

HRESULT doDeleteLink(const char* szDrive, const char* sysStr)
{
	STRING LinkName;
	CHAR szDestinationDrive[MAX_PATH];
	RtlSnprintf(szDestinationDrive, MAX_PATH, sysStr, szDrive);
	RtlInitAnsiString(&LinkName, szDestinationDrive);
	return ObDeleteSymbolicLink(&LinkName);
}

HRESULT deleteLink(const char* szDrive, BOOL both)
{
	HRESULT res = -1;
	if(both)
	{
		res = doDeleteLink(szDrive, OBJ_SYS_STRING);
		res |= doDeleteLink(szDrive, OBJ_USR_STRING);
	}
	else
	{
		if(KeGetCurrentProcessType() == PROC_SYSTEM)
			res = doDeleteLink(szDrive, OBJ_SYS_STRING);
		else
			res = doDeleteLink(szDrive, OBJ_USR_STRING);
	}
	return res;
}

HRESULT doMountPath(const char* szDrive, const char* szDevice, const char* sysStr)
{
	HRESULT res = -1;
	if(sysStr != NULL)
	{
		STRING DeviceName;
		STRING LinkName;
		CHAR szDestinationDrive[MAX_PATH];
		//DbgPrint("doMountPath: %08x %08x %08x\n", szDrive, szDevice, sysStr);
		//DbgPrint("doMountPath: sysStr %s\n", sysStr);
		//DbgPrint("doMountPath: szDevice %s\n", szDevice);
		//DbgPrint("doMountPath: szDrive %s\n", szDrive);
		//sprintf_s(szDestinationDrive, MAX_PATH, sysStr, szDrive);
		strcpy(szDestinationDrive, sysStr);
		strcat(szDestinationDrive, szDrive);
		//DbgPrint("doMountPath: szDestinationDrive %s\n", szDestinationDrive);
		RtlInitAnsiString(&DeviceName, szDevice);
		RtlInitAnsiString(&LinkName, szDestinationDrive);
		ObDeleteSymbolicLink(&LinkName);
		res = (HRESULT)ObCreateSymbolicLink(&LinkName, &DeviceName);
	}
	return res;
}
// #define OBJ_SYS_STRING	"\\System??\\%s"
// #define OBJ_USR_STRING	"\\??\\%s"
// #define OBJ_SYS_PATH	"\\System??\\"
// #define OBJ_USR_PATH	"\\??\\"

static CHAR OSYS_STR[] = OBJ_SYS_PATH;
static CHAR OUSR_STR[] = OBJ_USR_PATH;
HRESULT MountPath(const char* szDrive, const char* szDevice, BOOL both)
{
	HRESULT res = -1;
	if((szDrive != NULL)&&(szDevice != NULL))
	{
		if(both)
		{
			res = doMountPath(szDrive, szDevice, OSYS_STR);
			res |= doMountPath(szDrive, szDevice, OUSR_STR);
		}
		else
		{
			if(KeGetCurrentProcessType() == PROC_SYSTEM)
				res = doMountPath(szDrive, szDevice, OSYS_STR);
			else
				res = doMountPath(szDrive, szDevice, OUSR_STR);
		}
	}
	return res;
}

DWORD UnmountContainer(const char* szDrive)
{
	CHAR szMountPath[MAX_PATH];
	sprintf_s(szMountPath,MAX_PATH,"\\??\\%s",szDrive);
	//DbgPrint("umt path: %s\n", szMountPath);
	return  XamContentClose(szMountPath,0);
}

DWORD MountContainer(const char* szDrive, const char* szDevice, const char* szPath)
{
	CHAR szMountPath[MAX_PATH];
	sprintf_s(szMountPath,MAX_PATH,"\\??\\%s\\%s", szDevice, szPath);
	//DbgPrint("mounting: '%s' to: '%s'\n", szMountPath, szDrive);
	return XamContentOpenFile(0xFE, szDrive, szMountPath, 0x4000043,0,0,0);
}

NTSTATUS getSymbolicPath(char* path, char* outstr)
{
	STRING oStr, unistr;
	NTSTATUS retsts;
	HANDLE objHandle;
	OBJECT_ATTRIBUTES oob;
	ULONG retlen;
	RtlInitAnsiString(&oStr, path);
	unistr.Length = 255;
	unistr.MaximumLength = 256;
	unistr.Buffer = outstr;
	InitializeObjectAttributes(&oob, &oStr, OBJ_CASE_INSENSITIVE, 0);
	memset(outstr, 0x0, 256);
	retsts = NtOpenSymbolicLinkObject(&objHandle, &oob);
	//DbgPrint("getpath: open symbolic link returns %x\n", retsts);
	if(retsts == 0)
	{
		retsts = NtQuerySymbolicLinkObject(objHandle, &unistr, &retlen);
		//DbgPrint("getpath: Query symbolic link returns %x\n", retsts);
		//if(retsts == 0)
		//	DbgPrint("getpath: %s link is: %s\n", path, outstr);
	}
	CloseHandle(objHandle);
	return retsts;
}

PIMAGE_EXPORT_ADDRESS_TABLE getModuleEat(char* modName)
{
	PLDR_DATA_TABLE_ENTRY moduleHandle = (PLDR_DATA_TABLE_ENTRY)GetModuleHandle(modName);
	if(moduleHandle != NULL)
	{
		PIMAGE_DATA_DIRECTORY pddir;
		PIMAGE_XEX_HEADER xhead = (PIMAGE_XEX_HEADER)moduleHandle->XexHeaderBase;
		pddir = (PIMAGE_DATA_DIRECTORY)RtlImageXexHeaderField(xhead, XEX_HEADER_PE_EXPORTS);
		if(pddir == NULL)
		{
			PXEX_SECURITY_INFO xs = (PXEX_SECURITY_INFO)(xhead->SecurityInfo);
			return (PIMAGE_EXPORT_ADDRESS_TABLE)xs->ImageInfo.ExportTableAddress;
		}
	}
	return NULL;
}

DWORD resolveFunct(PCHAR modname, DWORD ord)
{
	DWORD ptr2=0;
	HANDLE hand;
	if(NT_SUCCESS(XexGetModuleHandle(modname, &hand)))
	{
		XexGetProcedureAddress(hand, ord, &ptr2);
	}
	return ptr2; // function not found
}

// this is how xam does it... XamFileExists
// BOOL fileExists(PCHAR path)
// {
	// OBJECT_ATTRIBUTES obAtrib;
	// FILE_NETWORK_OPEN_INFORMATION netInfo;
	// STRING filePath;
	// RtlInitAnsiString(&filePath, path); //  = 0x10
	// InitializeObjectAttributes(&obAtrib, &filePath, 0x40, NULL);
	// if(path[0] != '\\')
		// obAtrib.RootDirectory = (HANDLE)0xFFFFFFFD;
	// if(NT_SUCCESS(NtQueryFullAttributesFile(&obAtrib, &netInfo)))
	// {
		//filter out directories from the result
		// if((netInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			// return TRUE;
	// }
	// return FALSE;
// }

// this one was fixed to allow busy files to be detected as existing
BOOL fileExists(PCHAR path)
{
	HANDLE file = CreateFile(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(file == INVALID_HANDLE_VALUE)
	{
		if(GetLastError() != 5) // inaccessible means it exists but is probably open somewhere else
			return FALSE;
	}
	else
		CloseHandle(file);
	return TRUE;
}

// will check if a mounted drive is present via path = "link:\\", trailing \ needed for it to work
BOOL driveExists(PCHAR path)
{
	DWORD attr = GetFileAttributes(path);
	if(attr == NEG_ONE_AS_DWORD)
		return FALSE;
	else
		return TRUE;
}

DWORD makeBranch(DWORD currAddr, DWORD destAddr, BOOL linked)
{
	int dist = ((int)(destAddr&~0x80000000)) - ((int)(currAddr&~0x80000000));
	DWORD ret = 0x48000000 | ((DWORD)(dist&0x3FFFFFC));
	if(linked)
		ret = ret|1;
	return ret;
}

/*
currAddr 8007b3a4 brInst 4bfffdf4 currOff 0007b3a4 destOff fffffdf4
xexhook: branch found at 0x8007b3a4 to 0x8007b198
*/
DWORD interpretBranchDest(DWORD currAddr, DWORD brInst)
{
	DWORD ret;
	int destOff = brInst&0x3FFFFFC;
	int currOff = currAddr&~0x80000000; // make it a positive int
	if(brInst&0x2000000) // backward branch
		destOff = destOff|0xFC000000; // sign extend
	ret = (DWORD)(currOff+destOff);
	return (ret|(currAddr&0x80000000)); // put back the bit if it was used
}

DWORD findInterpretBranch(PDWORD startAddr, DWORD maxSearch)
{
	DWORD i;
	DWORD ret = 0;
	for(i = 0; i < maxSearch; i++)
	{
		if((startAddr[i]&0xFC000000) == 0x48000000)
		{
			ret = interpretBranchDest((DWORD)&startAddr[i], startAddr[i]);
			i = maxSearch;
		}
	}
	return ret;
}

DWORD findInterpretBranchOrd(PCHAR modname, DWORD ord, DWORD maxSearch)
{
	DWORD ret = 0;
	PDWORD search = (PDWORD)resolveFunct(modname, ord);
	if(search != NULL)
		ret = findInterpretBranch(search, maxSearch);
	return ret;
}

void patchInJump(PDWORD addr, DWORD dest, BOOL linked)
{
	if(dest & 0x8000) // If bit 16 is 1
		addr[0] = 0x3D600000 + (((dest >> 16) & 0xFFFF) + 1); // lis %r11, dest>>16 + 1
	else
		addr[0] = 0x3D600000 + ((dest >> 16) & 0xFFFF); // lis %r11, dest>>16

	addr[1] = 0x396B0000 + (dest & 0xFFFF); // addi %r11, %r11, dest&0xFFFF
	addr[2] = 0x7D6903A6; // mtctr %r11

	if(linked)
		addr[3] = 0x4E800421; // bctrl
	else
		addr[3] = 0x4E800420; // bctr
	doSync(addr);
}

DWORD hookExportOrd(char* modName, DWORD ord, DWORD dstFun)
{
	if(dstFun != 0)
	{
		PIMAGE_EXPORT_ADDRESS_TABLE expbase = getModuleEat(modName);
		if(expbase != NULL)
		{
			//if(expbase->ordOffset[ord-1] != 0){}
			DWORD modOffset = (expbase->ImageBaseAddress)<<16;
			DWORD origOffset = (expbase->ordOffset[ord-1])+modOffset;
			expbase->ordOffset[ord-1] = dstFun-modOffset;
			doSync(&expbase->ordOffset[ord-1]);
			return origOffset;
		}
	}
	return 0;
}

void unhookExportOrd(char* modName, DWORD ord, DWORD origFun)
{
	if(origFun != 0)
	{
		PIMAGE_EXPORT_ADDRESS_TABLE expbase = getModuleEat(modName);
		if(expbase != NULL)
		{
			//if(expbase->ordOffset[ord-1] != 0){}
			DWORD modOffset = (expbase->ImageBaseAddress)<<16;
			expbase->ordOffset[ord-1] = origFun-modOffset;
			doSync(&expbase->ordOffset[ord-1]);
		}
	}
}

BOOL hookImpStub(char* modname, char* impmodname, DWORD ord, DWORD patchAddr, PIMPORT_HOOK_SAVE hookSave)
{
	DWORD orgAddr;
	PLDR_DATA_TABLE_ENTRY ldat;
	int i, j;
	BOOL ret = FALSE;
	// get the address of the actual function that is jumped to
	orgAddr = resolveFunct(impmodname, ord);
	if(orgAddr != 0)
	{
		// find where kmod info is stowed
		ldat = (PLDR_DATA_TABLE_ENTRY)GetModuleHandle(modname);
		if(ldat != NULL)
		{
			// use kmod info to find xex header in memory
			PXEX_IMPORT_DESCRIPTOR imps = (PXEX_IMPORT_DESCRIPTOR)RtlImageXexHeaderField(ldat->XexHeaderBase, XEX_HEADER_IMPORTS);
			if(imps != NULL)
			{
				char* impName = (char*)(imps+1);
				PXEX_IMPORT_TABLE impTbl = (PXEX_IMPORT_TABLE)(impName + imps->NameTableSize);
				for(i = 0; i < (int)(imps->ModuleCount); i++)
				{
					// use import descriptor strings to refine table
					for(j = 0; j < impTbl->ImportCount; j++)
					{
						PDWORD add = (PDWORD)impTbl->ImportStubAddr[j];
						if(add[0] == orgAddr)
						{
							PDWORD addr = (PDWORD)(impTbl->ImportStubAddr[j+1]);
							if(hookSave != NULL)
							{
								int cnt;
								hookSave->addr = addr;
								for(cnt = 0; cnt < 4; cnt++)
									hookSave->orgInst[cnt] = addr[cnt];
							}
// 							dbgPrintFake("%s %s tbl %d has ord %x at tstub %d location %08x\n", modname, impName, i, ord, j, impTbl->ImportStubAddr[j+1]);
							patchInJump(addr, patchAddr, FALSE);
							j = impTbl->ImportCount;
							ret = TRUE;
						}
					}
					impTbl = (PXEX_IMPORT_TABLE)((BYTE*)impTbl+impTbl->TableSize);
					impName = impName+strlen(impName);
					while((impName[0]&0xFF) == 0x0)
						impName++;
				}
			}		
// 			else dbgPrintFake("could not find import descriptor for mod %s\n", modname);
		}
// 		else dbgPrintFake("could not find data table for mod %s\n", modname);
	}
// 	else dbgPrintFake("could not find ordinal %d in mod %s\n", ord, impmodname);

	return ret;
}

BOOL unhookImpStub(PIMPORT_HOOK_SAVE hookSave)
{
	if(hookSave != NULL)
	{
		if(hookSave->addr != NULL)
		{
			int i;
			for(i = 0; i<4; i++)
				hookSave->addr[i] = hookSave->orgInst[i];
			doSync(hookSave->addr);
			return TRUE;
		}
	}
	return FALSE;
}

BYTE* getModBaseSize(char* modName, PDWORD size)
{
	PLDR_DATA_TABLE_ENTRY ldat;
	ldat = (PLDR_DATA_TABLE_ENTRY)GetModuleHandle(modName);
	if(ldat != NULL)
	{
		if(ldat->EntryPoint > ldat->ImageBase)
			size[0] = ((DWORD)ldat->EntryPoint-(DWORD)ldat->ImageBase);
		else
			size[0] = ldat->SizeOfFullImage;
		return (BYTE*)ldat->ImageBase;
	}
	return NULL;
}

void __declspec(naked) GLPR_FUN(void)
{
	__asm{
		std     r14, -0x98(sp)
		std     r15, -0x90(sp)
		std     r16, -0x88(sp)
		std     r17, -0x80(sp)
		std     r18, -0x78(sp)
		std     r19, -0x70(sp)
		std     r20, -0x68(sp)
		std     r21, -0x60(sp)
		std     r22, -0x58(sp)
		std     r23, -0x50(sp)
		std     r24, -0x48(sp)
		std     r25, -0x40(sp)
		std     r26, -0x38(sp)
		std     r27, -0x30(sp)
		std     r28, -0x28(sp)
		std     r29, -0x20(sp)
		std     r30, -0x18(sp)
		std     r31, -0x10(sp)
		stw     r12, -0x8(sp)
		blr
	}
}

DWORD relinkGPLR(int offset, PDWORD saveStubAddr, PDWORD orgAddr)
{
	DWORD inst = 0, repl;
	int i;
	PDWORD saver = (PDWORD)GLPR_FUN;
	// if the msb is set in the instruction, set the rest of the bits to make the int negative
	if(offset&0x2000000)
		offset = offset|0xFC000000;
	//DbgPrint("frame save offset: %08x\n", offset);
	repl = orgAddr[offset/4];
	//DbgPrint("replacing %08x\n", repl);
	for(i = 0; i < 20; i++)
	{
		if(repl == saver[i])
		{
			int newOffset = (int)&saver[i]-(int)saveStubAddr;
			inst = 0x48000001|(newOffset&0x3FFFFFC);
			//DbgPrint("saver addr: %08x savestubaddr: %08x\n", &saver[i], saveStubAddr);
		}
	}
	//DbgPrint("new instruction: %08x\n", inst);
	return inst;
}

void hookFunctionStart(PDWORD addr, PDWORD saveStub, PDWORD oldData, DWORD dest)
{
	if((saveStub != NULL)&&(addr != NULL))
	{
		int i;
		DWORD addrReloc = (DWORD)(&addr[4]);// replacing 4 instructions with a jump, this is the stub return address
		//DbgPrint("hooking addr: %08x savestub: %08x dest: %08x addreloc: %08x\n", addr, saveStub, dest, addrReloc);
		// build the stub
		// make a jump to go to the original function start+4 instructions
		if(addrReloc & 0x8000) // If bit 16 is 1
			saveStub[0] = 0x3D600000 + (((addrReloc >> 16) & 0xFFFF) + 1); // lis %r11, dest>>16 + 1
		else
			saveStub[0] = 0x3D600000 + ((addrReloc >> 16) & 0xFFFF); // lis %r11, dest>>16

		saveStub[1] = 0x396B0000 + (addrReloc & 0xFFFF); // addi %r11, %r11, dest&0xFFFF
		saveStub[2] = 0x7D6903A6; // mtctr %r11
		// instructions [3] through [6] are replaced with the original instructions from the function hook
		// copy original instructions over, relink stack frame saves to local ones
		if(oldData != NULL)
		{
			for(i = 0; i<4; i++)
				oldData[i] = addr[i];
		}
		for(i = 0; i<4; i++)
		{
			if((addr[i]&0x48000003) == 0x48000001) // branch with link
			{
				//DbgPrint("relink %08x\n", addr[i]);
				saveStub[i+3] = relinkGPLR((addr[i]&~0x48000003), &saveStub[i+3], &addr[i]);
			}
			else
			{
				//DbgPrint("copy %08x\n", addr[i]);
				saveStub[i+3] = addr[i];
			}
		}
		saveStub[7] = 0x4E800420; // bctr
		doSync(saveStub);
		//DbgPrint("savestub:\n");
		//for(i = 0; i < 8; i++)
		//{
		//	DbgPrint("PatchDword(0x%08x, 0x%08x);\n", &saveStub[i], saveStub[i]);
		//}
		// patch the actual function to jump to our replaced one
		patchInJump(addr, dest, FALSE);
	}
}

void unhookFunctionStart(PDWORD addr, PDWORD oldData)
{
	if((addr != NULL)&&(oldData != NULL))
	{
		int i;
		for(i = 0; i < 4; i++)
		{
			addr[i] = oldData[i];
		}
		doSync(addr);
	}
}

PDWORD hookFunctionStartOrd(char* modName, DWORD ord, PDWORD saveStub, PDWORD oldData, DWORD dest)
{
	PDWORD addr = (PDWORD)resolveFunct(modName, ord);
	if(addr != NULL)
	{
		hookFunctionStart(addr, saveStub, oldData, dest);
	}
	return addr;
}

PDWORD unhookFunctionStartOrd(char* modName, DWORD ord, PDWORD oldData)
{
	PDWORD addr = NULL;
	if((modName != NULL)&&(oldData != NULL))
	{
		addr = (PDWORD)resolveFunct(modName, ord);
		if(addr != NULL)
			unhookFunctionStart(addr, oldData);
	}
	return addr;
}

void patchXamString(const char* str, const char* repl, BOOL terminate)
{
	DWORD siz = 0;
	int repsz = strlen(repl);
	int strsz = strlen(str);
	PBYTE ptr = getModBaseSize(MODULE_XAM, &siz);
	//DbgPrint("patchXamString start %08x size %08x\n", ptr, siz);
	//DbgPrint("str: %s (%d)\n", str, strsz);
	//DbgPrint("repl: %s (%d)\n", repl, repsz);
	if((ptr != NULL)&&(siz != 0))
	{
		if((repsz > 0) && (strsz > 0))
		{
			DWORD i;
			for(i = 0; i < siz; i++)
			{
				if(ptr[i] == str[0])
				{
					if(strnicmp(str, (char*)&ptr[i], strsz) == 0)
					{
						if(terminate)
							strcpy((char*)&ptr[i], repl);
						else
							memcpy(&ptr[i], repl, repsz);
						doSync(&ptr[i]);
						//DbgPrint("patched %s at %08x\n", &ptr[i], &ptr[i]);
						i+=strsz;
					}
				}
			}
		}
	}
}

BOOL patchModuleSearchkey(HANDLE modHand, PDWORD key, DWORD keySz, DWORD patchOffset, PDWORD patchData, DWORD patchSz)
{
	BOOL ret = FALSE;
	if(modHand != NULL)
	{
		DWORD i, j;
		PLDR_DATA_TABLE_ENTRY ldat = (PLDR_DATA_TABLE_ENTRY)modHand;
		PDWORD imageData = (PDWORD)ldat->ImageBase;
		DWORD imageSize = ldat->SizeOfFullImage/4;
		PDWORD imageFind = NULL;
		//dbgPrintFake("seeking patch base 0x%x size 0x%x (0x%x)\n", imageData, imageSize, ldat->SizeOfFullImage);
		for(i = 0; i < imageSize; i++)
		{
			if(imageData[i] == key[0])
			{
				//dbgPrintFake("0x%x %x == %x\n", i, imageData[i], key[0]);
				for(j = 1; j < keySz; j++)
				{
					if(key[j] != 0xFFFFFFFF)
					{
						if(imageData[i+j] != key[j])
							j = keySz;
						else if(j == (keySz -1))
						{
							imageFind = &imageData[(i+patchOffset)];
							j = keySz;
							i = imageSize;
						}
					}
				}
			}
		}
		if(imageFind != NULL)
		{
			//dbgPrintFake("patching module at base 0x%08x\n", imageFind);
			for(i = 0; i < patchSz; i++)
				imageFind[i] = patchData[i];
			doSync(imageFind);
			ret = TRUE;
		}
		//else
			//dbgPrintFake("ERROR! keysz 0x%x patchsz 0x%x not found!!\n", keySz, patchSz);

	}
	//else
		//dbgPrintFake("could not find data table for mod\n");

	return ret;
}

BOOL patchModuleSearchkeyByName(LPCSTR modName, PDWORD key, DWORD keySz, DWORD patchOffset, PDWORD patchData, DWORD patchSz)
{
	HANDLE moduleHandle = GetModuleHandle(modName);
	if(moduleHandle != NULL)
	{
		return patchModuleSearchkey(moduleHandle, key, keySz, patchOffset, patchData, patchSz);
	}
	return FALSE;
}


#define STACKTRACE_MAX_LOOP	25
VOID showStackTrace(VOID)
{
	int i;
	DWORD* stackVal;
	DWORD* stackPtr;
	__asm {
		mr	stackVal, r1
	}
	stackPtr = stackVal;
	sdbgLockPush();
	sdbgPrintL("Call Stack: (SP:0x%08x)\n", stackPtr);
	for(i = 0; i < STACKTRACE_MAX_LOOP; i++)
	{
		if(stackPtr != 0) // the first 2 results are because of the call to this function!
		{
			DWORD lr = *(stackPtr - 2);
			sdbgPrintL("\t0x%08X\n", lr);
			stackPtr = (DWORD*)*stackPtr;
		}
		else
			i = STACKTRACE_MAX_LOOP;
	}
	sdbgLockPop();
}
