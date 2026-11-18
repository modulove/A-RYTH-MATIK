# Funghi

Three mushrooms dance over a slime mold: a Game of Life colony grows, dies back and is fed at
the bottom of the display, one generation per clock, and the outputs fire on what happens in it.

![Funghi on the A-RYTH-MATIK display](img/Funghi_OLED.gif)

## What it does

- A colony of cells on a 42 x 7 grid at the bottom of the display lives by the Game of Life
  rules: a live cell with 2 or 3 live neighbours survives, an empty cell with exactly 3 is born,
  all other cells die. The edges don't wrap around.
- Every clock pulse is one step: one generation of the colony and one frame of the 8-frame
  mushroom dance above it. The outputs fire 10 ms triggers on the step of the generation they
  react to (see the table below).
- Three parameters keep the colony going: ATTRACTION feeds cells into the centre, REPULSION sets
  a population cap, RANDOMNESS lets single cells appear while the colony is small.
- Each colony grows from a seed: 45 random cells near the centre. The same seed with the same
  parameters gives the same generations, so RST replays the colony. Holding the button starts a
  new colony from a new seed.
- A colony that dies out doesn't leave the module silent: after 16 generations in which the
  Life rules left no live cell (single cells from RANDOMNESS die right away and don't count),
  the start pattern comes back, as after RST. Its random numbers run on, so it doesn't simply
  repeat the colony that died; RST still replays everything the same way.
- With no external clock it runs from its own clock: 30-600 BPM, 4 steps per beat, 75 BPM
  (5 generations per second) after the first start.
- The generations are computed up to 4 steps ahead, so the triggers come right on the clock
  edge. The display shows the generation that is playing; when the clock is faster than the
  display, the display skips generations, the outputs don't. Only with a clock of more than
  roughly 100 steps per second can a step come before its generation is ready; such a step
  fires nothing.

There is no start screen: the colony starts right after power-up.

## Controls

| Action | Function |
|---|---|
| Turn (or press + turn) | change the selected parameter (TEMPO: turn fast for steps of 5) |
| Click | show the selected parameter for 2 s; click again while it is shown for the next one |
| Hold 0.7 s | new colony from a new seed (shows "NEW COLONY") |

Parameters, in the order the clicks go through them:

| Parameter | Range | First start | What it does |
|---|---|---|---|
| TEMPO | 30-600 BPM | 75 BPM | internal clock, 4 steps per beat (selectable while no external clock runs) |
| DIVIDER | /1, /2, /4, /8 | /1 | clock edges per step (takes TEMPO's place while an external clock runs) |
| ATTRACTION | 0-10 | 7 | every 8 generations, cells are set at random places in the middle 10 columns of the middle row. At 7: 6 for an empty colony, fewer the fuller it is (2 just below the cap); other values scale this by ATTRACTION / 7 (up to 9 at 10). Nothing at or above the cap; 0 = off (the colony then soon dies out and starts again from its start pattern) |
| REPULSION | 0-10 | 3 | population cap: 120, 105, 90, 75 at 0-3, then 8 less per step (19 at 10). While the colony is at or above the cap, no cells are born |
| RANDOMNESS | 0-10 | 6 | every second generation while fewer than 45 cells live (and fewer than the cap), a cell may appear at a random place. At 6 the chance is 100 % for an empty colony and 2 % less per live cell (12 % at 44 cells); other values scale it by RANDOMNESS / 6; 0 = off |

The info box shows the parameter and its value. Below it: for TEMPO "INT CLOCK", for DIVIDER
"EXT CLOCK" and the incoming tempo; for the other three the number of live cells and the cap.
A change counts from the next step on (the generations computed ahead are computed again).

The info box also pops up when a clock is patched in or goes away; it then shows TEMPO or
DIVIDER, which is selected from then on.

## Inputs and outputs

- **CLK:** one step per rising edge (or per 2, 4 or 8 edges with the divider). The info box
  shows the incoming tempo with the clock counted as 16th notes. When no clock edge has come for
  2 s, the internal clock takes over.
- **RST:** back to the start colony of the current seed (it fires no triggers); the internal
  clock restarts too. A clock edge up to 5 ms after the reset counts as the same downbeat, so
  reset and clock together give the start colony and the next clock generation 1. As long as
  the parameters stay the same, the colony goes through the same generations again (restarts
  after a dead colony included).
- **Outputs:** 10 ms triggers on the step of the generation. Births, deaths and live cells are
  those of the Life rules; cells added by ATTRACTION and RANDOMNESS count from the next
  generation on. The step on which the start pattern comes back fires nothing.

| Output | Fires when |
|---|---|
| OUT1 | 7 or more cells died |
| OUT2 | 9 or more cells were born |
| OUT3 | stable: as many cells were born as died (also when nothing changed), and cells are alive |
| OUT4 | growth: more births than deaths, and fewer live cells than the cap |
| OUT5 | decline: more deaths than births |
| OUT6 | more live cells than half the cap (38 or more at REPULSION 3) |

A trigger that fires again while it is still high stays high and ends 10 ms after the last
step. The channel LEDs follow the outputs. The CLK LED mirrors the CLK input; with the internal
clock it lights on every beat of the tempo (every 4 steps).

## Settings

Tempo, divider, ATTRACTION, REPULSION, RANDOMNESS and the seed are saved 3 s after the last
change (as soon as no trigger is high, at the latest 2 s later) and come back at power-up, so
the module starts with the same colony again. The colony itself is not saved.

## Flashing

The easiest way is the [A-RYTH-MATIK firmware uploader](https://dl.modulove.io/arythmatik/) in
Chrome. The same file runs on an Arduino Nano and on LGT8F328P Nano boards; on an LGT8F328P
board leave "Use new bootloader" unchecked. Each release also has a `reversed` file for modules
whose encoder turns the wrong way round.

Building it yourself (Arduino IDE or arduino-cli):

- Board "Arduino Nano"; for LGT8F328P boards choose the processor "ATmega328P (Old Bootloader)".
- libModulove is a git submodule in `src/libmodulove`: clone the repository with
  `git clone --recursive` (or run `git submodule update --init`).
- Libraries: FastGPIO (Pololu), EncoderButton, Adafruit GFX Library, Adafruit SSD1306.
- Options: `ENCODER_REVERSED` (reverses the encoder), `ROTATE_PANEL` (upside-down panel).
  Uncomment them at the top of the sketch or pass them as `-D` flags.
- The release builds pass `-DSSD1306_NO_SPLASH`: it leaves out the display library's unused
  splash logo (about 1.2 KB). Without it (for example in the Arduino IDE) the build still works,
  it is just bigger.
- Builds for LGT8F328P boards must stay below 29,696 bytes, see
  [AUTOMATED_BUILD.md](../../AUTOMATED_BUILD.md).
- The animation frames are kept as plain bitmaps in `frames_src.h`; the sketch uses `frames.h`,
  which is generated from it with `../tools/oled_rle.py` (the command is at the top of
  `frames_src.h`).

```
arduino-cli compile -b arduino:avr:nano --build-property "build.extra_flags=-DSSD1306_NO_SPLASH" --build-property upload.maximum_size=29696 ARYTHMATIK_Funghi
```

## Credits

Modulove. Uses [libModulove](https://github.com/awonak/libModulove) by Adam Wonak.
