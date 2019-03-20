#include <Uefi.h>
#include <Guid/FileInfo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/PxeBaseCode.h>
#include <Protocol/SimpleFileSystem.h>

struct _EFI_TFTP_FILE_PROTOCOL {
	EFI_FILE_PROTOCOL File;
	struct _EFI_TFTP_FILE_PROTOCOL *Parent;
	UINTN Refs;
	char *FileName;
	UINTN FileNameSize;
	UINTN FileSize;
};

typedef struct _EFI_TFTP_FILE_PROTOCOL EFI_TFTP_FILE_PROTOCOL;

static int memcmp(const void *s1, const void *s2, UINTN n);
static char *stpcpy(char *dest, const char *src);
static UINTN strlen(const char *s);
static UINTN wcslen(const CHAR16 *s);

static EFI_STATUS EfiUnsupported(void);

static EFI_STATUS TftpOpen(EFI_FILE_PROTOCOL *This,
	EFI_FILE_PROTOCOL **NewHandle, CHAR16 *FileName, UINT64 OpenMode,
	UINT64 Attributes);
static EFI_STATUS TftpOpenVolume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,
	EFI_FILE_PROTOCOL **Root);
static EFI_STATUS TftpClose(EFI_FILE_PROTOCOL *This);
static EFI_STATUS TftpRead(EFI_FILE_PROTOCOL *This, UINTN *BufferSize,
	VOID *Buffer);
static EFI_STATUS TftpGetInfo(EFI_FILE_PROTOCOL *This,
	EFI_GUID *InformationType, UINTN *BufferSize, VOID *Buffer);

static const EFI_TFTP_FILE_PROTOCOL TftpFileProtocol = {
	.File = {
		.Revision = EFI_FILE_PROTOCOL_LATEST_REVISION,
		.Open = TftpOpen,
		.Close = TftpClose,
		.Delete = (void *)EfiUnsupported,
		.Read = TftpRead,
		.Write = (void *)EfiUnsupported,
		.GetPosition = (void *)EfiUnsupported,
		.SetPosition = (void *)EfiUnsupported,
		.GetInfo = TftpGetInfo,
		.SetInfo = (void *)EfiUnsupported,
		.Flush = (void *)EfiUnsupported,
		.OpenEx = (void *)EfiUnsupported,
		.ReadEx = (void *)EfiUnsupported,
		.WriteEx = (void *)EfiUnsupported,
		.FlushEx = (void *)EfiUnsupported
	},
	.Parent = NULL,
	.Refs = 1,
	.FileName = NULL,
	.FileNameSize = 0,
	.FileSize = 0
};

static const EFI_SIMPLE_FILE_SYSTEM_PROTOCOL TftpSimpleFileSystemProtocol = {
	.Revision = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION,
	.OpenVolume = TftpOpenVolume
};

static EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;
static EFI_GUID LoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID PxeBaseCodeProtocolGuid = EFI_PXE_BASE_CODE_PROTOCOL_GUID;
static EFI_GUID SimpleFileSystemProtocolGuid =
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

static EFI_HANDLE ImageHandle;
static EFI_PXE_BASE_CODE_PROTOCOL *PxeBaseCode;
static EFI_IP_ADDRESS ServerIp;
static EFI_SYSTEM_TABLE *SystemTable;

EFI_STATUS
_ModuleEntryPoint(EFI_HANDLE a0, EFI_SYSTEM_TABLE *a1)
{
	EFI_BOOT_SERVICES *BootServices;
	EFI_HANDLE DeviceHandle;
	EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *SimpleFileSystem;
	EFI_STATUS Status;

	ImageHandle = a0;
	SystemTable = a1;

	BootServices = SystemTable->BootServices;
	Status = BootServices->HandleProtocol(ImageHandle,
		&LoadedImageProtocolGuid, (void **)&LoadedImage);
	if (Status != EFI_SUCCESS)
	{
		goto err;
	}
	DeviceHandle = LoadedImage->DeviceHandle;
	Status = BootServices->HandleProtocol(DeviceHandle,
		&PxeBaseCodeProtocolGuid, (void **)&PxeBaseCode);
	if (Status != EFI_SUCCESS)
	{
		goto err;
	}
	Status = BootServices->HandleProtocol(DeviceHandle,
		&SimpleFileSystemProtocolGuid, (void **)&SimpleFileSystem);
	if (Status != EFI_SUCCESS)
	{
		goto err;
	}
	BootServices->CopyMem(&ServerIp.v4.Addr,
		PxeBaseCode->Mode->DhcpAck.Dhcpv4.BootpSiAddr,
		sizeof(ServerIp.v4.Addr));
	Status = BootServices->ReinstallProtocolInterface(DeviceHandle,
		&SimpleFileSystemProtocolGuid, SimpleFileSystem,
		(VOID *)(UINTN)&TftpSimpleFileSystemProtocol);
	if (Status != EFI_SUCCESS)
	{
		goto err;
	}
	return EFI_SUCCESS;

err:
	return Status;
}

static EFI_STATUS
EfiUnsupported(void)
{
	return EFI_UNSUPPORTED;
}

static EFI_STATUS
TftpClose(EFI_FILE_PROTOCOL *This)
{
	EFI_BOOT_SERVICES *BootServices;
	EFI_TFTP_FILE_PROTOCOL *TftpFile;
	EFI_TFTP_FILE_PROTOCOL *TftpFileParent;

	BootServices = SystemTable->BootServices;
	TftpFile = (EFI_TFTP_FILE_PROTOCOL *)This;
	while (TftpFile != NULL)
	{
		TftpFileParent = TftpFile->Parent;
		if (--TftpFile->Refs == 0)
		{
			BootServices->FreePool(TftpFile);
		}
		TftpFile = TftpFileParent;
	}
	return EFI_SUCCESS;
}

static EFI_STATUS
TftpGetInfo(EFI_FILE_PROTOCOL *This, EFI_GUID *InformationType,
	UINTN *BufferSize, VOID *Buffer)
{
	EFI_BOOT_SERVICES *BootServices;
	EFI_FILE_INFO *FileInfo;
	int i;
	UINTN Size;
	EFI_STATUS Status;
	EFI_TFTP_FILE_PROTOCOL *TftpFile;
	CHAR16 *wp;
	CHAR16 *wq;

	BootServices = SystemTable->BootServices;
	TftpFile = (EFI_TFTP_FILE_PROTOCOL *)This;
	Size = SIZE_OF_EFI_FILE_INFO + (strlen(TftpFile->FileName) + 1) *
		sizeof(FileInfo->FileName[0]);
	if (memcmp(InformationType, &FileInfoGuid, sizeof(FileInfoGuid)) == 0)
	{
		if (Buffer == NULL && BufferSize != NULL)
		{
			if (*BufferSize < Size)
			{
				*BufferSize = Size;
				Status = EFI_BUFFER_TOO_SMALL;
			}
			else
			{
				Status = EFI_SUCCESS;
			}
		}
		else if (Buffer != NULL && *BufferSize < Size)
		{
			Status = EFI_BUFFER_TOO_SMALL;
		}
		else if (Buffer != NULL && *BufferSize == Size)
		{
			FileInfo = (EFI_FILE_INFO *)Buffer;
			BootServices->SetMem(FileInfo, SIZE_OF_EFI_FILE_INFO, 0);
			FileInfo->Size = Size;
			FileInfo->FileSize = TftpFile->FileSize;
			FileInfo->PhysicalSize = TftpFile->FileSize;
			for (i = 0; i < TftpFile->FileNameSize; i++)
			{
				FileInfo->FileName[i] = TftpFile->FileName[i];
			}
			Status = EFI_SUCCESS;
		}
		else
		{
			Status = EFI_UNSUPPORTED;
			goto err;
		}
	}
	else
	{
		Status = EFI_UNSUPPORTED;
		goto err;
	}
	return Status;

err:
	return Status;
}

static EFI_STATUS
TftpOpenVolume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,
	EFI_FILE_PROTOCOL **Root)
{
	if (Root == NULL)
	{
		return EFI_INVALID_PARAMETER;
	}
	*Root = (VOID *)(UINTN)&TftpFileProtocol;
	return EFI_SUCCESS;
}

static EFI_STATUS
TftpOpen(EFI_FILE_PROTOCOL *This, EFI_FILE_PROTOCOL **NewHandle,
	CHAR16 *FileName, UINT64 OpenMode, UINT64 Attributes)
{
	EFI_BOOT_SERVICES *BootServices;
	UINT64 BufferSize;
	EFI_TFTP_FILE_PROTOCOL *File;
	char *FileName2;
	UINTN FileNameSize;
	int i;
	char *p;
	char *q;
	EFI_STATUS Status;
	EFI_TFTP_FILE_PROTOCOL *TftpFile;

	BootServices = SystemTable->BootServices;
	TftpFile = (EFI_TFTP_FILE_PROTOCOL *)This;
	FileNameSize = 0;
	for (File = TftpFile; File != NULL && File->FileName != NULL;
		File = File->Parent)
	{
		FileNameSize += strlen(File->FileName) + 1;
	}
	FileNameSize += wcslen(FileName) + 1;
	Status = BootServices->AllocatePool(EfiLoaderData, FileNameSize,
		(void **)&FileName2);
	if (Status != EFI_SUCCESS)
	{
		goto err;
	}
	p = (FileName2 + FileNameSize) - (wcslen(FileName) + 1);
	for (i = 0; i < wcslen(FileName) + 1; i++)
	{
		p[i] = FileName[i];
	}
	for (File = TftpFile; File != NULL && File->FileName != NULL;
		File = File->Parent)
	{
		p -= strlen(File->FileName) + 1;
		q = stpcpy(p, File->FileName);
		*q = '/';
	}
	BufferSize = 0;
	Status = PxeBaseCode->Mtftp(PxeBaseCode,
		EFI_PXE_BASE_CODE_TFTP_GET_FILE_SIZE, NULL, FALSE, &BufferSize, NULL,
		&ServerIp, FileName2, NULL, FALSE);
	BootServices->FreePool(FileName2);
	if (Status != EFI_SUCCESS)
	{
		if (Status == EFI_TFTP_ERROR)
		{
			Status = EFI_NOT_FOUND;
			goto out;
		}
		else
		{
			goto err;
		}
	}
	FileNameSize = wcslen(FileName) + 1;
	Status = BootServices->AllocatePool(EfiLoaderData, sizeof(*TftpFile) +
		FileNameSize, (void **)&File);
	if (Status != EFI_SUCCESS)
	{
		goto err;
	}
	BootServices->CopyMem(File, TftpFile, sizeof(*TftpFile));
	File->Refs = 1;
	File->Parent = TftpFile;
	File->FileName = (char *)(File + 1);
	File->FileNameSize = FileNameSize;
	File->FileSize = BufferSize;
	for (i = 0; i < FileNameSize; i++)
	{
		File->FileName[i] = FileName[i];
	}
	for (; TftpFile != NULL; TftpFile = TftpFile->Parent)
	{
		TftpFile->Refs++;
	}
	*NewHandle = (EFI_FILE_PROTOCOL *)File;
out:
	return Status;

err:
	return Status;
}

static EFI_STATUS
TftpRead(EFI_FILE_PROTOCOL *This, UINTN *BufferSize, VOID *Buffer)
{
	EFI_BOOT_SERVICES *BootServices;
	char c;
	char *FileName;
	UINTN FileNameSize;
	char *p;
	char *q;
	EFI_STATUS Status;
	EFI_TFTP_FILE_PROTOCOL *TftpFile;

	BootServices = SystemTable->BootServices;
	TftpFile = (EFI_TFTP_FILE_PROTOCOL *)This;
	FileNameSize = 0;
	for (; TftpFile != NULL && TftpFile->FileName != NULL;
		TftpFile = TftpFile->Parent)
	{
		FileNameSize += strlen(TftpFile->FileName) + 1;
	}
	Status = BootServices->AllocatePool(EfiLoaderData, FileNameSize,
		(void **)&FileName);
	if (Status != EFI_SUCCESS)
	{
		goto err;
	}
	c = '\0';
	p = FileName + FileNameSize;
	for (TftpFile = (EFI_TFTP_FILE_PROTOCOL *)This;
		TftpFile != NULL && TftpFile->FileName != NULL;
		TftpFile = TftpFile->Parent)
	{
		p -= strlen(TftpFile->FileName) + 1;
		q = stpcpy(p, TftpFile->FileName);
		*q = c;
		c = '/';
	}
	TftpFile = (EFI_TFTP_FILE_PROTOCOL *)This;
	Status = PxeBaseCode->Mtftp(PxeBaseCode,
		EFI_PXE_BASE_CODE_TFTP_READ_FILE, Buffer, FALSE, BufferSize, NULL,
		&ServerIp, FileName, NULL, FALSE);
	BootServices->FreePool(FileName);
	if (Status != EFI_SUCCESS)
	{
		goto err;
	}
	return Status;

err:
	return Status;
}

static int
memcmp(const void *s1, const void *s2, UINTN n)
{
	const char *p1 = s1;
	const char *p2 = s2;

	while (n-- != 0)
	{
		if (*p1 != *p2)
		{
			return *p1 - *p2;
		}
		p1++;
		p2++;
	}
	return 0;
}

static char *
stpcpy(char *dest, const char *src)
{
	while ((*dest++ = *src++) != '\0')
		;
	return dest - 1;
}

static UINTN
strlen(const char *s)
{
	const char *p = s;
	while (*p != '\0')
	{
		p++;
	}
	return p - s;
}

static UINTN
wcslen(const CHAR16 *s)
{
	const CHAR16 *p = s;
	while (*p != L'\0')
	{
		p++;
	}
	return p - s;
}
