#!/usr/bin/env python3
"""Upscales an embedded image of a .glb to a new size (Lanczos).

    python3 tools/upscale_glb_texture.py res/models/SM_Terrain.glb 4096 BaseColor

The last argument is part of the image name, the first image by default.

It adds no detail, the image just gets more texels, so close-ups stop being
blocky. The original image data stays in the file's buffer, unused.
"""

import io
import json
import struct
import sys
from pathlib import Path

from PIL import Image, ImageFilter

path, size = Path(sys.argv[1]), int(sys.argv[2])
wanted = sys.argv[3] if len(sys.argv) > 3 else ""
data = path.read_bytes()
jlen = struct.unpack("<I", data[12:16])[0]
gltf = json.loads(data[20:20 + jlen])
blen = struct.unpack("<I", data[20 + jlen:24 + jlen])[0]
binary = bytearray(data[28 + jlen:28 + jlen + blen])

image = next(i for i in gltf["images"] if wanted in i.get("name", ""))
view = gltf["bufferViews"][image["bufferView"]]
start = view.get("byteOffset", 0)
png = Image.open(io.BytesIO(bytes(binary[start:start + view["byteLength"]])))
print("was", png.size)
big = png.convert("RGB").resize((size, size), Image.LANCZOS)
big = big.filter(ImageFilter.UnsharpMask(radius=2, percent=60, threshold=2))
out = io.BytesIO()
big.save(out, "PNG", optimize=True)

while len(binary) % 4:
    binary.append(0)
gltf["bufferViews"].append({"buffer": 0, "byteOffset": len(binary),
                            "byteLength": out.tell()})
binary.extend(out.getvalue())
image["bufferView"] = len(gltf["bufferViews"]) - 1
gltf["buffers"][0]["byteLength"] = len(binary)

js = json.dumps(gltf, separators=(",", ":")).encode()
js += b" " * (-len(js) % 4)
while len(binary) % 4:
    binary.append(0)
total = 12 + 8 + len(js) + 8 + len(binary)
path.write_bytes(b"glTF" + struct.pack("<II", 2, total) +
                 struct.pack("<I", len(js)) + b"JSON" + js +
                 struct.pack("<I", len(binary)) + b"BIN\0" + bytes(binary))
print(image.get("name"), "now", size, "x", size, "->", path)
