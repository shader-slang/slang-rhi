import re
from pathlib import Path

BEGIN_MARKER = "#if !defined(WEBGPU_SKIP_PROCS)\n"
END_MARKER = "#endif  // !defined(WEBGPU_SKIP_PROCS)\n"

RE_PROC = re.compile(r"\(*WebGPUProc(\S+)\)")

header = open(
    Path(__file__).parent / "../build/_deps/dawn-src/include/dawn/webgpu.h", "r"
).read()

begin = header.find(BEGIN_MARKER) + len(BEGIN_MARKER)
end = header.find(END_MARKER)
lines = header[begin:end].splitlines()
output = "#define SLANG_RHI_WEBGPU_PROCS(x) \\\n"

for line in lines:
    line = line.strip()
    if line == "":
        continue
    if line.startswith("//"):
        output += "    /* " + line[3:] + " */ \\\n"
    else:
        name = RE_PROC.search(line).group(1)
        output += "    x(" + name + ") \\\n"

print(output)
