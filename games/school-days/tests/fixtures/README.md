# Synthetic SchoolDays media fixtures

These are newly generated test media, not School Days commercial assets:
blue 64×64 PNG, red 64×64 24 Hz WMV2/WMA2 movie with a 440 Hz tone, and a
220 Hz OGG/Vorbis tone (reused for voice and BGM intro/loop). Duration: 2 seconds.

Generated using FFmpeg lavfi `color`/`sine`, its `wmv2`/`wmav2`, `png` and native
`vorbis -strict experimental` encoders. The ORS fixture is authored solely for
testing NeoStart's media/asset-provider adapter. No FFmpeg executable is needed
to run these tests; the existing engine decoders read the fixtures.

The additional A03 script exercises the custom timed-choice scene with two
synthetic options, without loading retail dialogue or artwork.
