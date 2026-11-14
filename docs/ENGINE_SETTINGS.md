# Engine settings

**English (US)** | [Português (Brasil)](ENGINE_SETTINGS.pt-BR.md)

The engine settings menu provides **Display**, **Audio**, and **VN text** controls. Launcher **Advanced** exposes additional settings. Preferences are saved per game, with older configuration files supported.

- **Display:** VSync defaults to enabled unless explicitly disabled in saved settings. Upscaling can be configured by backend/profile/scale. Supported shader options depend on the active renderer. Text and subtitles are rasterized separately instead of being passed through image upscalers.
- **Audio:** global mute/volume and port-provided mixer channels. Audio changes take effect immediately and preserve the script's own fades and gains.
- **VN text:** optional font override (`.ttf`, `.otf`, `.ttc`), and separate dialogue/speaker color overrides. Changes apply without restarting the visual novel.

## Forced ASS subtitles

The **Enable forced subtitles** option defaults to enabled; it is persisted as `enableForcedSubtitles`. A `forced_subtitles/` folder can provide `.ass` files keyed by the current node/event ID (for example `80005:66` maps to `80005-66.ass`). Timed scenes use their timeline clock; standard scenes use elapsed, unpaused scene time. The F8 HUD displays IDs and timing.

With the F10 debug inspector, **Generate Forced Subtitles (.ass)** creates a template for the current node without overwriting existing files. Edit the generated ASS, turn `Comment` into `Dialogue`, adjust timing/text, then use **Reload forced subtitles**. The inspection UI shows the resolved source folder and active file. Menus pause the shared scene clock while existing subtitles remain behind the UI.

## Related guides

[Resource directories](DIRECTORY_POLICY.md) · [Overlays and translation](ASSET_OVERLAYS.md). Settings and GUI details may vary by game port.
