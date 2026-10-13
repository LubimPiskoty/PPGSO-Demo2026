#!/usr/bin/env python3
"""Interactive builder for scene JSON files (see docs/scene_format.md).

    python3 tools/scene_builder.py [scene.json]

Pick options by number, press Enter to accept the default in [brackets].
Nothing is written until you choose Save.
"""

import json
import os
import secrets
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
VERSION = 3.2


def guid():
    return secrets.token_hex(8)


def res_files(folder, exts):
    path = os.path.join(ROOT, "res", folder)
    if not os.path.isdir(path):
        return []
    return sorted(f for f in os.listdir(path) if f.endswith(exts))


# ---- Prompts ----

def ask(label, default=""):
    value = input(f"{label} [{default}]: ").strip()
    return value if value else default


def ask_float(label, default):
    while True:
        try:
            return float(ask(label, default))
        except ValueError:
            print("  not a number")


def ask_vec(label, default):
    text = " ".join(str(v) for v in default)
    while True:
        parts = ask(label, text).replace(",", " ").split()
        try:
            if len(parts) == len(default):
                return [float(p) for p in parts]
        except ValueError:
            pass
        print(f"  enter {len(default)} numbers")


def ask_bool(label, default):
    return ask(label + " (y/n)", "y" if default else "n").lower().startswith("y")


def choose(label, options) -> int:
    """Numbered pick from options, returns the index."""
    i = choose_optional(label, options, allow_none=False)
    assert i is not None
    return i


def choose_optional(label, options, allow_none=True):
    """Like choose, but 0 or Enter skips and returns None."""
    print(label)
    if allow_none:
        print("  0) none")
    for i, opt in enumerate(options, 1):
        print(f"  {i}) {opt}")
    while True:
        value = input("> ").strip()
        if allow_none and value in ("", "0"):
            return None
        if value.isdigit() and 1 <= int(value) <= len(options):
            return int(value) - 1
        print("  pick a number from the list")


def choose_or_type(label, options, default=""):
    """Pick from a list, or type a value that isn't in it."""
    print(label + " (number, or type a name)")
    for i, opt in enumerate(options, 1):
        print(f"  {i}) {opt}")
    value = ask(">", default)
    if value.isdigit() and 1 <= int(value) <= len(options):
        return options[int(value) - 1]
    return value


# ---- Scene helpers ----

def new_scene():
    camera = {
        "name": "Camera", "guid": guid(), "enabled": True,
        "transform": {"position": [0, 5, 15], "rotation": [-0.2, 0, 0],
                      "scale": [1, 1, 1]},
        "components": [{"type_id": "Camera", "fov": 0.873, "near": 0.1,
                        "far": 500}],
        "children": [],
    }
    root = {"name": "Root", "guid": guid(), "enabled": True,
            "components": [], "children": [camera]}
    return {"version": VERSION, "materials": [],
            "scene": {"activeCamera": camera["guid"], "graph": root}}


def walk(node, depth=0, parent=None):
    yield node, depth, parent
    for child in node.get("children", []):
        yield from walk(child, depth + 1, node)


def describe(node):
    types = [c.get("type_id", "?") for c in node.get("components", [])]
    return node.get("name", "Node") + (f"  ({', '.join(types)})" if types else "")


def print_tree(scene):
    graph = scene["scene"].get("graph")
    if not graph:
        print("(empty)")
        return
    for node, depth, _ in walk(graph):
        print("  " * depth + "- " + describe(node))


def pick_node(scene, label="Node:"):
    entries = list(walk(scene["scene"]["graph"]))
    i = choose(label, ["  " * d + describe(n) for n, d, _ in entries])
    return entries[i]


def pick_material(scene):
    materials = scene["materials"]
    names = [m["name"] for m in materials] + ["+ new material"]
    i = choose("Material:", names)
    if i == len(materials):
        return add_material(scene)
    return materials[i]


# ---- Actions ----

def add_node(scene):
    parent, _, _ = pick_node(scene, "Parent:")
    node = {
        "name": ask("Name", "Node"), "guid": guid(), "enabled": True,
        "transform": {
            "position": ask_vec("Position", [0, 0, 0]),
            "rotation": ask_vec("Rotation (radians)", [0, 0, 0]),
            "scale": ask_vec("Scale", [1, 1, 1]),
        },
        "components": [], "children": [],
    }
    parent.setdefault("children", []).append(node)
    print(f"Added '{node['name']}'")
    if ask_bool("Add components now", True):
        while add_component(scene, node):
            pass


def edit_transform(scene):
    node, _, _ = pick_node(scene)
    t = node.setdefault("transform", {})
    node["name"] = ask("Name", node.get("name", "Node"))
    t["position"] = ask_vec("Position", t.get("position", [0, 0, 0]))
    t["rotation"] = ask_vec("Rotation (radians)", t.get("rotation", [0, 0, 0]))
    t["scale"] = ask_vec("Scale", t.get("scale", [1, 1, 1]))


def remove_node(scene):
    node, _, parent = pick_node(scene)
    if parent is None:
        print("Can't remove the root")
        return
    if ask_bool(f"Remove '{node['name']}' and its children", False):
        parent["children"].remove(node)


def add_material(scene):
    shaders = res_files("shaders", (".vs", ".fs"))
    material = {
        "name": ask("Material name", "Material"), "guid": guid(),
        "shader": {
            "vertex": choose_or_type(
                "Vertex shader", [s for s in shaders if s.endswith(".vs")],
                "default.vs"),
            "fragment": choose_or_type(
                "Fragment shader", [s for s in shaders if s.endswith(".fs")],
                "texture.fs"),
            "instanced": ask_bool("Instanced", False),
        },
        "textures": {},
        "phong": {"ambient": [0.1, 0.1, 0.1],
                  "diffuse": ask_vec("Diffuse color", [1, 1, 1]),
                  "specular": [0.5, 0.5, 0.5], "shininess": 32},
        "opacity": 1, "blend": "opaque",
    }
    textures = res_files("textures", (".png", ".jpg"))
    i = choose_optional("Albedo texture:", textures)
    if i is not None:
        material["textures"]["albedo"] = textures[i]
    scene["materials"].append(material)
    print(f"Added material '{material['name']}'")
    return material


def component_camera():
    return {"type_id": "Camera", "fov": ask_float("FOV (radians)", 0.873),
            "near": ask_float("Near", 0.1), "far": ask_float("Far", 500)}


def component_mesh(scene):
    model = choose_or_type("Model", res_files("models", (".glb", ".gltf")))
    material = pick_material(scene)
    return {"type_id": "Mesh", "model": model,
            "material": {"name": material["name"], "guid": material["guid"]}}


def component_body():
    if choose("Collider:", ["sphere", "plane"]) == 0:
        collider: dict = {"shape": "sphere",
                          "radius": ask_float("Radius", 0.5)}
    else:
        collider = {"shape": "plane"}
        if not ask_bool("Infinite", True):
            collider["halfSize"] = ask_vec("Half size (x z)", [1, 1])
    offset = ask_vec("Collider offset", [0, 0, 0])
    if any(offset):
        collider["offset"] = offset

    body = {"type_id": "PhysicsBody", "collider": collider}
    if collider["shape"] == "sphere" and not ask_bool("Static", False):
        body["mass"] = ask_float("Mass", 1)
        body["velocity"] = ask_vec("Start velocity", [0, 0, 0])
    body["restitution"] = ask_float("Restitution (bounciness)", 0.5)
    body["friction"] = ask_float("Friction", 0.5)
    return body


def component_movement():
    return {"type_id": "UserMovement", "speed": ask_float("Speed", 1),
            "sensitivity": ask_float("Sensitivity", 0.1)}


def add_component(scene, node=None):
    """Returns False when the user is done adding."""
    if node is None:
        node, _, _ = pick_node(scene)
    kinds = ["Mesh", "PhysicsBody", "Camera", "UserMovement"]
    i = choose_optional(f"Component for '{node['name']}':", kinds)
    if i is None:
        return False
    if i == 0:
        component = component_mesh(scene)
    elif i == 1:
        component = component_body()
    elif i == 2:
        component = component_camera()
        if ask_bool("Make it the active camera", True):
            scene["scene"]["activeCamera"] = node["guid"]
    else:
        component = component_movement()
    component["enabled"] = True
    node.setdefault("components", []).append(component)
    print(f"Added {component['type_id']}")
    return True


def remove_component(scene):
    node, _, _ = pick_node(scene)
    components = node.get("components", [])
    if not components:
        print("No components")
        return
    i = choose_optional("Remove:",
                        [c.get("type_id", "?") for c in components])
    if i is not None:
        components.pop(i)


def edit_physics(scene):
    p = scene["scene"].setdefault("physics", {})
    p["gravity"] = ask_vec("Gravity", p.get("gravity", [0, -9.8, 0]))
    p["wind"] = ask_vec("Wind", p.get("wind", [0, 0, 0]))
    p["drag"] = ask_float("Drag", p.get("drag", 0.1))
    p["angularDrag"] = ask_float("Angular drag", p.get("angularDrag", 2))
    p["timeScale"] = ask_float("Time scale", p.get("timeScale", 1))


def save(scene, path):
    path = ask("Save to", path)
    scene["version"] = max(scene.get("version", 0), VERSION)
    with open(path, "w") as f:
        json.dump(scene, f, indent="\t")
    print(f"Saved {path}")
    return path


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "new_scene.json"
    if os.path.exists(path):
        with open(path) as f:
            scene = json.load(f)
        print(f"Opened {path}")
    else:
        scene = new_scene()
        print(f"New scene ({path} will be created on save)")

    actions = [
        ("Show tree", lambda: print_tree(scene)),
        ("Add node", lambda: add_node(scene)),
        ("Edit node name/transform", lambda: edit_transform(scene)),
        ("Remove node", lambda: remove_node(scene)),
        ("Add component", lambda: add_component(scene)),
        ("Remove component", lambda: remove_component(scene)),
        ("Add material", lambda: add_material(scene)),
        ("Physics settings", lambda: edit_physics(scene)),
    ]
    while True:
        print()
        i = choose("What now?", [a[0] for a in actions] + ["Save", "Quit"])
        print()
        try:
            if i < len(actions):
                actions[i][1]()
            elif i == len(actions):
                path = save(scene, path)
            elif ask_bool("Quit (unsaved changes are lost)", False):
                return
        except KeyboardInterrupt:
            print("\n(cancelled)")


if __name__ == "__main__":
    try:
        main()
    except (KeyboardInterrupt, EOFError):
        print()
