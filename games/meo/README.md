# Miss Each Other (MEO)

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Miss Each Other uses the shared Overflow CMP / Summer Radish runtime and the `SummerAssets` provider. The standalone target is `MEO`, with bundle identifier `me.hirameki.kotonoha.meo`. Its window title is **Miss Each Other**, not the internal legacy slug.

## Build and start

Configure with `-DKOTONOHA_BUILD_MEO=ON`, then build target `MEO`. Provide legally obtained retail data through `--meo DIRECTORY|ISO`, `MEO_DIR`, or the interactive folder launcher. For local data, use `games/meo/game-assets/`.

## Checks and limitations

The optional `MEO.Presentation` CTest requires `-DMEO_DATA_DIR=/path/to/data`. This checks startup and visible rendering, not full story, route or platform parity. The game shares the CMP codec with Summer Radish while retaining its own title and save/package identity.
