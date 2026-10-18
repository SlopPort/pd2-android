"""Generate static_asserts from D2MOO's '//0xNN' field offset comments.

Compiling the result for Android (armeabi-v7a) shows every struct whose layout differs from
the original 32-bit Windows game. Usage: python tools/d2moo_layout_check.py OUT.cpp
"""
import re, sys
from pathlib import Path
SRC = Path(__file__).resolve().parents[1] / 'tooling/D2MOO/source'
HEADERS = sorted((SRC / 'D2Common/include').rglob('*.h'))
struct_re = re.compile(r'^struct\s+(\w+)\s*(?://.*)?$')
field_re = re.compile(r'^\s*(?:const\s+)?[\w:<>\s\*]+?[\s\*](\w+)\s*(?:\[[^\]]*\])*\s*;\s*//\s*(0x[0-9A-Fa-f]+)\b')
out = ['#include <Windows.h>', '#include <cstddef>']
for h in HEADERS:
    out.append('#include <%s>' % h.relative_to(SRC / 'D2Common/include').as_posix())
checks = 0
for h in HEADERS:
    lines = h.read_text(encoding='utf-8', errors='replace').splitlines()
    name = None; depth = 0; nested = 0
    for line in lines:
        m = struct_re.match(line.strip()) if depth == 0 else None
        if m: name = m.group(1); continue
        if name is None: continue
        depth += line.count('{') - line.count('}')
        if depth == 1:
            f = field_re.match(line)
            if f and 'union' not in line:
                out.append('static_assert(offsetof(%s, %s) == %s, "%s::%s");' % (name, f.group(1), f.group(2), name, f.group(1)))
                checks += 1
        if depth == 0 and '}' in line: name = None
print(checks, 'checks')
Path(sys.argv[1]).write_text('\n'.join(out) + '\n')
