#ifndef _IRPCOMPARE_H
#define _IRPCOMPARE_H

void storeIrp(void* irp);
void compareIrp(void* irp);
void irpShow(void* irp);
void irpShowBuf(void* outBuffer, DWORD dwOutLen, void* inputBuffer, DWORD dwInLen);
VOID tfShowObjs(VOID);

#endif // _IRPCOMPARE_H
