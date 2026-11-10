#ifndef __CLIENT_H
#define __CLIENT_H

BOOL clientNetStart(void);
int clientShutdown(void);
int clientStartup(void);
BOOL clientGetGeometry(BYTE* geometryBuf);
BOOL clientGetDiskType(DWORD* typeBuf);
BOOL clientGetPing(void);
DWORD clientReadDisk(DWORD sector, DWORD len, BYTE* outbuf);
BOOL clientGetIndex(WORD index, DWORD maxOutLen, BYTE* outbuf);
BOOL clientMountIso(DWORD nameLen, BYTE* nameBuf);

#endif // __CLIENT_H
