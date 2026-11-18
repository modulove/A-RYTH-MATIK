# Labor (Nude Blacksmith)

A light-hearted show piece: two blacksmiths hammer away on the display, one animation frame per
clock, and four of the outputs play their hammer blows.

![Labor on the A-RYTH-MATIK display](img/Labor_OLED.gif)

## What it does

- A 12-frame animation, "Nude Blacksmith - Hammering Sequence". The right smith runs 3 frames
  ahead of the left one.
- Every clock pulse advances one frame and sets the six outputs for that frame. The output
  pattern is fixed (see the table below); there is nothing to program.
- With no external clock it runs from its own clock: 30-600 BPM, 4 frames per beat, 225 BPM
  (15 frames per second) after the first start.
- Four looks: classic, mirror (right smith flipped), night (inverted display) and night mirror.

At power-up it shows the Modulove logo, the title and "Happy Labourday - Patch: Omri Cohen"
(about 6 s). The clock and the outputs already run during these screens.

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
- **Outputs:** gates, one frame long. Frame numbers are the left smith's frames 0-11:

| Output | High on frame | What happens |
|---|---|---|
| OUT1 | 1 | left smith hammers |
| OUT5 | 2 | left smith hammers |
| OUT4 | 10 | right smith hammers (its frame 1) |
| OUT2 | 11 | right smith hammers (its frame 2) |
| OUT3 | 0 and 6 | |
| OUT6 | all other frames (the inverse of OUT3) | |

The channel LEDs follow the outputs. The CLK LED mirrors the CLK input; with the internal clock
it lights on every beat (frames 0, 4 and 8).

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
  `DISABLE_BOOT_LOGO` (skips the logo, the text screens stay). Uncomment them at the top of the
  sketch or pass them as `-D` flags.
- The release builds pass `-DSSD1306_NO_SPLASH`: it leaves out the display library's unused
  splash logo (about 1.2 KB). Without it (for example in the Arduino IDE) the build still works,
  it is just bigger.
- Builds for LGT8F328P boards must stay below 29,696 bytes, see
  [AUTOMATED_BUILD.md](../../AUTOMATED_BUILD.md).

```
arduino-cli compile -b arduino:avr:nano --build-property "build.extra_flags=-DSSD1306_NO_SPLASH" --build-property upload.maximum_size=29696 ARYTHMATIK_Labor
```

## Credits

Modulove, made for Labour Day; the boot screen credits the patch to Omri Cohen. Uses
[libModulove](https://github.com/awonak/libModulove) by Adam Wonak.
