# Kiss (Kissing Couple)

A light-hearted show piece: two figures meet for a kiss on the display, one animation frame per
clock, and the outputs follow the kiss.

![Kiss on the A-RYTH-MATIK display](img/Kiss_OLED.gif)

## What it does

- A 13-frame animation, "Kissing Couple - Love Animation": the two meet, kiss and part again.
- Every clock pulse advances one frame and sets the six outputs for that frame. The output
  pattern is fixed (see the table below); there is nothing to program.
- With no external clock it runs from its own clock: 30-600 BPM, 4 frames per beat, 225 BPM
  (15 frames per second) after the first start.
- Four looks: classic, mirror (couple flipped left-right), night (inverted display) and night
  mirror.

At power-up it shows the Modulove logo and the title "Kissing Couple - Love Animation" (about
3 s). The clock and the outputs already run during these screens.

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
- **RST:** jumps to frame 0 and sets the frame-0 outputs at once; OUT3 and OUT6 go low and the
  loop count starts again (see below). The internal clock restarts too. A clock edge up to 5 ms
  after the reset counts as the same downbeat, so reset and clock together give frame 0 and the
  next clock frame 1.
- **Outputs:** gates, one frame long unless stated. Frame numbers 0-12:

| Output | High on frame | What happens |
|---|---|---|
| OUT2 | 0 and 1 (one gate, two frames long) | start of the loop |
| OUT5 | 0 and 1 (the same as OUT2) | |
| OUT4 | 4, 6 and 8 | the kiss: OUT4 and OUT1 take turns |
| OUT1 | 5, 7 and 9 | |
| OUT6 | from frame 9 of the 1st, 3rd, 5th ... loop through frame 8 of the next loop | flip-flop with OUT3 |
| OUT3 | from frame 9 of the 2nd, 4th ... loop through frame 8 of the next loop | |

OUT3 and OUT6 switch over on every frame 9 and hold, so each one is high for 13 frames and low
for 13: a square wave at half the speed of the animation loop. The loops are counted from
power-up or the last reset; until the first frame 9 after that, both are low.

The channel LEDs follow the outputs. The CLK LED mirrors the CLK input; with the internal clock
it lights on every beat (every 4 frames).

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
- Options: `ENCODER_REVERSED` (reverses the encoder), `ROTATE_PANEL` (upside-down panel),
  `DISABLE_BOOT_LOGO` (skips the logo, the text screen stays). Uncomment them at the top of the
  sketch or pass them as `-D` flags.
- The release builds pass `-DSSD1306_NO_SPLASH`: it leaves out the display library's unused
  splash logo (about 1.2 KB). Without it (for example in the Arduino IDE) the build still works,
  it is just bigger.
- Builds for LGT8F328P boards must stay below 29,696 bytes, see
  [AUTOMATED_BUILD.md](../../AUTOMATED_BUILD.md).
- The animation frames are kept as plain bitmaps in `frames_src.h`; the sketch uses `frames.h`,
  which is generated from it with `../tools/oled_rle.py` (the command is at the top of
  `frames_src.h`).

```
arduino-cli compile -b arduino:avr:nano --build-property "build.extra_flags=-DSSD1306_NO_SPLASH" --build-property upload.maximum_size=29696 ARYTHMATIK_Kiss
```

## Credits

Modulove. Uses [libModulove](https://github.com/awonak/libModulove) by Adam Wonak.
