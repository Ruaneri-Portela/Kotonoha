#!/usr/bin/env python3
"""Branding regression checks: deterministic geometry, formats, and paths."""
import struct
import zlib
import xml.etree.ElementTree as ET
from pathlib import Path

from generate_brand_assets import DEFAULT_OUTPUT, png, safe_generate
from generate_pwa_icons import main as pwa_main

def valid_png(path,expected):
    data=path.read_bytes()
    assert data[:8]==b"\x89PNG\r\n\x1a\n", path
    assert struct.unpack_from(">II",data,16)==(expected,expected), path
    return data

def _png_rgba_rows(path):
    """Minimal RGBA/8 PNG decoder used only when PIL is unavailable."""
    data=path.read_bytes()
    assert data[:8]==b"\x89PNG\r\n\x1a\n", path
    pos,width,height,idat=8,None,None,bytearray()
    while pos<len(data):
        length=struct.unpack_from(">I",data,pos)[0]
        tag=data[pos+4:pos+8]
        payload=data[pos+8:pos+8+length]
        pos+=12+length
        if tag==b"IHDR":
            width,height,depth,color,_,_,_=struct.unpack(">IIBBBBB",payload)
            assert (depth,color)==(8,6), (path,depth,color)
        elif tag==b"IDAT":
            idat.extend(payload)
        elif tag==b"IEND":
            break
    raw=zlib.decompress(bytes(idat))
    stride=width*4
    rows,prev,offset=[],bytearray(stride),0
    for _ in range(height):
        filt=raw[offset]; offset+=1
        cur=bytearray(raw[offset:offset+stride]); offset+=stride
        for i in range(stride):
            a=cur[i-4] if i>=4 else 0
            b=prev[i]
            c=prev[i-4] if i>=4 else 0
            if filt==1: cur[i]=(cur[i]+a)&255
            elif filt==2: cur[i]=(cur[i]+b)&255
            elif filt==3: cur[i]=(cur[i]+((a+b)>>1))&255
            elif filt==4:
                p=a+b-c
                pa,pb,pc=abs(p-a),abs(p-b),abs(p-c)
                pr=a if pa<=pb and pa<=pc else (b if pb<=pc else c)
                cur[i]=(cur[i]+pr)&255
            else: assert filt==0, (path,filt)
        rows.append(cur)
        prev=cur
    return width,height,rows

def corner_alphas(path):
    try:
        from PIL import Image
        image=Image.open(path).convert("RGBA")
        width,height=image.size
        return [image.getpixel(pt)[3] for pt in
                ((0,0),(width-1,0),(0,height-1),(width-1,height-1))]
    except ImportError:
        width,height,rows=_png_rgba_rows(path)
        return [rows[0][3],rows[0][(width-1)*4+3],
                rows[height-1][3],rows[height-1][(width-1)*4+3]]

def is_full_bleed(path,expected):
    """getbbox() covers the whole canvas and alpha extrema are (255,255)."""
    try:
        from PIL import Image
        image=Image.open(path).convert("RGBA")
        return (image.getbbox()==(0,0,expected,expected)
                and image.getchannel("A").getextrema()==(255,255))
    except ImportError:
        width,height,rows=_png_rgba_rows(path)
        alpha=[row[i] for row in rows for i in range(3,len(row),4)]
        assert (width,height)==(expected,expected), (path,width,height)
        return (min(alpha),max(alpha))==(255,255)

def is_light(path,size):
    """Corners are opaque and clearly light (no dark navy square)."""
    pts=((8,8),(size-9,8),(8,size-9),(size-9,size-9))
    try:
        from PIL import Image
        image=Image.open(path).convert("RGBA")
        samples=[image.getpixel(p) for p in pts]
    except ImportError:
        _,_,rows=_png_rgba_rows(path)
        samples=[tuple(rows[py][px*4:px*4+4]) for px,py in pts]
    return all(min(s[:3])>=150 and s[3]==255 for s in samples)

def verify_favicons(directory):
    """Canonical Kotonoha favicons used by the browser shell and every PWA."""
    path=directory/"favicon.ico"
    data=path.read_bytes()
    assert struct.unpack_from("<HHH",data,0)==(0,1,3), path
    assert [data[6+i*16] for i in range(3)]==[16,32,48], path
    image=directory/"favicon.png"
    valid_png(image,32)
    assert is_full_bleed(image,32) and is_light(image,32), (
        "favicon.png must be a full-bleed light Kotonoha logo")
    assert image.read_bytes()==png(32), (
        "favicon.png must use the canonical Kotonoha monogram")

def verify(root):
    safe_generate(root,"all")
    ico=root/"generic/icon.ico"
    icns=root/"generic/icon.icns"
    assert ico.read_bytes()[:6]==struct.pack("<HHH",0,1,4)
    data=icns.read_bytes()
    assert data[:4]==b"icns" and struct.unpack_from(">I",data,4)[0]==len(data)
    valid_png(root/"generic/icon.png",512)
    assert corner_alphas(root/"generic/icon.png")==[255]*4, (
        "generic/icon.png must have opaque corners on every platform")
    assert is_light(root/"generic/icon.png",512), (
        "generic/icon.png must use the light ice/lilac fill, not a dark square")
    valid_png(root/"apple/AppIcon-1024.png",1024)
    assert is_full_bleed(root/"apple/AppIcon-1024.png",1024), (
        "apple/AppIcon-1024.png must be a full-bleed 1024x1024 PNG")
    assert is_light(root/"apple/AppIcon-1024.png",1024), (
        "apple/AppIcon-1024.png must use the light ice/lilac fill")
    valid_png(root/"apple/AppIcon-180.png",180)
    valid_png(root/"splash/SPLASH.PNG",1024)
    verify_favicons(root/"web")
    shell=(Path(__file__).resolve().parent.parent/
           "buildsystems/cmake/packaging/index.html.in")
    html=shell.read_text(encoding="utf8")
    assert 'rel="icon" href="./favicon.ico" sizes="any"' in html
    assert 'rel="icon" type="image/png" href="./favicon.png"' in html

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
            verify_favicons(out)
    finally:
        sys.argv=original
    assert (root/"pwa/Kotonoha/pwa-icon-192.png").read_bytes() != (
        root/"pwa/RealLive/pwa-icon-192.png").read_bytes()
    print("[Kotonoha/Brand] PNG, ICO, ICNS, Material 3 and PWA checks passed")

if __name__=="__main__":
    import sys
    target=Path(sys.argv[1]) if len(sys.argv)>1 else DEFAULT_OUTPUT
    verify(target)
