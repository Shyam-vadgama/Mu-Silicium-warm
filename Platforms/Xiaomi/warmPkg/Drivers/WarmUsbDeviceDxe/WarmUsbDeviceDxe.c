/** @file
  Polled DWC3 EFI_USB_DEVICE_PROTOCOL driver for Xiaomi Redmi A4 5G (SM4635 / warm).

  Exposes Qualcomm's EFI_USB_DEVICE_PROTOCOL over USB30_PRIM DWC3 at 0x04E00000
  in High-Speed peripheral mode so xnuloader's fastboot USB server and ./xl
  tools (run, log, screenshot, reboot) work without proprietary XBL DXE blobs.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <IndustryStandard/Usb.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>

extern EFI_GUID gEfiUsbDeviceProtocolGuid;

#define WARM_GCC_BASE                     0x01400000ULL
#define GCC_SYS_NOC_USB3_PRIM_AXI_CBCR    0x1A000U
#define GCC_CFG_NOC_USB3_PRIM_AXI_CBCR    0x1A004U
#define GCC_USB30_PRIM_GDSCR              0x1A010U
#define GCC_USB30_PRIM_MASTER_CBCR        0x1A024U
#define GCC_USB30_PRIM_SLEEP_CBCR         0x1A02CU
#define GCC_USB30_PRIM_MOCK_UTMI_CBCR     0x1A030U
#define GCC_USB30_PRIM_MASTER_CMD_RCGR    0x1A034U
#define GCC_USB30_PRIM_MOCK_UTMI_RCGR     0x1A04CU
#define GCC_USB3_PRIM_PHY_COM_AUX_CBCR    0x1A068U
#define GCC_USB30_PRIM_ATB_CBCR           0x1A08CU
#define GCC_USB2_CLKREF_EN                0x1B034U
#define GCC_AHB2PHY_USB_CBCR              0x1D008U

#define EFI_USB_DEVICE_PROTOCOL_REVISION  0x000000010000001ULL

typedef EFI_USB_STRING_DESCRIPTOR USB_STRING_DESCRIPTOR;

typedef enum {
  UsbDeviceEventNoEvent,
  UsbDeviceEventDeviceStateChange,
  UsbDeviceEventTransferNotification,
  UsbDeviceEventOemEvent
} USB_DEVICE_EVENT;

typedef enum {
  UsbDeviceTransferStatusActive,
  UsbDeviceTransferStatusCompleteOK,
  UsbDeviceTransferStatusCancelled,
  UsbDeviceTransferStatusCompleteError
} USB_DEVICE_TRANSFER_STATUS;

typedef enum {
  UsbDeviceStateConnected,
  UsbDeviceStateDisconnected
} USB_DEVICE_STATE;

typedef struct {
  USB_DEVICE_TRANSFER_STATUS  Status;
  UINT8                       EndpointIndex;
  UINTN                       BytesCompleted;
  VOID                       *DataBuffer;
} USB_DEVICE_TRANSFER_OUTCOME;

typedef union {
  USB_DEVICE_STATE             DeviceState;
  USB_DEVICE_TRANSFER_OUTCOME  TransferOutcome;
  UINT64                       Raw[8];
} USB_DEVICE_EVENT_DATA;

typedef struct {
  USB_DEVICE_DESCRIPTOR      *DeviceDescriptor;
  VOID                      **Descriptors;
  USB_DEVICE_DESCRIPTOR      *SSDeviceDescriptor;
  VOID                      **SSDescriptors;
  VOID                       *DeviceQualifierDescriptor;
  VOID                       *BinaryDeviceOjectStore;
  UINT8                       StringDescriptorCount;
  USB_STRING_DESCRIPTOR     **StringDescritors;
} USB_DEVICE_DESCRIPTOR_SET;

typedef EFI_STATUS (*USB_DEVICE_START) (
  IN USB_DEVICE_DESCRIPTOR      *DeviceDescriptor,
  IN VOID                      **Descriptors,
  IN VOID                       *DeviceQualifierDescriptor,
  IN VOID                       *BinaryDeviceOjectStore,
  IN UINT8                       StringDescriptorCount OPTIONAL,
  IN USB_STRING_DESCRIPTOR     **StringDescriptors OPTIONAL
  );

typedef EFI_STATUS (*USB_DEVICE_SEND) (
  IN UINT8  EndpointIndex,
  IN UINTN  Size,
  IN VOID  *Buffer
  );

typedef EFI_STATUS (EFIAPI *USB_DEVICE_HANDLE_EVENT) (
  OUT USB_DEVICE_EVENT      *Event,
  OUT UINTN                 *DataSize,
  OUT USB_DEVICE_EVENT_DATA *EventData
  );

typedef EFI_STATUS (EFIAPI *USB_DEVICE_ALLOCATE_TRANSFER_BUFFER) (
  IN  UINTN  Size,
  OUT VOID **Buffer
  );

typedef EFI_STATUS (EFIAPI *USB_DEVICE_FREE_TRANSFER_BUFFER) (
  IN VOID *Buffer
  );

typedef EFI_STATUS (*USB_DEVICE_STOP) (VOID);

typedef EFI_STATUS (*USB_DEVICE_ABORT_XFER) (
  IN UINT8 EndpointIndex
  );

typedef EFI_STATUS (EFIAPI *USB_DEVICE_SET_ENDPOINT_STALL_STATE) (
  IN UINT8   EndpointIndex,
  IN BOOLEAN State
  );

typedef EFI_STATUS (*USB_DEVICE_START_EX) (
  IN USB_DEVICE_DESCRIPTOR_SET *UsbDevDescSet
  );

typedef struct {
  UINTN                                 Revision;
  USB_DEVICE_START                      Start;
  USB_DEVICE_SEND                       Send;
  USB_DEVICE_HANDLE_EVENT               HandleEvent;
  USB_DEVICE_ALLOCATE_TRANSFER_BUFFER   AllocateTransferBuffer;
  USB_DEVICE_FREE_TRANSFER_BUFFER       FreeTransferBuffer;
  USB_DEVICE_STOP                       Stop;
  USB_DEVICE_ABORT_XFER                 AbortXfer;
  USB_DEVICE_SET_ENDPOINT_STALL_STATE   SetEndpointStallState;
  USB_DEVICE_START_EX                   StartEx;
} EFI_USB_DEVICE_PROTOCOL;

#define WARM_DWC3_BASE                    0x04E00000ULL

#define DWC3_GCTL                         0xC110U
#define DWC3_GSNPSID                      0xC120U
#define DWC3_GHWPARAMS3                   0xC14CU
#define DWC3_GUSB2PHYCFG(n)               (0xC200U + ((n) * 0x04U))
#define DWC3_GUSB3PIPECTL(n)              (0xC2C0U + ((n) * 0x04U))
#define DWC3_GTXFIFOSIZ(n)                (0xC300U + ((n) * 0x04U))
#define DWC3_GEVNTADRLO(n)                (0xC400U + ((n) * 0x10U))
#define DWC3_GEVNTADRHI(n)                (0xC404U + ((n) * 0x10U))
#define DWC3_GEVNTSIZ(n)                  (0xC408U + ((n) * 0x10U))
#define DWC3_GEVNTCOUNT(n)                (0xC40CU + ((n) * 0x10U))

#define DWC3_DCFG                         0xC700U
#define DWC3_DCTL                         0xC704U
#define DWC3_DEVTEN                       0xC708U
#define DWC3_DSTS                         0xC70CU
#define DWC3_DALEPENA                     0xC720U

#define DWC3_DEP_BASE(n)                  (0xC800U + ((n) * 0x10U))
#define DWC3_DEPCMDPAR2                   0x00U
#define DWC3_DEPCMDPAR1                   0x04U
#define DWC3_DEPCMDPAR0                   0x08U
#define DWC3_DEPCMD                       0x0CU

#define DWC3_GSNPSID_MASK                 0xFFFF0000U
#define DWC3_GSNPSID_USB3                 0x55330000U
#define DWC3_GSNPSID_USB31                0x33310000U

#define DWC3_GCTL_PRTCAPDIR(n)            ((UINT32)(n) << 12)
#define DWC3_GCTL_PRTCAP_DEVICE           2U
#define DWC3_GCTL_PRTCAP_OTG              3U
#define DWC3_GCTL_SCALEDOWN_MASK          (3U << 4)
#define DWC3_GCTL_DISSCRAMBLE             BIT3
#define DWC3_GCTL_GBLHIBERNATIONEN        BIT1
#define DWC3_GCTL_DSBLCLKGTNG             BIT0

#define DWC3_GUSB2PHYCFG_PHYSOFTRST       BIT31
#define DWC3_GUSB2PHYCFG_U2_FREECLK       BIT30
#define DWC3_GUSB2PHYCFG_USBTRDTIM(n)     ((UINT32)(n) << 10)
#define DWC3_GUSB2PHYCFG_USBTRDTIM_MASK   (0xFU << 10)
#define DWC3_GUSB2PHYCFG_ENBLSLPM         BIT8
#define DWC3_GUSB2PHYCFG_SUSPHY           BIT6
#define DWC3_GUSB2PHYCFG_ULPI_UTMI        BIT4
#define DWC3_GUSB2PHYCFG_PHYIF            BIT3

#define DWC3_GUSB3PIPECTL_SUSPHY          BIT17

#define DWC3_GEVNTSIZ_INTMASK             BIT31
#define DWC3_GEVNTSIZ_SIZE(n)             ((n) & 0xFFFFU)
#define DWC3_GEVNTCOUNT_MASK              0xFFFCU

#define DWC3_GHWPARAMS3_NUM_EPS(n)        (((n) >> 12) & 0x3FU)

#define DWC3_DCFG_DEVADDR(a)              (((UINT32)(a) & 0x7FU) << 3)
#define DWC3_DCFG_DEVADDR_MASK            (0x7FU << 3)
#define DWC3_DCFG_SPEED_MASK              7U
#define DWC3_DCFG_HIGHSPEED               0U

#define DWC3_DCTL_RUN_STOP                BIT31
#define DWC3_DCTL_CSFTRST                 BIT30
#define DWC3_DCTL_TSTCTRL_MASK            (0x0FU << 1)

#define DWC3_DEVTEN_WKUPEVTEN             BIT4
#define DWC3_DEVTEN_ULSTCNGEN             BIT3
#define DWC3_DEVTEN_CONNECTDONEEN         BIT2
#define DWC3_DEVTEN_USBRSTEN              BIT1
#define DWC3_DEVTEN_DISCONNEVTEN          BIT0

#define DWC3_DSTS_DEVCTRLHLT              BIT22
#define DWC3_DSTS_CONNECTSPD_MASK         7U

#define DWC3_DEPCMD_PARAM(x)              ((UINT32)(x) << 16)
#define DWC3_DEPCMD_GET_RSC_IDX(x)        (((x) >> 16) & 0x7FU)
#define DWC3_DEPCMD_STATUS(x)             (((x) >> 12) & 0x0FU)
#define DWC3_DEPCMD_HIPRI_FORCERM         BIT11
#define DWC3_DEPCMD_CMDACT                BIT10
#define DWC3_DEPCMD_CMDIOC                BIT8

#define DWC3_DEPCMD_DEPSTARTCFG           0x09U
#define DWC3_DEPCMD_ENDTRANSFER           0x08U
#define DWC3_DEPCMD_STARTTRANSFER         0x06U
#define DWC3_DEPCMD_CLEARSTALL            0x05U
#define DWC3_DEPCMD_SETSTALL              0x04U
#define DWC3_DEPCMD_SETTRANSFRESOURCE     0x02U
#define DWC3_DEPCMD_SETEPCONFIG           0x01U

#define DWC3_EP_TYPE_CONTROL              0U
#define DWC3_EP_TYPE_BULK                 2U

#define DWC3_DEPCFG_EP_TYPE(n)            (((UINT32)(n) & 0x3U) << 1)
#define DWC3_DEPCFG_MAX_PACKET_SIZE(n)    (((UINT32)(n) & 0x7FFU) << 3)
#define DWC3_DEPCFG_FIFO_NUMBER(n)        (((UINT32)(n) & 0x1FU) << 17)
#define DWC3_DEPCFG_ACTION_INIT           (0U << 30)
#define DWC3_DEPCFG_ACTION_MODIFY         (2U << 30)

#define DWC3_DEPCFG_XFER_COMPLETE_EN      BIT8
#define DWC3_DEPCFG_XFER_IN_PROGRESS_EN   BIT9
#define DWC3_DEPCFG_XFER_NOT_READY_EN     BIT10
#define DWC3_DEPCFG_EP_NUMBER(n)          (((UINT32)(n) & 0x1FU) << 25)

#define DWC3_TRB_SIZE_MASK                0x00FFFFFFU
#define DWC3_TRB_CTRL_HWO                 BIT0
#define DWC3_TRB_CTRL_LST                 BIT1
#define DWC3_TRB_CTRL_ISP_IMI             BIT10
#define DWC3_TRB_CTRL_IOC                 BIT11
#define DWC3_TRB_CTRL_TRBCTL(n)           (((UINT32)(n) & 0x3FU) << 4)

#define DWC3_TRBCTL_NORMAL                DWC3_TRB_CTRL_TRBCTL (1)
#define DWC3_TRBCTL_CONTROL_SETUP         DWC3_TRB_CTRL_TRBCTL (2)
#define DWC3_TRBCTL_CONTROL_STATUS2       DWC3_TRB_CTRL_TRBCTL (3)
#define DWC3_TRBCTL_CONTROL_STATUS3       DWC3_TRB_CTRL_TRBCTL (4)
#define DWC3_TRBCTL_CONTROL_DATA          DWC3_TRB_CTRL_TRBCTL (5)

#define DWC3_EVENT_IS_ENDPOINT(e)         (((e) & BIT0) == 0U)
#define DWC3_DEPEVT_EPNUMBER(e)           (((e) >> 1) & 0x1FU)
#define DWC3_DEPEVT_TYPE(e)               (((e) >> 6) & 0x0FU)
#define DWC3_DEPEVT_STATUS(e)             (((e) >> 12) & 0x0FU)

#define DWC3_DEPEVT_XFERCOMPLETE          0x01U
#define DWC3_DEPEVT_XFERINPROGRESS        0x02U
#define DWC3_DEPEVT_XFERNOTREADY          0x03U

#define DEPEVT_STATUS_BUSERR              BIT0
#define DEPEVT_STATUS_CONTROL_STATUS      2U
#define DEPEVT_STATUS_CONTROL_PHASE(n)    ((n) & 3U)

#define DWC3_DEVT_CATEGORY(e)             (((e) >> 1) & 0x7FU)
#define DWC3_DEVT_TYPE(e)                 (((e) >> 8) & 0x0FU)

#define DWC3_DEVICE_EVENT_DISCONNECT      0U
#define DWC3_DEVICE_EVENT_RESET           1U
#define DWC3_DEVICE_EVENT_CONNECT_DONE    2U

#define WARM_USB_MAX_PHYS_EP              8U
#define WARM_USB_EVENT_BUF_SIZE           SIZE_4KB
#define WARM_USB_EP0_BUF_SIZE             SIZE_4KB
#define WARM_USB_MSG_QUEUE_LEN            32U
#define WARM_USB_MAX_XFER_BUFS            16U

#pragma pack(1)
typedef struct {
  UINT32  BufferPtrLow;
  UINT32  BufferPtrHigh;
  UINT32  Size;
  UINT32  Control;
} WARM_DWC3_TRB;
#pragma pack()

typedef enum {
  Ep0PhaseSetup = 0,
  Ep0PhaseData,
  Ep0PhaseStatus
} EP0_PHASE;

typedef struct {
  BOOLEAN        Enabled;
  BOOLEAN        TransferActive;
  BOOLEAN        Stalled;
  UINT8          PhysEp;
  UINT8          EndpointAddress;
  UINT16         MaxPacketSize;
  UINT8          ResourceIndex;
  WARM_DWC3_TRB *Trb;
  VOID          *ClientBuffer;
  UINTN          ClientSize;
  UINTN          TrbLength;
} WARM_EP;

typedef struct {
  USB_DEVICE_EVENT      Event;
  USB_DEVICE_EVENT_DATA Data;
} WARM_USB_MSG;

typedef struct {
  VOID   *Buffer;
  UINTN   Pages;
} WARM_XFER_BUF;

typedef struct {
  BOOLEAN                   Initialized;
  BOOLEAN                   Connected;
  BOOLEAN                   TwoStageSetup;
  BOOLEAN                   Ep0StatusStarted;
  UINT8                     NumPhysicalEps;
  UINT8                     CurrentConfig;
  EP0_PHASE                 Ep0Phase;
  USB_DEVICE_DESCRIPTOR_SET DescSet;
  UINT8                    *EventBuf;
  UINT32                    EventPos;
  UINT8                    *Ep0Buf;
  WARM_EP                   Eps[WARM_USB_MAX_PHYS_EP];
  WARM_USB_MSG              MsgQueue[WARM_USB_MSG_QUEUE_LEN];
  UINT32                    MsgHead;
  UINT32                    MsgTail;
  WARM_XFER_BUF             XferBufs[WARM_USB_MAX_XFER_BUFS];
} WARM_USB_STATE;

STATIC WARM_USB_STATE mUsb;

STATIC
VOID *
AllocDmaPages (
  IN UINTN Size
  )
{
  UINTN  Pages = EFI_SIZE_TO_PAGES (Size);
  VOID  *Ptr   = AllocateAlignedPages (Pages, SIZE_4KB);
  if (Ptr == NULL) {
    return NULL;
  }
  ZeroMem (Ptr, EFI_PAGES_TO_SIZE (Pages));
  WriteBackDataCacheRange (Ptr, EFI_PAGES_TO_SIZE (Pages));
  return Ptr;
}

STATIC
EFI_STATUS
WarmUsbEnableClocks (VOID)
{
  UINT32 Gdsc;
  UINT32 Master;
  UINT32 Timeout;

  Gdsc = MmioRead32 ((UINTN)WARM_GCC_BASE + GCC_USB30_PRIM_GDSCR);
  if ((Gdsc & BIT0) != 0U) {
    MmioAnd32 ((UINTN)WARM_GCC_BASE + GCC_USB30_PRIM_GDSCR, ~BIT0);
  }
  for (Timeout = 0; Timeout < 10000U; Timeout++) {
    Gdsc = MmioRead32 ((UINTN)WARM_GCC_BASE + GCC_USB30_PRIM_GDSCR);
    if ((Gdsc & BIT31) != 0U) {
      break;
    }
    gBS->Stall (1);
  }
  if ((Gdsc & BIT31) == 0U) {
    Print (L"[WarmUsb] USB GDSC off: 0x%08x\n", Gdsc);
    return EFI_DEVICE_ERROR;
  }

  MmioOr32 ((UINTN)WARM_GCC_BASE + GCC_AHB2PHY_USB_CBCR, BIT0);
  MmioOr32 ((UINTN)WARM_GCC_BASE + GCC_USB2_CLKREF_EN, BIT0);
  MmioOr32 ((UINTN)WARM_GCC_BASE + GCC_SYS_NOC_USB3_PRIM_AXI_CBCR, BIT0);
  MmioOr32 ((UINTN)WARM_GCC_BASE + GCC_CFG_NOC_USB3_PRIM_AXI_CBCR, BIT0);
  MmioOr32 ((UINTN)WARM_GCC_BASE + GCC_USB30_PRIM_SLEEP_CBCR, BIT0);
  MmioOr32 ((UINTN)WARM_GCC_BASE + GCC_USB30_PRIM_MOCK_UTMI_CBCR, BIT0);
  MmioOr32 ((UINTN)WARM_GCC_BASE + GCC_USB30_PRIM_ATB_CBCR, BIT0);
  MmioOr32 ((UINTN)WARM_GCC_BASE + GCC_USB3_PRIM_PHY_COM_AUX_CBCR, BIT0);
  MmioOr32 ((UINTN)WARM_GCC_BASE + GCC_USB30_PRIM_MASTER_CBCR, BIT0);

  if ((MmioRead32 ((UINTN)WARM_GCC_BASE + GCC_USB30_PRIM_MASTER_CMD_RCGR) & BIT31) != 0U) {
    MmioOr32 ((UINTN)WARM_GCC_BASE + GCC_USB30_PRIM_MASTER_CMD_RCGR, BIT0);
  }
  if ((MmioRead32 ((UINTN)WARM_GCC_BASE + GCC_USB30_PRIM_MOCK_UTMI_RCGR) & BIT31) != 0U) {
    MmioOr32 ((UINTN)WARM_GCC_BASE + GCC_USB30_PRIM_MOCK_UTMI_RCGR, BIT0);
  }

  for (Timeout = 0; Timeout < 10000U; Timeout++) {
    Master = MmioRead32 ((UINTN)WARM_GCC_BASE + GCC_USB30_PRIM_MASTER_CBCR);
    if ((Master & BIT31) == 0U) {
      return EFI_SUCCESS;
    }
    gBS->Stall (1);
  }

  Print (L"[WarmUsb] USB master clk off: 0x%08x GDSC=0x%08x\n", Master, Gdsc);
  return EFI_DEVICE_ERROR;
}

STATIC
VOID
PushMsg (
  IN USB_DEVICE_EVENT             Event,
  IN CONST USB_DEVICE_EVENT_DATA *Data OPTIONAL
  )
{
  UINT32 Next = (mUsb.MsgTail + 1U) % WARM_USB_MSG_QUEUE_LEN;
  if (Next == mUsb.MsgHead) {
    return;
  }
  mUsb.MsgQueue[mUsb.MsgTail].Event = Event;
  if (Data != NULL) {
    CopyMem (&mUsb.MsgQueue[mUsb.MsgTail].Data, Data, sizeof (*Data));
  } else {
    ZeroMem (&mUsb.MsgQueue[mUsb.MsgTail].Data, sizeof (USB_DEVICE_EVENT_DATA));
  }
  mUsb.MsgTail = Next;
}

STATIC
BOOLEAN
PopMsg (
  OUT USB_DEVICE_EVENT      *Event,
  OUT USB_DEVICE_EVENT_DATA *Data
  )
{
  if (mUsb.MsgHead == mUsb.MsgTail) {
    return FALSE;
  }
  *Event = mUsb.MsgQueue[mUsb.MsgHead].Event;
  CopyMem (Data, &mUsb.MsgQueue[mUsb.MsgHead].Data, sizeof (*Data));
  mUsb.MsgHead = (mUsb.MsgHead + 1U) % WARM_USB_MSG_QUEUE_LEN;
  return TRUE;
}

STATIC
EFI_STATUS
SendEpCmd (
  IN  UINT8   PhysEp,
  IN  UINT32  Cmd,
  IN  UINT32  Par0,
  IN  UINT32  Par1,
  IN  UINT32  Par2,
  OUT UINT32 *CmdReg OPTIONAL
  )
{
  UINTN  Base = (UINTN)WARM_DWC3_BASE + DWC3_DEP_BASE (PhysEp);
  UINT32 Val;
  UINT32 Timeout;

  MmioWrite32 (Base + DWC3_DEPCMDPAR0, Par0);
  MmioWrite32 (Base + DWC3_DEPCMDPAR1, Par1);
  MmioWrite32 (Base + DWC3_DEPCMDPAR2, Par2);
  MemoryFence ();
  MmioWrite32 (Base + DWC3_DEPCMD, Cmd | DWC3_DEPCMD_CMDACT);

  for (Timeout = 0; Timeout < 200000U; Timeout++) {
    Val = MmioRead32 (Base + DWC3_DEPCMD);
    if ((Val & DWC3_DEPCMD_CMDACT) == 0U) {
      if (CmdReg != NULL) {
        *CmdReg = Val;
      }
      return (DWC3_DEPCMD_STATUS (Val) == 0U) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
    }
    gBS->Stall (1);
  }
  return EFI_TIMEOUT;
}

STATIC
VOID
EndTransfer (
  IN UINT8 PhysEp
  )
{
  WARM_EP *Ep = &mUsb.Eps[PhysEp];
  UINT32   Cmd;

  if (Ep->ResourceIndex != 0U) {
    Cmd = DWC3_DEPCMD_ENDTRANSFER | DWC3_DEPCMD_HIPRI_FORCERM | DWC3_DEPCMD_CMDIOC |
          DWC3_DEPCMD_PARAM (Ep->ResourceIndex);
    SendEpCmd (PhysEp, Cmd, 0, 0, 0, NULL);
    gBS->Stall (100);
    Ep->ResourceIndex = 0;
  }
  Ep->TransferActive = FALSE;
  if (Ep->Trb != NULL) {
    Ep->Trb->Control = 0;
    WriteBackDataCacheRange (Ep->Trb, SIZE_4KB);
  }
}

STATIC
EFI_STATUS
StartTransfer (
  IN UINT8   PhysEp,
  IN UINT64  BufPhys,
  IN UINT32  Len,
  IN UINT32  TrbType
  )
{
  WARM_EP    *Ep = &mUsb.Eps[PhysEp];
  UINT32      CmdReg = 0;
  EFI_STATUS  Status;

  if (Ep->Trb == NULL) {
    return EFI_NOT_READY;
  }

  Ep->Trb->BufferPtrLow  = (UINT32)(BufPhys & 0xFFFFFFFFULL);
  Ep->Trb->BufferPtrHigh = (UINT32)(BufPhys >> 32);
  Ep->Trb->Size          = Len & DWC3_TRB_SIZE_MASK;
  Ep->Trb->Control       = TrbType | DWC3_TRB_CTRL_HWO | DWC3_TRB_CTRL_ISP_IMI |
                           DWC3_TRB_CTRL_IOC | DWC3_TRB_CTRL_LST;
  WriteBackDataCacheRange (Ep->Trb, SIZE_4KB);
  MemoryFence ();

  Status = SendEpCmd (PhysEp,
                      DWC3_DEPCMD_STARTTRANSFER,
                      (UINT32)(((UINT64)(UINTN)Ep->Trb) >> 32),
                      (UINT32)(((UINT64)(UINTN)Ep->Trb) & 0xFFFFFFFFULL),
                      0,
                      &CmdReg);
  if (EFI_ERROR (Status)) {
    Ep->Trb->Control = 0;
    return Status;
  }

  Ep->ResourceIndex  = (UINT8)DWC3_DEPCMD_GET_RSC_IDX (CmdReg);
  Ep->TransferActive = TRUE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EnableEndpoint (
  IN UINT8   PhysEp,
  IN UINT8   EpType,
  IN UINT16  MaxPacketSize,
  IN BOOLEAN Modify
  )
{
  WARM_EP    *Ep = &mUsb.Eps[PhysEp];
  UINT32      Par0;
  UINT32      Par1;
  EFI_STATUS  Status;

  if (PhysEp >= mUsb.NumPhysicalEps || PhysEp >= WARM_USB_MAX_PHYS_EP) {
    return EFI_INVALID_PARAMETER;
  }

  Par0 = DWC3_DEPCFG_EP_TYPE (EpType) | DWC3_DEPCFG_MAX_PACKET_SIZE (MaxPacketSize);
  Par0 |= Modify ? DWC3_DEPCFG_ACTION_MODIFY : DWC3_DEPCFG_ACTION_INIT;
  if ((PhysEp & 1U) != 0U) {
    Par0 |= DWC3_DEPCFG_FIFO_NUMBER (PhysEp >> 1);
  }

  Par1 = DWC3_DEPCFG_EP_NUMBER (PhysEp);
  if (EpType == DWC3_EP_TYPE_CONTROL) {
    Par1 |= DWC3_DEPCFG_XFER_COMPLETE_EN | DWC3_DEPCFG_XFER_NOT_READY_EN;
  } else {
    Par1 |= DWC3_DEPCFG_XFER_COMPLETE_EN | DWC3_DEPCFG_XFER_IN_PROGRESS_EN;
  }

  Status = SendEpCmd (PhysEp, DWC3_DEPCMD_SETEPCONFIG, Par0, Par1, 0, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Ep->PhysEp        = PhysEp;
  Ep->MaxPacketSize = MaxPacketSize;
  Ep->EndpointAddress = (UINT8)(((PhysEp & 1U) ? 0x80U : 0x00U) | (PhysEp >> 1));

  if (!Modify) {
    MmioOr32 ((UINTN)WARM_DWC3_BASE + DWC3_DALEPENA, BIT0 << PhysEp);
    Ep->Enabled        = TRUE;
    Ep->TransferActive = FALSE;
    Ep->Stalled        = FALSE;
    Ep->ResourceIndex  = 0;
  }
  return EFI_SUCCESS;
}

STATIC
VOID
Ep0ArmSetup (VOID)
{
  mUsb.Ep0Phase         = Ep0PhaseSetup;
  mUsb.Ep0StatusStarted = FALSE;
  EndTransfer (1);
  EndTransfer (0);
  WriteBackInvalidateDataCacheRange (mUsb.Ep0Buf, WARM_USB_EP0_BUF_SIZE);
  StartTransfer (0, (UINT64)(UINTN)mUsb.Ep0Buf, 8U, DWC3_TRBCTL_CONTROL_SETUP);
}

STATIC
VOID
Ep0StartStatus (
  IN UINT8 PhysEp
  )
{
  UINT32 TrbType;

  if (mUsb.Ep0StatusStarted || mUsb.Ep0Phase == Ep0PhaseSetup) {
    return;
  }
  mUsb.Ep0Phase         = Ep0PhaseStatus;
  mUsb.Ep0StatusStarted = TRUE;
  TrbType               = mUsb.TwoStageSetup ? DWC3_TRBCTL_CONTROL_STATUS2 : DWC3_TRBCTL_CONTROL_STATUS3;
  EndTransfer (PhysEp);
  StartTransfer (PhysEp, (UINT64)(UINTN)mUsb.Ep0Buf, 0U, TrbType);
}

STATIC
VOID
Ep0StallAndRestart (VOID)
{
  SendEpCmd (0, DWC3_DEPCMD_SETSTALL, 0, 0, 0, NULL);
  Ep0ArmSetup ();
}

STATIC
VOID
HandleSetupPacket (VOID)
{
  USB_DEVICE_REQUEST      Req;
  CONST VOID             *ReplyPtr = NULL;
  UINTN                   ReplyLen = 0;
  UINT32                  Dcfg;
  UINT8                   DescType;
  UINT8                   DescIdx;

  InvalidateDataCacheRange (mUsb.Ep0Buf, WARM_USB_EP0_BUF_SIZE);
  CopyMem (&Req, mUsb.Ep0Buf, sizeof (Req));
  mUsb.TwoStageSetup    = (BOOLEAN)(Req.Length == 0);
  mUsb.Ep0StatusStarted = FALSE;

  if ((Req.RequestType & (0x03U << 5)) != USB_REQ_TYPE_STANDARD) {
    Ep0StallAndRestart ();
    return;
  }

  switch (Req.Request) {
    case USB_REQ_SET_ADDRESS:
      Dcfg  = MmioRead32 ((UINTN)WARM_DWC3_BASE + DWC3_DCFG);
      Dcfg &= ~DWC3_DCFG_DEVADDR_MASK;
      Dcfg |= DWC3_DCFG_DEVADDR (Req.Value & 0x7FU);
      MmioWrite32 ((UINTN)WARM_DWC3_BASE + DWC3_DCFG, Dcfg);
      mUsb.Ep0Phase = Ep0PhaseStatus;
      return;

    case USB_REQ_SET_CONFIG:
    {
      USB_DEVICE_EVENT_DATA EvData;
      UINT8                 CfgVal = (UINT8)(Req.Value & 0xFFU);

      mUsb.CurrentConfig = CfgVal;
      EndTransfer (2);
      EndTransfer (3);
      if (CfgVal != 0U) {
        EnableEndpoint (2, DWC3_EP_TYPE_BULK, 512U, FALSE);
        EnableEndpoint (3, DWC3_EP_TYPE_BULK, 512U, FALSE);
      }
      mUsb.Connected       = (BOOLEAN)(CfgVal != 0U);
      EvData.DeviceState   = mUsb.Connected ? UsbDeviceStateConnected : UsbDeviceStateDisconnected;
      PushMsg (UsbDeviceEventDeviceStateChange, &EvData);
      mUsb.Ep0Phase = Ep0PhaseStatus;
      return;
    }

    case USB_REQ_SET_INTERFACE:
    case USB_REQ_CLEAR_FEATURE:
    case USB_REQ_SET_FEATURE:
      mUsb.Ep0Phase = Ep0PhaseStatus;
      return;

    case USB_REQ_GET_DESCRIPTOR:
      DescType = (UINT8)(Req.Value >> 8);
      DescIdx  = (UINT8)(Req.Value & 0xFFU);
      if (DescType == USB_DESC_TYPE_DEVICE && mUsb.DescSet.DeviceDescriptor != NULL) {
        ReplyPtr = mUsb.DescSet.DeviceDescriptor;
        ReplyLen = *(CONST UINT8 *)ReplyPtr;
      } else if (DescType == USB_DESC_TYPE_CONFIG && mUsb.DescSet.Descriptors != NULL &&
                 mUsb.DescSet.Descriptors[0] != NULL)
      {
        CONST UINT8 *Cfg = (CONST UINT8 *)mUsb.DescSet.Descriptors[0];
        ReplyPtr = Cfg;
        ReplyLen = (UINTN)Cfg[2] | ((UINTN)Cfg[3] << 8);
      } else if (DescType == USB_DESC_TYPE_STRING &&
                 mUsb.DescSet.StringDescritors != NULL &&
                 DescIdx < mUsb.DescSet.StringDescriptorCount &&
                 mUsb.DescSet.StringDescritors[DescIdx] != NULL)
      {
        CONST UINT8 *Str = (CONST UINT8 *)mUsb.DescSet.StringDescritors[DescIdx];
        ReplyPtr = Str;
        ReplyLen = Str[0];
      } else if (DescType == 0x06U /* DEVICE_QUALIFIER */ &&
                 mUsb.DescSet.DeviceQualifierDescriptor != NULL)
      {
        ReplyPtr = mUsb.DescSet.DeviceQualifierDescriptor;
        ReplyLen = *(CONST UINT8 *)ReplyPtr;
      } else if (DescType == 0x0FU /* BOS */ &&
                 mUsb.DescSet.BinaryDeviceOjectStore != NULL)
      {
        CONST UINT8 *Bos = (CONST UINT8 *)mUsb.DescSet.BinaryDeviceOjectStore;
        ReplyPtr = Bos;
        ReplyLen = (UINTN)Bos[2] | ((UINTN)Bos[3] << 8);
      }
      break;

    case USB_REQ_GET_STATUS:
      mUsb.Ep0Buf[0] = 0;
      mUsb.Ep0Buf[1] = 0;
      ReplyPtr       = mUsb.Ep0Buf;
      ReplyLen       = 2;
      break;

    case USB_REQ_GET_CONFIG:
      mUsb.Ep0Buf[0] = mUsb.CurrentConfig;
      ReplyPtr       = mUsb.Ep0Buf;
      ReplyLen       = 1;
      break;

    case USB_REQ_GET_INTERFACE:
      mUsb.Ep0Buf[0] = 0;
      ReplyPtr       = mUsb.Ep0Buf;
      ReplyLen       = 1;
      break;

    default:
      break;
  }

  if (ReplyPtr == NULL || (Req.RequestType & USB_ENDPOINT_DIR_IN) == 0U || Req.Length == 0U) {
    Ep0StallAndRestart ();
    return;
  }

  if (ReplyLen > Req.Length) {
    ReplyLen = Req.Length;
  }
  if (ReplyLen > WARM_USB_EP0_BUF_SIZE) {
    ReplyLen = WARM_USB_EP0_BUF_SIZE;
  }
  if (ReplyPtr != mUsb.Ep0Buf) {
    CopyMem (mUsb.Ep0Buf, ReplyPtr, ReplyLen);
  }
  WriteBackDataCacheRange (mUsb.Ep0Buf, WARM_USB_EP0_BUF_SIZE);
  mUsb.Ep0Phase = Ep0PhaseData;
  StartTransfer (1, (UINT64)(UINTN)mUsb.Ep0Buf, (UINT32)ReplyLen, DWC3_TRBCTL_CONTROL_DATA);
}

STATIC
VOID
HandleEpEvent (
  IN UINT32 Event
  )
{
  UINT8                 PhysEp    = (UINT8)DWC3_DEPEVT_EPNUMBER (Event);
  UINT8                 EventType = (UINT8)DWC3_DEPEVT_TYPE (Event);
  UINT8                 EvStatus  = (UINT8)DWC3_DEPEVT_STATUS (Event);
  WARM_EP              *Ep;
  USB_DEVICE_EVENT_DATA EvData;
  UINTN                 Remaining;
  UINTN                 Done;

  if (PhysEp >= WARM_USB_MAX_PHYS_EP) {
    return;
  }
  Ep = &mUsb.Eps[PhysEp];

  if (PhysEp <= 1U) {
    if (EventType == DWC3_DEPEVT_XFERCOMPLETE) {
      Ep->TransferActive = FALSE;
      Ep->ResourceIndex  = 0;
      if (mUsb.Ep0Phase == Ep0PhaseSetup) {
        HandleSetupPacket ();
      } else if (mUsb.Ep0Phase == Ep0PhaseData) {
        mUsb.Ep0Phase = Ep0PhaseStatus;
      } else {
        Ep0ArmSetup ();
      }
    } else if (EventType == DWC3_DEPEVT_XFERNOTREADY &&
               DEPEVT_STATUS_CONTROL_PHASE (EvStatus) == DEPEVT_STATUS_CONTROL_STATUS)
    {
      Ep0StartStatus (PhysEp);
    }
    return;
  }

  if (EventType == DWC3_DEPEVT_XFERCOMPLETE || EventType == DWC3_DEPEVT_XFERINPROGRESS) {
    InvalidateDataCacheRange (Ep->Trb, SIZE_4KB);
  }
  if ((EventType == DWC3_DEPEVT_XFERCOMPLETE || EventType == DWC3_DEPEVT_XFERINPROGRESS) &&
      Ep->TransferActive && (Ep->Trb->Control & DWC3_TRB_CTRL_HWO) == 0U)
  {
    Ep->TransferActive = FALSE;
    Ep->ResourceIndex  = 0;
    Remaining          = Ep->Trb->Size & DWC3_TRB_SIZE_MASK;
    Done               = (Remaining <= Ep->TrbLength) ? (Ep->TrbLength - Remaining) : 0U;
    if ((PhysEp & 1U) == 0U) {
      if (Ep->ClientBuffer != NULL && Ep->TrbLength != 0U) {
        InvalidateDataCacheRange (Ep->ClientBuffer, Ep->TrbLength);
      }
    } else if (Done > Ep->ClientSize) {
      Done = Ep->ClientSize;
    }

    ZeroMem (&EvData, sizeof (EvData));
    EvData.TransferOutcome.Status = (EvStatus & DEPEVT_STATUS_BUSERR)
                                    ? UsbDeviceTransferStatusCompleteError
                                    : UsbDeviceTransferStatusCompleteOK;
    EvData.TransferOutcome.EndpointIndex  = Ep->EndpointAddress;
    EvData.TransferOutcome.BytesCompleted = Done;
    EvData.TransferOutcome.DataBuffer     = Ep->ClientBuffer;
    Ep->ClientBuffer = NULL;
    PushMsg (UsbDeviceEventTransferNotification, &EvData);
  }
}

STATIC
VOID
HandleDeviceEvent (
  IN UINT32 Event
  )
{
  UINT8                 EventType = (UINT8)DWC3_DEVT_TYPE (Event);
  USB_DEVICE_EVENT_DATA EvData;
  UINT8                 Idx;

  if (EventType == DWC3_DEVICE_EVENT_DISCONNECT) {
    if (mUsb.Connected) {
      mUsb.Connected     = FALSE;
      mUsb.CurrentConfig = 0;
      EvData.DeviceState = UsbDeviceStateDisconnected;
      PushMsg (UsbDeviceEventDeviceStateChange, &EvData);
    }
  } else if (EventType == DWC3_DEVICE_EVENT_RESET) {
    MmioAnd32 ((UINTN)WARM_DWC3_BASE + DWC3_DCFG, ~DWC3_DCFG_DEVADDR_MASK);
    for (Idx = 2U; Idx < WARM_USB_MAX_PHYS_EP; Idx++) {
      if (mUsb.Eps[Idx].TransferActive) {
        EndTransfer (Idx);
        ZeroMem (&EvData, sizeof (EvData));
        EvData.TransferOutcome.Status        = UsbDeviceTransferStatusCancelled;
        EvData.TransferOutcome.EndpointIndex = mUsb.Eps[Idx].EndpointAddress;
        EvData.TransferOutcome.DataBuffer    = mUsb.Eps[Idx].ClientBuffer;
        mUsb.Eps[Idx].ClientBuffer           = NULL;
        PushMsg (UsbDeviceEventTransferNotification, &EvData);
      }
    }
    if (mUsb.Connected) {
      mUsb.Connected     = FALSE;
      mUsb.CurrentConfig = 0;
      EvData.DeviceState = UsbDeviceStateDisconnected;
      PushMsg (UsbDeviceEventDeviceStateChange, &EvData);
    }
    Ep0ArmSetup ();
  } else if (EventType == DWC3_DEVICE_EVENT_CONNECT_DONE) {
    UINT32 Spd        = MmioRead32 ((UINTN)WARM_DWC3_BASE + DWC3_DSTS) & DWC3_DSTS_CONNECTSPD_MASK;
    UINT16 Ep0MaxPkt  = (Spd >= 4U) ? 512U : 64U;
    UINT16 BulkMaxPkt = (Spd == 0U || Spd >= 4U) ? 512U : 64U;

    EnableEndpoint (0, DWC3_EP_TYPE_CONTROL, Ep0MaxPkt, TRUE);
    EnableEndpoint (1, DWC3_EP_TYPE_CONTROL, Ep0MaxPkt, TRUE);
    if (mUsb.Eps[2].Enabled) {
      EnableEndpoint (2, DWC3_EP_TYPE_BULK, BulkMaxPkt, FALSE);
    }
    if (mUsb.Eps[3].Enabled) {
      EnableEndpoint (3, DWC3_EP_TYPE_BULK, BulkMaxPkt, FALSE);
    }
    if (!mUsb.Eps[0].TransferActive) {
      Ep0ArmSetup ();
    }
  }
}

STATIC
EFI_STATUS
WarmUsbStartEx (
  IN USB_DEVICE_DESCRIPTOR_SET *DescSet
  )
{
  UINT32 OrigGusb2;
  UINT32 SnpsId;
  UINT32 Hw3;
  UINT32 Reg;
  UINT32 Timeout;
  UINT8  Idx;

  if (DescSet == NULL || DescSet->DeviceDescriptor == NULL || DescSet->Descriptors == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (&mUsb.DescSet, DescSet, sizeof (mUsb.DescSet));
  mUsb.Connected     = FALSE;
  mUsb.CurrentConfig = 0;
  mUsb.MsgHead       = 0;
  mUsb.MsgTail       = 0;

  if (EFI_ERROR (WarmUsbEnableClocks ())) {
    return EFI_DEVICE_ERROR;
  }

  if (mUsb.EventBuf == NULL) {
    mUsb.EventBuf = AllocDmaPages (WARM_USB_EVENT_BUF_SIZE);
    mUsb.Ep0Buf   = AllocDmaPages (WARM_USB_EP0_BUF_SIZE);
    if (mUsb.EventBuf == NULL || mUsb.Ep0Buf == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    for (Idx = 0; Idx < WARM_USB_MAX_PHYS_EP; Idx++) {
      mUsb.Eps[Idx].Trb = AllocDmaPages (sizeof (WARM_DWC3_TRB));
      if (mUsb.Eps[Idx].Trb == NULL) {
        return EFI_OUT_OF_RESOURCES;
      }
    }
  }

  SnpsId    = MmioRead32 ((UINTN)WARM_DWC3_BASE + DWC3_GSNPSID);
  OrigGusb2 = MmioRead32 ((UINTN)WARM_DWC3_BASE + DWC3_GUSB2PHYCFG (0));
  Print (L"[WarmUsb] DWC3 ID=0x%08x GUSB2=0x%08x DSTS=0x%08x TXF1=0x%08x\n",
         SnpsId,
         OrigGusb2,
         MmioRead32 ((UINTN)WARM_DWC3_BASE + DWC3_DSTS),
         MmioRead32 ((UINTN)WARM_DWC3_BASE + DWC3_GTXFIFOSIZ (1)));

  if ((SnpsId & DWC3_GSNPSID_MASK) != DWC3_GSNPSID_USB3 &&
      (SnpsId & DWC3_GSNPSID_MASK) != DWC3_GSNPSID_USB31)
  {
    Print (L"[WarmUsb] Unexpected GSNPSID\n");
    return EFI_DEVICE_ERROR;
  }

  MmioAnd32 ((UINTN)WARM_DWC3_BASE + DWC3_DCTL, ~DWC3_DCTL_RUN_STOP);
  for (Timeout = 0; Timeout < 50000U; Timeout++) {
    if ((MmioRead32 ((UINTN)WARM_DWC3_BASE + DWC3_DSTS) & DWC3_DSTS_DEVCTRLHLT) != 0U) {
      break;
    }
    gBS->Stall (1);
  }

  Reg  = MmioRead32 ((UINTN)WARM_DWC3_BASE + DWC3_GCTL);
  Reg &= ~DWC3_GCTL_PRTCAPDIR (DWC3_GCTL_PRTCAP_OTG);
  Reg |= DWC3_GCTL_PRTCAPDIR (DWC3_GCTL_PRTCAP_DEVICE);
  MmioWrite32 ((UINTN)WARM_DWC3_BASE + DWC3_GCTL, Reg);
  gBS->Stall (100);

  MmioOr32 ((UINTN)WARM_DWC3_BASE + DWC3_DCTL, DWC3_DCTL_CSFTRST);
  for (Timeout = 0; Timeout < 500000U; Timeout++) {
    if ((MmioRead32 ((UINTN)WARM_DWC3_BASE + DWC3_DCTL) & DWC3_DCTL_CSFTRST) == 0U) {
      break;
    }
    gBS->Stall (1);
  }
  if ((SnpsId & DWC3_GSNPSID_MASK) == DWC3_GSNPSID_USB31) {
    gBS->Stall (20000);
  }

  Reg = (OrigGusb2 & (DWC3_GUSB2PHYCFG_PHYIF | DWC3_GUSB2PHYCFG_ULPI_UTMI |
                      DWC3_GUSB2PHYCFG_USBTRDTIM_MASK | DWC3_GUSB2PHYCFG_U2_FREECLK));
  if ((Reg & DWC3_GUSB2PHYCFG_USBTRDTIM_MASK) == 0U) {
    Reg |= (Reg & DWC3_GUSB2PHYCFG_PHYIF) ? DWC3_GUSB2PHYCFG_USBTRDTIM (5U)
                                          : DWC3_GUSB2PHYCFG_USBTRDTIM (9U);
  }
  Reg &= ~(DWC3_GUSB2PHYCFG_SUSPHY | DWC3_GUSB2PHYCFG_ENBLSLPM | DWC3_GUSB2PHYCFG_PHYSOFTRST);
  MmioWrite32 ((UINTN)WARM_DWC3_BASE + DWC3_GUSB2PHYCFG (0), Reg);
  MmioOr32 ((UINTN)WARM_DWC3_BASE + DWC3_GUSB3PIPECTL (0), DWC3_GUSB3PIPECTL_SUSPHY);

  Reg  = MmioRead32 ((UINTN)WARM_DWC3_BASE + DWC3_GCTL);
  Reg &= ~(DWC3_GCTL_SCALEDOWN_MASK | DWC3_GCTL_DISSCRAMBLE |
           DWC3_GCTL_GBLHIBERNATIONEN | DWC3_GCTL_DSBLCLKGTNG);
  Reg &= ~DWC3_GCTL_PRTCAPDIR (DWC3_GCTL_PRTCAP_OTG);
  Reg |= DWC3_GCTL_PRTCAPDIR (DWC3_GCTL_PRTCAP_DEVICE);
  MmioWrite32 ((UINTN)WARM_DWC3_BASE + DWC3_GCTL, Reg);

  Hw3 = MmioRead32 ((UINTN)WARM_DWC3_BASE + DWC3_GHWPARAMS3);
  mUsb.NumPhysicalEps = (UINT8)DWC3_GHWPARAMS3_NUM_EPS (Hw3);
  if (mUsb.NumPhysicalEps == 0U || mUsb.NumPhysicalEps > WARM_USB_MAX_PHYS_EP) {
    mUsb.NumPhysicalEps = WARM_USB_MAX_PHYS_EP;
  }

  mUsb.EventPos = 0;
  ZeroMem (mUsb.EventBuf, WARM_USB_EVENT_BUF_SIZE);
  WriteBackInvalidateDataCacheRange (mUsb.EventBuf, WARM_USB_EVENT_BUF_SIZE);
  MmioWrite32 ((UINTN)WARM_DWC3_BASE + DWC3_GEVNTADRLO (0),
               (UINT32)((UINT64)(UINTN)mUsb.EventBuf & 0xFFFFFFFFULL));
  MmioWrite32 ((UINTN)WARM_DWC3_BASE + DWC3_GEVNTADRHI (0),
               (UINT32)((UINT64)(UINTN)mUsb.EventBuf >> 32));
  MmioWrite32 ((UINTN)WARM_DWC3_BASE + DWC3_GEVNTSIZ (0),
               DWC3_GEVNTSIZ_INTMASK | DWC3_GEVNTSIZ_SIZE (WARM_USB_EVENT_BUF_SIZE));
  MmioWrite32 ((UINTN)WARM_DWC3_BASE + DWC3_GEVNTCOUNT (0),
               MmioRead32 ((UINTN)WARM_DWC3_BASE + DWC3_GEVNTCOUNT (0)) & DWC3_GEVNTCOUNT_MASK);

  Reg  = MmioRead32 ((UINTN)WARM_DWC3_BASE + DWC3_DCFG);
  Reg &= ~(DWC3_DCFG_SPEED_MASK | DWC3_DCFG_DEVADDR_MASK);
  Reg |= DWC3_DCFG_HIGHSPEED;
  MmioWrite32 ((UINTN)WARM_DWC3_BASE + DWC3_DCFG, Reg);

  MmioWrite32 ((UINTN)WARM_DWC3_BASE + DWC3_DEVTEN,
               DWC3_DEVTEN_DISCONNEVTEN | DWC3_DEVTEN_USBRSTEN |
               DWC3_DEVTEN_CONNECTDONEEN | DWC3_DEVTEN_ULSTCNGEN |
               DWC3_DEVTEN_WKUPEVTEN);
  MmioWrite32 ((UINTN)WARM_DWC3_BASE + DWC3_DALEPENA, 0);

  SendEpCmd (0, DWC3_DEPCMD_DEPSTARTCFG, 0, 0, 0, NULL);
  for (Idx = 0; Idx < mUsb.NumPhysicalEps; Idx++) {
    mUsb.Eps[Idx].Enabled        = FALSE;
    mUsb.Eps[Idx].TransferActive = FALSE;
    mUsb.Eps[Idx].Stalled        = FALSE;
    mUsb.Eps[Idx].ResourceIndex  = 0;
    mUsb.Eps[Idx].ClientBuffer   = NULL;
    mUsb.Eps[Idx].ClientSize     = 0;
    mUsb.Eps[Idx].TrbLength      = 0;
    SendEpCmd (Idx, DWC3_DEPCMD_SETTRANSFRESOURCE, 1U, 0, 0, NULL);
  }

  EnableEndpoint (0, DWC3_EP_TYPE_CONTROL, 64U, FALSE);
  EnableEndpoint (1, DWC3_EP_TYPE_CONTROL, 64U, FALSE);
  Ep0ArmSetup ();

  MmioOr32 ((UINTN)WARM_DWC3_BASE + DWC3_DCTL, DWC3_DCTL_RUN_STOP);
  for (Timeout = 0; Timeout < 100000U; Timeout++) {
    if ((MmioRead32 ((UINTN)WARM_DWC3_BASE + DWC3_DSTS) & DWC3_DSTS_DEVCTRLHLT) == 0U) {
      break;
    }
    gBS->Stall (1);
  }

  mUsb.Initialized = TRUE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
WarmUsbStart (
  IN USB_DEVICE_DESCRIPTOR      *DeviceDescriptor,
  IN VOID                      **Descriptors,
  IN VOID                       *DeviceQualifierDescriptor,
  IN VOID                       *BinaryDeviceOjectStore,
  IN UINT8                       StringDescriptorCount OPTIONAL,
  IN USB_STRING_DESCRIPTOR     **StringDescriptors OPTIONAL
  )
{
  USB_DEVICE_DESCRIPTOR_SET Set;
  ZeroMem (&Set, sizeof (Set));
  Set.DeviceDescriptor          = DeviceDescriptor;
  Set.Descriptors               = Descriptors;
  Set.DeviceQualifierDescriptor = DeviceQualifierDescriptor;
  Set.BinaryDeviceOjectStore    = BinaryDeviceOjectStore;
  Set.StringDescriptorCount     = StringDescriptorCount;
  Set.StringDescritors          = StringDescriptors;
  return WarmUsbStartEx (&Set);
}

STATIC
EFI_STATUS
WarmUsbSend (
  IN UINT8 EndpointIndex,
  IN UINTN Size,
  IN VOID *Buffer
  )
{
  UINT8    LogicalEp = EndpointIndex & 0x0FU;
  BOOLEAN  IsIn      = (EndpointIndex & 0x80U) != 0U;
  UINT8    PhysEp    = (UINT8)((LogicalEp << 1) | (IsIn ? 1U : 0U));
  WARM_EP *Ep;
  UINTN    TrbLen;

  if (!mUsb.Initialized || Buffer == NULL || PhysEp >= WARM_USB_MAX_PHYS_EP || PhysEp < 2U) {
    return EFI_INVALID_PARAMETER;
  }
  Ep = &mUsb.Eps[PhysEp];
  if (!Ep->Enabled || Ep->TransferActive) {
    return EFI_NOT_READY;
  }

  TrbLen = Size;
  if (!IsIn && Ep->MaxPacketSize != 0U) {
    TrbLen = ALIGN_VALUE (TrbLen, Ep->MaxPacketSize);
    if (TrbLen == 0U) {
      TrbLen = Ep->MaxPacketSize;
    }
  }

  if (IsIn) {
    if (Size != 0U) {
      WriteBackDataCacheRange (Buffer, Size);
    }
  } else {
    WriteBackInvalidateDataCacheRange (Buffer, TrbLen);
  }

  Ep->EndpointAddress = EndpointIndex;
  Ep->ClientBuffer    = Buffer;
  Ep->ClientSize      = Size;
  Ep->TrbLength       = TrbLen;

  return StartTransfer (PhysEp, (UINT64)(UINTN)Buffer, (UINT32)TrbLen, DWC3_TRBCTL_NORMAL);
}

STATIC
EFI_STATUS
EFIAPI
WarmUsbHandleEvent (
  OUT USB_DEVICE_EVENT      *Event,
  OUT UINTN                 *DataSize,
  OUT USB_DEVICE_EVENT_DATA *EventData
  )
{
  UINT32 Count;
  UINT32 RawEvent;

  if (Event == NULL || DataSize == NULL || EventData == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (PopMsg (Event, EventData)) {
    *DataSize = (*Event == UsbDeviceEventDeviceStateChange) ? sizeof (USB_DEVICE_STATE)
                                                            : sizeof (USB_DEVICE_TRANSFER_OUTCOME);
    return EFI_SUCCESS;
  }

  if (mUsb.Initialized) {
    Count = MmioRead32 ((UINTN)WARM_DWC3_BASE + DWC3_GEVNTCOUNT (0)) & DWC3_GEVNTCOUNT_MASK;
    if (Count >= sizeof (UINT32)) {
      InvalidateDataCacheRange (mUsb.EventBuf, WARM_USB_EVENT_BUF_SIZE);
    }
    while (Count >= sizeof (UINT32)) {
      MemoryFence ();
      RawEvent      = *(CONST volatile UINT32 *)(mUsb.EventBuf + mUsb.EventPos);
      mUsb.EventPos = (mUsb.EventPos + sizeof (UINT32)) % WARM_USB_EVENT_BUF_SIZE;
      Count        -= sizeof (UINT32);
      MmioWrite32 ((UINTN)WARM_DWC3_BASE + DWC3_GEVNTCOUNT (0), sizeof (UINT32));

      if (DWC3_EVENT_IS_ENDPOINT (RawEvent)) {
        HandleEpEvent (RawEvent);
      } else if (DWC3_DEVT_CATEGORY (RawEvent) == 0U) {
        HandleDeviceEvent (RawEvent);
      }

      if (PopMsg (Event, EventData)) {
        *DataSize = (*Event == UsbDeviceEventDeviceStateChange) ? sizeof (USB_DEVICE_STATE)
                                                                : sizeof (USB_DEVICE_TRANSFER_OUTCOME);
        return EFI_SUCCESS;
      }
    }
  }

  *Event    = UsbDeviceEventNoEvent;
  *DataSize = 0;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
WarmUsbAllocateTransferBuffer (
  IN  UINTN  Size,
  OUT VOID **Buffer
  )
{
  UINTN Idx;
  UINTN Pages;
  VOID *Ptr;

  if (Buffer == NULL || Size == 0U) {
    return EFI_INVALID_PARAMETER;
  }

  for (Idx = 0; Idx < WARM_USB_MAX_XFER_BUFS; Idx++) {
    if (mUsb.XferBufs[Idx].Buffer == NULL) {
      Pages = EFI_SIZE_TO_PAGES (Size);
      Ptr   = AllocateAlignedPages (Pages, SIZE_4KB);
      if (Ptr == NULL) {
        return EFI_OUT_OF_RESOURCES;
      }
      mUsb.XferBufs[Idx].Buffer = Ptr;
      mUsb.XferBufs[Idx].Pages  = Pages;
      *Buffer                   = Ptr;
      return EFI_SUCCESS;
    }
  }
  return EFI_OUT_OF_RESOURCES;
}

STATIC
EFI_STATUS
EFIAPI
WarmUsbFreeTransferBuffer (
  IN VOID *Buffer
  )
{
  UINTN Idx;

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  for (Idx = 0; Idx < WARM_USB_MAX_XFER_BUFS; Idx++) {
    if (mUsb.XferBufs[Idx].Buffer == Buffer) {
      FreeAlignedPages (Buffer, mUsb.XferBufs[Idx].Pages);
      mUsb.XferBufs[Idx].Buffer = NULL;
      mUsb.XferBufs[Idx].Pages  = 0;
      return EFI_SUCCESS;
    }
  }
  return EFI_INVALID_PARAMETER;
}

STATIC
EFI_STATUS
WarmUsbStop (VOID)
{
  UINT8 Idx;
  if (!mUsb.Initialized) {
    return EFI_SUCCESS;
  }
  MmioWrite32 ((UINTN)WARM_DWC3_BASE + DWC3_DEVTEN, 0);
  MmioAnd32 ((UINTN)WARM_DWC3_BASE + DWC3_DCTL, ~DWC3_DCTL_RUN_STOP);
  for (Idx = 0; Idx < mUsb.NumPhysicalEps && Idx < WARM_USB_MAX_PHYS_EP; Idx++) {
    EndTransfer (Idx);
    mUsb.Eps[Idx].Enabled = FALSE;
  }
  MmioWrite32 ((UINTN)WARM_DWC3_BASE + DWC3_DALEPENA, 0);
  mUsb.Initialized = FALSE;
  mUsb.Connected   = FALSE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
WarmUsbAbortXfer (
  IN UINT8 EndpointIndex
  )
{
  UINT8                 LogicalEp = EndpointIndex & 0x0FU;
  BOOLEAN               IsIn      = (EndpointIndex & 0x80U) != 0U;
  UINT8                 PhysEp    = (UINT8)((LogicalEp << 1) | (IsIn ? 1U : 0U));
  USB_DEVICE_EVENT_DATA EvData;

  if (PhysEp >= WARM_USB_MAX_PHYS_EP) {
    return EFI_INVALID_PARAMETER;
  }
  if (mUsb.Eps[PhysEp].TransferActive) {
    EndTransfer (PhysEp);
    ZeroMem (&EvData, sizeof (EvData));
    EvData.TransferOutcome.Status        = UsbDeviceTransferStatusCancelled;
    EvData.TransferOutcome.EndpointIndex = EndpointIndex;
    EvData.TransferOutcome.DataBuffer    = mUsb.Eps[PhysEp].ClientBuffer;
    mUsb.Eps[PhysEp].ClientBuffer        = NULL;
    PushMsg (UsbDeviceEventTransferNotification, &EvData);
  }
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
WarmUsbSetEndpointStallState (
  IN UINT8   EndpointIndex,
  IN BOOLEAN State
  )
{
  UINT8   LogicalEp = EndpointIndex & 0x0FU;
  BOOLEAN IsIn      = (EndpointIndex & 0x80U) != 0U;
  UINT8   PhysEp    = (UINT8)((LogicalEp << 1) | (IsIn ? 1U : 0U));

  if (PhysEp >= WARM_USB_MAX_PHYS_EP) {
    return EFI_INVALID_PARAMETER;
  }
  return SendEpCmd (PhysEp, State ? DWC3_DEPCMD_SETSTALL : DWC3_DEPCMD_CLEARSTALL, 0, 0, 0, NULL);
}

STATIC EFI_USB_DEVICE_PROTOCOL mWarmUsbDeviceProtocol = {
  EFI_USB_DEVICE_PROTOCOL_REVISION,
  WarmUsbStart,
  WarmUsbSend,
  WarmUsbHandleEvent,
  WarmUsbAllocateTransferBuffer,
  WarmUsbFreeTransferBuffer,
  WarmUsbStop,
  WarmUsbAbortXfer,
  WarmUsbSetEndpointStallState,
  WarmUsbStartEx
};

EFI_STATUS
EFIAPI
WarmUsbDeviceEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_HANDLE Handle = NULL;
  ZeroMem (&mUsb, sizeof (mUsb));
  return gBS->InstallMultipleProtocolInterfaces (&Handle,
                                                   &gEfiUsbDeviceProtocolGuid, &mWarmUsbDeviceProtocol,
                                                   NULL);
}
