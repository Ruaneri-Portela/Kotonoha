# TITLE_AC_CHIP.PNG in School Days HQ v1.02

The retail `System.GPK` contains `System/TITLE/TITLE_AC.PNG`,
`TITLE_AC_CHIP.PNG`, and the matching CMAP variants. The v3 title model
recognizes this resource family: `SchoolDaysTitleModel::SelectFamily` selects
`TitleAc` when `HostState::ac` is true, and `ChipStem(TitleAc)` resolves
`System/Title/Title_AC_Chip`.

The installed original EXE has SHA-256
`e4e09563f44a57a023884fb8f023138a4e2168b9fe174b363fe1cfcfb3a98e26`.
The preserved research report
`REA_WORK/evidence/full-reconstruction-study/R7-STATIC-VALIDATION.md`
records the native title gate at host `+0xE8`, backed by the secondary
`FILMEngine` value at outer `+0x7A4`. The normal constructor at `0x004217E0`
initializes this value to zero. The alternate constructor at `0x00421B30`
queries a provider named `CrossDays`, but the shipped EXE analysis found no
callers or incoming references to that constructor. The AC title and selector
1 are therefore dormant in standalone School Days HQ v1.02.

`SchoolDays::Runtime::openTitle` leaves `HostState::ac` false. This matches
the reachable original path. Enabling the image on an AllClear or completion
flag would invent behavior that the original EXE does not establish. A future
CrossDays integration would need an authenticated host provider and selector
1 route before this gate should become active.
