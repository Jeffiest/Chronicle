# Replacing 3D models (Windows fork)

1. Dump the game's models: `E:\Chronicle-project-Claude\run_win.ps1 -DumpModels`, play until the model has been on screen
   (open the Equip screen for weapons), then quit. Files appear in `mods\_dump\models\<texture>__<N>v.obj`.
   `<texture>` is the model's texture name, the same name the PNG texture mods use (for example `c01w01`, Toan's knife).
2. Make a mod folder, for example `mods\myweapon\models\`, and copy or create your model there as `c01w01.obj`
   (replaces every model that uses texture c01w01) or keep the dump's `c01w01__312v.obj` name to replace only that exact
   model (the number is its vertex count).
3. Put a PNG named like the texture in `mods\myweapon\textures\c01w01.png` to paint it. It is scaled to the original
   texture's size, so paint to your model's UV layout.
4. Run the game. The console says `mods: replaced model c01w01 (312 -> N vertices)`.

## Blender round trip (recommended)
Import a dumped `.obj` into Blender with default settings, edit or replace the shape, export `.obj` with the same
default axis settings, UVs and normals ticked, "Triangulate faces" ticked. Units and axes then match the game.

## glTF (.glb) from Unity, Unreal or Blender
Export "glTF Binary (.glb)", mesh only (first mesh is used; node transforms and skinning are ignored). Name it like the
texture, `c01w01.glb`. Its units and axes are used as they are, so add `c01w01.json` next to it to fit it to the game:
`{"scale": 100, "rotate_deg": [0, 90, 0], "offset": [0, 0, 0], "flip_v": false}`. Compare with the dumped OBJ of the
original to find the right scale and rotation, then adjust and relaunch.

## Several materials: `usemtl strip<N>`
A dumped model lists its faces in groups, `usemtl strip0`, `usemtl strip1`, ... one per original strip. A replacement keeps
strip N's material (texture, colours) for the faces under that group, so a character with a body, boots and hair stays painted
correctly. Keep those lines when you edit the dump (in Blender, name your materials `strip0`, `strip1`, ... and export OBJ with
materials on). A face group named anything else, or a model with no groups (also every .glb), uses the original's first material.
`mod_tool.py` keeps the groups of an OBJ you give it. Dumps made before this feature have no groups: dump again.

## Animated characters and enemies
Characters and enemies are skinned by the game each tick (bones move groups of vertices with weights). A replacement follows that
automatically: every vertex of your model takes the bone weights of the nearest vertex of the original (in the bind pose) and is posed
with the game's own bone matrices, so limbs swing about the right joints even when your model is a different shape or size. So:
1. dump the model (`run_win.ps1 -DumpModels`, let it be on screen; Toan's body is `c01d02__1300v.obj`),
2. edit it in Blender *in the same pose as the dump* (it is a T-pose bind pose, not what you see in game) and export OBJ,
3. put it in `mods\<mod>\models\<same name>.obj`. No skeleton, weights or animation files are needed.

Your shape can differ a lot (a wider body, longer arms and legs, a bulkier outfit); keep it roughly around the original so each part
sits near the bones that move it. A different vertex count is fine. Normals (lighting) stay as authored in the bind pose, and `.glb`
skinning, bones and animation clips are ignored. `DC_DEBUG_SKIN=1` prints which replaced models the game is skinning.
(Before this was done by copying each vertex's movement from its neighbours, which bent limbs wrongly on big reshapes.)

## Limits
Static models (weapons, props) and animated ones (above) work. A model is replaced when it is built, so changes show after
the next load of the screen or area (restart the game if unsure).

## The easy way: mod_tool.py
`python E:\Chronicle-project-Claude\mod_tool.py` opens a window. Search the list of dumped models, pick the one to
replace, choose your own .glb or .obj (or drop it on the window if `pip install tkinterdnd2` is installed), check the
preview (grey = original, orange = yours), adjust size/turn if needed and press Install. It scales, orients and aligns
your model to the original, converts it to the game's format, and saves its embedded texture as the PNG override.
