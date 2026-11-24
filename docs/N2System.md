# N2System — format and integration status

Current support is made up of the independent modules
`Modules::Providers::N2` and `Modules::VMs::N2System`. **It is neither a
game executable nor a complete NsScript VM.** The tests below use
synthetic data, with no commercial files.

## AFS and NPA archives

The provider reads simple AFS and NPA archives that are **neither
compressed nor encrypted**. File indexing uses names normalized to ASCII
and `/` / `\\` separators, preserving legacy multibyte bytes.

- The parser rejects unsafe names and aliases (`.` / `..` segments,
  repeated slashes, absolute prefixes, `:`, NUL and control characters)
  before adding the file to the index.
- Importing AFS `name,index` aliases remains transactional: an invalid
  record does not add partial names.
- Asset streams allow reading and seeking without duplicating each
  unpacked file; the mounted archive file is still held **entirely in
  memory**.
- Encrypted/compressed NPA is still explicitly rejected.

## NsScript

`ScriptIndex` identifies chapters, labels and calls without executing the
script. Line counting also supports files containing only `CR` (in
addition to `LF` and `CRLF`), including comments and text literals.

The semantics of invocations, transitions, audio, save/load and the full
VM are not implemented yet.

## Tests

In a CMake build with `BUILD_TESTING=ON` and the modules enabled:

```sh
ctest --test-dir build -R 'Kotonoha.N2System.(Archives|ScriptIndex)' \
    --output-on-failure
```

To build the C++17 parsers and their tests directly, without SDL:

```sh
g++ -std=c++17 -Imodules/providers/n2/include \
  modules/providers/n2/src/Archive.cpp \
  modules/providers/n2/tests/ArchiveTest.cpp -o /tmp/n2-archive-test
/tmp/n2-archive-test

g++ -std=c++17 -Imodules/vms/n2system/include \
  modules/vms/n2system/src/ScriptIndex.cpp \
  modules/vms/n2system/tests/ScriptIndexTest.cpp -o /tmp/n2-script-test
/tmp/n2-script-test
```

## Experimental chapter navigation

`ScriptCursor` walks the calls identified by `ScriptIndex` with
`peek()`, `advance()`, `enterChapter()` and `jumpToLabel()` operations.
A missing chapter/label reference does not change the current position.
This enables inspection tools and entry points for a future VM.

**The cursor does not execute NsScript.** Commands inside conditions and
expressions are still indexed in textual order, without branch evaluation,
loops or command semantics. This navigation must not be presented as proof
of playability.

## Experimental NsScript executor (C++17)

`N2System::ScriptExecutor` connects `ScriptCursor` to host callbacks,
with a command budget per `run()` call, explicit pause
(`VmActionKind::Pause` / `resume()`), termination and jumps to chapters
or labels. Instead of running the call list indiscriminately, the
compiler **validates the whole script before the first action**. The
accepted subset is: chapters, labels at the start of a line and direct
function calls with literal arguments (strings or decimal integers),
terminated by a semicolon. Callbacks decide the meaning of each function;
names such as `Show` and `WaitKey` are integration examples, not a
contract claimed for every N2System title.

```cpp
N2System::ScriptExecutor vm(source, "main",
  [&](const N2System::Invocation& call,
      const std::vector<std::string>& args) -> N2System::VmAction {
    if (call.function == "Show") {
      drawText(args.at(0)); // callback implemented by the frontend
      return {};
    }
    if (call.function == "WaitKey")
      return {N2System::VmActionKind::Pause, {}};
    throw std::runtime_error("NsScript command unsupported");
  });
vm.run(1024); // does not block the UI thread indefinitely
// On the next player confirmation:
vm.resume();
vm.run(1024);
```

Any `if` flow, loop, assignment or nested expression is rejected during
pre-processing. This is intentional: N2System still **does not have a
complete NsScript VM** or an integrated game frontend. Standalone test:

```sh
cmake --build build --target KotonohaN2SystemExecutorTests
ctest --test-dir build -R Kotonoha.N2System.Executor --output-on-failure
```

## Executor lifetime safety

`ScriptExecutor` holds a `ScriptCursor` that references the NsScript
index owned by the executor itself. Copies and moves of this object are
now forbidden to avoid dangling pointers when the original instance is
moved or destroyed. The guarantees are verified by compile-time
assertions in the synthetic execution test. The frontend must keep the
executor alive while callbacks or NsScript state are active.

## Experimental subroutine calls

The NsScript executor accepts `CallLabel`, `CallChapter` and `Return`
callback results for calls with return between labels and chapters. The
stack stores the chapter and the exact index of the next instruction;
the limit is 1024 frames, and a missing target does not change the cursor
or the stack. `EnterChapter` (permanent jump) discards pending returns;
`Finish` also clears the stack.

These are **VM actions proposed for frontend integration**, not an
automatic interpretation of the entire NsScript dialect. Real commands,
expressions, loops and conditions still need to be identified and
supported. Synthetic tests cover local calls, cross-chapter calls,
returns, missing targets and excessive recursion.
