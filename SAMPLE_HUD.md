# Free HUD asset for framework testing

For the visual/UI test pass, use **Kenney UI Pack**:

- Official page: https://kenney.nl/assets/ui-pack
- License: CC0 1.0 (public-domain dedication)
- Purpose here: sample HUD/buttons/panels/icons only; it is not an engine dependency.

The minimal executable remains `RA-Minimal.exe` and is built from the repository's own `examples/basic_game.cpp` against `RA::Engine` (`ra_engine`). The HUD pack is intended as disposable test content while asset loading, sprites, UI layout, and interaction are extracted into reusable framework modules.

Do not couple engine code to this pack. Sample assets belong under a future `samples/assets/hud/` directory so projects can replace them without changing the engine.
