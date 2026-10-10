# LiD Offline Fixes

Fixes bugs in the offline version of Let It Die (Steam): things the game does
itself, with no mods involved, that close it for no good reason.

- **lid-offlinefixes.dll** (`dll/`): a plugin for the
  [LiD Plugin Loader](https://github.com/NicoMullin/LiD-Plugin-Loader).

## What it fixes

### The game closing when you take the stairs

Kill a Hater and take the stairs while it is still dying, and the game can
close on the spot: no error, no crash report from the game, only an
"APPCRASH" in Windows' event log (`KERNELBASE.dll`, exception code 1).
`BrgGame\Logs\Launch.log` ends with a line like

    Killed zombie entity not found. zid=32122c23-...

While you play a floor, the game reports what happens on it (kills, broken
boxes, opened chests, rewards taken) to its built-in offline server, which
looks each one up in the floor's data. A Hater still dying as you leave is
reported after the floor has changed, so it is not there any more. The
server's code is written to skip such an event: it logs the message and moves
on to the next one. But it also sends the same message to the engine's fatal
error log first, and that closes the game.

The plugin takes out exactly those fatal-error calls, so the server does what
it was written to do: logs the message (it still lands in `Launch.log`) and
skips the event. 19 calls, for these floor events:

| Event                                                           | The game's message starts                      |
|-----------------------------------------------------------------|------------------------------------------------|
| Killed a Hater                                                  | `Killed zombie`                                |
| Killed by a Hater                                               | `current time Killed from`                     |
| Met a Hater                                                     | `encountPlayerEnemy`                           |
| A Hater's reward, and its Bloodnium                             | `user_get_zombie_reward`, `getRewardBloodnium` |
| Killed a regular enemy                                          | `Killed zako`                                  |
| A regular enemy's chest money                                   | `getZakoTrboxMoney`                            |
| Killed a boss, beast, Jackal master or one of the Four Forcemen | `not found kill`                               |
| Broke an object                                                 | `brekable_object`                              |
| Opened a floor chest                                            | `floor_trbox`, `rUserPopTrbox`                 |

A skipped event gives nothing: a Hater killed that way gives no reward. Before,
it gave a closed game.

The same check on your own beasts, materials and the vending machine is left
as it is. Those are your inventory, not floor events, so "not found" there
means something really is wrong.

## How it works

Safety rules, all in [dll/lid_offlinefixes.c](dll/lid_offlinefixes.c):

- It only does anything inside `BrgGame-Steam.exe`. Loaded by any other
  program, it changes nothing.
- Each call is found from its message: the code that loads the message for a
  log call, then the call itself. Which log is the fatal one is read off the
  "Killed zombie" handler, which logs its message twice, first to the normal
  log and then to the fatal one. Every message must lead to a fatal call, all
  through the same log function, and there must be exactly 19 calls, or
  nothing is changed. After a game update that moves the code, the plugin does
  nothing and says so in its log.
- Changes are made **in memory only**: each of the 19 calls (5 bytes) becomes
  a no-op. The exe on disk is never touched.
- No network, no registry. The only file it writes is its log,
  `lid-offlinefixes.log`, next to itself.
- Delete `lid-offlinefixes.dll` and the game is exactly stock again.

## Why the DLL is built by GitHub

A DLL can do anything, so you should not have to take one on trust. Every
release on this page is built by GitHub Actions from the source in this
repository, on GitHub's own machines, and the tests run before anything is
published. You can check the DLL in two ways:

1. **Signed build record.** Each release DLL has an attestation: a signed
   record, kept by GitHub, of the exact workflow run and commit that built it.
   With the [GitHub CLI](https://cli.github.com/):

   ```
   gh attestation verify lid-offlinefixes.dll --repo NicoMullin/LiD-Offline-Fixes
   ```

2. **Rebuild it yourself.** The build is reproducible: the same source and the
   same compiler give a byte-identical file. See
   [Building it yourself](#building-it-yourself) and compare the SHA-256 with
   the one on the release page.

## Installing

**The easy way.** In LiD DB Editor (the LET IT DIE DB Mod Manager) 0.12.6 or
newer, close the game, click **Get mods...** and then **Get** next to **Offline
Fixes**. It downloads this repository's newest release, checks it against the
SHA-256 GitHub lists for it, and once you say yes puts `lid-offlinefixes.dll`
in the game's `Binaries\Win64\lid-plugins` - with the [LiD Plugin
Loader](https://github.com/NicoMullin/LiD-Plugin-Loader) too, if you do not
have it yet.

**By hand.**

1. Install the [LiD Plugin Loader](https://github.com/NicoMullin/LiD-Plugin-Loader)
   once, if you have not already. It puts a `dinput8.dll` and a `lid-plugins`
   folder in the game's `Binaries\Win64` folder.
2. Download `LiD-Offline-Fixes.zip` from the
   [latest release](https://github.com/NicoMullin/LiD-Offline-Fixes/releases/latest).
   Put `lid-plugins\lid-offlinefixes.dll` from it in the game's `Binaries\Win64\lid-plugins`.

The release also has `lid-offlinefixes.dll` on its own, for anyone who only wants the plugin.

It works alongside other plugins, such as LiD Death Metals, LiD Timers, LiD
Login Bonus and Fighter Class Passives: they change different parts of the
game.

## Building it yourself

You need Windows and Python 3. The compiler is Zig, installed with pip; no
Visual Studio needed.

```
py -3 -m pip install ziglang==0.16.0
py -3 build.py --test
```

The DLL lands in `out\lid-offlinefixes.dll` and the SHA-256 is printed at the
end. Use Zig 0.16.0 exactly: another version builds a working DLL, but not a
byte-identical one.

`--test` builds stand-ins for the game exe whose floor-event handlers log
their messages through the same instruction shapes the game uses (each
message to the normal log, then to the fatal one; two messages sharing one
fatal call; one message with two), plus three inventory handlers. It checks
that:

- with the plugin, only the three inventory handlers still reach the fatal
  log, every handler still logs to the normal log and still answers no;
- with one floor event's fatal call missing, as after a game update, nothing
  is changed and the log says why;
- with none of the game code, nothing is changed and the log says so.

`--exe PATH\BrgGame-Steam.exe` also runs `tests/realexe.c`: the plugin's own
search on the real game file (mapped, not run), which must find all 19 calls.
The game is not in this repository, so GitHub does not run it.

## Making a release

1. Set the new version in [dll/version.h](dll/version.h).
2. Push a tag with the same version, for example `v0.1.0`.

GitHub builds, tests and publishes the release. The build stops if the tag and
`version.h` don't match.

## License

MIT - see [LICENSE](LICENSE). Made by KSFA.
