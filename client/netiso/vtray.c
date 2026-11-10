// virtual tray manager
#include "common.h"

extern BOOL g_HasDvd;

#define SMC_SEND_MESSAGE_ORD	0x29
static PLIST_ENTRY kernelSMCRegistrationList;
static BYTE virtualTrayState;
static IMPORT_HOOK_SAVE xamSendSmcMessageSave;
// just a dummy function for our registration
void vtraySmcNotify(PHAL_SMC_REGISTRATION SMCRegistration, PSMC_MAILBOX_MESSAGE NotificationMessage)
{
	sdbgPrint("smc notify type 0x%x\n", NotificationMessage->MessageCode);
}

//typedef struct _HAL_SMC_REGISTRATION { 
//	void* NotificationRoutine; // 0x0 sz:0x4 function: void Notify(PHAL_SMC_REGISTRATION SMCRegistration, PSMC_MAILBOX_MESSAGE NotificationMessage);
//	long Priority; // 0x4 sz:0x4
//	LIST_ENTRY ListEntry; // 0x8 sz:0x8
//} HAL_SMC_REGISTRATION, *PHAL_SMC_REGISTRATION; // size 16
//typedef struct _LIST_ENTRY {
//	struct _LIST_ENTRY *Flink;
//	struct _LIST_ENTRY *Blink;
//} LIST_ENTRY, *PLIST_ENTRY, *RESTRICTED_POINTER PRLIST_ENTRY;

void vtrayPwnSmcNotify(void)
{
	HAL_SMC_REGISTRATION hsmcr;
	memset(&hsmcr, 0, sizeof(HAL_SMC_REGISTRATION)); // leaving priority as 0 so it gets inserted into the end of the list
	hsmcr.NotificationRoutine = vtraySmcNotify;
	// register the notification so we can use Flink to grab the base LIST_ENTRY address in kernel
	HalRegisterSMCNotification(&hsmcr, TRUE);
	kernelSMCRegistrationList = hsmcr.ListEntry.Flink;
	//sdbgPrint("kernelSMCRegistrationList at 0x%08x\n", kernelSMCRegistrationList);
	// remove the notification registration now that we have our kernel list address
	HalRegisterSMCNotification(&hsmcr, FALSE);
}

VOID xamHalSendSMCMessage(BYTE* pSnd, BYTE* pRec)
{
// 	sdbgPrint("xam smc send %02x %02x %02x tray %02x!\n", pSnd[0], pSnd[1], pSnd[2], virtualTrayState);
	if(virtualTrayState != 0)
	{
		if(pRec != NULL) // the command we are intercepting does both send and receive
		{
			if((pSnd[0]&0xFF) == smc_query_tray)
			{
// 				sdbgPrint("xam tray query received!\n");
				pRec[0] = smc_query_tray;
				pRec[1] = virtualTrayState;
				return;
			}
		}
	}
	HalSendSMCMessage(pSnd, pRec);
}

// traverse the list and notify everyone of the tray state change
void vtrayDoNotify(void)
{
	PLIST_ENTRY lCur;
	SMC_MAILBOX_MESSAGE smm;
	memset(&smm, 0, sizeof(SMC_MAILBOX_MESSAGE));
	smm.MessageCode = 0x83;
	smm.PayloadAsUCHARs[0] = virtualTrayState;
	lCur = kernelSMCRegistrationList->Flink;
// 	sdbgPrint("notifying list starting at 0x%08x\n", lCur);
	while(lCur != kernelSMCRegistrationList)
	{
		SMCNOTIFYROUTINE pnot;
		PHAL_SMC_REGISTRATION preg = (PHAL_SMC_REGISTRATION)(((DWORD)lCur)-8);
		pnot = (SMCNOTIFYROUTINE)preg->NotificationRoutine;
// 		sdbgPrint("calling 0x%08x from 0x%08x\n", pnot, preg);
		pnot(preg, &smm);
		lCur = lCur->Flink;
	}
}

void vtrayFakeTrayOpen(void)
{
	sdbgPrint("vtrayFakeTrayOpen\n");
	virtualTrayState = SMC_TRAY_OPEN;
	doSync(&virtualTrayState);
	vtrayDoNotify();
}

void vtrayFakeTrayClose(void)
{
	sdbgPrint("vtrayFakeTrayClose\n");
	virtualTrayState = SMC_TRAY_CLOSE;
	doSync(&virtualTrayState);
	vtrayDoNotify();
	if(g_HasDvd)
		virtualTrayState = 0;
	doSync(&virtualTrayState);
}

void vtrayFakeTrayCycle(void)
{
	sdbgPrint("vtrayFakeTrayCycle\n");
	vtrayFakeTrayOpen();
	Sleep(200);
	vtrayFakeTrayClose();
	Sleep(200);
}


void vtrayStartup(void)
{
	virtualTrayState = 0;
	vtrayPwnSmcNotify();
	if(hookImpStub(MODULE_XAM, MODULE_KERNEL, SMC_SEND_MESSAGE_ORD, (DWORD)xamHalSendSMCMessage, &xamSendSmcMessageSave) == FALSE)
		sdbgPrint("vtrayStartup: could not hook xam import of HalSendSMCMessage!\n");
	else
		sdbgPrint("vtrayStartup: complete!\n");
}
