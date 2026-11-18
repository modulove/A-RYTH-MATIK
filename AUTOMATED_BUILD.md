# Automated Build System

This repository now includes an automated build and release system using GitHub Actions.

## How It Works

When you push a git tag (e.g., `v1.0.0`), the system automatically:

1. ✅ Compiles all firmware variants
2. ✅ Builds for multiple Arduino boards
3. ✅ Creates standard and reversed encoder versions
4. ✅ Generates a GitHub Release with all .hex files
5. ✅ Triggers deployment on modulove.github.io
6. ✅ Makes firmware available for web-based flashing

## Creating a Release

### Automatic Release (Recommended)

```bash
# Commit your changes
git add .
git commit -m "Update firmware features"
git push

# Create and push a version tag
git tag -a v1.0.0 -m "Release version 1.0.0"
git push origin v1.0.0
```

The system will automatically build and release everything!

### Manual Build

You can also trigger a build manually without creating a release:

1. Go to the "Actions" tab on GitHub
2. Select "Build and Release Firmware"
3. Click "Run workflow"
4. Select the branch and click "Run workflow"

### Pull Requests

Every pull request is built the same way (no release). The `.hex` files are attached to the
workflow run as artifacts: open the run from the PR's "Checks" tab and download them at the bottom.

## What Gets Built

For each firmware listed in the workflow's `firmware` matrix:

- **ARYTHMATIK_Buds**
- **ARYTHMATIK_Euclid**
- **ARYTHMATIK_Gate-seq**
- **ARYTHMATIK_Labor**
- **ARYTHMATIK_Pong**
- **ARYTHMATIK_Stitch**
- **ARYTHMATIK_DEBUG** (hardware test)
- **ARYTHMATIK_Cat**
- **ARYTHMATIK_Funghi**
- **ARYTHMATIK_Kiss**
- **ARYTHMATIK_Walz**

A new firmware folder is only built after it has been added to that list.

The system creates:

### Board Variants
- `FIRMWARE.nano.hex` - Arduino Nano
- `FIRMWARE.nanoOldBootloader.hex` - Arduino Nano (Old Bootloader)

### Encoder Direction Variants
- `FIRMWARE.nano.reversed.hex` - Arduino Nano (Reversed Encoder)
- `FIRMWARE.nanoOldBootloader.reversed.hex` - Arduino Nano Old Bootloader (Reversed Encoder)

**Total**: 4 files per firmware × 11 firmware = **44 .hex files** per release

## Dependencies

The build system automatically installs:

- Arduino CLI
- Arduino AVR core
- FastGPIO (from pololu/fastgpio-arduino)
- Adafruit GFX Library
- Adafruit SSD1306
- Encoder (PJRC Encoder library)
- EncoderButton
- FastLED

**Note:** libModulove is included as a git submodule from awonak/libModulove and is automatically cloned when checking out the repository with `submodules: recursive`.

## Build Configuration

The workflow is defined in:
```
.github/workflows/build_release.yml
```

### Build Flags

All builds pass:
```
-DSSD1306_NO_SPLASH
```
It leaves out the Adafruit SSD1306 library's splash logos (~1.2 KB of flash that no firmware
shows). It only works as a compiler flag, because it has to reach the library's own source.
Building in the Arduino IDE without it still works, but the program is ~1.2 KB bigger: on an
LGT8F328P board use the release files or make sure the build stays below 29,696 bytes.

Reversed encoder builds add:
```
-DENCODER_REVERSED
```

### LGT8F328P boards

Every firmware includes `LGT8F_Compat.h` (one copy per sketch folder), so each
Nano `.hex` also runs on LGT8F328P Nano boards: the chip is detected at power-up,
then the LGT8F gets its 16 MHz clock and emulated EEPROM set up. LGT8F boards are
flashed with the `nanoOldBootloader` files (same protocol, 57600 baud).

The workflow enforces two rules:
- every program must stay below **29,696 bytes** (the LGT8F bootloader starts at
  0x7400), so all builds pass `--build-property upload.maximum_size=29696`;
- all `LGT8F_Compat.h` copies must be identical to `Firmware/ARYTHMATIK_Euclid/LGT8F_Compat.h`.

### Animation frames

Cat, Funghi, Kiss and Walz draw their animation frames from `frames.h`: run-length coded
display pages, about a third of the flash of plain bitmaps. `frames.h` is generated from the
editable bitmaps in `frames_src.h` by `Firmware/tools/oled_rle.py`; the exact command is in
line 2 of each `frames.h`. The drawing code is `OledRle.h`, one identical copy per folder.
The workflow checks that all `OledRle.h` copies are identical and that every `frames.h` is
what its `frames_src.h` generates.

### EEPROM on the LGT8F

On the LGT8F every EEPROM write freezes the chip for ~46 ms (a flash page swap).
Save several bytes with one `EEPROM.put()` (one page swap), not with a loop of
`EEPROM.update()` calls. Details are in the header.

## Workflow Status

Check build status:
- Visit the "Actions" tab in this repository
- Each build takes approximately 10-15 minutes
- Failed builds will send notifications (if configured)

## Release Assets

After a successful build, find your .hex files:

1. Go to the "Releases" section
2. Click on the latest release
3. Download .hex files from the "Assets" section

Or flash directly from the website:
👉 **https://modulove.github.io/arythmatik**

## Troubleshooting

### Build Fails

**Common issues:**
- Syntax errors in firmware code
- Missing library dependencies
- Incorrect board configuration

**Solution:**
- Check the workflow logs in the Actions tab
- Test compilation locally with Arduino CLI
- Verify all libraries are accessible

### Release Not Created

**Check:**
- Tag format matches `v[0-9]+.[0-9]+.[0-9]+` (e.g., v1.0.0)
- GitHub Actions has write permissions
- Build completed successfully

### Website Not Updated

**Check:**
- The MODULOVE_DEPLOY_TOKEN secret is configured
- The modulove.github.io workflow runs successfully
- Wait 3-5 minutes for deployment to complete

## Setup Requirements

For the automated system to work, you need:

1. ✅ GitHub Actions enabled
2. ✅ Workflow permissions set to "Read and write"
3. ✅ MODULOVE_DEPLOY_TOKEN secret configured
4. ✅ modulove.github.io repository properly configured

See `../modulove.github.io/SETUP_GUIDE.md` for complete setup instructions.

## Versioning

This project uses [Semantic Versioning](https://semver.org/):

- **MAJOR** (v1.0.0): Breaking changes
- **MINOR** (v1.1.0): New features, backward compatible
- **PATCH** (v1.1.1): Bug fixes, backward compatible

## Contributing

When contributing firmware changes:

1. Create a feature branch
2. Make your changes
3. Test locally
4. Create a pull request
5. After merge, maintainer will create a release tag

## Credits

Build system inspired by [HagiwoModulove](https://github.com/awonak/HagiwoModulove) by Adam Wonak.

Adapted for the Modulove project with enhanced automation.
