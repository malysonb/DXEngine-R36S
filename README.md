# DXEngine

Deus Ex's engine for modern systems: a hard fork of
[Surreal Engine](https://github.com/dpjudas/SurrealEngine), the open-source reimplementation of
Unreal Engine 1, made to play Deus Ex as the original does, on Linux and Windows, from the
desktop to a handheld.

<p align="center">
  <img src="docs/screenshots/liberty-island.jpg" width="100%"
       alt="Liberty Island at night: the statue and the city across the water, from the pier">
</p>

**You need your own copy of Deus Ex, version 1112fm.** A 1112fm copy whose `DeusEx.exe` another
launcher or a patch replaced plays too, with a warning. None of the game's files are included.
Made with AI assistance.

> **This is the R36S fork of DXEngine**, tuned for the Anbernic R36S (aarch64, Mali-G31,
> 640×480, dArkOS). The engine, its scripts and its renderers are DXEngine's: every R36S
> setting keeps upstream's default when the port's launcher leaves it out, so a desktop build
> behaves as DXEngine does. [What the fork changes](#the-r36s-fork).

## Features

- **Deus Ex as the original plays it.** The game's own scripts run on the engine: its
  conversations and cinematics, its characters' AI, saving, loading and travel between maps,
  multiplayer. Each feature is checked against the original by scripted runs of both engines,
  and [`docs/NATIVES.md`](docs/NATIVES.md) says what differs.
- **Modern renderers.** Vulkan by default, OpenGL, and OpenGL ES 3.1 for handhelds, with
  multisampling and a render scale for slower GPUs; on the CPU alone, on every core, where the GPU
  has no driver.
- **Gamepads.** Through SDL3 or SDL2, mapped to the game's own joystick bindings in `User.ini`,
  its menus included.
- **Linux and Windows.** Linux on x86_64 and aarch64, glibc 2.28 or newer, so any current
  distribution; Windows 10 (1903) or newer on x86_64.
- **From the desktop to the handheld.** The game plays on a PC, on the TrimUI Smart Pro (an
  aarch64 handheld with a PowerVR GPU) and on the Anbernic R36S (aarch64, Mali-G31), which this
  fork is tuned for.
- **Made for a launcher.**
  [DXLauncher](https://github.com/JuggyMcNutty/deusex-launcher/blob/linux-x86_64/README.md)
  keeps the player's settings and saves in profiles and starts the engine; the engine runs on
  its own too, or as a dedicated server.

## The R36S fork

This repository is a fork of [DXEngine](https://github.com/JuggyMcNutty/DXEngine) that targets
the **Anbernic R36S**: an aarch64 handheld (Rockchip RK3326, Mali-G31 with the vendor's OpenGL
ES 3.2 driver, a 640×480 panel) running dArkOS. It is the same engine — the game's scripts, the
Vulkan/OpenGL/software renderers — with a performance profile for that hardware, the diagnostics
to measure it, and a cross build. Nothing here changes a desktop build: each setting keeps
DXEngine's default unless the port's launcher turns it on.

The fork's work:

- **Settings for slow hardware** (`LauncherSettings::Performance`, read from `Settings.json`'s
  `"Performance"` block and overridden by `DXE_R36S_*` environment variables): the render
  scale, vsync, the scene's detail bands (0 to 3), trilinear filtering and anisotropy, an RGBA8
  scene buffer, and the scene's streaming vertex/index buffer sizes with optional orphaning.
- **Light maps on a small GPU**: `FreezeAnimatedLights` bakes flickering and pulsing lights once
  — the per-frame light-map rebuild cost 20-40 ms in some areas — while the `bDynamicLight`
  ones (the flashlight, muzzle flashes, explosions) still light their surfaces. `NoDynamicLights`
  is the game's own, harsher option, and leaves those out too (which turns the flashlight off);
  `PrecacheTextures` and `PrecacheLightmaps` fill the texture and light-map caches during the
  load instead of on each area's first sight; `AtlasPartialUpload` uploads only the rectangle a
  new light map wrote, not the whole atlas page.
- **Larger text on the small panel**: the conversation, choice, HUD log and mission-start text
  is drawn at 1.5× (help, message boxes, goals, logs and inventory descriptions at 1.25×), with
  measurement and drawing sharing the scale and a dark drop shadow. Since the game sizes the
  conversation letterbox from the screen alone, the lower window grows upward to what the speech
  and the choices ask for, so a long line or a long list of replies stays on the 640×480 screen.
- **Measuring it**: `DXE_R36S_LOG_SLOW_MS` logs every frame over a threshold with its stages
  (`tick`, `game`, `draw`, `visibility`, `mesh`, `lights`, `present`), the draw and asset
  breakdown (`surfaces`, `upload`, `lightmaps`, `textureDriver`) and the streaming counters
  (`streamFlushes`, `streamKiB`). The first frame of each map also says what the engine applied,
  as `R36S setting: ...`, so a launcher that forgot a variable cannot pass for a knob that did
  nothing.

The launcher and the `r36s.ini` profile are distributed with the port: it raises the CPU and
GPU frequency ceilings and sets the `performance` governor for the session, restoring them on
exit, exports the `DXE_R36S_*` variables, and passes the profile's stick dead zone through
`DXE_R36S_DEADZONE`. In a conversation the pad's d-pad walks the replies and either face button
presses the one highlighted, as the keyboard's arrows and Enter do. The port's
`PERFORMANCE.md` (Portuguese) records what each performance knob measured on the device and its
trade-offs.

The fork's changes are kept to the files it touches, so the engine can still be merged from
DXEngine as it moves.

## Screenshots

<table>
  <tr>
    <td width="50%"><img src="docs/screenshots/tonnochi-road.jpg" width="100%"
        alt="Hong Kong's Tonnochi Road at night under neon signs, the game's HUD on screen"></td>
    <td width="50%"><img src="docs/screenshots/intro.jpg" width="100%"
        alt="The intro: two men talk in a red-walled room, a subtitle beneath them"></td>
  </tr>
  <tr>
    <td align="center">Hong Kong's Tonnochi Road, the HUD on</td>
    <td align="center">The intro's cinematic</td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/screenshots/conversation.jpg" width="100%"
        alt="A conversation in Paris: two men face each other, the choices below"></td>
    <td width="50%"><img src="docs/screenshots/market.jpg" width="100%"
        alt="Hong Kong's market: cloth banners under a green roof, a sign to Canal Road"></td>
  </tr>
  <tr>
    <td align="center">A conversation and its choices</td>
    <td align="center">Hong Kong's market</td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/screenshots/helibase.jpg" width="100%"
        alt="A helicopter on its pad in a hangar lit red and white"></td>
    <td width="50%"><img src="docs/screenshots/ocean-lab.jpg" width="100%"
        alt="A yellow mini-submarine in a flooded bay under a steel roof"></td>
  </tr>
  <tr>
    <td align="center">The MJ12 helibase</td>
    <td align="center">The Ocean Lab's submarine bay</td>
  </tr>
</table>

Each is the engine's own shot of the game, on Vulkan with 4× multisampling.

## Download

To play, take your platform's app from
[Port Ex Machina's releases](https://github.com/JuggyMcNutty/port-ex-machina/releases):
DXLauncher and the engine together. This repository's
[releases](https://github.com/JuggyMcNutty/DXEngine/releases) have the engine alone, for each
platform. GitHub's CI builds both, on a pull request here and when Port Ex Machina's pins take a
commit ([CI](https://github.com/JuggyMcNutty/port-ex-machina/blob/main/docs/DEVELOPMENT.md#ci)).

This fork does not use those releases: its R36S build is distributed with the port, beside the
device's launcher and `r36s.ini`.

## Building and running

Linux builds are made and tested on Linux, and Windows' cross-built by llvm-mingw and tested
under Proton's wine.

### The easy way: Port Ex Machina

[Port Ex Machina](https://github.com/JuggyMcNutty/port-ex-machina) fetches the engine, builds it
with the launcher for each of its ports, and stages, runs or deploys the result: follow
[its quick start](https://github.com/JuggyMcNutty/port-ex-machina/blob/main/README.md#quick-start).

### Building it on its own

You need:

- a C++20 compiler (GCC 10 or later), CMake 3.15 or later and pkg-config;
- the development files of OpenAL, SDL3 or SDL2, and X11 (with Xi and Xext); with
  `-DDXE_BUNDLE_OPENAL=ON`, PipeWire's, PulseAudio's and ALSA's in OpenAL's place: OpenAL Soft
  is then built from `Thirdparty/openal-soft` and put beside the engine as `libopenal.so.1`,
  as the Linux ports ship it;
- optionally Wayland's (waylandpp, xkbcommon), EGL, dbus, GLib and fontconfig, used when found.

On Arch these are among the packages
[Port Ex Machina's dependencies](https://github.com/JuggyMcNutty/port-ex-machina/blob/main/docs/DEVELOPMENT.md#dependencies)
list. Then:

```sh
git clone https://github.com/JuggyMcNutty/DXEngine.git
cd DXEngine
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target DXEngine -j"$(nproc)"
```

Its SDL window is SDL3's when SDL3 is found, else SDL2's: either has the engine's gamepad
support, which its X11 and Wayland windows lack, and is the window when `DXE_DISPLAY_BACKEND`
names it ([the gamepad](docs/ENGINE.md#running-on-our-devices)). The build leaves two files
in `build/` that go together:

- `DXEngine`, the engine;
- `DXEngine.pk3`, its resources (the UI's fonts among them), found beside the executable.

### Running it

Give it the game's folder, or start it from the game's `System` folder:

```sh
build/DXEngine /path/to/deusex
```

It draws with Vulkan unless its settings name another device or the game's ini names `SoftDrv`,
and on the CPU, with its software device, where no GPU device starts
([which device draws](docs/ENGINE.md#rendering)). Where to go next:

- [Running it](docs/ENGINE.md#running-it): the rest of the command line -- the log on
  stderr, a map to start on, a dedicated server, the inis.
- [Settings and environment](docs/ENGINE.md#settings-and-environment): the settings file
  and the variables the engine reads, the window backend a pad needs among them.

### For advanced users

The build's own switches:

| Switch | Default | What it does |
|---|---|---|
| `ENABLE_SDL2` | `ON`; on Windows `OFF` | The SDL2 window backend, built when SDL3's is not. |
| `ENABLE_SDL3` | `ON`; on Windows `OFF` | The SDL3 backend: when SDL3 is found, it is built in place of SDL2's. |
| `ENABLE_X11`, `ENABLE_WAYLAND` | `ON` | The desktop's window backends; off for a device with neither. |
| `DXE_SDL2_BORDERLESS_FULLSCREEN` | `OFF` | SDL2's fullscreen as a borderless window, for a vendor SDL2 that deadlocks in `SDL_SetWindowFullscreen`. |
| `ZIPDIR_EXECUTABLE` | unset | A host-built `zipdir` to pack `DXEngine.pk3` with when cross-compiling. |

- **A port's build** is an initial cache (`cmake -C`), `ports/<id>/engine.cmake` on the port's
  launcher branch: the desktop's toolchain and switches
  ([linux-x86_64's](https://github.com/JuggyMcNutty/deusex-launcher/blob/linux-x86_64/ports/linux-x86_64/engine.cmake)),
  or a cross build's
  ([trimui-smartpro's](https://github.com/JuggyMcNutty/deusex-launcher/blob/trimui-smartpro/ports/trimui-smartpro/engine.cmake)).
  Port Ex Machina's `scripts/engine.sh build <port>` builds with it
  ([a port's files](https://github.com/JuggyMcNutty/port-ex-machina/blob/main/docs/PORTING.md#what-a-port-is)).
- **Debugging**: a build with symbols, or with AddressSanitizer
  ([crashes](docs/DEVELOPMENT.md#crashes)).
- **Profiling and validation**: [measuring it](docs/ENGINE.md#measuring-it).
- **Kept, but neither built nor tested here**: MSVC's own, the natvis and the crash reporter's
  Windows part ([Windows](docs/ENGINE.md#running-on-our-devices)).
- **The D3D11 device** is built for Windows alone, drawing in its SDL3 or Win32 window, and
  offered by no launcher.
- **The software device** (`Settings.json`'s `"Type": "Software"`) is built on every platform,
  drawing on the CPU into its window's bitmap on every core: DXLauncher's Software renderer, and
  the device that draws where no GPU device starts
  ([the software device](docs/ENGINE.md#rendering)).

### Building for the R36S

The R36S build is aarch64. It can be made on the device itself (Debian 13/dArkOS, GCC, SDL2 and
OpenAL) or cross-compiled from x86_64 with `aarch64-linux-gnu-g++` and an aarch64 sysroot that
carries the device's glibc, SDL2 and OpenAL. The port's tree distributes what that needs:
`build.sh`, `cmake/aarch64-linux-gnu.cmake` and `scripts/make-sysroot.sh`, which takes the
sysroot from a running aarch64 container so the cross build asks the handheld for no symbol its
glibc lacks. The result is `DXEngine` beside `DXEngine.pk3`, with `RenderScale=1.0` (the native
640×480) by default.

## Status

The engine plays the game, which is not yet played through end to end:
[`docs/NATIVES.md`](docs/NATIVES.md) lists where it differs from the original. Where each
platform stands:
[the ports](https://github.com/JuggyMcNutty/port-ex-machina/blob/main/README.md#engine-ports).
A bug in the game goes to
[this repository's issues](https://github.com/JuggyMcNutty/DXEngine/issues)
([what to say](https://github.com/JuggyMcNutty/port-ex-machina/blob/main/README.md#reporting-bugs)).

## Documentation

| Doc | Read it for |
|---|---|
| [`docs/ENGINE.md`](docs/ENGINE.md) | how the engine is kept, run and profiled, and [what it changes from Surreal Engine](docs/ENGINE.md#what-it-changes-from-surreal-engine) |
| [`docs/NATIVES.md`](docs/NATIVES.md) | where it differs from the original, feature by feature |
| [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) | working on it: scripted runs of both engines, debugging a crash, proving a change |
| [`docs/PackageFormat.md`](docs/PackageFormat.md) | the package files' format, as the engine reads and writes them |

## DXEngine and Surreal Engine

DXEngine began as Surreal Engine, by Magnus Norddahl, Lupert Everett and its contributors. It
is a hard fork: not in Surreal Engine's fork network, nothing is merged from it or sent to it,
the two different at the core ([how it is kept](docs/ENGINE.md#how-it-is-kept)). It plays Deus Ex
alone; other Unreal Engine 1 games do not start.

## License

zlib, as Surreal Engine is: DXEngine's licence, Surreal Engine's and those of the third-party
code it carries are in [`LICENSE.md`](LICENSE.md). Deus Ex belongs to its owners; this project is
not affiliated with or endorsed by them.
