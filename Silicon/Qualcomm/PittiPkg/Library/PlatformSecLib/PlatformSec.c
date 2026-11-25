/**
  Copyright (c) 2011-2012, ARM Limited. All rights reserved.
  Copyright (c) 2026, HakiOS Project.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/PlatformSecLib.h>
#include <Library/ArmSmmuDetachLib.h>

VOID
PlatformInitialize ()
{
  // Set MDP SIDs (0x420, 0x421, 0x43C, 0x43D) and UFS SID (0x180) from SM4635 device tree
  CONST UINT16 MdpStreams[] = { 0x420, 0x421, 0x43C, 0x43D, 0x180 };

  // Detach IOMMU Domains
  ArmSmmuDetach (MdpStreams, ARRAY_SIZE (MdpStreams));
}
