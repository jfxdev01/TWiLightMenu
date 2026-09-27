#!/usr/bin/env python3
# Same output as BlocksDS' bin2c
import os, sys
src, outdir = sys.argv[1], sys.argv[2]
name = os.path.basename(src).replace('.', '_')
data = open(src, 'rb').read()
with open(os.path.join(outdir, name + '.h'), 'w') as h:
    h.write(f'#pragma once\n#include <stdint.h>\n#define {name}_size ({len(data)})\nextern "C" const uint8_t {name}[{len(data)}];\n')
with open(os.path.join(outdir, name + '.c'), 'w') as c:
    c.write(f'#include <stdint.h>\nextern "C" const uint8_t {name}[{len(data)}] __attribute__((aligned(4))) = {{\n')
    for i in range(0, len(data), 16):
        c.write(','.join(str(b) for b in data[i:i + 16]) + ',\n')
    c.write('};\n')
