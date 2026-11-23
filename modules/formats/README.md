# File Format Modules (`modules/formats/`)

**English (US)** | [Português (Brasil)](README.pt-BR.md)

`modules/formats/` holds pure, reusable file-format codecs. The directory is a
hierarchy: `modules/formats/<type>/<format>/`, where `<type>` is the media type
(`image`, `audio`, `animation`, `video`, `data`) and `<format>` is the **file
format itself** (`cz`, `tlg`, `adpcm`, `psb`, `pss`, `anm`, `cri`, `tim2`,
`spu`, …). Folder names are never game or engine names.

| Layer | Directory | Responsibility |
| --- | --- | --- |
| Format | `modules/formats/` | Decode bytes into neutral RGBA pixels, interleaved PCM or frame sequences. Depends only on the C++ standard library. |
| Provider | `modules/providers/` | Archive/package containers and `Kotonoha::AssetProvider` implementations (mounting, overlays, caching). |
| VM | `modules/vms/` | Execute script bytecode; independent of rendering and archive decoding. |

## Neutral implementation contract

- Image codecs return RGBA8888 bytes plus width, height and an anchor
  (`Kotonoha::Formats::DecodedImage`). They never create an SDL texture or
  surface.
- Audio codecs return interleaved `int16_t` samples plus sample rate and channel
  count (`Kotonoha::Formats::DecodedPcm`). No SDL audio stream, `MediaPlayer` or
  engine type is used.
- Animation codecs return a sequence of frames, each carrying the same neutral
  RGBA blob and an explicit frame index (`Kotonoha::Formats::DecodedFrame`).

Games and providers convert the neutral blobs to their preferred engine
representation (`TexturePtr`, `SurfacePtr` or `AudioClip`).

> Note: `audio/cri` and `audio/spu` still route through engine decoding and are
> marked with a `TODO(neutral)` in their CMake files; they are the remaining
> migration work for this contract.

## Modules

| Path | Format | CMake target |
| --- | --- | --- |
| `image/cz` | CZ0–CZ4 compressed images | `kotonoha_format_cz` |
| `image/tlg` | TLG5/TLG6 images | `kotonoha_format_tlg` |
| `image/tim2` | TIM2/GIM images | `kotonoha_format_tim2` |
| `image/tex` | TEX/TEXB/CHR/BGD/CGD/ALP images | `kotonoha_format_tex` |
| `image/crgb` | CRGB-family images/animation | `kotonoha_format_crgb` |
| `image/evd` | EVD pictures and alpha masks | `kotonoha_format_evd` |
| `image/ui-atlas` | Font/UI atlas images | `kotonoha_format_ui_atlas` |
| `audio/adpcm` | ADPS/ADP4/IMA ADPCM | `kotonoha_format_adpcm` |
| `audio/cri` | CRI ADX/AHX audio | `kotonoha_format_cri` |
| `audio/spu` | SPU HD/BD sound banks | `kotonoha_format_spu` |
| `audio/pcm-block` | Block-coded SND/BGM PCM | `kotonoha_format_block_pcm` |
| `audio/smfd` | SMFD MIDI streams | `kotonoha_format_smfd` |
| `animation/anm` | ANM/CHD/CHAR sprites | `kotonoha_format_anm` |
| `data/psb` | PSB/MDF structured data | `kotonoha_format_psb` |

Shared neutral data types and byte readers live in `modules/formats/internal/`.
