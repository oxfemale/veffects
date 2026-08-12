# The `.veffects` format

A `.veffects` file is a compact, precomputed "mathematical score" of a track:
per-frame audio features that a player turns into visuals. It contains **no audio**
and no imagery -- only normalized numbers. Roughly **9.6 KB per second** of audio.

The analyzer (`veffects_gen`, or `vfxAnalyzeMp3()` in-process) decodes an mp3, runs
a windowed FFT at 60 data-frames per second, and extracts the features below.

## Layout

Binary, **little-endian**. A header followed by `frameCount` frames.

### Header (`VfxHeader`, packed)

| field        | type       | meaning                                  |
|--------------|------------|------------------------------------------|
| `magic`      | `char[4]`  | `"VFX1"`                                 |
| `version`    | `uint32`   | `1`                                      |
| `fps`        | `uint32`   | data frame rate (usually `60`)           |
| `frameCount` | `uint32`   | number of data frames                    |
| `bandCount`  | `uint32`   | spectrum bands per frame (usually `32`)  |
| `sampleRate` | `uint32`   | source audio sample rate                 |
| `duration`   | `float`    | track length in seconds                  |
| `bpm`        | `float`    | tempo estimate (0 if unknown)            |
| `reserved`   | `uint32[6]`| reserved                                 |

### Frames

Each frame is `VFX_NSCALARS` (8) scalar floats followed by `bandCount` spectrum
floats. All values are normalized to `[0..1]`.

Scalar order (`enum VfxScalar`):

| index | name         | meaning                                   |
|-------|--------------|-------------------------------------------|
| 0     | `RMS`        | overall loudness (smoothed)               |
| 1     | `BASS`       | low-band energy (~40-160 Hz)              |
| 2     | `MID`        | mid-band energy (~160-2000 Hz)            |
| 3     | `TREBLE`     | high-band energy (~2-16 kHz)              |
| 4     | `CENTROID`   | spectral centroid ("brightness")          |
| 5     | `FLUX`       | spectral flux (rate of spectral change)   |
| 6     | `BEAT`       | onset/beat strength (peaks = beats)       |
| 7     | `LOUD`       | peak loudness (fast, unsmoothed)          |

The `bandCount` band values are log-spaced spectrum energies from ~40 Hz to ~16 kHz.

## Reading it in code

`include/veffects_format.h` is a single self-contained header with the structs,
`vfxLoad()` / `vfxSave()`, and helpers `scalarLerp(t, s)` / `bandLerp(t, b)` that
linearly interpolate a value at an arbitrary time `t` (seconds). Players sample by
time, so render frame rate is independent of the 60 fps data rate.
