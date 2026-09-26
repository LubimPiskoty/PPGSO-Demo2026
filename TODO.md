# TODO list

##  Required

- [ ] 3D objects [9b]
  - [ ] (2b) Objects as unique 3D polygonal models
  - [x] (2b) Unique texture mapping via UV coordinates on a 3D object
  - [ ] (2b) Efficient instancing: render at least 5000 instances using OpenGL instancing
  - [ ] (2b) Transparent objects, correctly blended together with opaque objects in one scene (e.g. sort transparent objects and draw farthest-to-nearest)
  - [ ] (1b) Object using a bump texture (normal mapping)
- [ ] Scene [8b]
  - [ ] (2b) Procedurally generated scene (object placement/constraints defined by a (non-)deterministic algorithm and/or data structure)
  - [x] (4b) Hierarchical scene representation
    - [x] Logical spatial layout (e.g. floor, background, sky, ceiling, walls...) implemented via a scene graph, at minimum a tree data structure
    - [x] Hierarchical transformations: at least 2 levels of hierarchy among 3 objects, using composition of matrix transformations
  - [ ] (2b) Cube mapping — as a skybox, or as environment mapping onto an object's surface
- [ ] Animated objects [6b]
  - [ ] (2b) Procedural animation (simulating complex object behavior/decision-making: branching, cycles, a closed-form method with parameters, etc.)
  - [ ] (3b) Keyframe-based animation with interpolation (a keyframe is a dedicated data structure storing an object's transform — at least position and rotation — over time)
  - [ ] (1b) Animation driven by complex animation curves for smooth motion (linear interpolation and smoothstep are not sufficient)
- [ ] Simulation, particle system [7b]
  - [ ] (3b) Efficient collision detection between objects, implemented with bounding-volume tests, with dynamic collision response (correct bounce off an inclined/slanted surface based on the surface normal at the point of collision)
  - [ ] (2b) Simulation with at least two forces using vector algebra
    - [ ] Must compute acceleration in addition to velocity (e.g. gravity + wind), and support mass
    - [ ] Correct numerical integration, e.g. semi-implicit Euler or RK4
  - [ ] (2b) Rigid body simulation with correct force computation, combining linear and angular motion, and applying torque
- [ ] Camera work [3b]
  - [ ] (2b) Perspective-projection camera; correct handling of window/viewport resizing; adjustable camera parameters
  - [ ] (1b) Animated camera using keyframes storing position and look-at direction over time, using interpolation other than linear
- [ ] Lighting with multiple light sources [13b]
  - [ ] (3b) All light source types with appropriate parameters: directional, point, spot
  - [ ] (1b) Changeable position/orientation of light sources and light color tint
  - [ ] (1b) Correctly combine diffuse light from 2 light sources with diffuse materials
  - [ ] (2b) Correct Phong lighting model with multiple (at least 3) light sources
    - [ ] Correct light attenuation based on depth/distance
    - [ ] At least three material color components and three light color components used
    - [ ] Material and light color components correctly combined
  - [ ] (1b) Blinn-Phong modified lighting model
  - [ ] (1b) HDR rendering with tone mapping and gamma correction
  - [ ] Shadows (pick one)
    - [ ] (1b) Simple approach — shadow must be recomputed based on the relative position/distance of object and light, i.e. not "statically" glued to the object, or
    - [ ] (4b) Shadow maps
- [ ] Post-processing [4b] (pick one)
  - [ ] (1b) Basic per-pixel filter, e.g. grayscale, or via a framebuffer:
  - [ ] (2b) Convolution filter, or
  - [ ] (4b) Advanced filter, e.g. bloom

## Optional

- [ ] Shell texturing for animal fur
- [ ] Automatic Camera exposure

## Engine side of project

- [ ] Make material class so simplify shaders and textures
- [ ] Rewrite assimp loader
