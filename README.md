# Project Silicium UEFI for ARM64-Based Devices (`warm` / SM4635 Fork)

> **HakiOS UEFI Firmware for Xiaomi Redmi A4 5G (`warm`, Qualcomm Snapdragon 4s Gen 2 / SM4635 `pitti`)**  
> **Repository:** [https://github.com/Shyam-vadgama/Mu-Silicium-warm](https://github.com/Shyam-vadgama/Mu-Silicium-warm) (Branch: `warm`)  
> **Kernel Repository:** [https://github.com/Shyam-vadgama/xnu-kernel-warm](https://github.com/Shyam-vadgama/xnu-kernel-warm)

---

## Redmi A4 5G (`warm`) Additions

This fork extends **Project Silicium (`Mu-Silicium`)** with an **open-source-only UEFI target** for `warm` (`SM4635` / `Pitti`) to boot **HakiOS / XNU (`loader.efi`)** in RAM via `fastboot boot` without proprietary XBL DXE blobs:

| Component | Path | Description |
|---|---|---|
| **SoC SEC Package** | [`Silicon/Qualcomm/PittiPkg/`](Silicon/Qualcomm/PittiPkg) | SM4635 `PlatformSecLib` detaches stale bootloader SMMU mappings while preserving MDP display streams (`0x420`, `0x421`, `0x43C`, `0x43D`). |
| **Board Platform Package** | [`Platforms/Xiaomi/warmPkg/`](Platforms/Xiaomi/warmPkg) | `warm.dsc`, `warm.fdf`, DDR/MMIO `MemoryMapLib`, `ConfigurationMapLib`, and EDK2 storage/USB bindings. |
| **Open-Source UFS HC Driver** | [`WarmUfsHcDxe`](Platforms/Xiaomi/warmPkg/Drivers/WarmUfsHcDxe) | Pure MMIO `EDKII_UFS_HOST_CONTROLLER_PROTOCOL` & `EDKII_UFS_HC_PLATFORM_PROTOCOL` at `0x04804000` (`NonCoherentDmaLib` + dynamic `HCE`/`HCS` state detection) feeding `UfsPassThruDxe` -> `ScsiBusDxe` -> `ScsiDiskDxe` -> `PartitionDxe`. |
| **Polled DWC3 Fastboot Driver** | [`WarmUsbDeviceDxe`](Platforms/Xiaomi/warmPkg/Drivers/WarmUsbDeviceDxe) | Pure open-source `EFI_USB_DEVICE_PROTOCOL` (`0x04E00000` DWC3 High-Speed) with GCC `USB30_PRIM` (`0x0141A010` GDSC & CBCRs) clock activation for `xnuloader` USB log/screenshot capture. |
| **BootShim & XnuLoader Bridge** | [`BootShim/`](BootShim), [`BootManagerLib/XnuLoader.c`](Silicon/Silicium/SiliciumPkg/Library/BootManagerLib/XnuLoader.c) | Relocatable Android `boot` v4 shim staging appended `XNUP` payloads to `0xF0000000` and launching `loader.efi` from memory via BDS fallback. |

### Hardware Provenance (100% Stock Firmware & Open-Source Kernel)

All physical base addresses, carveouts, stream IDs, and clock registers were derived directly from **Redmi A4 5G Stock HyperOS firmware (`OS3.0.303.0.WGVINXM`)** and **Xiaomi's open-source SM4635 Linux kernel**:

- **Stock Device Tree (`boot` / `dtbo`)**:
  - DDR carveouts (`HYP`, `XBL_BOOT`, `SMEM`, `RAMOOPS @ 0x80D00000`, `Display Reserved @ 0x82D00000`, `WLAN_WPSS`, `MPSS_MODEM`, `ADSP`, `TZ_STAT`, `TAGS`, `QTEE`, `TRUSTED_APPS`)
  - MMIO & Interrupts: GICv3 (`0x0F200000` / `0x0F240000`), `apps-smmu@0xc600000`, `ufshc@4804000` (UFS PHY `0x4807000`, SMMU SID `0x180`), `dwc3@4e00000` (`hsphy@1613000`, SMMU SID `0x260`), MDP display SIDs (`0x420, 0x421, 0x43C, 0x43D`)
  - Display panel geometry: `720 × 1640` 32-bpp framebuffer at `0x82D00000`
- **Stock Partition Tables & Firehose XML (`gpt_main0..5.bin`, `rawprogram0..5.xml`)**:
  - Confirmed 6 physical UFS LUNs with `4096`-byte logical sectors (`SECTOR_SIZE_IN_BYTES="4096"`).
- **Xiaomi Kernel Open Source (`drivers/clk/qcom/gcc-pitti.c`, `include/dt-bindings/clock/qcom,gcc-pitti.h`)**:
  - Verified `GCC` (`0x01400000`) `gcc_usb30_prim_gdsc` (`0x1A010`) and `USB30_PRIM` clock branch registers (`0x1A000`, `0x1A004`, `0x1A024`, `0x1A02C`, `0x1A030`, `0x1A068`, `0x1A08C`, `0x1B034`, `0x1D008`).

### Build (`warm`)

```bash
CLANGPDB_AARCH64_PREFIX=aarch64-linux-gnu- python3 build_uefi.py -d warm
```

Output FD: `Build/warmPkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd` (and `BootShim/BootShim.bin`).

---

## Credits

- **[Project-Silicium/Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium)** — Upstream ARM64 EDK2 UEFI framework, `ArmSmmuDetachLib`, and SoC/Platform scaffolding.
- **[oosexgang/hakios-xnu-kernel](https://github.com/oosexgang/hakios-xnu-kernel)** — HakiOS XNU kernel, `xnuloader`, and UEFI integration architecture.
- **[slayedkaito/xnu-kernel-peridot-amiaxf](https://github.com/slayedkaito/xnu-kernel-peridot-amiaxf)** (`amiaxf`) — `peridot` (SM8635) port base, `XnuLoader` BDS payload bridge, and Android v4 `boot.img` bring-up model.
- [Microsoft](https://github.com/microsoft/) for their [Project Mu](https://microsoft.github.io/mu/) which Powers this Project. <br>
- [WOA-Project](https://github.com/WOA-Project/) for their [mu_andromeda_platforms](https://github.com/WOA-Project/mu_andromeda_platforms/) which is used as Base for this Project. <br>
- [Project Aloha](https://github.com/Project-Aloha/) for their [mu_aloha_platforms](https://github.com/Project-Aloha/mu_aloha_platforms/) which is used as Reference for some SoCs. <br>
- [Open Surface RT](https://openrt.gitbook.io/open-surfacert/) for their Help on Tegra Related Stuff.
- [sonic011gamer](https://github.com/sonic011gamer/) and [halal-beef](https://github.com/halal-beef/) for their Amazing Work on Exynos Related Stuff.

## Description

This Project focuses on Making ARM-Based Devices Boot Operating Systems like Windows or Linux using Custom UEFI Firmware.

## Resources

- [Status](Status.md)
- [Building](Building.md)
- [Guides](https://github.com/Project-Silicium/Guides/blob/main/README.md)
- [Discord Server](https://discord.gg/Dx2QgMx7Sv)

## Frequently asked Questions

### **Is my Device Supported in this Project?**
- To Check if your Device is Supported, Take a Look at the Status Page Referenced in the Section Above.

### **What do I do if my Device isn't Supported in this Project?**
- We made some Guides that Explain How to Port your Device that are Referenced in the Section Above.

## Credits

[Microsoft](https://github.com/microsoft/) for their [Project Mu](https://microsoft.github.io/mu/) which Powers this Project. <br>
[WOA-Project](https://github.com/WOA-Project/) for their [mu_andromeda_platforms](https://github.com/WOA-Project/mu_andromeda_platforms/) which is used as Base for this Project. <br>
[Project Aloha](https://github.com/Project-Aloha/) for their [mu_aloha_platforms](https://github.com/Project-Aloha/mu_aloha_platforms/) which is used as Reference for some SoCs. <br>
[Open Surface RT](https://openrt.gitbook.io/open-surfacert/) for their Help on Tegra Related Stuff.

[sonic011gamer](https://github.com/sonic011gamer/) and [halal-beef](https://github.com/halal-beef/) for their Amazing Work on Exynos Related Stuff.

## License

All code except drivers in `GPLDrivers` & `GPLApplications` & `GPLLibrary` directories are licensed under BSD-2-Clause. <br />
GPL Drivers are licensed under `GPLv2` or later license.
