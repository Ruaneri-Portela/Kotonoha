# Original message-window composition and auto mode

The DVD `pmail.exe` and `locpu.dll` were inspected with REA's
`execute-rizin-command`; instruction boundaries were cross-checked with LLVM
objdump where Rizin linear disassembly lost alignment.

- `pmail.exe:0x40a89b..0x40a8f0` draws WAKUH, WAKU and BOT in that order.
  WAKU is only the border. Omitting WAKUH leaves the message background empty.
  Each CHAR supplies its own coordinates; the engine uses 800×600 game units.
- `0x415153..0x415195` loads WAKUH and applies the configured message-window
  RGB adjustment through `0x4178d0`. The config defaults table at `0x440480`
  contains `MesWin_R=0`, `MesWin_G=0`, `MesWin_B=150` (entries 10–12,
  20-byte strings). Configuration loading is in `0x4156f0`.
- The `locpu.dll` AddRgbChr export (`0x100018f0`, implementation `0x10002800`)
  computes `channel + (((255 - channel) * adjustment) >> 8)`. It preserves
  the CHAR alpha. A black pixel becomes RGB `(0,0,149)`, not `(0,0,150)`.
  The port applies this once to the decoded WAKUH texture, before blending.
- `pmail.exe:0x41b1f7..0x41b254` waits for voice completion before starting
  the auto delay. Defaults are `AutoMessageVoiceEnd=1` and
  `AutoMessageWait=2000` milliseconds (config table entries 7 and 6).
- The original dialogue wait uses CURSOR01 frames `0,1,2,3,2,1` with 170ms
  per frame. Auto switches to CURSOR02 frames `0,1,2,3` with the same period.
  Native auto toggling is at `0x419034`; the port additionally exposes `A`
  as an input shortcut and a component API for menu integration.

`PmailTextBoxCheck` compares a rendered 800×600 window against CPU composition
of the three retail layers, including their original alpha and default tint.
It also checks that auto cannot interrupt voice, cannot advance behind a menu
or hidden textbox, consumes the first restoring click, and waits the full
post-voice delay before advancing. The test writes `pmail-textbox.bmp` to the
system temporary directory for visual inspection.
