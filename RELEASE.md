# Preparing a Žygis release

The current project is a source/community preview. Do not present successful
host tests or firmware builds as physical-device validation.

1. Confirm the changelog, roadmap, setup guide and tested-device matrix reflect
   the candidate commit. Include known limitations and migration instructions.
2. Run the `Žygis checks` workflow on the release commit. Both firmware targets
   and host checks must pass.
3. Record on-device results for each included target. Verify the M5 partition
   migration, GPS acquisition/sleep, persistence, controls, audio and LEDs.
4. Review the licenses of all distributed dependencies. The pinned T-Echo Lite
   ES8311 dependency declares AGPL-3.0; the upstream MIT license does not cover
   every binary component. Resolve distribution requirements before attaching
   builds and preserve required notices/source availability.
5. Use a Žygis-specific tag, e.g. `zygis-v0.1.0-preview.1`, after choosing an
   actual version. Create a draft release with the exact build SHA, environments,
   flashing instructions, checksums and known limitations.
6. Publish only the reviewed artifacts and notes. Use clear device-specific
   filenames; distinguish full ESP32 images from application-only OTA images.

Inherited Solo/MeshCore release workflows are manual in this fork. The standard
CI does not create tags, publish releases, upload firmware, or deploy Pages.

Run **Build Žygis release artifacts** manually from GitHub Actions on the
candidate ref. It builds each environment in `solo/zygis/platformio.ini` and
uploads a separate artifact with a commit manifest and SHA-256 checksums.
Firmware images are available for manual release review. The workflow does
not create a release or tag. Diagnostic environments are named `*_test`.

To build and package an environment locally, run
`python3 scripts/zygis_release.py WioTrackerL1_zygis`.
