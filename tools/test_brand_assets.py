#!/usr/bin/env python3
"""Branding regression checks: deterministic geometry, formats, and paths."""
import struct
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

from generate_brand_assets import safe_generate
from generate_pwa_icons import main as pwa_main

def valid_png(path,expected):
    data=path.read_bytes()
    assert data[:8]==b"\x89PNG\r\n\x1a\n", path
    assert struct.unpack_from(">II",data,16)==(expected,expected), path
    return data

def verify(root):
    safe_generate(root,"all")
    ico=root/"generic/icon.ico"
    icns=root/"generic/icon.icns"
    assert ico.read_bytes()[:6]==struct.pack("<HHH",0,1,4)
    data=icns.read_bytes()
    assert data[:4]==b"icns" and struct.unpack_from(">I",data,4)[0]==len(data)
    valid_png(root/"generic/icon.png",512)
    valid_png(root/"apple/AppIcon-1024.png",1024)
    valid_png(root/"apple/AppIcon-180.png",180)
    valid_png(root/"splash/SPLASH.PNG",1024)

    for density,size in (("mdpi",48),("hdpi",72),("xhdpi",96),
                         ("xxhdpi",144),("xxxhdpi",192)):
        valid_png(root/f"android/res/mipmap-{density}/ic_launcher.png",size)
        valid_png(root/f"android/res/mipmap-{density}/ic_launcher_round.png",size)
    for name in ("ic_launcher","ic_launcher_round"):
        path=root/f"android/res/mipmap-anydpi-v26/{name}.xml"
        doc=ET.parse(path).getroot()
        assert doc.tag.endswith("adaptive-icon")
        assert {c.tag.split("}")[-1] for c in doc} == {"foreground","background","monochrome"}
    valid_png(root/"android/res/drawable-nodpi/ic_launcher_monochrome.png",432)
    valid_png(root/"android/res/drawable-nodpi/ic_launcher_foreground.png",432)

    # Incremental and parallel-safe repeat invocation.
    before=(root/"generic/icon.png").stat().st_mtime_ns
    safe_generate(root,"all")
    assert (root/"generic/icon.png").stat().st_mtime_ns == before

    # Existing PWA contract: stable K, varied accents, installable PNGs.
    import sys
    original=sys.argv
    try:
        for game in ("Kotonoha","RealLive"):
            out=root/"pwa"/game
            sys.argv=["generate_pwa_icons.py",game,str(out)]
            assert pwa_main()==0
            for size in (180,192,512):
                valid_png(out/f"pwa-icon-{size}.png",size)
    finally:
        sys.argv=original
    assert (root/"pwa/Kotonoha/pwa-icon-192.png").read_bytes() != (
        root/"pwa/RealLive/pwa-icon-192.png").read_bytes()
    print("[Kotonoha/Brand] PNG, ICO, ICNS, Material 3 and PWA checks passed")

if __name__=="__main__":
    import sys
    target=Path(sys.argv[1]) if len(sys.argv)>1 else Path("build/branding")
    verify(target)
