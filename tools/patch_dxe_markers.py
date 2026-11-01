#!/usr/bin/env python3
"""CI-only instrumentation: paint framebuffer markers from inside MU_BASECORE.

Used to locate where the DXE hand-off dies on Apple Silicon. Markers write
directly to the framebuffer m1n1 already configured, so they work with no
console, no serial and no GOP.

  marker at LoadDxeCoreFromFv() entry -> x=1480 (green)
  marker at DxeMain() entry           -> x=1760 (blue)

The script is best-effort: if an anchor cannot be found it prints a warning so
the build still produces firmware, and the run log records the miss.
"""
import os
import re
import sys

FB = '0x9E2624000ULL'
LABELS = {80: 'PREPI', 360: 'HOB', 640: 'MEM', 920: 'PLAT', 1200: 'FV', 1480: 'DXELOAD', 1760: 'DXE', 2040: 'DISP', 2320: 'CONS'}
STRIDE = 2560


FONT_C = r'''
static const unsigned char MuGlyphs[][7] = {
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
  {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
  {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
  {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* D */
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
  {0x04,0x04,0x04,0x04,0x04,0x04,0x04}, /* I */
  {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* M */
  {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
  {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
  {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
  {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* S */
  {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
  {0x11,0x11,0x11,0x0A,0x0A,0x04,0x04}, /* X */
};
static void MuText(unsigned X, unsigned Y, const char *S)
{
  volatile unsigned *F = (volatile unsigned *)0x9E2624000ULL;
  static const char *Set = "ABCDEFIMNOPSTX";
  for (; *S; S++, X += 18) {
    const char *Q = 0;
    const char *T;
    unsigned Gi, R, C, Dy, Dx;
    for (T = Set; *T; T++) if (*T == *S) Q = T;
    if (!Q) continue;
    Gi = (unsigned)(Q - Set);
    for (R = 0; R < 7; R++) {
      unsigned char Bits = MuGlyphs[Gi][R];
      for (C = 0; C < 5; C++) {
        if (!(Bits & (0x10u >> C))) continue;
        for (Dy = 0; Dy < 3; Dy++)
          for (Dx = 0; Dx < 3; Dx++)
            F[(Y + R * 3 + Dy) * 2560 + (X + C * 3 + Dx)] = 0x00FFFFFFu;
      }
    }
  }
}
'''


def marker_code(x, color):
    return (
        '  { volatile UINT32 *MuFb = (volatile UINT32 *)' + FB + '; '
        'UINTN MuR, MuK; '
        'for (MuR = 0; MuR < 120; MuR++) { '
        'for (MuK = 0; MuK < 200; MuK++) { '
        'MuFb[(80 + MuR) * ' + str(STRIDE) + ' + (' + str(x) + ' + MuK)] = ' + color + '; } } }\n'
    )


def find_definition(root, name):
    """Return (path, line_index) of a top-level definition of name."""
    # EDK2 style puts the return type and EFIAPI on their own lines, so the
    # definition starts with the bare function name at column 0.
    pat = re.compile(r'^' + name + r'\s*\(', re.M)
    for dirpath, _dirnames, filenames in os.walk(root):
        for fn in filenames:
            if not fn.endswith('.c'):
                continue
            path = os.path.join(dirpath, fn)
            try:
                text = open(path, 'r', errors='ignore').read()
            except OSError:
                continue
            for m in pat.finditer(text):
                # skip prototypes / calls: require a '{' shortly after the match
                tail = text[m.start():m.start() + 800]
                if '{' in tail and tail.index('{') > tail.find('('):
                    return path, m.start()
    return None, None


def insert_marker(path, offset, x, color):
    text = open(path, 'r', errors='ignore').read()
    brace = text.find('{', offset)
    if brace < 0:
        return False
    text = text[:brace + 1] + '\n' + marker_code(x, color) + text[brace + 1:]
    # inject the tiny font helper once per file, then a readable label
    if 'MuText' not in text:
        # font helpers must live at file scope: put them after the last #include
        pos = 0
        for m in re.finditer(r'^#include[^\n]*\n', text, re.M):
            pos = m.end()
        text = text[:pos] + FONT_C + text[pos:]
    text = text.replace(marker_code(x, color),
                        marker_code(x, color) + '  MuText(' + str(x) + ', 220, "' + (LABELS.get(x, '')) + '");\n', 1)
    open(path, 'w').write(text)
    return True


def insert_after_call(path, needle, x, color):
    """Append a marker statement right after the first line containing needle."""
    text = open(path, 'r', errors='ignore').read()
    idx = text.find(needle)
    if idx < 0:
        return False
    eol = text.find('\n', idx)
    if eol < 0:
        return False
    text = text[:eol + 1] + marker_code(x, color) + text[eol + 1:]
    open(path, 'w').write(text)
    return True


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else 'MU_BASECORE'
    targets = [('LoadDxeCoreFromFv', 1480, '0x0000FF00'),
               ('DxeMain', 1760, '0x00FF0000')]
    rc = 0
    for name, x, color in targets:
        path, off = find_definition(root, name)
        if not path:
            # LoadDxeCoreFromFv lives in a package we do not patch; only DxeMain
            # is treated as a hard requirement.
            print('WARN: definition of %s not found under %s' % (name, root))
            if name == 'DxeMain':
                rc = 1
            continue
        if insert_marker(path, off, x, color):
            print('patched %s in %s (x=%d)' % (name, path, x))
        else:
            print('WARN: no body brace for %s in %s' % (name, path))
            rc = 1
    dxe = os.path.join(root, 'MdeModulePkg/Core/Dxe/DxeMain/DxeMain.c')
    if os.path.exists(dxe):
        for needle, x, color in [('CoreInitializeDispatcher (', 2040, '0x0000FFFF'),
                                 ('CoreInitializeDispatcher (', 2320, '0x00FFFFFF')]:
            if insert_after_call(dxe, needle, x, color):
                print('patched %s in DxeMain.c (x=%d)' % (needle.strip(), x))
            else:
                print('WARN: call %r not found in DxeMain.c' % needle)
    else:
        print('WARN: DxeMain.c not found at %s' % dxe)

    return rc


if __name__ == '__main__':
    # Fail the build if instrumentation did not land: a silently unpatched
    # firmware wastes a whole hardware test cycle.
    sys.exit(main())
