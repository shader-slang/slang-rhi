from pathlib import Path
import re
import urllib.request

# Markers in the Dawn header that contain proc declarations
BEGIN_MARKER = "#if !defined(WGPU_SKIP_PROCS)\n"
END_MARKER = "#endif  // !defined(WGPU_SKIP_PROCS)\n"

# Regex to extract proc names
RE_PROC = re.compile(r"\(*WGPUProc(\S+)\)")

EMSCRIPTEN_HEADER_URL = "https://raw.githubusercontent.com/webgpu-native/webgpu-headers/60cd9020309b87a30cd7240aad32accd24262a5e/webgpu.h"

def extract_procs_from_content(content: str) -> tuple[list[tuple[str, str]], set[str]]:
    """
    Extract procs from header content.
    Returns:
        - List of (type, name) tuples where type is 'comment' or 'proc'
        - Set of proc names
    """
    begin = content.find(BEGIN_MARKER)
    if begin == -1:
        # Emscripten header uses different format - search for typedef patterns
        procs = set()
        for match in re.finditer(r"typedef\s+\S+\s+\(\*WGPUProc(\w+)\)", content):
            procs.add(match.group(1))
        return [], procs
    
    begin += len(BEGIN_MARKER)
    end = content.find(END_MARKER)
    lines = content[begin:end].splitlines()
    
    items = []
    proc_names = set()
    
    for line in lines:
        line = line.strip()
        if line == "":
            continue
        if line.startswith("//"):
            items.append(("comment", line[3:]))
        else:
            match = RE_PROC.search(line)
            if match:
                name = match.group(1)
                items.append(("proc", name))
                proc_names.add(name)
    
    return items, proc_names


def fetch_url(url: str) -> str:
    import subprocess
    result = subprocess.run(["curl", "-sL", url], capture_output=True, text=True)
    if result.returncode != 0:
        raise Exception(f"curl failed: {result.stderr}")
    return result.stdout


def main():

    dawn_header_path = Path(__file__).parent / "../build/_deps/dawn-src/include/dawn/webgpu.h"
    if not dawn_header_path.exists():
        print(f"Error: Dawn header not found at {dawn_header_path}")
        print("Please run cmake configure first to fetch Dawn.")
        return
    
    dawn_content = open(dawn_header_path, "r").read()
    dawn_items, dawn_procs = extract_procs_from_content(dawn_content)
    
    print(f"// Fetching Emscripten header from {EMSCRIPTEN_HEADER_URL}...")
    try:
        em_content = fetch_url(EMSCRIPTEN_HEADER_URL)
        em_items, em_procs = extract_procs_from_content(em_content)
    except Exception as e:
        print(f"// Error fetching Emscripten header: {e}")
        em_procs = set()
        
    print(f"// Dawn procs:       {len(dawn_procs)}")
    print(f"// Emscripten procs: {len(em_procs)}")
    print()
    
    print("#if !SLANG_WASM")
    output = "#define SLANG_RHI_WGPU_PROCS(x) \\\n"
    for i, (item_type, item_value) in enumerate(dawn_items):
        is_last = (i == len(dawn_items) - 1)
        suffix = "" if is_last else " \\"
        if item_type == "comment":
            output += "    /* " + item_value + " */" + suffix + "\n"
        else:
            output += "    x(" + item_value + ")" + suffix + "\n"
    print(output)
    print("#else")
    output = "#define SLANG_RHI_WGPU_PROCS(x) \\\n"
    for i, (item_type, item_value) in enumerate(em_items):
        is_last = (i == len(em_items) - 1)
        suffix = "" if is_last else " \\"
        if item_type == "comment":
            output += "    /* " + item_value + " */" + suffix + "\n"
        else:
            output += "    x(" + item_value + ")" + suffix + "\n"
    print(output)
    print("#endif")

if __name__ == "__main__":
    main()
