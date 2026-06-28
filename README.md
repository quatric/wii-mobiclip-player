# Wii Mobiclip Player

A Wii homebrew app that plays Wii Mobiclip files directly on hardware. No transcoding is used, every video is decoded on the Wii. It supports ADPCM, FastAudio, PCM, and Vorbis audio.

## What's inside

| Component | File | Notes |
|-----------|------|-------|
| Video decoder | `source/mobi_dec.{c,h}` | Standalone port of FFmpeg's `libavcodec/mobiclip.c`. I/P frames, intra prediction, motion compensation, IDCT, run/level VLC. All FFmpeg infrastructure removed. |
| Bit/VLC/Golomb | `source/mobi_bits.h` | Self-contained MSB-first bit reader, exp-Golomb, and a fixed-table VLC builder matching `ff_init_vlc_from_lengths` / `get_vlc2`. |
| Container demuxer | `source/mo_demux.{c,h}` | Parses the `.mo` (`MOC5`) header and chunk stream from a `FILE*`. Ported from FFmpeg `libavformat/modec.c`. |
| Audio decoder | `source/mo_audio.{c,h}` | PCM s16, IMA Mobiclip-Wii ADPCM, and FastAudio → native int16. |
| Vorbis decoder | `source/mo_vorbis.{c,h}` | Ogg Vorbis (`AV` sections) via Tremor (libvorbisidec), both the retail single-packet and `[0xFFFF]` multi-packet section forms. |
| App / browser | `source/main.c` | libfat SD browser + playback loop with WPAD input. |

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

Produces `mobiclip-player.dol`. Copy it (with `meta.xml`) to
`sd:/apps/mobiclip-player/boot.dol` for the Homebrew Channel, or send it with
`make run` (wiiload).

## Controls

Browser:
- **+Control Pad Up/Down** — move selection
- **A** — open folder / play file
- **B** — parent folder
- **HOME** — exit

During playback:
- **A** / **+** — pause / resume
- **B** / **HOME** — stop, back to browser

## Host validation

The decoder and demuxer are platform-independent C and were verified on the
host against real `.mo` samples (see `test/host_test.c`), decoding I-frames and
P-frames (motion compensation) at 624×352 and 384×288 into coherent images.

```sh
cc -O2 -o host_test test/host_test.c source/mo_demux.c source/mobi_dec.c
./host_test some.mo 30      # dumps frame_000.ppm ...
```

## Credits

# Mobiclip Support Credits

The implementation of Mobiclip support in this software was made possible thanks to the research, documentation, and source code from the following projects and their respective authors:

* [PlayMobic](https://code.pleonex.dev/pleonex/PlayMobic)
* [MobiclipDecoder](https://github.com/Gericom/MobiclipDecoder)
* [Gericom's x264 fork](https://github.com/Gericom/x264)
* [WiiLink24 FFmpeg fork](https://github.com/WiiLink24/FFmpeg)