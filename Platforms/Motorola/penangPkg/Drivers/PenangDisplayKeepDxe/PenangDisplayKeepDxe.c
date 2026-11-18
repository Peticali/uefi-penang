/** @file
  PENANG: keep the panel the bootloader left on alive across ClockDxe.

  The moto g53's ABL hands over a running video-mode panel (continuous splash) and SimpleFbDxe draws into its
  framebuffer. ClockDxe's init turns the display clocks off (nobody holds a vote for them), so the panel keeps its
  backlight and shows black. This driver runs right after ClockDxe: it logs each display clock's state, then votes
  the MDSS power domain and the clocks back on.
**/
#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/EFIClock.h>
#include <Library/IoLib.h>

//
// Qualcomm EFI_NPA_PROTOCOL (QcomPkg/Include/Protocol/EFINpa.h, revision 0x10002 layout; only the leading members used).
//
STATIC EFI_GUID  mNpaProtocolGuid = { 0x79d6c870, 0x725e, 0x489e, { 0xa0, 0xa1, 0x27, 0xe7, 0xa5, 0xd0, 0xcb, 0x35 } };
#define PENANG_NPA_CLIENT_REQUIRED  (1 << 6)

typedef struct {
  UINT64      Revision;
  EFI_STATUS  (EFIAPI *NpaInit)(VOID);
  EFI_STATUS  (EFIAPI *CreateSyncClientEx)(CONST CHAR8 *Resource, CONST CHAR8 *Client, UINT32 Type, UINT32 Value,
                                           VOID *Ref, VOID **Handle);
  EFI_STATUS  (EFIAPI *CompleteRequest)(VOID *Handle);
  VOID        *DefineNodeCb;
  EFI_STATUS  (EFIAPI *ScalarRequest)(VOID *Handle, UINT32 State);
} PENANG_NPA_PROTOCOL;

//
// The display's AXI clock (gcc_disp_hf_axi_clk) has no RCG of its own: it runs from the RPM-controlled real-time
// multimedia NOC clock "/clk/mmrt". ClockDxe re-creates that NPA node with no request, the RPM stops mmrt and the MDP
// can no longer fetch the framebuffer (CBCR 0x01417020 0x00004221 -> 0x80004221). Hold a REQUIRED vote on it.
//
STATIC
VOID
VoteBus (
  IN CONST CHAR8  *Resource,
  IN UINT32       KHz
  )
{
  PENANG_NPA_PROTOCOL  *Npa;
  VOID                 *Handle = NULL;
  EFI_STATUS           Status;

  Status = gBS->LocateProtocol (&mNpaProtocolGuid, NULL, (VOID **)&Npa);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "PENANG DISPKEEP: no NPA protocol %r\n", Status));
    return;
  }
  Status = Npa->CreateSyncClientEx (Resource, "penang_display", PENANG_NPA_CLIENT_REQUIRED, 0, NULL, &Handle);
  DEBUG ((DEBUG_ERROR, "PENANG DISPKEEP: NPA rev 0x%lx client %a %r %p\n", Npa->Revision, Resource, Status, Handle));
  if (EFI_ERROR (Status) || (Handle == NULL)) {
    return;
  }
  Status = Npa->ScalarRequest (Handle, KHz);
  DEBUG ((DEBUG_ERROR, "PENANG DISPKEEP: %a <- %u kHz %r\n", Resource, KHz, Status));
}

//
// Raw CBCRs (from ClockDxe's own tables): bit 31 CLK_OFF, bit 1 HW_CTL, bit 0 CLK_ENABLE.
//
typedef struct {
  CONST CHAR8  *Name;
  UINTN        Cbcr;
} PENANG_CBCR;

STATIC CONST PENANG_CBCR  mCbcrs[] = {
  { "gcc_disp_ahb",        0x0141700C }, { "gcc_disp_hf_axi",     0x01417020 }, { "gcc_disp_throttle",  0x01417064 },
  { "gcc_vote_9000c",      0x0147900C }, { "dcc_mdss_gdscr",      0x05F01004 }, { "dcc_xo",             0x05F05008 },
  { "dcc_non_gdsc_ahb",    0x05F02004 }, { "dcc_rscc_ahb",        0x05F0200C }, { "dcc_rscc_vsync",     0x05F02008 },
  { "dcc_mdss_ahb",        0x05F0104C }, { "dcc_mdp",             0x05F01010 }, { "dcc_mdp_lut",        0x05F01020 },
  { "dcc_vsync",           0x05F01028 }, { "dcc_byte0",           0x05F0102C }, { "dcc_byte0_intf",     0x05F01030 },
  { "dcc_pclk0",           0x05F0100C }, { "dcc_esc0",            0x05F01034 },
};

STATIC
VOID
DumpCbcrs (
  IN CONST CHAR8  *When
  )
{
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (mCbcrs); Index++) {
    DEBUG ((DEBUG_ERROR, "PENANG CBCR %a: %a @%x = %08x\n", When, mCbcrs[Index].Name, (UINT32)mCbcrs[Index].Cbcr,
            MmioRead32 (mCbcrs[Index].Cbcr)));
  }
}


STATIC CONST CHAR8  *mDisplayClocks[] = {
  "gcc_disp_ahb_clk",
  "gcc_disp_hf_axi_clk",
  "gcc_disp_throttle_core_clk",
  "gcc_disp_gpll0_div_clk_src",
  "disp_cc_xo_clk",
  "disp_cc_mdss_non_gdsc_ahb_clk",
  "disp_cc_mdss_rscc_ahb_clk",
  "disp_cc_mdss_rscc_vsync_clk",
  "disp_cc_mdss_ahb_clk",
  "disp_cc_mdss_mdp_clk",
  "disp_cc_mdss_mdp_lut_clk",
  "disp_cc_mdss_rot_clk",
  "disp_cc_mdss_vsync_clk",
  "disp_cc_mdss_byte0_clk",
  "disp_cc_mdss_byte0_intf_clk",
  "disp_cc_mdss_pclk0_clk",
  "disp_cc_mdss_esc0_clk",
};

STATIC
VOID
LogClocks (
  IN EFI_CLOCK_PROTOCOL  *Clock,
  IN CONST CHAR8         *When
  )
{
  UINTN    Index;
  UINTN    Id;
  BOOLEAN  Enabled;
  BOOLEAN  On;
  UINT32   Hz;

  for (Index = 0; Index < ARRAY_SIZE (mDisplayClocks); Index++) {
    if (EFI_ERROR (Clock->GetClockID (Clock, mDisplayClocks[Index], &Id))) {
      DEBUG ((DEBUG_ERROR, "PENANG DISPKEEP %a: %a: no id\n", When, mDisplayClocks[Index]));
      continue;
    }
    Enabled = FALSE;
    On = FALSE;
    Hz = 0;
    Clock->IsClockEnabled (Clock, Id, &Enabled);
    Clock->IsClockOn (Clock, Id, &On);
    Clock->GetClockFreqHz (Clock, Id, &Hz);
    DEBUG ((DEBUG_ERROR, "PENANG DISPKEEP %a: %a en %d on %d %u Hz\n", When, mDisplayClocks[Index], Enabled, On, Hz));
  }
}

STATIC VOID  *mRegistration;

STATIC
VOID
EFIAPI
OnClockProtocol (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_CLOCK_PROTOCOL  *Clock;
  EFI_STATUS          Status;
  UINTN               Index;
  UINTN               Id;

  Status = gBS->LocateProtocol (&gEfiClockProtocolGuid, NULL, (VOID **)&Clock);
  if (EFI_ERROR (Status)) {
    return;
  }
  gBS->CloseEvent (Event);
  DumpCbcrs ("after ClockDxe");
  DEBUG ((DEBUG_ERROR, "PENANG DISPKEEP: clock protocol version 0x%lx\n", Clock->Version));
  LogClocks (Clock, "after ClockDxe");

  if (!EFI_ERROR (Clock->GetClockPowerDomainID (Clock, "disp_cc_mdss_core_gdsc", &Id))) {
    DEBUG ((DEBUG_ERROR, "PENANG DISPKEEP: mdss_core_gdsc on %r\n", Clock->EnableClockPowerDomain (Clock, Id)));
  }
  for (Index = 0; Index < ARRAY_SIZE (mDisplayClocks); Index++) {
    if (!EFI_ERROR (Clock->GetClockID (Clock, mDisplayClocks[Index], &Id))) {
      Status = Clock->EnableClock (Clock, Id);
      if (EFI_ERROR (Status)) {
        DEBUG ((DEBUG_ERROR, "PENANG DISPKEEP: enable %a %r\n", mDisplayClocks[Index], Status));
      }
    }
  }
  LogClocks (Clock, "after enable");
  VoteBus ("/clk/mmrt", 200000);
  VoteBus ("/clk/mmnrt", 200000);
  //
  // The shim's rails (shim.c rpm_rails_vote: "rwcx"/"rwmx" vlvl 0x180 = NOM): without them the RPM lowers CX/MX under
  // the running AP and the SoC freezes (iOS through UniXNU died ~70 s in, PS_HOLD). xnu's penang_mpss.c counts on them.
  //
  VoteBus ("/vcs/vdd_cx/vlvl", 0x180);
  VoteBus ("/vcs/vdd_mx/vlvl", 0x180);
  gBS->Stall (1000);
  DumpCbcrs ("after mmrt vote");
}

EFI_STATUS
EFIAPI
PenangDisplayKeepEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  DumpCbcrs ("before ClockDxe");
  EfiCreateProtocolNotifyEvent (&gEfiClockProtocolGuid, TPL_CALLBACK, OnClockProtocol, NULL, &mRegistration);
  return EFI_SUCCESS;
}
