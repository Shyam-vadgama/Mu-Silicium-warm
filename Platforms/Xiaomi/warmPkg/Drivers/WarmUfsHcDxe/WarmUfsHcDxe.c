/** @file
  MMIO UFS Host Controller & Platform Protocol driver for Xiaomi Redmi A4 5G (SM4635 / warm).

  Exposes the SM4635 UFS host controller at 0x04804000 (ufshc_mem) using
  EDKII_UFS_HOST_CONTROLLER_PROTOCOL and EDKII_UFS_HC_PLATFORM_PROTOCOL so that
  EDK2's UfsPassThruDxe + ScsiBusDxe + ScsiDiskDxe + PartitionDxe can access
  physical UFS LUNs and GPT partitions without proprietary XBL DXE blobs.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DmaLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>

#include <Protocol/DevicePath.h>
#include <Protocol/PciIo.h>
#include <Protocol/UfsHostController.h>
#include <Protocol/UfsHostControllerPlatform.h>

#define WARM_UFS_HC_BASE         0x04804000ULL
#define WARM_UFS_HC_SIZE         0x3000U

#define UFS_REG_CAP              0x00U
#define UFS_REG_VER              0x08U
#define UFS_REG_IS               0x20U
#define UFS_REG_IE               0x24U
#define UFS_REG_HCS              0x30U
#define UFS_REG_HCE              0x34U
#define UFS_REG_UTRLDBR          0x58U
#define UFS_REG_UTRLRSR          0x60U
#define UFS_REG_UTMRLDBR         0x78U
#define UFS_REG_UTMRLRSR         0x80U

typedef struct {
  VENDOR_DEVICE_PATH        Vendor;
  EFI_DEVICE_PATH_PROTOCOL  End;
} WARM_UFS_HC_DEVICE_PATH;

STATIC WARM_UFS_HC_DEVICE_PATH mWarmUfsHcDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8)(sizeof (VENDOR_DEVICE_PATH)),
        (UINT8)((sizeof (VENDOR_DEVICE_PATH)) >> 8)
      }
    },
    { 0x8F4B2A10, 0x3C7D, 0x4E91, { 0xA2, 0x55, 0x60, 0x04, 0x80, 0x40, 0x00, 0x01 } }
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      (UINT8)(sizeof (EFI_DEVICE_PATH_PROTOCOL)),
      (UINT8)((sizeof (EFI_DEVICE_PATH_PROTOCOL)) >> 8)
    }
  }
};

STATIC EFI_HANDLE mWarmUfsControllerHandle = NULL;

STATIC
EFI_STATUS
EFIAPI
WarmUfsHcGetMmioBar (
  IN  EDKII_UFS_HOST_CONTROLLER_PROTOCOL  *This,
  OUT UINTN                               *MmioBar
  )
{
  if ((This == NULL) || (MmioBar == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *MmioBar = (UINTN)WARM_UFS_HC_BASE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
WarmUfsHcAllocateBuffer (
  IN  EDKII_UFS_HOST_CONTROLLER_PROTOCOL  *This,
  IN  EFI_ALLOCATE_TYPE                   Type,
  IN  EFI_MEMORY_TYPE                     MemoryType,
  IN  UINTN                               Pages,
  OUT VOID                                **HostAddress,
  IN  UINT64                              Attributes
  )
{
  if ((This == NULL) || (HostAddress == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Attributes & ~(EFI_PCI_IO_ATTRIBUTE_MEMORY_WRITE_COMBINE | EFI_PCI_IO_ATTRIBUTE_MEMORY_CACHED)) != 0) {
    return EFI_UNSUPPORTED;
  }

  return DmaAllocateBuffer (MemoryType, Pages, HostAddress);
}

STATIC
EFI_STATUS
EFIAPI
WarmUfsHcFreeBuffer (
  IN EDKII_UFS_HOST_CONTROLLER_PROTOCOL  *This,
  IN UINTN                               Pages,
  IN VOID                                *HostAddress
  )
{
  if ((This == NULL) || (HostAddress == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  return DmaFreeBuffer (Pages, HostAddress);
}

STATIC
EFI_STATUS
EFIAPI
WarmUfsHcMap (
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL   *This,
  IN     EDKII_UFS_HOST_CONTROLLER_OPERATION  Operation,
  IN     VOID                                 *HostAddress,
  IN OUT UINTN                                *NumberOfBytes,
  OUT    EFI_PHYSICAL_ADDRESS                 *DeviceAddress,
  OUT    VOID                                 **Mapping
  )
{
  DMA_MAP_OPERATION  DmaOp;

  if ((This == NULL) || (HostAddress == NULL) || (NumberOfBytes == NULL) ||
      (DeviceAddress == NULL) || (Mapping == NULL))
  {
    return EFI_INVALID_PARAMETER;
  }

  switch (Operation) {
    case EdkiiUfsHcOperationBusMasterRead:
      DmaOp = MapOperationBusMasterRead;
      break;
    case EdkiiUfsHcOperationBusMasterWrite:
      DmaOp = MapOperationBusMasterWrite;
      break;
    case EdkiiUfsHcOperationBusMasterCommonBuffer:
      DmaOp = MapOperationBusMasterCommonBuffer;
      break;
    default:
      return EFI_INVALID_PARAMETER;
  }

  return DmaMap (DmaOp, HostAddress, NumberOfBytes, DeviceAddress, Mapping);
}

STATIC
EFI_STATUS
EFIAPI
WarmUfsHcUnmap (
  IN EDKII_UFS_HOST_CONTROLLER_PROTOCOL  *This,
  IN VOID                                *Mapping
  )
{
  if ((This == NULL) || (Mapping == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  return DmaUnmap (Mapping);
}

STATIC
EFI_STATUS
EFIAPI
WarmUfsHcFlush (
  IN EDKII_UFS_HOST_CONTROLLER_PROTOCOL  *This
  )
{
  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  MemoryFence ();
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
WarmUfsHcMmioAccess (
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL        *This,
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL_WIDTH  Width,
  IN     UINT64                                    Offset,
  IN     UINTN                                     Count,
  IN OUT VOID                                      *Buffer,
  IN     BOOLEAN                                   Write
  )
{
  UINTN  Stride;
  UINTN  Index;

  if ((This == NULL) || (Buffer == NULL) || (Width >= EfiUfsHcWidthMaximum)) {
    return EFI_INVALID_PARAMETER;
  }

  Stride = (UINTN)1 << Width;
  if ((Offset >= WARM_UFS_HC_SIZE) || (Count > ((WARM_UFS_HC_SIZE - (UINTN)Offset) / Stride))) {
    return EFI_UNSUPPORTED;
  }

  for (Index = 0; Index < Count; Index++) {
    UINTN  Addr = (UINTN)WARM_UFS_HC_BASE + (UINTN)Offset + (Index * Stride);

    switch (Width) {
      case EfiUfsHcWidthUint8:
        if (Write) {
          MmioWrite8 (Addr, ((UINT8 *)Buffer)[Index]);
        } else {
          ((UINT8 *)Buffer)[Index] = MmioRead8 (Addr);
        }
        break;
      case EfiUfsHcWidthUint16:
        if (Write) {
          MmioWrite16 (Addr, ((UINT16 *)Buffer)[Index]);
        } else {
          ((UINT16 *)Buffer)[Index] = MmioRead16 (Addr);
        }
        break;
      case EfiUfsHcWidthUint32:
        if (Write) {
          MmioWrite32 (Addr, ((UINT32 *)Buffer)[Index]);
        } else {
          ((UINT32 *)Buffer)[Index] = MmioRead32 (Addr);
        }
        break;
      case EfiUfsHcWidthUint64:
        if (Write) {
          MmioWrite64 (Addr, ((UINT64 *)Buffer)[Index]);
        } else {
          ((UINT64 *)Buffer)[Index] = MmioRead64 (Addr);
        }
        break;
      default:
        return EFI_INVALID_PARAMETER;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
WarmUfsHcRead (
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL        *This,
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL_WIDTH  Width,
  IN     UINT64                                    Offset,
  IN     UINTN                                     Count,
  IN OUT VOID                                      *Buffer
  )
{
  return WarmUfsHcMmioAccess (This, Width, Offset, Count, Buffer, FALSE);
}

STATIC
EFI_STATUS
EFIAPI
WarmUfsHcWrite (
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL        *This,
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL_WIDTH  Width,
  IN     UINT64                                    Offset,
  IN     UINTN                                     Count,
  IN OUT VOID                                      *Buffer
  )
{
  return WarmUfsHcMmioAccess (This, Width, Offset, Count, Buffer, TRUE);
}

STATIC EDKII_UFS_HOST_CONTROLLER_PROTOCOL mWarmUfsHcProtocol = {
  WarmUfsHcGetMmioBar,
  WarmUfsHcAllocateBuffer,
  WarmUfsHcFreeBuffer,
  WarmUfsHcMap,
  WarmUfsHcUnmap,
  WarmUfsHcFlush,
  WarmUfsHcRead,
  WarmUfsHcWrite
};

STATIC
VOID
StopRunList (
  IN UINTN  DbrOffset,
  IN UINTN  RsrOffset
  )
{
  UINTN  Timeout;

  for (Timeout = 0; Timeout < 100000; Timeout++) {
    if (MmioRead32 ((UINTN)WARM_UFS_HC_BASE + DbrOffset) == 0) {
      break;
    }
    MicroSecondDelay (1);
  }

  if ((MmioRead32 ((UINTN)WARM_UFS_HC_BASE + RsrOffset) & BIT0) != 0) {
    MmioWrite32 ((UINTN)WARM_UFS_HC_BASE + RsrOffset, 0);
    for (Timeout = 0; Timeout < 50000; Timeout++) {
      if ((MmioRead32 ((UINTN)WARM_UFS_HC_BASE + RsrOffset) & BIT0) == 0) {
        break;
      }
      MicroSecondDelay (1);
    }
  }
}

STATIC
EFI_STATUS
EFIAPI
WarmUfsHcPlatformCallback (
  IN     EFI_HANDLE                            ControllerHandle,
  IN     EDKII_UFS_HC_PLATFORM_CALLBACK_PHASE  CallbackPhase,
  IN OUT VOID                                  *CallbackData
  )
{
  UINT32  Is;

  if (CallbackPhase == EdkiiUfsHcPreHce) {
    if ((gST != NULL) && (gST->ConOut != NULL)) {
      Print (
        L"WarmUfsHc: CAP=%08x VER=%08x HCS=%08x HCE=%08x IS=%08x\n",
        MmioRead32 ((UINTN)WARM_UFS_HC_BASE + UFS_REG_CAP),
        MmioRead32 ((UINTN)WARM_UFS_HC_BASE + UFS_REG_VER),
        MmioRead32 ((UINTN)WARM_UFS_HC_BASE + UFS_REG_HCS),
        MmioRead32 ((UINTN)WARM_UFS_HC_BASE + UFS_REG_HCE),
        MmioRead32 ((UINTN)WARM_UFS_HC_BASE + UFS_REG_IS)
        );
    }
  } else if (CallbackPhase == EdkiiUfsHcPostHce) {
    //
    // If ABL left HCE=1, its old transfer/task-management lists may still have
    // UTRLRSR/UTMRLRSR set. Stop both run bits cleanly before UfsPassThruDxe
    // reprograms UTRLBA/UTMRLBA.
    //
    StopRunList (UFS_REG_UTRLDBR, UFS_REG_UTRLRSR);
    StopRunList (UFS_REG_UTMRLDBR, UFS_REG_UTMRLRSR);

    Is = MmioRead32 ((UINTN)WARM_UFS_HC_BASE + UFS_REG_IS);
    if (Is != 0) {
      MmioWrite32 ((UINTN)WARM_UFS_HC_BASE + UFS_REG_IS, Is);
    }
  } else if (CallbackPhase == EdkiiUfsHcPostLinkStartup) {
    if ((gST != NULL) && (gST->ConOut != NULL)) {
      Print (L"WarmUfsHc: link ready HCS=%08x\n", MmioRead32 ((UINTN)WARM_UFS_HC_BASE + UFS_REG_HCS));
    }
  }

  return EFI_SUCCESS;
}

STATIC EDKII_UFS_HC_PLATFORM_PROTOCOL mWarmUfsHcPlatformProtocol = {
  EDKII_UFS_HC_PLATFORM_PROTOCOL_VERSION,
  NULL,
  WarmUfsHcPlatformCallback,
  EdkiiUfsCardRefClkFreqObsolete,
  TRUE,
  TRUE
};

EFI_STATUS
EFIAPI
WarmUfsHcEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  UINT32      Hce;
  UINT32      Hcs;

  Hce = MmioRead32 ((UINTN)WARM_UFS_HC_BASE + UFS_REG_HCE);
  Hcs = MmioRead32 ((UINTN)WARM_UFS_HC_BASE + UFS_REG_HCS);

  mWarmUfsHcPlatformProtocol.SkipHceReenable = ((Hce & BIT0) != 0);
  mWarmUfsHcPlatformProtocol.SkipLinkStartup = ((Hcs & BIT0) != 0);

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mWarmUfsControllerHandle,
                  &gEfiDevicePathProtocolGuid,
                  &mWarmUfsHcDevicePath,
                  &gEdkiiUfsHostControllerProtocolGuid,
                  &mWarmUfsHcProtocol,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->InstallProtocolInterface (
                  &ImageHandle,
                  &gEdkiiUfsHcPlatformProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mWarmUfsHcPlatformProtocol
                  );
  return Status;
}
