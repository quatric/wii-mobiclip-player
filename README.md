# Wii Mobiclip Player

A Wii homebrew app that plays Wii Mobiclip files directly on hardware. No transcoding is used, every video is decoded on the Wii. It supports ADPCM, FastAudio, PCM, and Vorbis audio.

It also plays **KWZ** (Flipnote Studio 3D) and **PPM** (Flipnote Studio DS)
animations and **THP** (GameCube/Wii) video, each decoded on-device with audio.

## What's inside

| Component | File | Notes |
|-----------|------|-------|
| Video decoder | `source/mobi_dec.{c,h}` | Standalone port of FFmpeg's `libavcodec/mobiclip.c`. I/P frames, intra prediction, motion compensation, IDCT, run/level VLC. All FFmpeg infrastructure removed. |
| Bit/VLC/Golomb | `source/mobi_bits.h` | Self-contained MSB-first bit reader, exp-Golomb, and a fixed-table VLC builder matching `ff_init_vlc_from_lengths` / `get_vlc2`. |
| Container demuxer | `source/mo_demux.{c,h}` | Parses the `.mo` (`MOC5`) header and chunk stream from a `FILE*`. Ported from FFmpeg `libavformat/modec.c`. |
| Audio decoder | `source/mo_audio.{c,h}` | PCM s16, IMA Mobiclip-Wii ADPCM, and FastAudio → native int16. |
| Vorbis decoder | `source/mo_vorbis.{c,h}` | Ogg Vorbis (`AV` sections) via Tremor (libvorbisidec), both the retail single-packet and `[0xFFFF]` multi-packet section forms. |
| KWZ decoder | `source/kwz_dec.c` | Flipnote Studio 3D `.kwz` → RGB24 + mixed mono ADPCM audio @ 32768 Hz. Port of `libavformat/kwzdec.c`. |
| PPM decoder | `source/ppm_dec.c` | Flipnote Studio DS `.ppm` → RGB24 + mixed mono ADPCM audio @ 32768 Hz. Port of `libavformat/ppmflipdec.c`. |
| THP decoder | `source/thp_dec.c` | THP container + DSP/THP ADPCM audio (from `libavcodec/adpcm.c`); video is baseline MJPEG via the bundled NanoJPEG in THP unescaped-scan mode. |
| JPEG decoder | `source/nanojpeg.{c,h}` | Martin Fiedler's NanoJPEG (MIT), patched with an unescaped-scan mode for THP's non-byte-stuffed entropy data. |
| RGB source API | `source/rgb_source.h` | Common interface (`kwz_open`/`ppm_open`/`thp_open`) driven by the shared `play_rgb()` loop in `main.c`. |
| App / browser | `source/main.c` | libfat SD browser + playback loop with WPAD input; dispatches `.mo` vs the packed-RGB24 formats by extension. |

Color space is chosen **per frame** from the bitstream `moflex` flag:
`moflex=0` → YCgCo (`R=Y+U-V, G=Y+V, B=Y-U-V`); `moflex=1` → limited-range
BT.601 YCbCr. Both match the reference decoder; `.mo` files of either kind
exist in the wild, so the choice cannot be hardcoded.

## Building

Requires devkitPro with **devkitPPC** and **libogc**. Vorbis playback needs
libogg + Tremon (libvorbisidec) for PPC; either install them with
`dkp-pacman -S ppc-libogg ppc-libvorbisidec`, or build them rootless into
`portlibs/ppc` with the bundled script:

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=$DEVKITPRO/devkitPPC
./scripts/build_portlibs.sh   # one-time: builds libogg + Tremor for PPC
make
```

The Makefile links Tremor from `portlibs/ppc` (`LIBDIRS`), so the pacman and
script approaches are interchangeable.

Produces `wii-mobiclip-player.dol`. Copy it (with `meta.xml`) to
`sd:/apps/wii-mobiclip-player/boot.dol` for the Homebrew Channel, or send it with
`make run` (wiiload).

### Optional controllers

The normal build supports Wii Remotes, Classic Controllers and GameCube pads.
Build the pinned libraries into portlibs/ppc, then enable them independently:

    ./scripts/build_controller_portlibs.sh

- [FIX94/libwiidrc](https://github.com/FIX94/libwiidrc)
- [FIX94/libwupc](https://github.com/FIX94/libwupc)
- [xerpi/libsicksaxis](https://github.com/xerpi/libsicksaxis)

    make WITH_WIIDRC=1       # Wii U GamePad via FIX94/libwiidrc
    make WITH_WUPC=1         # Wii U Pro Controller via FIX94/libwupc
    make WITH_SICKSAXIS=1    # DualShock 3 / Sixaxis via xerpi/libsicksaxis

Any combination is allowed. The Wii U GamePad backend only works in Wii U
Virtual Console mode with a properly patched fw.img; ordinary Wii and vWii
execution cannot expose the GamePad. DualShock 3 support uses a wired USB
connection under IOS58 and attempts to open the controller automatically. The Wii U Pro
Controller backend requires the project's current-libogc compatibility wrapper.

Note that libwupc is GPLv3. Distributing a binary linked against it requires
complying with that license even though this project's own source is MIT.

## Controls

Browser:
- **+Control Pad Up/Down** - move selection
- **A** - open folder / play file
- **B** - parent folder
- **HOME** - exit

During playback:
- **A** / **+** - pause / resume
- **B** / **HOME** - stop, back to browser

## Contact

General questions or comments can be sent to [quatricsoftware@gmail.com](mailto:quatricsoftware@gmail.com). No support will be provided for this tool.

## Credits

See [CREDITS_MOBICLIP.md](CREDITS_MOBICLIP.md) for the projects and authors that made the Mobiclip support possible.

## License

Wii Mobiclip Player uses the MIT license. See [LICENSE](LICENSE).

Copyright (c) 2026 quatric