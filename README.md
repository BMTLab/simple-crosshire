# crosshair

A crosshair in the centre of the primary monitor, above every window,
fullscreen games under Wine and Proton included.
Mouse clicks pass through it.

It is a tiny X11 client for x86-64 Linux,
so on a Wayland session it lives in XWayland, beside the games Proton runs there.
It uses no library at all, not even the C library:
it speaks the X11 protocol over the socket itself
and makes its own system calls.

## Use

```bash
crosshair
```

Without `-f` the command toggles:
it starts a crosshair in the background, or stops the one that runs.
Bound to a key in the desktop's shortcuts, it shows and hides the cross in a game.

| Option | Default | Meaning |
|---|---|---|
| `-f`, `--foreground` | off | stay attached; Ctrl+C, SIGTERM or the parent's exit stops it |
| `-s`, `--size SIZE` | 9 | side of the cross in pixels, 1 to 512 |
| `-t`, `--thickness N` | 3 | width of both bars in pixels, at most the size |
| `-c`, `--color RRGGBB` | `ff0000` | colour, with or without a leading `#` |
| `-h`, `--help` | | the usage |

### With a game

A game launcher starts `crosshair --foreground` as its own child right before the game,
and stops it when the game exits.
The crosshair asks the kernel to send it SIGTERM when its parent dies,
so even a launcher killed with SIGKILL takes it along.
ScriptsLib's `killing-floor-launcher` does this by default
(`KF_CROSSHAIR`, `KF_CROSSHAIR_OPTIONS`).

## How it works

- The window is override-redirect,
  so no window manager places it or keeps it under a fullscreen window.
- The SHAPE extension cuts the window down to the cross,
  and an empty input shape lets every click through to the window below.
  The X server paints the cross from the window's background colour;
  the program never draws anything.
- RANDR 1.5 gives the primary monitor, whose centre the cross takes.
- Then the program sleeps in `poll()` on the X connection and a `signalfd`.
  It wakes only when another window covers the cross (it raises itself),
  when the monitors change (it centres itself again), or when told to stop.
  It has no timers, so a running game never wakes it.

## Why no library

The first C version used Xlib, which costs a lot before the first window:
`XOpenDisplay` alone makes several round trips (BIG-REQUESTS, XKB, the resource database),
and the start ran some 600 000 instructions.
XCB is leaner, but it still allocates every reply, locks a mutex for every request,
and needs the C library, whose static start-up resolves its IFUNCs
and registers the unwind tables of every function in the binary.

Without any of that:

- `src/kernel.h` is the whole runtime: `_start`, the system calls,
  and `memcpy`/`memset` for the compiler.
- `src/x11.h` sends the connection setup and both extension queries in one write,
  the window, its names, its shapes and the RANDR queries in a second,
  and the move and the map in a third: two round trips in all.
  The authorisation cookie comes from the Xauthority file, chosen as libXau chooses it.
- The program keeps no global variables,
  so the binary has a single segment, code and constants, with nothing to relocate.
- Every division is by a constant, which the compiler turns into multiplications and shifts:
  the binary holds no division instruction.

## Build

Needs clang and lld (LLVM 24 by default; `make LLVM=/path/to/llvm/bin` for another),
the kernel headers (`linux-libc-dev`), and an X display for the training.

```bash
make
```

```bash
make install
```

`make` builds with profile-guided optimisation in two stages:
an instrumented build shows the crosshair a few hundred times (`train.sh`),
a second one counts again by calling context,
and the release build uses both profiles.
The cross flashes in the screen centre meanwhile.
`make plain` builds the same flags without profiles and needs no display.
`make install` copies `./crosshair` to `~/.local/bin` (`BINDIR=` for another place).

## Measurements

On 2026-10-17, i7-12700H, Kubuntu 26.04 with KDE Plasma on Wayland, XWayland 24.1.
One start means `crosshair -f` from exec until its window is mapped
(the PID file names it), then SIGTERM and exit,
counted by `perf stat` at nice -20 on one P-core,
1000 rounds with every build in a fresh random order each round.
Every difference below is beyond the spread of two copies of the same binary.

| Per start | 1.2.0, Python and Qt | 2.0.0, Xlib (shared) | 2.0.0, Xlib (static) | 3.0.0 |
|---|---|---|---|---|
| CPU time | 53-58 ms | 0.896 ms | 0.559 ms | **0.315 ms** |
| Instructions in the program | | 556 654 | 605 997 | **14 797** |
| Page faults | | 143.4 | 54.2 | **3.7** |
| Exec to mapped window | | 1.434 ms | 1.141 ms | **0.872 ms** |
| Memory (RSS) | 100-145 MB | 3.0 MB | | **52 kB** |
| Threads | 3-4 | 1 | | 1 |
| Wakeups in 30 s idle | 13-90 | 0 | | 0 |

The Python CPU time and every memory figure come from /proc instead of perf.
The Python version also sat 36 px off centre under the Wayland platform plugin.

How the build was chosen (each step against the one before, same method):

| Step | Effect |
|---|---|
| clang 24 instead of GCC 16, both -O2 | 19 % fewer instructions, 12 % fewer cycles |
| -O3 instead of -O2 | a tie; -Os and -Oz lost |
| one segment (`--no-rosegment`, no writable data) | 20 % fewer page faults, 2-3.5 % fewer cycles |
| PGO | 17 % fewer cycles |
| context-sensitive PGO on top | 8.5 % fewer instructions |
| BOLT on top of either | fewer cycles, but a new 2 MB-aligned segment and 26 % more page faults: not used |
| ext-TSP block placement in the compiler | no change: not used |

What it costs a game: a full-screen OpenGL window under Proton on the RTX 3050,
started through the Killing Floor launcher with and without the crosshair,
six pairs unpaced and six at 60 FPS, showed no certain change
in frame time (2.06 ms unpaced, 16.5 ms paced),
Intel GPU load, package power or the CPU time of KWin and XWayland.
On this laptop without a MUX switch the compositor copies every frame
from the NVIDIA GPU anyway, and the cross adds 45 pixels to that.

## License

MIT, see [LICENSE.md](LICENSE.md).
