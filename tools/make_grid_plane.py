# Writes res/models/grid_plane.glb: a flat SIZE x SIZE m quad at y = 0,
# centered on the origin, normal +Y, with res/textures/grid_dark.png embedded
# as its base colour (the model loader only uses embedded textures). The UVs
# go past 1 so the texture repeats (GL_REPEAT) once every TILE m.
#   python3 tools/make_grid_plane.py

import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIZE = 10.0
TILE = 10.0
h = SIZE / 2
r = SIZE / TILE

positions = [(-h, 0, -h), (h, 0, -h), (h, 0, h), (-h, 0, h)]
normals = [(0, 1, 0)] * 4
uvs = [(0, 0), (r, 0), (r, r), (0, r)]
indices = [0, 3, 2, 0, 2, 1]  # counter-clockwise seen from above
image = (ROOT / "res/textures/grid_dark.png").read_bytes()


def pad4(b, fill=b"\0"):
    return b + fill * (-len(b) % 4)


chunks = [
    b"".join(struct.pack("<3f", *p) for p in positions),
    b"".join(struct.pack("<3f", *n) for n in normals),
    b"".join(struct.pack("<2f", *t) for t in uvs),
    struct.pack(f"<{len(indices)}I", *indices),
    image,
]
views, binary = [], b""
for i, chunk in enumerate(chunks):
    view = {"buffer": 0, "byteOffset": len(binary), "byteLength": len(chunk)}
    if i < 3:
        view["target"] = 34962  # ARRAY_BUFFER
    elif i == 3:
        view["target"] = 34963  # ELEMENT_ARRAY_BUFFER
    views.append(view)
    binary += pad4(chunk)

gltf = {
    "asset": {"version": "2.0", "generator": "tools/make_grid_plane.py"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [{"mesh": 0, "name": "GridPlane"}],
    "meshes": [{"name": "GridPlane", "primitives": [{
        "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
        "indices": 3, "material": 0}]}],
    "materials": [{"name": "Grid", "doubleSided": True, "pbrMetallicRoughness": {
        "baseColorTexture": {"index": 0}, "metallicFactor": 0, "roughnessFactor": 1}}],
    "textures": [{"source": 0, "sampler": 0}],
    "samplers": [{"wrapS": 10497, "wrapT": 10497}],  # REPEAT
    "images": [{"bufferView": 4, "mimeType": "image/png", "name": "grid_dark"}],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
         "min": [-h, 0, -h], "max": [h, 0, h]},
        {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
        {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
        {"bufferView": 3, "componentType": 5125, "count": len(indices), "type": "SCALAR"},
    ],
    "bufferViews": views,
    "buffers": [{"byteLength": len(binary)}],
}

json_chunk = pad4(json.dumps(gltf, separators=(",", ":")).encode(), b" ")
glb = b"".join([
    struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(json_chunk) + 8 + len(binary)),
    struct.pack("<I4s", len(json_chunk), b"JSON"), json_chunk,
    struct.pack("<I4s", len(binary), b"BIN\0"), binary,
])
out = ROOT / "res/models/grid_plane.glb"
out.write_bytes(glb)
print(f"Wrote {out} ({len(glb)} bytes)")
