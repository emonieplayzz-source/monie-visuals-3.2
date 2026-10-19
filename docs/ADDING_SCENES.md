# Adding a scene (no rewrite needed)
1. `Shaders/scene.frag`: add `vec3 sceneN(vec2 p)` (p = aspect-corrected centered coords; use `pal(t)` for colors, `a_bass/a_mid/a_treb/a_beat/a_rms/a_flux` for audio, `img(p)` for the user image) and one branch in `main()`.
2. `Source/Core/SceneRegistry.h`: add `{ N, "Name", "Category", "search tags" }`.
3. `Source/Params.h`: append the name to `sceneNames()` (same index N). Do not renumber old scenes (saved projects/automation use the index).
4. Bump the clamp `jlimit (0, 14, ...)` in `VisualizerComponent.cpp` (search "sceneIdx") and the `scene` choice list follows `sceneNames()` automatically.
5. `tests/test_engines.cpp`: update the catalogue count.
Known debt: names are listed in two places (Params.h and SceneRegistry.h) and per-scene parameters are not yet supported - every scene still uses the shared parameter set. Per-scene parameter blocks are the next architectural step toward the 60+ scene goal.
