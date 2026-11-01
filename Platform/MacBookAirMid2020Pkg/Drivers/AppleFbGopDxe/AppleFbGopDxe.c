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

STATIC EFI_SIMPLE_TEXT_OUTPUT_MODE  mTextMode = {
  0, 0, 0, 0, EFI_LIGHTGRAY, EFI_BLACK
};
STATIC EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  mTextOut;

STATIC
EFI_STATUS
EFIAPI
TextReset (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN BOOLEAN                          ExtendedVerification
  )
{
  mTextMode.CursorColumn = 0;
  mTextMode.CursorRow    = 0;
  return EFI_SUCCESS;
}

STATIC
VOID
DrawGlyph (
  IN CHAR16  Ch,
  IN UINTN   X,
  IN UINTN   Y
  )
{
  volatile UINT32  *Fb;
  CONST CHAR8      *Set = " OAFEBCPKSTX";
  CONST CHAR8      *Q   = NULL;
  CONST CHAR8      *T;
  UINTN            R, C, Dy, Dx;
  CHAR8            A = (CHAR8)Ch;

  Fb = (volatile UINT32 *)(UINTN)APPLE_FB_BASE;
  if (A >= 'a' && A <= 'z') {
    A = (CHAR8)(A - 32);
  }
  for (T = Set; *T; T++) {
    if (*T == A) {
      Q = T;
    }
  }
  if (Q == NULL) {
    return;
  }
  for (R = 0; R < 7; R++) {
    UINT8 Bits = (Q == Set) ? 0 : mGlyph[Q - Set - 1][R];
    for (C = 0; C < 5; C++) {
      if ((Bits & (0x10u >> C)) == 0) {
        continue;
      }
      for (Dy = 0; Dy < 3; Dy++) {
        for (Dx = 0; Dx < 3; Dx++) {
          Fb[(Y + R * 3 + Dy) * APPLE_FB_STRIDE + (X + C * 3 + Dx)] = 0x00FFFFFFu;
        }
      }
    }
  }
}

STATIC
EFI_STATUS
EFIAPI
TextOutputString (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN CHAR16                           *String
  )
{
  CONST UINTN  Cw = 20;
  CONST UINTN  Ch = 24;

  if (String == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  for (; *String; String++) {
    if (*String == L'\n') {
      mTextMode.CursorColumn = 0;
      mTextMode.CursorRow++;
      continue;
    }
    if (*String == L'\r') {
      mTextMode.CursorColumn = 0;
      continue;
    }
    DrawGlyph (*String, 40 + mTextMode.CursorColumn * Cw, 640 + mTextMode.CursorRow * Ch);
    mTextMode.CursorColumn++;
    if (mTextMode.CursorColumn >= 120) {
      mTextMode.CursorColumn = 0;
      mTextMode.CursorRow++;
    }
  }
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
TextTestString (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN CHAR16                           *String
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
TextQueryMode (
  IN  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN  UINTN                            ModeNumber,
  OUT UINTN                            *Columns,
  OUT UINTN                            *Rows
  )
{
  if (ModeNumber != 0) {
    return EFI_UNSUPPORTED;
  }
  *Columns = 120;
  *Rows    = 40;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
TextSetMode (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN UINTN                            ModeNumber
  )
{
  return (ModeNumber == 0) ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
TextSetAttribute (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN UINTN                            Attribute
  )
{
  mTextMode.Attribute = (INT32)Attribute;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
TextClearScreen (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This
  )
{
  volatile UINT32  *Fb;
  UINTN            R, C;

  Fb = (volatile UINT32 *)(UINTN)APPLE_FB_BASE;
  for (R = 620; R < APPLE_FB_HEIGHT; R++) {
    for (C = 0; C < 2400; C++) {
      Fb[R * APPLE_FB_STRIDE + C] = 0;
    }
  }
  mTextMode.CursorColumn = 0;
  mTextMode.CursorRow    = 0;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
TextSetCursorPosition (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN UINTN                            Column,
  IN UINTN                            Row
  )
{
  mTextMode.CursorColumn = (INT32)Column;
  mTextMode.CursorRow    = (INT32)Row;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
TextEnableCursor (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN BOOLEAN                          Visible
  )
{
  mTextMode.CursorVisible = Visible;
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

  mTextOut.Reset              = TextReset;
  mTextOut.OutputString       = TextOutputString;
  mTextOut.TestString         = TextTestString;
  mTextOut.QueryMode          = TextQueryMode;
  mTextOut.SetMode            = TextSetMode;
  mTextOut.SetAttribute       = TextSetAttribute;
  mTextOut.ClearScreen        = TextClearScreen;
  mTextOut.SetCursorPosition  = TextSetCursorPosition;
  mTextOut.EnableCursor       = TextEnableCursor;
  mTextOut.Mode               = &mTextMode;

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiGraphicsOutputProtocolGuid,
                  &mGop,
                  &gEfiSimpleTextOutProtocolGuid,
                  &mTextOut,
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
      TextOutputString (&mTextOut, EFI_ERROR (Status) ? L"TEXTOUT FAIL\n" : L"CONSOLE OK\n");
      TextOutputString (&mTextOut, L"PROJECT MU\n");
      TextOutputString (&mTextOut, L"GOPTX\n");
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
