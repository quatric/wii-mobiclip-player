# Changelog

## v1.1 — 2026-08-28

- Added THP video playback with DSP/THP ADPCM audio.
- Added PPM and KWZ Flipnote animation playback.
- Added optional Wii U GamePad, Wii U Pro Controller, and wired DualShock 3 support.
- Streamed Mobiclip audio and THP video with bounded memory use instead of retaining duration-sized media data in RAM.
- Added a bounded PCM video pipeline to prevent playback stalls and audio underruns.
- Fixed Mobiclip reference-frame selection for files produced by current Mobipeg builds.
- Fixed THP playback falling permanently behind when full-resolution JPEG decoding misses a presentation deadline.
- Fixed audio initialization and replay behavior across supported codecs.
- Added automated all-controller builds and Homebrew Channel packaging.

## v1.0 — 2026-06-28

- Initial Wii Mobiclip playback release.
