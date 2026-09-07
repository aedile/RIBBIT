# RIBBIT

**Konami's 1981 Frogger, emulated on an ESP32-C6 Fiesta medal.** Two Z80s, an
AY-3-8910, a pair of 8255 PPIs, and the Galaxian video system — running at the
cabinet's 60.6 Hz.

A San Antonio Fiesta medal is a collectible pin. This one plays Frogger.

---

## 🎮 Quick Start Guide

### How to Play

You hold the medal upright, like a phone. This is the first game in the set
whose monitor was **already vertical**, so the picture fills the whole panel
with no bars.

| Control | What it does |
|---|---|
| **Tilt left / right** | Hop left / right |
| **Tilt away / toward you** | Hop forward / back |
| **Middle button** | Hop forward |
| **Power button, short press** | Insert a coin and start |
| **Power button, hold 1 second** | Power off |

Frogger is a strict four-way game — one hop per push, and the stick has to come
back to centre before the next one counts. Tilt suits that, because letting go
returns it to centre for you. Only the **dominant axis** is reported, so a
diagonal tilt never fires two directions at once.

Tilt is measured against however you are holding it right now. Coin up to
re-centre.

### Charging

USB-C. Holding the power button for a second cuts the battery rail.

### Troubleshooting

**The frog hops the wrong way.** Each axis is one sign in `main/input.cpp`
(`X_SIGN`, `Y_SIGN`). Flip the one that is wrong.

**It hops twice when I meant once.** Return the medal closer to level between
hops; the threshold is `HOP_DEG`.

---

## 🔨 Building Your Own

```sh
git clone https://github.com/aedile/RIBBIT.git
cd RIBBIT
python3 tools/convert_roms.py /path/to/frogger
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker build
cd build_docker && esptool --chip esp32c6 -p /dev/cu.usbmodemXXXX \
    -b 460800 write_flash @flash_args
```

Flashing has to run **from inside `build_docker`** and **from the host** —
Docker Desktop on macOS cannot reach USB.

### The ROMs

Not included. You need MAME's `frogger` set: `frogger.26`, `frogger.27` and
`frsm3.7` (main program), `frogger.608`–`610` (sound), `frogger.607` and
`frogger.606` (graphics), and `pr-91.6l` (colour PROM).

**Two of them are scrambled.** The first sound ROM and the second graphics ROM
come off the board with data lines D0 and D1 swapped. The converter undoes that,
so the emulator never has to think about it.

---

## 🔬 Technical Details

### The original hardware

Galaxian-derived: a Z80 at 3.072 MHz for the game, a second Z80 at 1.79 MHz that
does nothing but sound, one AY-3-8910, and two 8255 PPIs — one carrying the
inputs in, the other carrying the sound latch out.

The monitor is rotated 90°, giving a 224×256 portrait picture.

### The scroll that makes the game

The video is a 32×32 tilemap of 8×8 characters with eight 16×16 sprites over it,
and behind everything a flat blue field covering half the screen. That blue is
the river; the tilemap's pen 0 is transparent, which is how it shows through.

The hardware provides **per-column vertical scroll** — 32 independent scroll
values, one per tile column. Rotate the monitor and that becomes the sideways
drift of the traffic lanes and the logs. It is the whole game, implemented as a
scroll register.

Frogger, uniquely, swaps the nibbles of every scroll value and every sprite Y on
the way into the adder, and permutes the three colour bits on the way to the
PROM. None of that is documented anywhere except in MAME's source.

### Making it fast

Leaning into the rotation is what makes the renderer quick. A display row is a
fixed raw x, so it sits entirely inside **one tilemap column** — the column's
scroll and colour are fetched once per row, and the writes run straight down the
frame buffer.

Two further changes took it from 36 frames a second to 55:

**The character ROM is expanded once at init.** It is two bit planes that have
to be picked apart a bit at a time, but every pixel in a display row comes from
the same bit position — so the whole ROM becomes ready-made pixel values indexed
by tile, bit and row, and the inner loop is a single load.

**The main program's countdown at 0x036B is run as arithmetic.** It is
`LD A,H / OR L / DEC HL / JR NZ`, 26 T-states an iteration, and more than half
the main CPU's time went into it doing nothing observable. The same number of
cycles is charged, so this is **cycle-exact rather than a skip** — verified by
building the unoptimised core alongside it and confirming 25 of 25 frames come
out byte-identical across a scripted game.

### The interrupt that has to wait

The sound interrupt is raised where the main CPU writes the control latch, but
it is *not delivered* there. Both Z80s share one set of global memory callbacks
with a variable selecting the map; taking the interrupt at that moment would
push the sound CPU's return address through the **main** CPU's memory map. It is
held until the sound CPU is next scheduled with interrupts enabled.

### Audio

One AY-3-8910: three square-wave tone channels, a shared noise source, and one
envelope generator. `core/ay8910.c` was written for this project. Its port A is
the sound latch; port B is a counter chain divided down from the sound clock,
which Frogger reads with bits 3 and 5 swapped relative to every other Konami
board of the era.

---

## 📁 Project Structure

```
core/           platform-independent emulation, shared with the host harness
  frogger.c       two Z80s, the PPIs, the sound handshake, timing
  frogger_video.c tilemap, sprites, palette, rotation
  ay8910.c        AY-3-8910
  z80/            Marat Fayzullin's Z80 (non-commercial — see notices)
main/           the ESP32 application
components/     display, IMU and audio HAL for the Waveshare board
host/           builds the same core on a desktop; frames to PPM, audio to WAV
tools/          ROM converter, including the D0/D1 descramble
```

## 💻 Running It on Your Computer

```sh
cd host && make
./harness /tmp/out 20 --every 1 --wav /tmp/fr.wav \
    --script "2.0:coin=1,2.2:coin=0,3.0:start=1,3.2:start=0,6.0:up=1,6.2:up=0"
```

Script keys: `coin`, `start`, `up`, `down`, `left`, `right`. The
`FR_NO_DELAY_OPT` define builds the core without the delay-loop shortcut, which
is how that optimisation was proved harmless.

## ⚙️ Configuration

| What | Where |
|---|---|
| DIP switches | `fr_set_dips()` in `main/main.cpp` — lives, coinage, cabinet |
| Tilt direction | `X_SIGN`, `Y_SIGN` in `main/input.cpp` |
| Hop threshold | `HOP_DEG` in `main/input.cpp` |
| Button behaviour | `in->up` in `main/input.cpp` — set it to 0 to make the button dead |

## 📌 Status and Known Gaps

The game runs at full speed — 60.6 Hz, correct timing — with sound.

- **The panel refreshes at about 55 of those 60.6 frames**, not all of them. The
  SPI transfer alone is ~15 ms a frame and the main loop is already 90% busy.
  The game logic and its timing are correct; roughly one frame in ten is not
  drawn.
- Cocktail flip-screen is not implemented.
- The analogue output filters the sound board switches between are not modelled.

## 📄 Legal Notice

### ROM files

No ROMs here. Frogger is © 1981 Konami / Sega. This project ships a converter,
not a game.

### Third-party code

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The machine model and
video are written from MAME (BSD-3-Clause). **`core/z80/` is Marat Fayzullin's
Z80 emulator, which its author licenses for non-commercial use only** — that
term applies to this repository as a whole in any commercial context.

### Disclaimer

Not affiliated with, endorsed by, or connected to Konami, Sega, their
successors, or the Fiesta San Antonio Commission.

## 🙏 Credits

The MAME team, whose `galaxian.cpp` documents every one of Frogger's nibble
swaps and bit permutations. Marat Fayzullin, for a Z80 that has been portable
for thirty years.

## 📜 License

[0BSD](LICENSE) for the project's own code — but see the note above about the
Z80 core, which is non-commercial.
