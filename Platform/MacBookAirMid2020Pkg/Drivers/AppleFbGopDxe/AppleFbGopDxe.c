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
  return Status;
}
