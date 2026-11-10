# Description of the final demo scene

The demo is four shots. Shots 1 to 3 are the city on one clock ("Demo scene",
`src/scenes/demo_scene/demo_scene.cpp`), then it cuts to shot 4, the inside of
the smithy, which is its own scene ("Smithy interior",
`src/scenes/smithy/smithy_scene.cpp`), so the city isn't drawn behind the walls.
The cut goes through `Scene::cutTo`, the SceneManager loads the named scene.

| Shot | Scene | Time | What |
|---|---|---|---|
| 1 | Demo scene | 0-75 s | The day, five takes: fields, river, gate, market, main street |
| 2 | Demo scene | 75-97 s | Tilt-shift timelapse from high above, day into night |
| 3 | Demo scene | 97-133 s | At night: low down the north street, round the market, into the smithy's door |
| 4 | Smithy interior | 0-44 s | Inside the smithy: bellows, coal, anvil |

Each take's length comes from its path (`DAY_TAKES`, `NIGHT_TAKE`: points with
the camera's speed at each, at least the take's cruise speed between them,
slowing down over 20 m, `EASE`, to the slower points), the rest from
`LAPSE` and `APPROACH` at the top of the demo scene's file; the smithy has
`DOORS_SHUT`, `GUST`, `SLOW_IN`, `SLOW_OUT`, `END`.

## Shot 1: the day flyby

Late morning, the sun stands still: its shadow map is baked (drawn once), one
box over the whole city. The story of the city's day, five takes, a short dip
to black between each two:

| Take | Time | What |
|---|---|---|
| Fields | 0-16 s | A slow glide low over the east wheat past the farmers, rising a little, the walls on the island ahead |
| River | 16-31 s | Gliding east low over the water, looking across it: the ships, the port and the walls slide by |
| East gate | 31-47.5 s | Over the bridge toward the two guards, then straight up beside the gate, tilting up to the towers and archers |
| Market | 47.5-62.5 s | In from the gate, a little above the people, along the street to the stalls |
| Main street | 62.5-75 s | South past the church, the south gate and its guards ahead |

Each take is one simple, slow move (1.5 to 3.5 m/s, 5 at night). The camera
has two paths: where it is, and the point it looks at, turned to face that
point every frame (no angles interpolated, so it can't overshoot or swing the
long way round). A take's points say what it looks at there (the gate, the
guards, the towers; across the river for the river); the looked-at point
moves smoothly from one to the next. A point with nothing to look at looks
25 m ahead along the path, a little down. It turns at most about 6 deg/s
(`tools/flyby/rot2.py` measures it).

The paths were read off a top-down picture of the city and every point checked
against a height map of everything in it, at least 1 m clear of whatever
stands within 2.5 m, people too (`tools/flyby/`: `hm.py` makes the height
maps, `poi.py` marks the guards, gates, towers, ships and farmers on the
picture, `planner.py` checks and draws the takes of `takes.py`, a copy of the
ones in the code).

The grass and dirt patches on the ground are laid onto the terrain under
them (`tools/drape_ground.py`, they were flat and hung over the slopes) and
don't cast shadows.

The boats and ships ride the water's waves (`Floating`: the height and slope
of the same sum of waves `water.vs` draws, at the boat, every frame), and
roll from side to side a little on their own; their crews and cargo are their
children. The wreck on the rocks stays put.

### To add

- People walking: no mesh animation, only their transforms hop along through
  the city. For now they stay where they stand and each hops a little on the
  spot at random times, 6 to 25 s apart (`Hopping`, on the node with a
  model's instances)
- Birds circling, their wings flapping (a model is still to be found)

## Shot 2: the timelapse into the night

High above the city, narrow 30 deg view, a slow drift while the day goes by at
an even speed, from the late morning to the middle of the night. The sun
crosses the sky along the view direction and the camera drifts across it, so
the shadows don't sweep along with the camera.

### Technical

- Day and night: the sun and moon animators are driven by the demo's clock
  (`seek`), the sky tint follows
- Tilt-shift blur: post-processing pass (to do, post-processing)

### To add

- The people and the birds of shot 1 are still there, and disappear as it
  gets dark
- Lamps (point lights) turning on one by one

## Shot 3: the night

Under the moon, slowly (3 m/s) and low (about 4 m up, just over the people
and the stalls): in through the north gate, down the north street to the
market square, round the stalls on the west of its tree and back east along
its south side (lamps to be added along this way). At the end of the market it turns to
the smithy's doorway (the forge building, standing where one House_7_1 of the
city was, door toward the square), comes onto the door's axis over the stalls
in front of it, sinks to eye height and
pushes in slowly while the doors open (from 5 s before the cut), into the
dark doorway (a black card behind the doors). It fades to black only over
the last 1.2 s, then the demo cuts to shot 4.

### To add

- The lamps of shot 2, lit

Smoke rises out of every chimney of the city and the smithy's own (a
particle emitter each, on from half way into the timelapse). The water
reflects the sky with its night tint, so it's dark too.

## Shot 4: Blacksmith closeup

### Story

Camera flies into doors that open in front of it revealing very dark room full of dust, illuminated only by hot coal, metal and one small window.
Door close behind camera and the blacksmith is pressing onto bellows, closeup of the hot coal sparks, red hot glowing cload in the middle (some kind of space aware shader, with outside impulses)
As bellows blow some coals get blown away (showing away physics simulation)
Then second blacksmith moves prepares to strike red hot metal, vertigo effect slowmotion closeup, sparks fly off the metal as its hit, time goes back to normal and blacksmith shows off different strike techniques (procedural animation)

### What's in it

The room is the high-poly smithy (`tools/export_hpforge.py` makes
`res/models/HPF_*.glb` and `src/scenes/smithy/forge_layout.hpp` from the pack
in `~/Downloads/highpoly-forge.zip`): its stone house, hearth with the coal
in its bowl, hood, anvil, workbenches, buckets, hammers, swords. Decimated
to about 100k triangles, the house sheared level (it stood on a slope in the
pack), plank doors made for its doorway. The same house stands in the city.
The pack's hammer and bellows move on their own (no smith yet), procedural:
the hammer swings about its grip onto the sword on the anvil, heavy blows
and light taps; the bellows press down and open, every press blows over the
coal.

- 0-3.5 s: the camera comes in, the doors shut behind it
- 3.5-11 s: the bellows pumping (the lever, rope and bellows body animate)
- 11-18 s: the coal bed, close; at 15 s a big gust throws some coal over the
  tray's low rim, it rolls off the hearth onto the floor
- 21-31 s: the anvil, dolly zoom (vertigo: the camera moves in from 1.4 m to
  0.75 m while the view widens from 30 to 53 deg) in slow motion (0.2x), sparks
  off every hammer blow
- 31-44 s: back to normal speed, pulling back to the whole room

### Technical

- The pack's keyframes (hammer, sword, bellows lever) replayed by the engine's
  Animator, looping every 6.8 s; the bellows body and rope follow the lever
  (not in the pack), the grindstone wheel spins
- Coal: 120 instanced pieces (3 draws), each a physics sphere with high
  friction in the hearth tray; the air of the bellows is a wind field over the
  bed (`physics::Context::windField`), every closing stroke pushes, the gust
  throws
- The fire's heat follows the bellows: forge light intensity, coal glow
  (emissive maps) and ember particles
- Particles: embers off the coal, sparks off every hammer blow (bursts),
  dust in the window's light
- Lights: four fill lights (points, no shadows) so the corners aren't black,
  the fire (point, shadows), the hot metal (point), the window (spot,
  shadows), a dim bounce light so the room isn't black

### To add

- The blacksmiths: the pack has none, the hammer swings on its own for now
- Different strike techniques (procedural animation)
- Red hot glowing cloud over the coal (space aware shader, with outside
  impulses)
- PBR: metallic hammer, red hot glowing metal

### Maybe

Watter ripples when hammer strikes
Shell textured fur on wall
VOLUMETRIC FOR THE DUST IN THE ROOM

## Storyboard

![Demo storyboard](Gemini_Generated_Image_deuxzkdeuxzkdeux.jpeg)
