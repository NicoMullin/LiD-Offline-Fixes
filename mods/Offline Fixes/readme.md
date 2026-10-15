# Offline Fixes

The switch for `lid-offlinefixes.dll` in LiD DB Editor's mod list. This mod
changes nothing in the database; it only says which plugin it goes with.

- **Ticked and saved:** `lid-offlinefixes.dll` is loaded, and the game's
  offline mode gets its fixes.
- **Unticked and saved:** the plugin is renamed to `lid-offlinefixes.dll.off`,
  which the Plugin Loader skips. Nothing is deleted, and ticking it again
  brings it back.

Close the game before saving: the game holds a plugin it has loaded.

Needs LiD DB Editor 0.12.8 or newer, which knows a mod that is only a plugin.

## What it fixes

The game closing on the spot when a Hater dies just as you take the stairs.
See the [LiD Offline Fixes](https://github.com/NicoMullin/LiD-Offline-Fixes)
page for the details.
