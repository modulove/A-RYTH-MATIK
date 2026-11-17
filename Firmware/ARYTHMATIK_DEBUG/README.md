# A-RYTH-MATIK hardware test (DEBUG firmware v2)

Checks a module step by step: board, power, display, inputs, outputs and the complete
output-to-input signal path. The same `.hex` runs on an Arduino Nano and on an LGT8F328P board.
Works with and without a working display: everything is also reported over USB.

## Flash it

Use the firmware uploader on the A-RYTH-MATIK page like any other firmware.
LGT8F328P boards: leave "Use new bootloader" unchecked.

## On the module

Turn the encoder for the next page, click for the page action.

| Page | Shows | Click |
|---|---|---|
| 1 SYSTEM | chip, CPU clock, supply voltage, free RAM, reset cause | EEPROM test (writes one spare byte, puts it back) |
| 2 I2C | idle voltage on SDA/SCL, pull-ups, devices on the bus, display frame time, timeouts | bus speed test: the same picture at 400 kHz, 800 kHz, 1 MHz |
| 3 INPUTS | CLK and RST level, count, BPM (16th notes), pulse width; encoder count; button | clear counts. Turning counts here, **hold 1 s** for the next page |
| 4 OUTPUTS | output test mode | next mode: chase, all on/off 1 Hz, follow CLK input, LED4+5, off |
| 5 LOOP | which output arrives at CLK / RST, delay, pulse width | clear |

- **No display found:** the CLK LED blinks 3 times every 2 s, the display is retried every 4 s.
  The firmware never hangs on a bad display bus (I2C timeout, bus recovery, automatic re-init).
- **Loopback test (page 5):** the module pulses OUT1..OUT6 in turn (5 ms every 50 ms). Patch any
  output into CLK or RST: the page shows which output arrives, the delay and the pulse width.
  Re-patch to check all six outputs and both inputs.
- **LED 4 and 5** share their pins with the USB serial port. They light only in the LED4+5 mode;
  serial pauses for those 10 s, then the mode returns to chase.

## Over USB (115200 baud)

Opening the port restarts the module and prints a report. Single-character commands:

| Command | Answer |
|---|---|
| `r` or `?` | readable report, ends with `=== end ===` |
| `j` | one JSON status line |
| `b` | measure the display bus again, then JSON status |
| `t` | bus speed test (~4 s), then `{"t":"bus",...}` |
| `e` | EEPROM test, then `{"t":"ee","ok":1}` |
| `c` | clear counters |
| `0` ... `4` | select page |
| `m0` ... `m4` | output mode (`m3` = LED4+5, pauses serial for 10 s) |

Lines starting with `{` are JSON (used by the web test page), all other lines are for people.
JSON status fields: `chip`, `mhz`, `id` (LGT8F unique ID), `reset`, `vcc` (mV), `vref`
(`lgt` ~1 %, `bandgap` up to +-10 %), `ram` (bytes never used), `ee_size`, `ee_test`, `sda`/`scl`
(mV), `pullups`, `oled`, `devs`, `i2c_timeouts`, `oled_reinits`, `draw_us`/`send_us`,
`clk`/`rst` (`level`, `count`, `period_us`, `width_us`, `age_ms`), `enc`, `button`, `page`,
`mode`, `uptime_ms`, `loop` (`sent`, hits per output for `clk` and `rst`, `delay_us`, `width_us`).

## Typical values

| Value | Good | Look closer |
|---|---|---|
| VCC | 4.75-5.25 V in the rack; 4.2-4.8 V on USB alone (diode on the board) | below 4.5 V in the rack |
| SDA / SCL idle | equal, about 2.6-3.6 V (OLED board supply through its pull-ups), or ~5 V with 5 V pull-ups | below ~2.5 V, or SDA and SCL different: OLED board not properly powered or damaged |
| pull-ups | ok | missing / line held low |
| display frame | 45-75 ms at 400 kHz | timeouts or re-inits counting up: unstable display bus |
| RAM free | 60 bytes or more | below 60 bytes |
| loopback | hits for the patched output, delay below ~100 us, width ~5 ms | no hits: output or input stage, cable, jack |

The idle SDA/SCL voltage is the check that finds a weak OLED supply: a module with a black,
noisy or blinking screen showed 1.6-2.7 V there while the board supply was fine.
