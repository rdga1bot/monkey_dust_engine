Minimal Godot project stub, copied next to any `MD_USE_LIBGODOT`
executable at build time (CMake `POST_BUILD` step).

Real finding (2026-08-21, not a guess): `libgodot_create_godot_instance()`
+ `instance->start()` requires a *loadable* `run/main_scene` when a real
window is attached (`window_init()`'s implicit windowing / `LibGodotBridge`'s
`attach_to_screen=true`) -- without one, `Main::start()` fails
(`Failed loading scene: res://main_scene.tscn`) or, if `run/main_scene`
is unset entirely, shows a blocking `zenity` "no main scene defined"
dialog with no human to click it in a headless/CI context.

Project detection is resolved relative to the *executable's own
directory* (`/proc/self/exe`-style), not the process's CWD -- this
stub must land in the same directory as the final binary, not just
somewhere in the repo. `main_scene.tscn` is intentionally a trivial
empty `Node3D` -- none of this project's LibGodot code relies on
Godot's scene-tree main-loop convention; everything is driven manually
via `RenderingServer`/`LibGodotBridge`/direct `Node::add_child()`
calls, same as every probe in `probes/`.
