#ifndef _ISOIO_H
#define _ISOIO_H

#define IRP_FAKE_GET_INDEX	1
#define IRP_FAKE_MOUNT_ISO	2
// 6 is taken as a normal IRP

void isoIoStartup(void);
NTSTATUS isoIoQueueRead(PIRP irp);
VOID isoIoQueueOther(PIRP irp);
NTSTATUS isoIoGetSense(PIRP irp);
NTSTATUS isoIoGetGeometry(PIRP irp);
LONGLONG isoIoGetPartitionSize(void);
BOOL isoIoHasDisk(void);

#endif // _ISOIO_H
