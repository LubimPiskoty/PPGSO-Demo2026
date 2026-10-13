# Scene file format

Scenes are JSON files loaded with `loader::loadScene(path)`
(`src/loaders/scene/deserializer.cpp`). The current format version is
**3.2**.

To build or edit a scene without writing JSON by hand, run the interactive
builder: `python3 tools/scene_builder.py my_scene.json`.

Every field is optional unless stated otherwise. A missing field, or one
with the wrong type, falls back to its default, so old and hand-edited
files still load. Unknown components are skipped with a warning.

## Top level

```json
{
  "version": 3.2,
  "materials": [ ... ],
  "scene": {
    "activeCamera": "f9f38a23d2bec37a",
    "physics": { ... },
    "graph": { ... root node ... }
  }
}
```

| Field | Meaning |
|---|---|
| `version` | Format version. A newer version than the loader prints a warning. |
| `materials` | All materials. Meshes reference them by `guid`. |
| `scene.graph` | The root node. |
| `scene.activeCamera` | `guid` of the node with the camera to render from. |
| `scene.physics` | Physics settings for the scene, see [Physics settings](#physics-settings). |

**Guids** are 16 hex characters, for example `"07fb5de87aeca419"`. They
must be unique within a file.

## Materials

```json
{
  "name": "Textured",
  "guid": "07fb5de87aeca419",
  "shader": { "vertex": "default.vs", "fragment": "texture.fs", "instanced": false },
  "textures": { "albedo": "grid_dark.png", "normal": "", "specular": "" },
  "phong": {
    "ambient": [0.1, 0.1, 0.1],
    "diffuse": [1, 1, 1],
    "specular": [0.5, 0.5, 0.5],
    "shininess": 32
  },
  "opacity": 1,
  "blend": "opaque"
}
```

- Shaders come from `res/shaders`, textures from `res/textures`.
- Set `shader.instanced` to `true` for materials used on a mesh with an
  `Instances` component.
- `blend` is `"opaque"` or `"transparent"`.

## Nodes

```json
{
  "name": "Barrel",
  "guid": "a3ad11edb519e22f",
  "enabled": true,
  "transform": {
    "position": [0, 0, 0],
    "rotation": [0, 0.698, 0],
    "scale": [1, 1, 1]
  },
  "components": [ ... ],
  "children": [ ... ]
}
```

- `transform` is relative to the parent.
- `rotation` is Euler angles in **radians**.
- `children` are nodes in the same format.

## Components

Every component has a `type_id`, and can have `"enabled": false`.

### Camera

```json
{ "type_id": "Camera", "fov": 0.873, "near": 0.1, "far": 500 }
```

`fov` is in radians. The window size is applied automatically.

### Mesh

```json
{ "type_id": "Mesh", "model": "SM_PROP_barrel_01.glb",
  "material": { "name": "Textured", "guid": "07fb5de87aeca419" } }
```

`model` is a file in `res/models`. `material.guid` must match a material in
the top-level list; otherwise the mesh is skipped. `name` is only for
readability.

### UserMovement

Fly camera controls.

```json
{ "type_id": "UserMovement", "speed": 1, "sensitivity": 0.1,
  "keys": { "forward": 87, "left": 65, "backward": 83, "right": 68,
            "up": 32, "down": 67 } }
```

Keys are GLFW key codes. The defaults are WASD, Space and C.

### Instances

Draws the node's mesh many times. Either list the transforms, or give a
generator recipe. When `generate` is present, it is used and `instances` is
ignored.

Explicit transforms, each as `[px, py, pz, rx, ry, rz, sx, sy, sz]`:

```json
{ "type_id": "Instances",
  "instances": [[0, 0, 0, 0, 0, 0, 1, 1, 1], [2, 0, 0, 0, 1.5, 0, 1, 1, 1]] }
```

Recipe (steps run in order, with a fixed `seed` so the result is the same
every load):

```json
{ "type_id": "Instances", "seed": 1234,
  "generate": [
    { "fn": "generateDisc", "center": [0, 0, 0], "innerRadius": 5,
      "outerRadius": 20, "up": [0, 1, 0], "count": 200 },
    { "fn": "randomizeScale", "min": 0.8, "max": 1.2 },
    { "fn": "randomizeRotation", "min": [0, 0, 0], "max": [0, 6.28, 0] }
  ] }
```

| `fn` | Fields |
|---|---|
| `generateCircle` | `center`, `radius`, `up`, `count` |
| `generateDisc` | `center`, `innerRadius`, `outerRadius`, `up`, `count` |
| `generateGrid` | `center`, `cellSize`, `cellCount` `[x, z]`, `up`, `count` |
| `generatePlane` | `center`, `size` `[x, z]`, `up`, `count` |
| `randomizeScale` | `min`, `max` (uniform scale) |
| `randomizePosition` | `min`, `max` (offset per axis) |
| `randomizeRotation` | `min`, `max` (Euler radians per axis) |

### Animator

Animates the node along curves.

```json
{ "type_id": "Animator",
  "tracks": [
    { "target": "position", "duration": 4, "speed": 1, "mode": "loop",
      "playing": true, "curve": "path.bin" },
    { "target": "rotation", "duration": 2, "mode": "pingpong",
      "control": [[0, 0, 0], [0, 1.5, 0], [0, 3.14, 0], [0, 6.28, 0]] }
  ] }
```

- `target`: `"position"`, `"rotation"` or `"scale"`.
- `mode`: `"single"`, `"loop"` or `"pingpong"`.
- The curve is either a file in `res/curves` (`curve`) or inline
  `control` points.

### PhysicsBody

```json
{ "type_id": "PhysicsBody",
  "mass": 1,
  "restitution": 0.5,
  "friction": 0.5,
  "velocity": [0, 0, 0],
  "angularVelocity": [0, 0, 0],
  "collider": { "shape": "sphere", "radius": 0.5, "offset": [0, 0, 0] } }
```

- `mass` of 0, or a missing `mass`, makes the body **static**.
- `collider.shape` is `"sphere"` (the default) or `"plane"`.
- `collider.offset` moves the collider in the node's local space.
- The inertia is computed from the collider (solid sphere), so it isn't
  stored.

A plane faces the node's local +Y and collides only on that side. Leave
out `halfSize` for an infinite plane. Planes are always static.

```json
{ "type_id": "PhysicsBody",
  "collider": { "shape": "plane", "halfSize": [3, 1.5] } }
```

## Physics settings

Scene-wide settings in `scene.physics`. The values shown are the defaults.

```json
"physics": {
  "gravity": [0, -9.8, 0],
  "wind": [0, 0, 0],
  "drag": 0.1,
  "angularDrag": 2,
  "timeScale": 1,
  "fixedDt": 0.0027778
}
```

| Field | Meaning |
|---|---|
| `gravity` | Acceleration applied to every dynamic body. |
| `wind` | Wind velocity. Bodies are pushed toward it by `drag`. |
| `drag` | How strongly bodies follow the wind (and are slowed in still air). |
| `angularDrag` | Spin lost per second, so rolling bodies settle. |
| `timeScale` | Simulation speed. `0` pauses, `0.5` is slow motion. |
| `fixedDt` | Length of one physics step in seconds (default 1/360). Smaller is more accurate but slower. |

## Version history

| Version | Change |
|---|---|
| 3 | Shaders store `instanced`. UserMovement, Instances and Animator components. |
| 3.1 | Instances can be a generator recipe. |
| 3.2 | PhysicsBody components and `scene.physics` settings. |
