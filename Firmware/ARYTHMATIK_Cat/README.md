# Cat (Muybridge)

A light-hearted show piece: a cat gallops across the display, one animation frame per clock, and
four of the outputs follow its paws.

![Cat on the A-RYTH-MATIK display](img/Cat_OLED.gif)

## What it does

- A 24-frame animation of a galloping cat; the boot screen credits Muybridge (1887).
- Every clock pulse advances one frame and sets the six outputs for that frame. The output
  pattern is fixed (see the tables below); there is nothing to program.
- With no external clock it runs from its own clock: 30-600 BPM, 4 frames per beat, 300 BPM
  (20 frames per second) after the first start.
- Four looks: classic, mirror (cat flipped left-right), night (inverted display) and night
  mirror.

At power-up it shows the title "Cat - Muybridge (1887)" (about 2 s). The clock and the outputs
already run during this screen.

## Controls

| Action | Function |
|---|---|
| Turn (or press + turn) | no external clock: tempo 30-600 BPM (turn fast for steps of 5); with an external clock: clock divider /1, /2, /4, /8 |
| Click | info box for 1.5 s: clock source, tempo or incoming BPM, divider, look, and what turning does |
| Hold 0.7 s | next look: classic, mirror, night, night mirror |

The info box also pops up when a clock is patched in or goes away.

## Inputs and outputs

- **CLK:** one frame per rising edge (or per 2, 4 or 8 edges with the divider). The info box
  shows the incoming tempo with the clock counted as 16th notes. When no clock edge has come for
  2 s, the internal clock takes over.
- **RST:** jumps to frame 0 and sets the frame-0 outputs at once; the internal clock restarts
  too. A clock edge up to 5 ms after the reset counts as the same downbeat, so reset and clock
  together give frame 0 and the next clock frame 1.
- **Outputs:** gates. The paw outputs OUT1, OUT2, OUT4 and OUT5 are set on the paw frames and
  stay as they are until the next paw frame:

| Paw frame | Paw outputs high (the others low) |
|---|---|
| 0, 6, 12, 18 | OUT1, OUT5 |
| 3, 9, 15, 21 | OUT2, OUT4 |
| 5, 11, 17, 23 | OUT1, OUT2 |
| 7, 13, 19 | OUT2, OUT5 |
| 10, 16, 22 | OUT4 |

That gives these gates over the 24 frames (0-23):

| Output | High on frame |
|---|---|
| OUT1 | 0-2, 5-6, 11-12, 17-18 and 23 |
| OUT2 | 3-5, 7-9, 11, 13-15, 17, 19-21 and 23 |
| OUT4 | 3-4, 9-10, 15-16 and 21-22 |
| OUT5 | 0-2, 6-8, 12-14 and 18-20 |
| OUT6 | 5, 11, 17 and 23 (one frame long) |
| OUT3 | all other frames (the inverse of OUT6) |

An output that is high on frames next to each other gives one long gate: OUT1 stays high from
frame 23 through frame 2 while the animation loops.

The channel LEDs follow the outputs. The CLK LED mirrors the CLK input; with the internal clock
it lights on every beat (frames 0, 4, 8, 12, 16 and 20).

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
arduino-cli compile -b arduino:avr:nano --build-property "build.extra_flags=-DSSD1306_NO_SPLASH" --build-property upload.maximum_size=29696 ARYTHMATIK_Cat
```

## Credits

Modulove; the boot screen credits the motion study to Muybridge (1887). Uses
[libModulove](https://github.com/awonak/libModulove) by Adam Wonak.
