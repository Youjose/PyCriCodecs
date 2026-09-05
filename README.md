# CriStudio GUI and CriCodecs

<p align="center">
  <img src="https://raw.githubusercontent.com/Youjose/CriCodecs/main/CriStudio/packaging/linux/io.github.Youjose.CriStudio.svg" width="128" alt="CriStudio logo">
</p>

<p align="center">
  <strong>Desktop GUI and Python/C++ toolkit for CRI middleware game files, including CPK, USM, ACB/AWB, HCA, ADX, and UTF.</strong>
</p>

<p align="center">
  <a href="https://github.com/Youjose/CriCodecs/releases/latest"><img src="https://img.shields.io/badge/Download-CriStudio_GUI-2ea44f?style=for-the-badge&logo=github" alt="Download CriStudio GUI"></a>
  <a href="https://pypi.org/project/cricodecs/"><img src="https://img.shields.io/badge/pip_install-cricodecs-3775A9?style=for-the-badge&logo=pypi&logoColor=white" alt="pip install cricodecs"></a>
</p>

### Notice
**AI assistance has been used throughout the project implementation, only some core parts have been fully manually reviewed.** 

## Download CriStudio GUI

[**Download the latest CriStudio release for Windows, Linux, or macOS →**](https://github.com/Youjose/CriCodecs/releases/latest)

The portable GUI is the recommended option for most users. It can inspect,
preview, extract, edit, and build CRI middleware files without requiring
Python.

## Install CriCodecs for Python

```console
pip install cricodecs
```

The Python package provides native codec/container bindings and the shared
`cricodecs` command-line interface. CriCodecs is also available as a standalone
C++23 library and CLI for application integration and batch workflows.

![CriStudio USM playback](https://raw.githubusercontent.com/Youjose/CriCodecs/main/docs/images/cristudio-usm-preview.png)

*Inspect and play muxed VP9 and ADX streams directly from a USM container.*

![CriStudio CPK inspection](https://raw.githubusercontent.com/Youjose/CriCodecs/main/docs/images/cristudio-cpk-inspection.png)

*Browse nested CPK entries and inspect structured binary data without extracting it first.*

## Downloads and packages

The [latest GitHub release](https://github.com/Youjose/CriCodecs/releases/latest)
provides platform-specific downloads:

| Download | Intended use |
|---|---|
| CriStudio portable | Recommended. Extract and run; Qt and FFmpeg are included. |
| CriStudio slim | Smaller advanced-user build; requires compatible Qt and FFmpeg on the system. |
| CriCodecs CLI | Optimized command-line executable for scripts and batch work. |
| C++ SDK | Headers, CMake package metadata, and static/shared libraries. |
| Python wheels | Install from PyPI with `python -m pip install cricodecs`. |

Portable CriStudio needs no installer. Windows packages expose only
`CriStudio.exe` and an `_internal` runtime directory, Linux uses an AppImage,
and macOS uses a self-contained application bundle.

## CriStudio

CriStudio has two workspaces:

- **Browse** loads files and archives, searches nested entries, previews audio,
  video, images, tables, and raw bytes, and extracts selected content.
- **Editor** edits archive entries and tables, builds and muxes media, and
  manages encryption keys without changing the global CRI key implicitly.

Key recovery is available for supported HCA, ADX, AHX, USM, AWB, and ACB
inputs. Multi-file recovery runs in the background and ranks a bounded set of
candidates rather than blocking the interface.

For ACB files, Browse changes the archive switch to **Cue / List**. Cue keeps
every authored cue visible, exposes selector and static path choices, shows
block, waveform, AWB ID/stream, timing, action-chain, and command counts, and
automatically renders the selected static path in the normal audio
preview without a separate render action.
Authored audio-bearing infinite blocks use the normal selectable loop-range
control; empty infinite holds are omitted unless explicitly included as finite
silence. List remains the flat waveform view and extraction surface.

Decoded ACB extraction remains waveform-based by default. Enable **Edit >
Extract ACBs as Rendered Cues** to replace those flat outputs with the
deduplicated static cue renders and their selector-qualified filenames. In Cue
view, **Extract Entry** exports the currently selected cue path directly.

### Format coverage

| Area | Formats |
|---|---|
| Audio and audio containers | ADX, AHX, HCA, WAV/PCM, AAX, AIX |
| Sound banks and tables | ACB, AWB, ACX, CSB, UTF |
| Archives and disc containers | AFS, CPK, CVM/ROFS |
| Video and stream containers | USM, SFD/SofDec |

Editing, encoding, muxing, and key-recovery actions appear only where
supported.

## CLI

Release builds provide a standalone `cricodecs` executable. Python installs also
provide a `cricodecs` console command backed by the same native implementation.

Common usage is input-driven:

```sh
cricodecs input.hca
cricodecs input.cpk
```

Audio inputs such as ADX, AHX, HCA, and AAX decode to WAV by default. Archive
and container inputs extract to a sibling directory by default. Use `-m` for
metadata, `-m --json` for JSON metadata, `-f` to force the input format, and
`-o` to choose the output path or extraction root.

### Key recovery

```sh
cricodecs --recover-key -f hca music.hca
cricodecs --recover-key -f hca music.acb
cricodecs --recover-key -f usm movie.usm
cricodecs --recover-key -f adx music.adx
cricodecs --recover-key -f ahx voice_a.ahx voice_b.ahx
cricodecs --recover-key -f awb BGM.awb
cricodecs --recover-key -f acb BGM.acb --json
```

HCA recovery scans standalone files, AWB/ACB/USM containers, and folders. USM
recovery combines supported video, encrypted HCA, and masked ADX evidence.
ADX/AHX recovery returns directly usable `start,mult,add` triplets, while
AWB/ACB recovery targets encrypted AAC/M4A entries. Multiple inputs are assumed
to share one base key unless `--independent` is supplied.

HCA type 56 uses only bits 0 through 55, so recovery cannot determine the
original 64-bit key's upper byte. If the exact stored value matters, the seven
recovered bytes can be searched in application metadata or executables, but a
match is only a heuristic; the key may instead be derived, obfuscated, split,
or absent.

### USM muxing

When building a USM, `--audio` accepts repeatable ADX or HCA inputs. Supplying
`--key` masks the video and ADX audio by default, converts plain HCA audio to
cipher type 56, and preserves HCA that is already encrypted:

```sh
cricodecs --build -f usm --key 0x165CF4E2138F7BDA \
  --audio dialogue.adx --audio music.hca -o movie.usm movie.ivf
```

See [USAGE.md](https://github.com/Youjose/CriCodecs/blob/main/USAGE.md) for the
complete command and key-recovery options.

## Python Usage

Install from PyPI and import the package as `cricodecs`:

```sh
python -m pip install cricodecs
```

The top-level `cricodecs.load()` helper accepts a path or bytes and returns the
matching object. Individual modules expose operations such as `load()`,
`decode()`, `encode()`, `extract()`, `demux()`, and `save_bytes()`.

### Top-Level Load

```python
import cricodecs

obj = cricodecs.load("input.cpk")
print(type(obj))
print(repr(obj))
```

### ADX Decode And Encode

```python
from pathlib import Path

from cricodecs import adx

wav_bytes = adx.decode("input.adx")
Path("output.wav").write_bytes(wav_bytes)

encoded = adx.encode(Path("source.wav").read_bytes())
Path("output.adx").write_bytes(encoded)
```

### HCA Decode, Encode, Encrypt

```python
from pathlib import Path

from cricodecs import hca

wav_bytes = hca.decode("input.hca", keycode=0xCF222F1FE0748978)
Path("decoded.wav").write_bytes(wav_bytes)

config = hca.HcaEncodeConfig()
config.sample_rate = 48000
config.channel_count = 2
config.quality = hca.HcaQuality.HIGH

hca_bytes = hca.encode(Path("source.wav").read_bytes(), config)
Path("encoded.hca").write_bytes(hca_bytes)

encrypted = hca.encrypt(hca_bytes, cipher_type=56, keycode=0xCF222F1FE0748978)
Path("encrypted.hca").write_bytes(encrypted)
```

### CPK Inspect, Extract, Build

```python
from pathlib import Path

from cricodecs import cpk

archive = cpk.load("input.cpk")
print(archive.info())
archive.extract("input_extracted")

new_archive = cpk.create(cpk.CpkPreset.FILENAME)
new_archive.add_file("data/voice.adx", "voice/voice.adx")
new_archive.add_bytes(b"hello", "text/readme.txt")
Path("output.cpk").write_bytes(new_archive.save_bytes())
```

### ACB Cue Graphs, AWB, And USM

```python
from cricodecs import acb, usm

cue_sheet = acb.load("sound.acb")
print(cue_sheet.info())
print(cue_sheet.graph)

cue_index = 0
print(cue_sheet.selector_options(cue_index))
resolution = cue_sheet.resolve_cue(cue_index)
print(resolution.filenames())

# Choose one inspected static result; named selectors can also be passed to
# cue_sheet.cue_plan(...) when the ACB provides them.
plan = resolution.plans[0].plan
plan.export(cue_sheet, "cue.wav")

# Flat waveform extraction remains available separately.
cue_sheet.extract("sound_waveforms")

movie = usm.load("movie.usm")
print(movie.info())
movie.extract("movie_streams")

config = usm.UsmMuxConfig(
    video_path="movie.264",
    audio_tracks=[usm.UsmMuxAudioTrack("movie.adx")],
    subtitle_tracks=[
        usm.UsmMuxSubtitleTrack(
            "subtitles_en.srt",
            language_id=0,
            format=usm.UsmSubtitleFormat.SRT,
        ),
    ],
)
usm.mux(config, "movie_with_subtitles.usm")
```

Objects and configuration structs provide useful `repr()` output:

```python
from cricodecs import adx

print(repr(adx.AdxEncodeConfig()))
```

## C++ Usage

Include the complete API and link the namespaced CMake target:

```cpp
#include <cricodecs/cricodecs.hpp>
```

```cmake
find_package(CriCodecs CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE CriCodecs::CriCodecs)
```

Module headers are installed under `<cricodecs/...>`, so projects can include
only the formats they use. Public operations report recoverable failures with
`std::expected<T, std::string>`.

### ADX

```cpp
#include <filesystem>
#include <fstream>
#include <vector>

#include <cricodecs/adx/adx_codec.hpp>

int main() {
    auto adx = cricodecs::adx::Adx::load("input.adx");
    if (!adx) {
        return 1;
    }

    auto decoded = adx->decode();
    if (!decoded) {
        return 1;
    }

    const auto& pcm = decoded->pcm_data;
    (void)pcm;
}
```

### HCA

```cpp
#include <cstdint>
#include <vector>

#include <cricodecs/hca/hca_codec.hpp>

std::vector<int16_t> decode_hca(std::span<const uint8_t> bytes) {
    auto decoded = cricodecs::hca::decode(bytes, 0xCF222F1FE0748978ULL);
    if (!decoded) {
        return {};
    }
    return std::move(*decoded);
}
```

### CPK

```cpp
#include <cricodecs/cpk/cpk_container.hpp>

int main() {
    auto archive = cricodecs::cpk::Cpk::load("input.cpk");
    if (!archive) {
        return 1;
    }

    return archive->files().empty() ? 1 : 0;
}
```

## Build from source

### Requirements

- CMake 4.2 or newer
- A C++23 compiler:
  - GCC 16.1 or newer on Linux
  - Visual Studio 2026 / MSVC v145 or newer on Windows
  - A current Apple Clang/Xcode toolchain on macOS
- Python 3.9 or newer when building the Python package

Use a fresh build directory after changing the compiler, generator, Python
version, or CMake version.

### Python package

From the repository root:

```sh
python -m pip install .
```

For an editable development install:

```sh
python -m pip install --no-build-isolation -ve .
```

### C++ SDK and CLI

Build and install an optimized static library and the CLI:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
cmake --install build --prefix ./install
```

Set `-DBUILD_SHARED_LIBS=ON` for a shared library. To omit the CLI, set
`-DCRICODECS_BUILD_CLI=OFF`.

On Windows with Visual Studio 2026:

```sh
cmake -S . -B build-vs -G "Visual Studio 18 2026" -A x64
cmake --build build-vs --config Release --parallel 4
cmake --install build-vs --config Release --prefix install
```

## Credits

CriCodecs builds on public knowledge and prior open-source work around CRI
formats. Many thanks and credits to:

- [vgmstream](https://github.com/vgmstream/vgmstream) for HCA and CRI audio
  research.
- [VGAudio](https://github.com/Thealexbarney/VGAudio) for ADX and HCA codec
  behavior references.
- [bnnm](https://github.com/bnnm) for CRI audio-format research and tooling.
- [Nyagamon](https://github.com/Nyagamon) for CRI format research.
