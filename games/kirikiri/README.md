# Kirikiri2 / KirikiriZ

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Kotonoha's C++17 KiriKiri port: XP3 asset mounting, an experimental KAG interpreter, and basic image, dialogue, menu, choice and audio presentation.

## Compatibility currently implemented

- XP3 archives use a virtual provider. The reader supports canonical raw and zlib-compressed indexes, chained indexes, multiple raw/zlib file segments, Unicode names, case-insensitive ASCII lookups, protected archive size mismatch resolution (allowing protected packages like NEKOPARA `data.xp3`, `emote.xp3`, `evimage.xp3`, `voice.xp3` to index all assets) and layered patch precedence. Individual files are extracted on demand.
- Standard SDL/FFmpeg images, **TLG5 RGB/RGBA** and **TLG6 Golomb-coded images** can be decoded, including TLG0 wrappers. The TLG6 decoder includes bounded Golomb runs, LZSS filter types, color filters and MED/AVG prediction; synthetic tests cover basic pixels, wrappers and malformed inputs.
- KAG commands including `[bg storage=...]`, `[image layer=... storage=...]`, `[freeimage]` and `[layopt visible=false]` update renderer image layers. Some transitions are displayed immediately rather than using original timing.
- A **Kotonoha fallback title menu** appears even when no executable `.ks` exists. In addition to the static scanner, an **experimental bounded TJS2 source subset interpreter** runs variable assignments, arithmetic and string expressions, conditionals, loops, compiled TJS2 bytecode (`TJS2100\0`) string constant extraction, and `Scripts.execStorage` / `Scripts.evalStorage` calls across source scripts. This selects a referenced KAG entry ahead of the alphabetical fallback when the user chooses Start; it does not create the native TJS2 menu. Plugin requests are only recorded and never executed.
- Damaged indexes, invalid segment ranges and cyclic index chains are rejected. A failed archive auto-mount is logged without suppressing unrelated XP3s.

## Known limitations

**A fallback title is not the original NEKOPARA menu.** The original KiriKiri startup path executes `startup.tjs` and depends on the TJS2 runtime, plugins and title-specific UI. This port does not yet execute general TJS2 or all compiled PSB/SCN semantics. **TLG6 method 3 raw planes are handled experimentally only when precisely sized. Gamma (method 1), modified LZSS (method 2), and game-specific XP3 filters remain unsupported.** The upstream KiriKiri Z decoder itself rejects non-Golomb TLG6 modes. Support for save states, effects, translations and audio is not equivalent to an original retail game.

Full class semantics, complete native Window/Layer behavior, native plugins and compiled TJS2 bytecode **are not implemented**; the subset stops on unhandled syntax, with an explicit diagnostic. These changes have not been validated against a commercial NEKOPARA installation; successful XP3 indexing or a fallback title does not establish full playability. Only synthetic data is used in repository tests.

## HTTP(S) and WebAssembly

In browser builds, ordinary HTTP hosting cannot enumerate unknown files.
The Kirikiri validator and engine detector now probe known paths such as
`data.xp3`, `patch.xp3`, `startup.tjs` and `scenario/first.ks` directly.
When there is no directory index, the XP3 provider also probes common archive
names, then reads their index and segments on demand through Fetch/Range.
Loose KAG entrypoints are probed by name. Other archive naming conventions
still require a manifest/index or a local folder. Detection alone is not
proof of complete game compatibility.

Use an HTTP server with Range (206), Content-Range and appropriate CORS;
see [WebAssembly build guide](../../docs/BUILD_WEB.md).

## Build and test

```sh
cmake -S . -B build -DKOTONOHA_BUILD_KIRIKIRI=ON -DBUILD_TESTING=ON
cmake --build build --target KirikiriXp3Tests Kirikiri
ctest --test-dir build -R '^KirikiriXp3$' --output-on-failure
```

Use a lawful game folder with KAG scripts or XP3 packages. Start the launcher or pass `--kirikiri /path/to/game-data`. Do not commit commercial assets.

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)

## TJS2 source function and Dictionary subset

The experimental `modules/vms/tjs2` VM now accepts named source functions
with parameters and `return`, per-call local variables, bounded recursion,
bounded `for` loops and shared `new Dictionary()` values with dotted
member reads and writes.
These capabilities allow simple bootstrap configuration functions to compute
a KAG entry or title caption before the Kotonoha fallback menu is shown.

**Not full TJS2:** Script-defined class declarations and methods, inline event functions and virtual Window/Layer objects are supported in a limited form. Lexical closures, arbitrary class inheritance, advanced properties, the original KiriKiri title flow and compiled TJS2 bytecode are still missing. Unsupported native constructors cause explicit
errors instead of showing a fake original menu. Only synthetic cases are
covered by these regressions; execution with retail assets is unverified.

## KAG choice order

The supported KAG subset's `[select]`/`[choices]` tags now sort numbered `option1`, `option2`, `option10` attributes numerically rather than lexicographically, and exclude unrelated fields such as `optionColor`. A synthetic regression test checks choice order and VM resume. This does not provide a TJS2 interpreter or prove NEKOPARA's original menu works.

## Experimental TJS2 → Kotonoha graphics bridge

The bounded TJS2 VM now supports virtual `new Window()`,
`window.primaryLayer` and `new Layer(window[, parentLayer])` objects.
Layer properties include position, size, visibility, enabled state, opacity,
order, `loadImages`, `setPos`, `setSize`, `show` and `hide`. XP3-backed
textures are rendered using parent-relative coordinates.

A **named or inline anonymous** TJS2 function (without lexical closure capture) can be assigned to `layer.onClick`, `layer.onMouseDown`, `layer.onMouseEnter` or `layer.onMouseLeave`. Mouse-down and hover transition events are hit-tested front-to-back and invoke the retained bounded VM.
A callback may choose a `.ks` entry using `Scripts.execStorage` and start
the KAG interpreter. A completely parsed interactive title may replace
the generic fallback; unsupported bootstrap scripts still use the fallback.

```tjs
var window = new Window();
var button = new Layer(window, window.primaryLayer);
button.setPos(100, 80);
button.setSize(320, 100);
button.loadImages("image/title.tlg");
function startGame() { Scripts.execStorage("scenario/start.ks"); }
button.onClick = startGame;
```

This is **not** complete KiriKiri native windowing or the original NEKOPARA
menu. Full closures, keyboard callbacks, continuous mouse-motion callbacks, timers, native plugins and compiled TJS2 bytecode remain unsupported. The compatibility menu is restored if the scripted title cannot render or its callback fails. Tests are synthetic.

## Bounded TJS2 source classes and instance methods

The source subset now accepts `class Name { function Name(...) {...} function method(...) {...} }` with instances, a `this` binding and associated methods. Classes may derive directly from virtual `Layer`, `Window` or `Dictionary` objects. A method reference such as `this.start` can be assigned to `layer.onClick` without losing the receiver.

```tjs
class TitleButton extends Layer {
  function TitleButton(owner) {
    this.setPos(100, 80);
    this.setSize(320, 100);
    this.loadImages("image/title.tlg");
    this.onClick = this.start;
  }
  function start() { Scripts.execStorage("scenario/start.ks"); }
}
var window = new Window();
var button = new TitleButton(window);
```

Explicit limits: 128 classes, 64 methods per class, 256 instances, 2048 bound methods and bounded call depth. Inheritance from other user classes, `super`, lexical closures, native event loops and bytecode remain unsupported. Tests are synthetic and do not establish real-game compatibility.
