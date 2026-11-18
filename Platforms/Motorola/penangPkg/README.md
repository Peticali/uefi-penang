# moto g53 5G (penang, SM4350 "holi") — Mu-Silicium UEFI for UniXNU

Boots from RAM through the stock ABL (`fastboot boot`), never flashed. Built as in [UniXNU's uefi-sheng](https://github.com/UniXNU/uefi-sheng):
the UniXNU boot manager, `UniXNUDxe`, `UniXNURamDiskDxe` and `DarwinBoot` come from a UniXNU checkout next to this
one (`../UniXNU`, or `UNIXNU_ROOT`).

## Device binaries

No extracted firmware is committed. Generate `Binaries/penang` from the phone's own XBL:

1. Dump the XBL firmware volume with UEFIExtract (`UEFIExtract xbl.elf all`).
2. `Scripts/gen-binaries.py <dump> Binaries/caprip Binaries/penang` (another Moto Kamorta tree as the layout).
3. The memory/configuration maps were made with `Scripts/cfg2maplib.py uefiplat.cfg Library/` from the stock
   `uefiplat.cfg`, then edited (RAM Partition, RAMOOPS, the TZ + GPU pool reservation for DarwinBoot).

## Build and boot

    python3 build_uefi.py -d penang -r DEBUG          # Mu-penang.img
    fastboot boot <boot image: Mu FD + UniXNU payload as ramdisk>

## Penang-specific pieces

- `Drivers/PenangDisplayKeepDxe`: runs before ClockDxe. ClockDxe re-creates the RPM bus clock `/clk/mmrt` with no
  vote, which stops `gcc_disp_hf_axi_clk` (CBCR 0x01417020 -> CLK_OFF) and blanks the ABL's continuous-splash panel.
  The driver votes `/clk/mmrt` and `/clk/mmnrt` (200 MHz) and the CX/MX rails (`/vcs/vdd_*/vlvl` 0x180) through
  `EFI_NPA_PROTOCOL`. The votes outlive ExitBootServices (the panel keeps working under XNU).
- The stock `DisplayDxe` is not used (it hangs after `get_mbm_fdt`; the drivers the stock order loads before it,
  `MotCoreServicesDxe`/`AbDxe`, write partitions and PMIC reset settings).
- APRIORI: the Moto drivers (`MotHwIdDxe`, utags, storage) after TLMM/SPMI/PMIC, as in the stock XBL.
- SEC keeps the ABL's DT address (x0) in the entry-argument HOB (`gSiliciumEntryArgHobGuid`) for UniXNU's payload
  discovery. The ABL puts vendor_boot's ramdisk before ours: UniXNURamDiskDxe needs the payload search patch
  (in UniXNU-penang).
- `FrameBufferSerialPortLib` mirrors the console to the Linux ramoops zone (0xAF03F800, "DBGC") so the log can be read
  after a reset.
