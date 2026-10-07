# Slang logo mesh source

`slang-logo.svg` is the official artwork retrieved on 2026-10-07 from
[shader-slang/shader-slang.github.io](https://github.com/shader-slang/shader-slang.github.io/blob/main/images/logo/slang-logo.svg).
The Slang name and logo identify the Slang project; including this artwork
does not change ownership of the mark.

`slang-logo-mesh.h` is derived from the six colored icon paths in that SVG.
The generator unions the overlapping patches into orange and cyan silhouettes,
flattens the curves, triangulates the faces, and adds a 0.4-unit extrusion with
a 0.025-unit bevel. The wordmark and SVG gradients are omitted. Front, back,
bevel, and side triangles use counterclockwise winding. Normals are computed
offline by welding positions for adjacency and averaging face normals with
corner-angle weights across edges below a 30-degree crease threshold. Sharper
edges and separate colored meshes retain split normals. The generator also
checks that each extruded mesh is watertight.

To regenerate from the bundled SVG, install the offline dependencies in a
virtual environment and run:

```sh
python -m pip install shapely==2.2.0 svgpathtools==1.8.0
python examples/base/assets/generate-logo.py
```

The generated header contains 1,178 vertices and 1,128 triangles in two mesh
ranges. `../logo-scene.h` defines the vertex/range types, assigns the materials,
and adds the ground plane. No SVG parsing or tessellation occurs at runtime.
