# Buds

Two independent clock processors in one module: coin flip, toggle, swing and clock divider.

![Buds on the A-RYTH-MATIK display](img/Buds_OLED.gif)

## What it does

The module is split into two sides. Side A is clocked from the RST jack, side B from the CLK
jack. Each side runs one of four modes:

- **Buds** (probability): on every clock a coin flip decides. With the set probability the main
  output fires, otherwise the inverted output fires.
- **Toggle:** every clock flips the main output; the inverted output is always the opposite.
  Both stay where they are until the next clock (gates).
- **Swing:** every clock gives a trigger on the main output, and every second one is moved later
  or earlier by the swing amount. The delay is worked out from the measured clock period.
- **Factor:** clock divider 1-8. The main output fires on every Nth clock.

Each side also has a clock output that fires on every step. All triggers are 12 ms long and timed
by a hardware timer, so the two sides run at any tempo, independent of each other and of the
display.

The left half of the display belongs to side A, the right half to side B. The selected side's
mode name is highlighted.

## Controls

| Action | Function |
|---|---|
| Click | select side A or side B |
| Press + turn | next mode for the selected side: Buds, Toggle, Swing, Factor |
| Turn | change the selected side's value (see below) |
| Long press (0.4 s) | flip the toggle state of the selected side (for Toggle mode) |

What turning does in each mode:

| Mode | Turn |
|---|---|
| Buds | probability 0-100 % (2 % steps, turn faster for bigger steps); 50 % after the first start |
| Toggle | flips the toggle state by hand |
| Swing | swing amount 0-70 %; 50 % = straight (no swing) |
| Factor | divider 1-8 |

## Inputs and outputs

| | Side A | Side B |
|---|---|---|
| Clock input | RST | CLK |
| Main output | OUT2 | OUT5 |
| Inverted output | OUT1 | OUT4 |
| Clock output | OUT3 | OUT6 |

What the outputs do in each mode:

| Mode | Main output | Inverted output | Clock output |
|---|---|---|---|
| Buds | 12 ms trigger when the coin flip says yes | 12 ms trigger when it says no | 12 ms trigger on every clock |
| Toggle | gate, flips on every clock | the opposite of main | 12 ms trigger on every clock |
| Swing | 12 ms trigger on every (swung) step | stays low | 12 ms trigger on every (swung) step |
| Factor | 12 ms trigger on every Nth clock | stays low | 12 ms trigger on every clock |

Swing in detail: the clock period is measured between rising edges. The step on the beat plays on
the clock, the off-beat step is placed at 2 x swing x clock period after it: at 50 % it lands on
the next clock (straight), above 50 % later, below 50 % earlier. After a pause of 2 s or more the
period is measured again, and until then the steps play straight on the clock.

The channel LEDs show the outputs.

## Settings

The mode, probability, swing amount and divider of both sides are saved 3 s after the last
change and come back at power-up. The toggle state and the selected side are not saved.

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
- Options: `ENCODER_REVERSED` (reverses the encoder); uncomment it at the top of the sketch or
  pass it as a `-D` flag. The sketch also has `ROTATE_PANEL` (upside-down panel), but
  libModulove's rotated pin table puts OUT4 on the wrong pin (D1 instead of D5), so it needs a
  fix there first.
- The release builds pass `-DSSD1306_NO_SPLASH`: it leaves out the display library's unused
  splash logo (about 1.2 KB). Without it (for example in the Arduino IDE) the build still works,
  it is just bigger.
- Builds for LGT8F328P boards must stay below 29,696 bytes, see
  [AUTOMATED_BUILD.md](../../AUTOMATED_BUILD.md).

```
arduino-cli compile -b arduino:avr:nano --build-property "build.extra_flags=-DSSD1306_NO_SPLASH" --build-property upload.maximum_size=29696 ARYTHMATIK_Buds
```

## Credits

Modulove. Uses [libModulove](https://github.com/awonak/libModulove) by Adam Wonak.
