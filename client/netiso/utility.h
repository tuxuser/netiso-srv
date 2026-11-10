#ifndef _UTILITY_H
#define _UTILITY_H

#define SYSCALL_KEY		0x72627472 // rbtr

// change whether TLB memory protections are in effect
#define SET_RETAIL_KEY	0
#define SET_DEV_KEY		1
#define SET_PROT_OFF	2
#define SET_PROT_ON		3

#define LI_R3_0		0x38600000 // li %r3, 0
#define LI_R3_1		0x38600001 // li %r3, 1
#define ASM_BLR		0x4E800020 // blr
#define ASM_NOP		0x60000000 // nop

typedef struct _IMPORT_HOOK_SAVE {
	PDWORD addr;
	DWORD orgInst[4];
} IMPORT_HOOK_SAVE, *PIMPORT_HOOK_SAVE;

void sdbgLockPush(void);
void sdbgLockPop(void);
// needs push/pop
void sdbgPrintL(const char* s, ...);
// does not need push/pop
void sdbgPrint(const char* s, ...);

DWORD HvxSetState(DWORD key, DWORD mode);
BYTE KeGetCurrentIrql(VOID);

// resolve an ordinal to an address
DWORD resolveFunct(PCHAR modname, DWORD ord);

// returns the original contents of the address
DWORD patchInNop(PDWORD addr);
DWORD patchInDword(PDWORD addr, DWORD val);

// clean up a mount point link
HRESULT deleteLink(const char* szDrive, BOOL both);

// mount a path to a drive name
HRESULT MountPath(const char* szDrive, const char* szDevice, BOOL both);

// unmount a container that is mounted on a drive name
// returns 0 on success, destroys symbolic link as well
DWORD UnmountContainer(const char* szDrive);

// mount a container to a drive name
// returns 0 on success, creates symbolic link on it's own to the new szDrive
DWORD MountContainer(const char* szDrive, const char* szDevice, const char* szPath);

// given a symbolic link path like "\\??\\GAME:" the device path will
// be copied into outstr (must be at least MAX_PATH size)
// returns 0 on success
NTSTATUS getSymbolicPath(char* path, char* outstr);

// find the Export Address Table in a given module
// only works in threads with the ability to peek crypted memory
// only tested on "xam.xex" and "xboxkrnl.exe"
PIMAGE_EXPORT_ADDRESS_TABLE getModuleEat(char* modName);

// returns true if the file exists
BOOL fileExists(PCHAR path);
// returns true if the drive exists (path ie: "dlaunch:\\"
BOOL driveExists(PCHAR path);

// creates a single branch instruction when given current address and destination address
// it's up to the caller of this function to ensure the addresses are 4 byte aligned
DWORD makeBranch(DWORD currAddr, DWORD destAddr, BOOL linked);

// when given the address of a branch, and the instruction will return
// the destination address of the branch
DWORD interpretBranchDest(DWORD currAddr, DWORD brInst);

DWORD findInterpretBranch(PDWORD startAddr, DWORD maxSearch);
DWORD findInterpretBranchOrd(PCHAR modname, DWORD ord, DWORD maxSearch);

// patches in a 4 instruction jump which uses R11/scratch reg and ctr to assemble
// addr = pointer to address being patched
// dest = address of the new destination
// linked = (true = ctr branch with link used) (false = ctr branch, link register unaffected)
void patchInJump(PDWORD addr, DWORD dest, BOOL linked);

// hook export table ordinals of a module, anything linked after this hook is redirected to dstFun
// modName = pointer to string of the module name to alter the export table, like "xam.xex" or "xboxkrnl.exe"
// ord = ordinal number
// dstFun = address to change ordinal link address to
// returns the address of the start of the hook patched into modName@ord
// ** note that this type of hook ONLY works on things that haven't been linked by the time the patch is made
DWORD hookExportOrd(char* modName, DWORD ord, DWORD dstFun);
// same params, same idea just doesn't return anything
void unhookExportOrd(char* modName, DWORD ord, DWORD origFun);

// hook imported jumper stubs to a different function
// modname = module with the import to patch
// impmodname = module name with the function that was imported
// ord = function ordinal to patch
// patchAddr = destination where it is patched to
// returns TRUE if hooked
// ** NOTE THIS FUNCTION MAY STILL BE BROKEN FOR MODULES WITH MULTIPLE IMPORT TABLES OF THE SAME impmodname
BOOL hookImpStub(char* modname, char* impmodname, DWORD ord, DWORD patchAddr, PIMPORT_HOOK_SAVE hookSave);
BOOL unhookImpStub(PIMPORT_HOOK_SAVE hookSave);

// hook a function start based on address, using 8 instruction saveStub to do the deed
// addr = address of the hook
// saveStub = address of the area to create jump stub for replaced instructions
// olddata = pointer to 4 u32 slot to save the original data in (optional)
// dest = where the hook at addr is pointing to
void hookFunctionStart(PDWORD addr, PDWORD saveStub, PDWORD oldData, DWORD dest);
void unhookFunctionStart(PDWORD addr, PDWORD oldData);

// hook a function start based on ordinal, using 8 instruction saveStub to do the deed
// modName = pointer to string of the module name to alter the export table, like "xam.xex" or "xboxkrnl.exe"
// ord = ordinal number of the function to hook in module modName
// saveStub = address of the area to create jump stub for replaced instructions
// olddata = pointer to 4 u32 slot to save the original data in (optional, use NULL if not needed)
// dest = where the hook at addr is pointing to
// returns the address of the start of the hook patched into modName@ord
PDWORD hookFunctionStartOrd(char* modName, DWORD ord, PDWORD saveStub, PDWORD oldData, DWORD dest);
PDWORD unhookFunctionStartOrd(char* modName, DWORD ord, PDWORD oldData);

// tries to get the data segment size and start address of named module
// modName = pointer to string of the module name to alter the export table, like "xam.xex" or "xboxkrnl.exe"
// size = pointer to a DWORD to take the size from base
BYTE* getModBaseSize(char* modName, PDWORD size);

// patches any instance of a string in xam's data area
// str = string to patch
// repl = string to replace it with
// terminate = if true, the replaced bytes are terminated with 0x0 at the end of repl string
void patchXamString(const char* str, const char* repl, BOOL terminate);

// used to patch an already loaded module, key u32 of 0xFFFFFFFF in key will be ignored as 'any'
// key can be any series of bytes, this will only patch the first instance found so it's best to be unique
// NEVER put 0xFFFFFFFF as the first element in the key!!!
// modHand = handle to the module
// key = pointer to u32 array of key search data
// keySz = number of u32 in the key data
// patchOffset = number of u32 from start of key where patch data will be placed
// patchSz = number of u32 contained in patch
BOOL patchModuleSearchkey(HANDLE modHand, PDWORD key, DWORD keySz, DWORD patchOffset, PDWORD patchData, DWORD patchSz);
// as above, but tries to get module handle itself
BOOL patchModuleSearchkeyByName(LPCSTR modName, PDWORD key, DWORD keySz, DWORD patchOffset, PDWORD patchData, DWORD patchSz);

// traverses stack to show previous link registers
VOID showStackTrace(VOID);


#endif // _UTILITY_H
