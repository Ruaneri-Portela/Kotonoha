School Days HQ / Kotonoha v3 - External Option Artwork

The OPTION assets under System/Option are custom user-supplied replacements.
The retail Packs/System.GPK is never edited or repacked.

General (all copied byte-for-byte from Downloads/editados):
- System/Option/Option_Def.png        (800x450)
- System/Option/Option_Def_Chip.png   (684x436)

Sound (all copied byte-for-byte from Downloads/editados):
- System/Option/Option_Sound.png      (800x450)
- System/Option/Option_Sound_Chip.png (684x354)

The original retail CMAP files for General and Sound remain authoritative,
as do the game's existing control/hover/selected state tables.

Display (separately supplied user art and generated interaction maps):
- System/Option/Option_Display.png       (800x450)
- System/Option/Option_Display_Chip.png  (684x1212)
- System/Option/Option_Display[variant].cmap (4 variants)

The School Days Windows build copies this whole overlay directory into
<SchoolDays.exe directory>/display-options every time the SchoolDays target
is built (including when no C++ source has changed). The game automatically
mounts it in preference to assets inside the original GPKs, unless an
explicit alternate --asset-overlay was selected. The other Kotonoha games
are not affected. Any explicitly chosen alternative overlay must include the
Option replacement PNGs if those overrides are wanted.

See games/school-days/docs/configuration/GENERAL_SOUND_EXTERNAL_ART_2026-11-05.md.
