# Miss Each Other (MEO)

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Miss Each Other uses the shared Overflow CMP / Summer Radish runtime and the `SummerAssets` provider. The standalone target is `MEO`, with bundle identifier `me.hirameki.kotonoha.meo`. Its window title is **Miss Each Other**, not the internal legacy slug.

## Build and start

Configure with `-DKOTONOHA_BUILD_MEO=ON`, then build target `MEO`. Provide legally obtained retail data through `--meo DIRECTORY|ISO`, `MEO_DIR`, or the interactive folder launcher. For local data, use `games/meo/game-assets/`.

## Checks and limitations

The optional `MEO.Presentation` and `MEO.Audio` CTests require `-DMEO_DATA_DIR=/path/to/data`. They check visible startup, BGM and SND decoding, and opening music and voice requests, but do not prove full story, route, or platform parity. The game shares the CMP archive provider with Summer Radish while retaining its own title and save/package identity. MEO `.BGM` music and `.SND` voice tracks play through its own ADPCM decoder at the original 24 script frames per second.
