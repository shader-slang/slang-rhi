"""Regenerate slang-logo-mesh.h; offline only (pip install shapely svgpathtools).

The six icon paths in the official SVG form two colored silhouettes. Union them
before extrusion so the SVG's overlapping gradient patches do not become
overlapping triangles. The wordmark is intentionally omitted.
"""

from pathlib import Path
import math
from collections import defaultdict
import xml.etree.ElementTree as ET

from shapely import constrained_delaunay_triangles
from shapely.geometry import Polygon, Point
from shapely.geometry.polygon import orient
from shapely.ops import unary_union, nearest_points
from svgpathtools import parse_path

ROOT = Path(__file__).resolve().parent
paths = ET.parse(ROOT / "slang-logo.svg").getroot().findall(".//{*}path")[:6]
polygons = []
for element in paths:
    points = []
    for segment in parse_path(element.attrib["d"]):
        steps = max(1, math.ceil(segment.length() / 5))
        for i in range(steps):
            p = segment.point(i / steps)
            points.append(((p.real - 303) * .008, (450 - p.imag) * .008))
    polygons.append(Polygon(points).buffer(0))

vertices, indices, parts = [], [], []
lookup = {}


def triangle(points, normals):
    a, b, c = points
    ab, ac = [b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)]
    cross = (ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0])
    if sum(cross[i] * normals[0][i] for i in range(3)) < 0:
        points, normals = points[::-1], normals[::-1]
    for p, n in zip(points, normals):
        key = tuple(round(v, 6) for v in (*p, *n))
        if key not in lookup:
            lookup[key] = len(vertices)
            vertices.append(key)
        indices.append(lookup[key])


for group in ((1, 2, 4), (0, 3, 5)):
    start = len(indices)
    # Close subpixel cracks from the original SVG's separately rounded patches.
    outer = unary_union([polygons[i] for i in group]).buffer(.006).buffer(-.006).simplify(.004)
    assert outer.geom_type == "Polygon" and outer.is_valid and not outer.interiors
    outer = orient(outer)
    bevel, half_depth = .025, .20
    inner = outer.buffer(-bevel, join_style="mitre")
    assert inner.geom_type == "Polygon" and inner.is_valid and not inner.interiors

    for side in (-1, 1):
        for tri in constrained_delaunay_triangles(inner).geoms:
            points = list(tri.exterior.coords)[:3]
            triangle([(x, y, side * half_depth) for x, y in points], [(0, 0, side)] * 3)
        for tri in constrained_delaunay_triangles(outer.difference(inner)).geoms:
            positions, normals = [], []
            for x, y in list(tri.exterior.coords)[:3]:
                p = Point(x, y)
                on_inner = inner.boundary.distance(p) < 1e-7
                positions.append((x, y, side * (half_depth if on_inner else half_depth - bevel)))
                q = nearest_points(p, outer.boundary if on_inner else inner.boundary)[1]
                nx, ny = (q.x-x, q.y-y) if on_inner else (x-q.x, y-q.y)
                length = math.hypot(nx, ny)
                normals.append((nx / length / math.sqrt(2), ny / length / math.sqrt(2), side / math.sqrt(2)))
            triangle(positions, normals)

    points = list(outer.exterior.coords)
    for a, b in zip(points, points[1:]):
        nx, ny = b[1]-a[1], a[0]-b[0]
        length = math.hypot(nx, ny)
        normal = (nx/length, ny/length, 0)
        z = half_depth - bevel
        quad = [(a[0], a[1], -z), (b[0], b[1], -z), (b[0], b[1], z), (a[0], a[1], z)]
        triangle(quad[:3], [normal]*3)
        triangle([quad[0], quad[2], quad[3]], [normal]*3)
    parts.append((start, len(indices)-start))


def smooth_normals(crease_degrees=30):
    """Angle-weighted normals across edge-connected faces below the crease angle.

    Weld positions for adjacency, then split corners into smoothing groups. This
    joins duplicated side vertices while retaining the cap/bevel/side creases.
    Process each colored mesh separately so touching parts are never welded.
    """
    global vertices, indices
    new_vertices, new_indices, vertex_lookup = [], [], {}
    threshold = math.cos(math.radians(crease_degrees))

    def sub(a, b):
        return tuple(x-y for x, y in zip(a, b))

    def dot(a, b):
        return sum(x*y for x, y in zip(a, b))

    def cross(a, b):
        return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])

    for start, count in parts:
        positions = [vertices[i][:3] for i in indices[start:start+count]]
        parents = list(range(count))
        normals, angles = [], []
        edges = defaultdict(list)

        def find(i):
            while parents[i] != i:
                parents[i] = parents[parents[i]]
                i = parents[i]
            return i

        for i in range(0, count, 3):
            face = positions[i:i+3]
            normal = cross(sub(face[1], face[0]), sub(face[2], face[0]))
            length = math.sqrt(dot(normal, normal))
            assert length > 1e-12
            normals.append(tuple(x/length for x in normal))
            for corner in range(3):
                a = sub(face[(corner+1) % 3], face[corner])
                b = sub(face[(corner+2) % 3], face[corner])
                c = cross(a, b)
                angles.append(math.atan2(math.sqrt(dot(c, c)), dot(a, b)))
                j, k = i+corner, i+(corner+1) % 3
                edges[tuple(sorted((positions[j], positions[k])))].append((j, k))

        for incident in edges.values():
            assert len(incident) == 2, "Extruded logo must be watertight"
            (a, b), (c, d) = incident
            if dot(normals[a//3], normals[c//3]) >= threshold:
                for j in (a, b):
                    k = c if positions[j] == positions[c] else d
                    parents[find(j)] = find(k)

        sums = defaultdict(lambda: [0., 0., 0.])
        for i in range(count):
            total = sums[find(i)]
            for axis in range(3):
                total[axis] += normals[i//3][axis] * angles[i]
        for i, position in enumerate(positions):
            normal = sums[find(i)]
            length = math.sqrt(dot(normal, normal))
            key = (*position, *(round(x/length, 6) for x in normal))
            if key not in vertex_lookup:
                vertex_lookup[key] = len(new_vertices)
                new_vertices.append(key)
            new_indices.append(vertex_lookup[key])
    vertices, indices = new_vertices, new_indices


smooth_normals()


def number(v):
    return f"{v:.6f}f"


with (ROOT / "slang-logo-mesh.h").open("w", newline="\n") as out:
    out.write("// Generated by generate-logo.py from the official Slang logo. See README.md.\n")
    out.write("// clang-format off\n#pragma once\n\nnamespace rhi::logo {\n\n")
    out.write("inline constexpr Vertex kVertices[] = {\n")
    for v in vertices:
        out.write("    {{" + ", ".join(map(number, v[:3])) + "}, {" + ", ".join(map(number, v[3:])) + "}},\n")
    out.write("};\n\ninline constexpr uint32_t kIndices[] = {\n")
    for i in range(0, len(indices), 18):
        out.write("    " + ", ".join(map(str, indices[i:i+18])) + ",\n")
    out.write("};\n\ninline constexpr MeshRange kMeshRanges[] = {\n")
    for i, (start, count) in enumerate(parts):
        out.write(f"    {{{start}, {count}, {i}}},\n")
    out.write("};\n\n} // namespace rhi::logo\n// clang-format on\n")
print(f"Generated {len(vertices)} vertices, {len(indices)//3} triangles")
