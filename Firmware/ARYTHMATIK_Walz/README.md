# Walz (Waltz Couple)

A light-hearted show piece: a couple dances a waltz on the display, one animation frame per
clock, and the outputs count the 3/4 time.

![Walz on the A-RYTH-MATIK display](img/Walz_OLED.gif)

## What it does

- A 13-frame animation, "Waltz Couple - Dancing Animation".
- Every clock pulse is one step: it advances the animation by one frame and the 3/4 beat by
  one (1, 2, 3, 1, ...) and sets the six outputs. The beat count runs on its own, so the 3/4
  time goes on unbroken across the end of the 13-frame loop. The output pattern is fixed (see
  the table below); there is nothing to program.
- With no external clock it runs from its own clock: 30-600 BPM, 4 steps per beat, 75 BPM
  (5 steps per second) after the first start.
- Four looks: classic, mirror (couple flipped left-right), night (inverted display) and night
  mirror.

At power-up it shows the title "Waltz Couple - Dancing Animation" (about 2 s). The clock and the
outputs already run during this screen.

## Controls

| Action | Function |
|---|---|
| Turn (or press + turn) | no external clock: tempo 30-600 BPM (turn fast for steps of 5); with an external clock: clock divider /1, /2, /4, /8 |
| Click | info box for 1.5 s: clock source, tempo or incoming BPM, divider, look, and what turning does |
| Hold 0.7 s | next look: classic, mirror, night, night mirror |

The info box also pops up when a clock is patched in or goes away.

## Inputs and outputs

- **CLK:** one step per rising edge (or per 2, 4 or 8 edges with the divider). The info box
  shows the incoming tempo with the clock counted as 16th notes. When no clock edge has come for
  2 s, the internal clock takes over.
- **RST:** jumps to frame 0 and beat 1 and sets their outputs at once; the internal clock
  restarts too. A clock edge up to 5 ms after the reset counts as the same downbeat, so reset and
  clock together give frame 0 and the next clock frame 1.
- **Outputs:** gates, one step long unless stated:

| Output | High on | What happens |
|---|---|---|
| OUT1 | beat 1 | |
| OUT2 | beat 2 | |
| OUT3 | beat 3 | |
| OUT4 | frame 0 of the animation | once per 13-step dance loop |
| OUT5 | beats 2 and 3 (one gate, two steps long) | the inverse of OUT6 |
| OUT6 | beat 1 (the same as OUT1) | |

OUT4 runs 13 steps against the 3 of the waltz: it falls on beat 1, then beat 2, then beat 3,
and is back on beat 1 after 39 steps.

The channel LEDs follow the outputs. The CLK LED mirrors the CLK input; with the internal clock
it lights on beat 1 of the waltz (every 3 steps).

## Settings

Tempo, divider and look are saved 3 s after the last change and come back at power-up.

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
arduino-cli compile -b arduino:avr:nano --build-property "build.extra_flags=-DSSD1306_NO_SPLASH" --build-property upload.maximum_size=29696 ARYTHMATIK_Walz
```

## Credits

Modulove. Uses [libModulove](https://github.com/awonak/libModulove) by Adam Wonak.
