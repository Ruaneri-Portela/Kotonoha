#!/usr/bin/env python3
"""Regenerate the original, minimal SVG-glyph test font (requires fonttools)."""

from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import newTable
from fontTools.ttLib.tables.S_V_G_ import SVGDocument

builder = FontBuilder(1000, isTTF=True)
builder.setupGlyphOrder([".notdef", "svg_square"])
builder.setupCharacterMap({0xE000: "svg_square"})
builder.setupGlyf({name: TTGlyphPen(None).glyph() for name in builder.font.getGlyphOrder()})
builder.setupHorizontalMetrics({".notdef": (1000, 0), "svg_square": (1000, 0)})
builder.setupHorizontalHeader(ascent=800, descent=-200)
builder.setupNameTable(
    {
        "familyName": "Kotonoha SVG Test",
        "styleName": "Regular",
        "uniqueFontIdentifier": "KotonohaSVGTest-Regular",
        "fullName": "Kotonoha SVG Test Regular",
        "psName": "KotonohaSVGTest-Regular",
        "copyright": "Dedicated to the public domain (CC0 1.0).",
    }
)
builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
builder.setupPost()
builder.setupMaxp()

svg = newTable("SVG ")
svg.docList = [
    SVGDocument(
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 -800 1000 1000">'
        '<g id="glyph1"><rect x="50" y="-750" width="900" height="900" '
        'fill="#ff0000"/></g></svg>',
        1,
        1,
    )
]
builder.font["SVG "] = svg
builder.font.recalcTimestamp = False
builder.font["head"].created = 0
builder.font["head"].modified = 0
builder.save(Path(__file__).with_name("SvgTestFont.ttf"))
