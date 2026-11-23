# Módulos de Formatos de Arquivo (`modules/formats/`)

[English (US)](README.md) | **Português (Brasil)**

`modules/formats/` contém codecs puros e reutilizáveis de formatos de arquivo. O diretório é uma
hierarquia: `modules/formats/<type>/<format>/`, onde `<type>` é o tipo de mídia
(`image`, `audio`, `animation`, `video`, `data`) e `<format>` é o **próprio formato de arquivo** (`cz`, `tlg`, `adpcm`, `psb`, `pss`, `anm`, `cri`, `tim2`,
`spu`, …). Nomes de pastas nunca são nomes de jogos ou engines.

| Camada | Diretório | Responsabilidade |
| --- | --- | --- |
| Formato | `modules/formats/` | Decodifica bytes em pixels RGBA neutros, PCM entrelaçado ou sequências de frames. Depende apenas da biblioteca padrão C++. |
| Provider | `modules/providers/` | Contêineres de arquivos e implementações de `Kotonoha::AssetProvider` (montagem, overlays, cache). |
| VM | `modules/vms/` | Executa bytecode de scripts; independente de renderização e decodificação de arquivos. |

## Contrato de implementação neutra

- Codecs de imagem retornam bytes RGBA8888 mais largura, altura e âncora
  (`Kotonoha::Formats::DecodedImage`). Nunca criam texturas ou superfícies SDL.
- Codecs de áudio retornam amostras `int16_t` entrelaçadas mais taxa de amostragem e
  número de canais (`Kotonoha::Formats::DecodedPcm`). Sem stream de áudio SDL, `MediaPlayer` ou tipos do motor.
- Codecs de animação retornam uma sequência de frames, cada um com o mesmo blob RGBA neutro e um índice explícito (`Kotonoha::Formats::DecodedFrame`).

Jogos e providers convertem os blobs neutros para a representação preferida do motor (`TexturePtr`, `SurfacePtr` or `AudioClip`).

> Note: `audio/cri` and `audio/spu` still route through engine decoding and are
> marked with a `TODO(neutral)` in their CMake files; they are the remaining
> migration work for this contract.

## Módulos

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

Tipos de dados neutros e leitores de bytes compartilhados ficam em `modules/formats/internal/`.
