#include "_hook_includes.h"


//void __stdcall HalReturnToFirmware(DWORD dwPowerDownMode)
#define HALRETURNTOFIRMWARE_ORD		40
#define HALRETURNFIRM_VAL 1
typedef void (*HALRETURNTOFIRMWARE)(DWORD halRoutine); // HalReturnToFirmware

void __declspec(naked) HalReturnToFirmwareSaveVar(void)
{
	__asm{
		li r3, HALRETURNFIRM_VAL
		nop
		nop
		nop
		nop
		nop
		nop
		blr
	}
}

HALRETURNTOFIRMWARE HalReturnToFirmwareSave = (HALRETURNTOFIRMWARE)HalReturnToFirmwareSaveVar;
void HalReturnToFirmwareHook(FIRMWARE_REENTRY halRoutine)
{
	DWORD rout = halRoutine;
	//if(getOpt(OPT_SAFE_REBOOT) == 0) // not safe to reboot, jtag
	//{
		if(rout == HalRebootQuiesceRoutine) // bad reboot for jtag
			rout = HalRebootRoutine;
		//else if (rout == HalForceShutdownRoutine) // bad poweroff for jtag
		//	rout = HalPowerDownRoutine;
	//}
	//dbgPrintFake("halreturntofirmware called with %d calling with %d\n", halRoutine, rout);
	HalReturnToFirmwareSave(rout);
}

static BOOL isRebootHooked = FALSE;
static DWORD rebootOld[4];
void HalRebootHook(void)
{
	if(!isRebootHooked)
	{
		hookFunctionStartOrd(MODULE_KERNEL, HALRETURNTOFIRMWARE_ORD, (PDWORD)HalReturnToFirmwareSaveVar, rebootOld, (DWORD)HalReturnToFirmwareHook);
		//dbgPrintFake("halreturn hooked\n");
		isRebootHooked = TRUE;
	}
}

void HalRebootUnhook(void)
{
	if(isRebootHooked)
	{
		unhookFunctionStartOrd(MODULE_KERNEL, HALRETURNTOFIRMWARE_ORD, rebootOld);
		//dbgPrintFake("halreturn unhooked\n");
		isRebootHooked = FALSE;
	}
}
