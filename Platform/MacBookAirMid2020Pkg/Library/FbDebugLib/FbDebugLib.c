/** @file
  DebugLib that writes straight to the framebuffer m1n1 set up.

  Formatting is intentionally not implemented: the goal is to see *which*
  DEBUG strings the DXE core reaches on a machine with no serial port.
  Output goes to a scrolling text area starting at the bottom third.
**/

#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>

#define FB_BASE    0x9E2624000ULL
#define FB_STRIDE  2560

STATIC UINTN  mCol = 0;
STATIC UINTN  mRow = 0;

STATIC CONST UINT8  mFont[][7] = {
  /* A B C D E F G H I J K L M N O P Q R S T U V W X Y Z */
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
  {0x1F,0x11,0x11,0x1F,0x11,0x11,0x1F},
  {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
  {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
  {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E},
  {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
  {0x04,0x04,0x04,0x04,0x04,0x04,0x04},
  {0x04,0x04,0x04,0x04,0x04,0x04,0x0E},
  {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
  {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
  {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
  {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
  {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
  {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
  {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
  {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
  {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
  {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
  {0x11,0x11,0x11,0x0A,0x0A,0x04,0x04},
  {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
  {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
};

STATIC CONST CHAR8  mSet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

STATIC
VOID
PutChar (
  IN CHAR8  Ch
  )
{
  volatile UINT32  *Fb = (volatile UINT32 *)(UINTN)FB_BASE;
  CONST CHAR8      *Q = NULL;
  CONST CHAR8      *T;
  UINTN            R, C, Dy, Dx, X, Y;

  if ((Ch == '\n') || (Ch == '\r')) {
    mCol = 0;
    mRow++;
    if (mRow > 34) {
      mRow = 0;
    }
    return;
  }
  if (Ch >= 'a' && Ch <= 'z') {
    Ch = (CHAR8)(Ch - 32);
  }
  if ((Ch < 'A') || (Ch > 'Z')) {
    Ch = ' ';
  }
  for (T = mSet; *T; T++) {
    if (*T == Ch) {
      Q = T;
    }
  }
  X = 40 + mCol * 16;
  Y = 1000 + mRow * 16;
  if ((Q != NULL) && (X + 20 < FB_STRIDE) && (Y + 28 < 1600)) {
    for (R = 0; R < 7; R++) {
      UINT8 Bits = mFont[Q - mSet][R];
      for (C = 0; C < 5; C++) {
        if ((Bits & (0x10u >> C)) == 0) {
          continue;
        }
        for (Dy = 0; Dy < 2; Dy++) {
          for (Dx = 0; Dx < 2; Dx++) {
            Fb[(Y + R * 2 + Dy) * FB_STRIDE + (X + C * 2 + Dx)] = 0x00FFFFFFu;
          }
        }
      }
    }
  }
  mCol++;
  if (mCol > 140) {
    mCol = 0;
    mRow++;
    if (mRow > 34) {
      mRow = 0;
    }
  }
}

VOID
EFIAPI
DebugPrint (
  IN UINTN        ErrorLevel,
  IN CONST CHAR8  *Format,
  ...
  )
{
  CONST CHAR8  *P;

  if (Format == NULL) {
    return;
  }
  for (P = Format; *P; P++) {
    PutChar (*P);
  }
  PutChar ('\n');
}

VOID
EFIAPI
DebugVPrint (
  IN UINTN        ErrorLevel,
  IN CONST CHAR8  *Format,
  IN VA_LIST      VaListMarker
  )
{
  DebugPrint (ErrorLevel, Format);
}

VOID
EFIAPI
DebugBPrint (
  IN UINTN        ErrorLevel,
  IN CONST CHAR8  *Format,
  IN BASE_LIST    BaseListMarker
  )
{
  DebugPrint (ErrorLevel, Format);
}

BOOLEAN
EFIAPI
DebugPrintEnabled (
  VOID
  )
{
  return TRUE;
}

BOOLEAN
EFIAPI
DebugPrintLevelEnabled (
  IN CONST UINTN  ErrorLevel
  )
{
  return TRUE;
}

BOOLEAN
EFIAPI
DebugAssertEnabled (
  VOID
  )
{
  return TRUE;
}

BOOLEAN
EFIAPI
DebugCodeEnabled (
  VOID
  )
{
  return TRUE;
}

VOID
EFIAPI
DebugAssert (
  IN CONST CHAR8  *FileName,
  IN UINTN        LineNumber,
  IN CONST CHAR8  *Description
  )
{
  DebugPrint (0, "ASSERT");
  DebugPrint (0, Description);
  CpuDeadLoop ();
}

VOID *
EFIAPI
DebugClearMemory (
  IN VOID  *Buffer,
  IN UINTN Length
  )
{
  return Buffer;
}
