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

## What Gets Built

For each firmware in the `Firmware/` directory:

- **ARYTHMATIK_Buds**
- **ARYTHMATIK_Euclid**
- **ARYTHMATIK_Gate-seq**
- **ARYTHMATIK_Labor**
- **ARYTHMATIK_Pong**

The system creates:

### Board Variants
- `FIRMWARE.nano.hex` - Arduino Nano
- `FIRMWARE.nanoOldBootloader.hex` - Arduino Nano (Old Bootloader)

### Encoder Direction Variants
- `FIRMWARE.nano.reversed.hex` - Arduino Nano (Reversed Encoder)
- `FIRMWARE.nanoOldBootloader.reversed.hex` - Arduino Nano Old Bootloader (Reversed Encoder)

**Total**: 4 files per firmware × 5 firmware = **20 .hex files** per release

## Dependencies

The build system automatically installs:

- Arduino CLI
- Arduino AVR core
- Adafruit GFX Library
- Adafruit SSD1306
- EncoderButton
- FastLED

**Note:** libModulove is embedded in each firmware's `src/libmodulove` directory and does not need to be installed separately.

## Build Configuration

The workflow is defined in:
```
.github/workflows/build_release.yml
```

### Build Flags

Standard builds use default flags.

Reversed encoder builds add:
```
-DENCODER_REVERSED
```

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
