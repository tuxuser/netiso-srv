#include "common.h"
#include "utility.h"
#include "fsdDriver.h"
#include "dvdHook.h"
#include "isoio.h"
#include "client.h"

#define PLUGIN_LOAD_PATH_MAGIC	0x504C5041 // 'PLPA' (PLugin PAth)
#define DL_ORDINALS_PLUGINPATH	14
#define MODULE_LAUNCH			"launch.xex"

// devicePath should never be more than 64 chars or so, this is what is mounted to a symlink ie: \Device\BuiltInMuUsb\Storage\
// ini path has a max of 260 chars, this is what is appended to a symlink ie: \path\from\ini\thexex.xex
// symlink ie: adrive:
typedef struct _PLUGIN_LOAD_PATH{
	DWORD magic; // will be PLUGIN_LOAD_PATH_MAGIC when other items are stuffed
	const char* devicePath;
	const char* iniPath;
}PLUGIN_LOAD_PATH, *PPLUGIN_LOAD_PATH;

char g_HostIp[32] = "192.168.2.100";

BOOL tryLoadIp(const char* drivePath, const char* xexPath)
{
	BOOL ret = FALSE;
	if(MountPath("isoio:", drivePath, FALSE) >= 0)
	{
		HANDLE fhand;
		char iniPath[MAX_PATH] = "isoio:";
		strcat(iniPath, xexPath);
		strcat(iniPath, ".txt");
		fhand = CreateFile(iniPath, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if(fhand != INVALID_HANDLE_VALUE)
		{
			DWORD bRead;
			ReadFile(fhand, g_HostIp, 32, &bRead, NULL);
			sdbgPrint("read 0x%x bytes from file\n", bRead);
			if(bRead != 0)
			{
				DWORD i;
				for(i = 0; i < bRead; i++)
				{
					char ab = g_HostIp[i]&0xFF;
					if((ab == 0xD)||(ab == 0xA))
					{
						g_HostIp[i] = 0;
						i = 32;
					}
				}
				sdbgPrint("read host '%s' from file\n", g_HostIp);
				ret = TRUE;
			}
			CloseHandle(fhand);
		}
		else
			sdbgPrint("failed (%d) to CreateFile ini not found at drive '%s' xex '%s'\n", GetLastError(), drivePath, xexPath);
		deleteLink("isoio:", FALSE);
	}
	return ret;
}

void loadIpInit(void)
{
	BOOL hasIni = FALSE;
	// check for dash launch export that gives this info
	PPLUGIN_LOAD_PATH dlaunchPluginPath;
	dlaunchPluginPath = (PPLUGIN_LOAD_PATH)resolveFunct(MODULE_LAUNCH, DL_ORDINALS_PLUGINPATH);
	if(dlaunchPluginPath != NULL)
	{
		hasIni = tryLoadIp(dlaunchPluginPath->devicePath, dlaunchPluginPath->iniPath);
	}
	// check root of usb, then hdd
	if(hasIni == FALSE)
	{
		hasIni = tryLoadIp("\\Device\\Harddisk0\\Partition1\\", "\\netiso.xex");
	}
	if(hasIni == FALSE)
	{
		hasIni = tryLoadIp("\\Device\\Mass0\\", "\\netiso.xex");
	}
	// give up and use hardcoded value
}

BOOL APIENTRY DllMain(HANDLE hInstDLL, DWORD reason, LPVOID lpReserved)
{
	sdbgPrint("netiso entry!\n");
	if(reason == DLL_PROCESS_ATTACH)
	{
		// all that is needed for dvd emulation
		loadIpInit();
		isoIoStartup();
		DvdHookStartup();

		// all that is needed for the SMB type stuff
		if(NT_SUCCESS(FsdStartup()))
		{
			sdbgPrint("netiso FsdStartup completed!\n");
		}
	}
	return TRUE;
}


//typedef DWORD (*TLSALLOCFUN)(VOID);
//typedef LPVOID (*TLSGETVALFUN)(DWORD dwTlsIndex);
//typedef BOOL (*TLSSETVALFUN)(DWORD dwTlsIndex, LPVOID lpTlsValue);
//typedef BOOL (*TLSFREEFUN)(DWORD dwTlsIndex);
//
//TLSALLOCFUN gpFlsAlloc;
//TLSGETVALFUN gpFlsGetValue;
//TLSSETVALFUN gpFlsSetValue;
//TLSFREEFUN gpFlsFree;
//
//DWORD __crtTlsAlloc(VOID)
//{
//	return TlsAlloc();
//}
//
//BOOL _mtinit(void)
//{
//	gpFlsAlloc = __crtTlsAlloc;
//	gpFlsGetValue = (TLSGETVALFUN)TlsGetValue;
//	gpFlsSetValue = (TLSSETVALFUN)TlsSetValue;
//	gpFlsFree = (TLSFREEFUN)TlsFree;
//}
//
//static DWORD proc_attached = 0;
//static BOOL proc_started = FALSE;
//extern int  __cdecl _cinit(int /* initFloatingPrecision */);   /* crt0dat.c */
//__declspec(noinline) BOOL __cdecl MyDllMain(HANDLE hDllHandle, DWORD dwReason, LPVOID lpreserved)
//{
//	BOOL retVal = TRUE;
//	sdbgPrint("netiso entry!\n");
//	if(dwReason == DLL_PROCESS_ATTACH)
//	{
//		_mtinit();
//		_cinit(1);
//		proc_attached++;
//		if(proc_attached == 1)
//		{
//			sdbgPrint("netiso starting up!\n");
//		}
//		if(proc_started == FALSE)
//		{
//// 			if(FsdStartup())
//				proc_started = TRUE;
//// 			else
//// 				retVal = FALSE;
//		}
//	}
//	return retVal;
//}

