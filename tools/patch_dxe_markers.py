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
STRIDE = 2560


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
            print('WARN: definition of %s not found under %s' % (name, root))
            rc = 1
            continue
        if insert_marker(path, off, x, color):
            print('patched %s in %s (x=%d)' % (name, path, x))
        else:
            print('WARN: no body brace for %s in %s' % (name, path))
            rc = 1
    return rc


if __name__ == '__main__':
    # Fail the build if instrumentation did not land: a silently unpatched
    # firmware wastes a whole hardware test cycle.
    sys.exit(main())
