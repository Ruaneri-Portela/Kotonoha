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

## KAG parser compatibility (Kirikiroid2Yuri reference)

The KAG scenario parser now recognizes line-control markers after leading tabs or
spaces, preserves line/column information, rejects an unnamed first bare label,
and assigns duplicate bare labels stable `name:2`, `name:3` aliases
(including `*|page title` for the previous name). It also rejects unclosed
brackets and unterminated quoted attributes instead of silently consuming the
rest of a malformed scenario. These behaviors are covered by synthetic
regression tests and are informed by the Kirikiroid2Yuri/Kirikiri KAG parser.
No third-party runtime code has been copied.

This is a focused compatibility improvement, **not full KAG/TJS2 parity**.
The original TJS2 VM, native plugins and complete rendering/event behavior
remain separate longer-term work.

## Inline TJS blocks and ignore conditions

KAG parsing now treats the contents of `[iscript]` / `@iscript` as
opaque TJS source until a standalone `[endscript]` / `@endscript`
line, instead of interpreting embedded brackets as KAG commands or
dialogue. Unterminated inline scripts fail with an actionable error.
The TJS body is deliberately **not executed** by this limited KAG VM.

Command tag names are case-insensitive (as in Kirikiri), while labels
retain their original case. The runtime now recognizes
`[ignore exp=...]` / `[endignore]` with nested conditional exclusion,
using the inverse of `[if]` behavior. Synthetic regression tests cover
both features; native TJS2 integration remains a separate task.

## KAG inline TJS2 execution

KAG `[iscript]` and `@iscript` now retain their source and pass it to the
bounded TJS2 interpreter on the runtime thread, sharing variables and
script-defined Window/Layer objects across inline blocks and the startup VM.
`Scripts.execStorage` requests a scenario transition through the normal
KAG queue. Unsupported TJS features are reported without executing native
plugins; the script parser still recognizes exact standalone end markers.
Synthetic tests verify consecutive blocks, shared values and diagnostics.
This does not imply parity with full Kirikiri2/KirikiriZ TJS2 bytecode.

## Bounded KAG macro support

The parser now processes static `[macro name=...]`/`[endmacro]` declarations,
expands macro calls with `%parameter` and `%parameter|default` arguments,
and recomputes label PCs after expansion. Recursion, nested declarations,
malformed/missing end tags and expansion beyond explicit budgets fail with a
clear error. These cover common macro-driven KAG menu/action snippets but do
not reproduce every dynamic KiriKiri macro and TJS2 binding behavior.

## TJS2 text-only menu Layers

Script-created `Layer.drawText(x, y, text[, color[, opacity]])` commands
are now captured as bounded text runs, projected through the Layer hierarchy,
and rendered with Kotonoha's GPU-backed VN UI text path. `Layer.font.height`
and `Layer.clearText()` are supported. A title with text-only interactive
layers is eligible for the scripted menu instead of needing image textures.
These changes cover a source-level subset, not every KiriKiri native font or
canvas drawing operation. The menu test is synthetic.

## Scripted menu mouse-release handlers

Layer `onMouseUp` events are now retained, hit-tested front-to-back,
executed on left-button release, and can select a KAG scenario just like
existing `onClick`/mouse-down handlers. The top-level runtime routes these
through the same fallback and diagnostic logic as press events. Synthetic
coverage checks release-only menu buttons and hit testing.

## Original menu keyboard events

Source-created `Window.onKeyDown` handlers now receive bounded virtual-key
values for Enter, Escape, Space, and directional arrows. UI state and KAG
scene requests are refreshed after each callback; unsupported arities are
reported. Source-only menu tests check both ignored and recognized keys.
Keyboard routing is a subset of the KiriKiri Windows-key event model and
is not intended to imply full native platform input parity.

## Script Layer.fillRect panels

TJS2 title scripts may now construct colored menu panels with
`Layer.fillRect(x,y,width,height,color[,opacity])`. Rectangle operations are
bounded, projected with their parent layers and drawn using SDL's GPU-backed
render geometry before associated text. Native `Layer.drawText` remains on
Kotonoha's UI text path. The implementation does not yet emulate all
KiriKiri2 Layer bitmap operations, masks, blend modes or draw ordering.

## Original title priority versus VNX+ fallback

When a source `startup.tjs` completes the supported subset and produces
visible image/text/rectangle layers plus valid pointer/keyboard callbacks,
Kotonoha now **prefers the script-created title** over a hard-coded VNX+ title
screen. The overlay's Save/Load/Config and story UI remain available once a
scenario starts. If script evaluation or rendering fails, the existing VNX+
compatibility title is used instead. This is a presentation priority fix and
does not make incomplete TJS2 bytecode or native plugins work.

## Native menu pointer movement

Source-created `Layer.onMouseMove` callbacks now receive bounded pointer
coordinates via hit testing against the topmost active layer. The retained
TJS2 VM can update its scene graph during rollover, with the same runtime
fallback/error checks used for click and keyboard handlers. A synthetic test
checks that movement outside a button does not invoke it.

## Bounded TJS2 menu event budgets

Mouse click, mouse release, mouse movement, hover transitions and key-down
handlers now receive a fresh per-event bounded instruction allowance instead
of consuming the single startup budget forever. This prevents ordinary title
interaction from slowly exhausting the VM while still terminating one event
handler that loops excessively. Synthetic coverage exercises 100 consecutive
movement events with a deliberately small 24-instruction budget.

## KAG visual layer persistence and ordering

KAG `[layopt]` now changes the existing layer's visibility, coordinates,
opacity and optional draw order without destroying its image. Explicit
`[freeimage]` remains destructive. Foreground sprites are drawn in numeric
layer/order sequence, fixing the previous `1,10,2` lexical ordering, and
SDL texture alpha modulation is restored after each frame. Synthetic tests
cover retained layer state, visibility restoration and invalid properties.
This remains a front-page subset; backlay/forelay transitions and blend modes
still need dedicated compatibility work.
