/** @file
  Minimal GOP driver that republishes the linear framebuffer m1n1 already set up.

  Apple Silicon has no CPU-programmable display controller: the panel is driven by
  the DCP coprocessor. m1n1 brings DCP up and leaves a plain linear framebuffer
  behind, so the cheapest way to get a UEFI console is to wrap that buffer in
  EFI_GRAPHICS_OUTPUT_PROTOCOL instead of writing a DCP driver.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>

#define APPLE_FB_BASE    0x9E2624000ULL
#define APPLE_FB_WIDTH   2560
#define APPLE_FB_HEIGHT  1600
#define APPLE_FB_STRIDE  2560
#define APPLE_FB_BPP     32

/* 5x7 glyphs for the letters we need on screen (no console available yet). */
STATIC CONST UINT8 mGlyph[][7] = {
  {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
  {0x1F,0x11,0x11,0x1F,0x11,0x11,0x1F}, /* B */
  {0x0E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
  {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* C */
  {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}, /* K */
  {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E}, /* S */
  {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
  {0x11,0x11,0x11,0x0A,0x0A,0x04,0x04}, /* X */
};

STATIC
VOID
DrawText (
  IN UINTN        X,
  IN UINTN        Y,
  IN CONST CHAR8  *S
  )
{
  volatile UINT32  *Fb;
  CONST CHAR8      *Set = "OAFEBCPKSTX";
  UINTN            R, C, Dy, Dx;

  Fb = (volatile UINT32 *)(UINTN)APPLE_FB_BASE;
  for (; *S; S++, X += 24) {
    CONST CHAR8 *Q = NULL;
    CONST CHAR8 *T;
    for (T = Set; *T; T++) {
      if (*T == *S) {
        Q = T;
      }
    }
    if (Q == NULL) {
      continue;
    }
    for (R = 0; R < 7; R++) {
      UINT8 Bits = mGlyph[Q - Set][R];
      for (C = 0; C < 5; C++) {
        if ((Bits & (0x10u >> C)) == 0) {
          continue;
        }
        for (Dy = 0; Dy < 4; Dy++) {
          for (Dx = 0; Dx < 4; Dx++) {
            Fb[(Y + R * 4 + Dy) * APPLE_FB_STRIDE + (X + C * 4 + Dx)] = 0x00FFFFFFu;
          }
        }
      }
    }
  }
}

STATIC EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  mModeInfo;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE     mMode;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL          mGop;

STATIC
EFI_STATUS
EFIAPI
GopQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This  OPTIONAL,
  IN  UINT32                                ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION   **Info
  )
{
  if (ModeNumber != 0) {
    return EFI_UNSUPPORTED;
  }

  *Info = AllocateCopyPool (sizeof (mModeInfo), &mModeInfo);
  if (*Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  *SizeOfInfo = sizeof (mModeInfo);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GopSetMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *This  OPTIONAL,
  IN UINT32                        ModeNumber
  )
{
  if (ModeNumber != 0) {
    return EFI_UNSUPPORTED;
  }

  mMode.Mode = 0;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GopBlt (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL       *This  OPTIONAL,
  IN  EFI_GRAPHICS_OUTPUT_BLT_PIXEL      *BltBuffer,
  IN  EFI_GRAPHICS_OUTPUT_BLT_OPERATION  BltOperation,
  IN  UINTN                              SourceX,
  IN  UINTN                              SourceY,
  IN  UINTN                              DestinationX,
  IN  UINTN                              DestinationY,
  IN  UINTN                              Width,
  IN  UINTN                              Height,
  IN  UINTN                              Delta
  )
{
  UINT32  *Fb;
  UINTN   Row;
  UINTN   Col;

  if (BltOperation != EfiBltVideoFill && BltBuffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Fb = (UINT32 *)(UINTN)APPLE_FB_BASE;

  for (Row = 0; Row < Height; Row++) {
    for (Col = 0; Col < Width; Col++) {
      UINTN Dx = DestinationX + Col;
      UINTN Dy = DestinationY + Row;

      if ((Dx >= APPLE_FB_WIDTH) || (Dy >= APPLE_FB_HEIGHT)) {
        continue;
      }

      if (BltOperation == EfiBltVideoFill) {
        Fb[Dy * APPLE_FB_STRIDE + Dx] = *(UINT32 *)BltBuffer;
      } else if (BltOperation == EfiBltBufferToVideo) {
        UINTN Stride = (Delta != 0) ? (Delta / sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)) : Width;
        Fb[Dy * APPLE_FB_STRIDE + Dx] =
          *(UINT32 *)&BltBuffer[(SourceY + Row) * Stride + (SourceX + Col)];
      }
    }
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
AppleFbGopEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;

  mModeInfo.Version              = 0;
  mModeInfo.HorizontalResolution = APPLE_FB_WIDTH;
  mModeInfo.VerticalResolution   = APPLE_FB_HEIGHT;
  mModeInfo.PixelFormat          = PixelBlueGreenRedReserved8BitPerColor;
  mModeInfo.PixelsPerScanLine    = APPLE_FB_STRIDE;

  mMode.Mode             = 0;
  mMode.Info             = &mModeInfo;
  mMode.SizeOfInfo       = sizeof (mModeInfo);
  mMode.MaxMode          = 1;
  mMode.FrameBufferBase  = APPLE_FB_BASE;
  mMode.FrameBufferSize  = (UINTN)APPLE_FB_STRIDE * APPLE_FB_HEIGHT * (APPLE_FB_BPP / 8);

  /* prove the driver actually loaded: paint a solid band at y=360..470 */
  {
    volatile UINT32 *Fb = (volatile UINT32 *)(UINTN)APPLE_FB_BASE;
    UINTN R, C;
    for (R = 360; R < 470; R++) {
      for (C = 0; C < 2400; C++) {
        Fb[R * APPLE_FB_STRIDE + C] = 0x0000FF00u; /* GOP LOADED */
      }
    }
  }

  mGop.QueryMode  = GopQueryMode;
  mGop.SetMode    = GopSetMode;
  mGop.Blt        = GopBlt;
  mGop.Mode       = &mMode;

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiGraphicsOutputProtocolGuid,
                  &mGop,
                  NULL
                  );

  DEBUG ((DEBUG_INFO, "AppleFbGop: framebuffer published, status=%r\n", Status));

  if (!EFI_ERROR (Status)) {
    /* BDS normally connects console drivers; we are running before BDS, so ask
     * the core to bind GraphicsConsole/ConSplitter to this handle right now. */
    {
      EFI_STATUS Cs;
      volatile UINT32 *Fb = (volatile UINT32 *)(UINTN)APPLE_FB_BASE;
      UINTN R, C;
      Cs = gBS->ConnectController (Handle, NULL, NULL, TRUE);
      DrawText (80, 640, EFI_ERROR (Cs) ? "FAIL" : "OKBP");
      DrawText (80, 720, "GOPTX");
      /* blue band = console connect OK, red band = failed (status encoded) */
      for (R = 500; R < 600; R++) {
        for (C = 0; C < 2400; C++) {
          Fb[R * APPLE_FB_STRIDE + C] = EFI_ERROR (Cs) ? 0x00FF0000u : 0x00FF8000u;
        }
      }
    }
  }
  return Status;
}
