# Optional Discord Rich Presence

**English (US)** | [Portugu?s (Brasil)](RICH_PRESENCE.pt-BR.md)

The `kotonoha_rich_presence` static CMake target is a reusable, opt-in transport. It is not linked into the shared engine or any game by default. No login, token, game assets or Discord SDK binaries are included.

## Build and use

Configure a desktop build with `-DKOTONOHA_BUILD_RICH_PRESENCE=ON`. Link `kotonoha_rich_presence` into the consuming game target and include `<Kotonoha/integrations/RichPresence.hpp>`. Windows uses a named pipe; Linux and macOS use a local Unix domain socket; Android uses the optional Social SDK. RPC is disabled on **WebAssembly and iOS**, where `KOTONOHA_HAS_DISCORD_RPC` is 0. On macOS the native Xcode targets compile `DiscordUnix.cpp`; iOS keeps the null transport.

The game owns its opt-in setting, Discord application ID, and a sanitized activity policy. Check `platformPresenceAvailable()` before constructing a session. Create a transport using `discordPlatform(applicationId, largeImage, applicationName)`, then create `RichPresence` with the transport, Unix start time in seconds, `RichPresence::Timing{}`, and the game display name. The name is always published; `setDetails()` appends curated text and `setState()` selects Menu/Loading/Gameplay. Destroy the session before shutting down its host. Never publish dialogue, player choices, filesystem paths, credentials, or other sensitive data.

The System settings menu and the debug overlay expose a **Discord RPC** on/off switch. `setRichPresenceEnabled(false)` disconnects all sessions immediately and is shared across every game in the process.

## Android adapter (host integration required)

The optional Android transport requires the separately obtained official Discord Social SDK. Extract the release AAR Prefab module to a directory containing `prefab/modules/discord_partner_sdk`, and configure with `-DKOTONOHA_BUILD_RICH_PRESENCE=ON`, `-DKOTONOHA_ANDROID_DISCORD_RPC=ON`, and `-DKOTONOHA_ANDROID_DISCORD_SDK_DIR=<extracted-root>`. The module must contain the headers and library for the selected ABI. The consuming Android application must package the matching AAR, initialize the SDK with its Activity after explicit user opt-in, and call `setAndroidPresenceReady(true)` through its own JNI glue; it must destroy sessions before releasing the SDK context. The adapter does not authenticate or request voice access. Android Gradle packaging, launcher preferences, and game-specific activity policy are not wired by this foundation change.

## Tests

Build `KotonohaRichPresenceTest` with `BUILD_TESTING=ON` and `KOTONOHA_BUILD_RICH_PRESENCE=ON`, then run `ctest --test-dir build -R '^Kotonoha.RichPresence$' --output-on-failure`. The test uses a fake transport; it does not connect to Discord.
