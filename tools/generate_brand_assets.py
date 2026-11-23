#!/usr/bin/env python3
"""Generate brand PNG, ICO, ICNS, Android resources and universal black splash.

Output defaults to design/generated/ at the repository root: the versioned
source of truth for Kotonoha's visual identity. Buildsystems only import the
specific files they need from there; the folder itself is never packaged.
Use --out only for isolated tests or one-off exports.
"""
import argparse
import hashlib
import math
import struct
import zlib
import os
import time
from pathlib import Path

# Versioned source of truth, independent of the current working directory and
# of any ephemeral build directory.
DEFAULT_OUTPUT = Path(__file__).resolve().parent.parent / "design" / "generated"

NAVY = (13,20,46)
VIOLET = (106,91,231)
PINK = (255,143,189)
CYAN = (0,214,255)
WHITE = (255,255,255)
# Light icon fill: icy blue -> lilac, with a deeper sakura wave and an indigo
# contour that keeps the white monogram readable on the bright background.
ICE = (196,222,255)
LILAC = (205,188,252)
WAVE = (236,106,165)
HALO = (74,52,150)

def blend(a,b,t):
    return tuple(int(max(0,min(255,a[i]*(1-t)+b[i]*t))) for i in range(3))

def distance(x,y,ax,ay,bx,by):
    dx,dy=bx-ax,by-ay
    t=max(0,min(1,((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy)))
    return math.hypot(x-ax-t*dx,y-ay-t*dy)

def k(x,y,n):
    if x<.30 or x>.71 or y<.24 or y>.76:
        return 0
    lines=((.375,.31,.375,.69),(.615,.31,.385,.51),(.395,.50,.635,.69))
    d=min(distance(x,y,*line) for line in lines)
    return max(0,min(1,(.041+1/n-d)*n/2))

def flower(x,y,cx,cy,r):
    # Cull 98% of pixels before costly flower geometry.
    if abs(x-cx)>r*1.5 or abs(y-cy)>r*1.5:
        return False
    for i in range(5):
        angle=-math.pi/2 + i*2*math.pi/5
        co,si=math.cos(angle),math.sin(angle)
        dx=x-cx-co*r*.60
        dy=y-cy-si*r*.60
        if ((dx*co+dy*si)/(r*.7))**2+((-dx*si+dy*co)/(r*.43))**2<1:
            return True
    return math.hypot(x-cx,y-cy)<r*.2

def pixel(x,y,n,variant,accent):
    if variant=="splash":
        # Pure black outside the centered emblem, independent of display ratio.
        # Keep any glow INSIDE the icon; the surrounding pixels are #000000.
        fx=(x-.5)/.38+.5
        fy=(y-.5)/.38+.5
        if fx<0 or fx>1 or fy<0 or fy>1:
            return (0,0,0,255)
        radius=.19
        dx=max(0,abs(fx-.5)-(.5-radius))
        dy=max(0,abs(fy-.5)-(.5-radius))
        mask=max(0,min(1,(radius-math.hypot(dx,dy))*n*.38+.5))
        if mask<=0:
            return (0,0,0,255)
        r,g,b,_=pixel(fx,fy,max(32,int(n*.38)),"apple",accent)
        return (*blend((0,0,0),(r,g,b),mask),255)
    if variant in ("foreground","mono"):
        rgb=(255,255,255)
        alpha=0
    else:
        rgb=blend(ICE,LILAC,.12+.55*(1-y)+.18*x)
        glow=max(0,1-math.hypot(x-.49,y-.52)*1.5)
        # Per-game accent survives as a soft, heavily-lightened centre glow.
        rgb=blend(rgb,blend(accent,WHITE,.55),glow*.22)
        if y>.78+math.sin(7*x)*.04:
            rgb=blend(rgb,WAVE,.28)
        if variant=="apple":
            shine=max(0,1-math.hypot(x-.08,y-.06)*1.4)
            rgb=blend(rgb,CYAN,shine*.18)
            edge=min(x,y,1-x,1-y)
            if .018<edge<.028:
                rgb=blend(rgb,PINK if y>.5 else CYAN,.28)
        # Full-bleed: the background fills the whole canvas for every variant.
        # Transparent corners made macOS composite a white edge around icons.
        # The K geometry (k()) is intentionally unchanged.
        alpha=255
    if variant!="mono" and (flower(x,y,.81,.82,.11) or flower(x,y,.93,.89,.063)):
        return (*PINK,255)
    amount=k(x,y,n)
    if variant not in ("foreground","mono") and .28<x<.73 and .22<y<.78:
        o=.011
        halo=max(k(x+dx,y+dy,n) for dx,dy in
                 ((o,o),(-o,o),(o,-o),(-o,-o)))
        rgb=blend(rgb,HALO,halo*.82)
    return (*blend(rgb,WHITE,amount),int(max(alpha,255*amount)))

def png(n,variant="generic",accent=VIOLET):
    raw=bytearray()
    for yi in range(n):
        raw.append(0)
        y=(yi+.5)/n
        for xi in range(n):
            raw.extend(pixel((xi+.5)/n,y,n,variant,accent))
    def chunk(tag,payload):
        return struct.pack(">I",len(payload))+tag+payload+struct.pack(">I",zlib.crc32(tag+payload)&0xffffffff)
    return (b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",n,n,8,6,0,0,0))+
            chunk(b"IDAT",zlib.compress(bytes(raw),9))+chunk(b"IEND",b""))

def ico(images):
    entries=bytearray()
    blobs=bytearray()
    offset=6+16*len(images)
    for n,data in images:
        entries.extend(struct.pack("<BBBBHHII",n if n<256 else 0,n if n<256 else 0,0,0,1,32,len(data),offset+len(blobs)))
        blobs.extend(data)
    return struct.pack("<HHH",0,1,len(images))+entries+blobs

def icns(images):
    blocks=b"".join(tag.encode()+struct.pack(">I",len(data)+8)+data for tag,data in images)
    return b"icns"+struct.pack(">I",len(blocks)+8)+blocks

def favicon_assets():
    """Browser favicons from the same monogram as the desktop icon."""
    sizes=(16,32,48)
    images={size:png(size) for size in sizes}
    return ico([(size,images[size]) for size in sizes]), images[32]

def save(root,path,data):
    target=root/path
    target.parent.mkdir(parents=True,exist_ok=True)
    if isinstance(data,bytes): target.write_bytes(data)
    else: target.write_text(data,encoding="utf8")

def generate(root,kind):
    if kind in ("all","desktop"):
        sizes=(16,32,48,64,128,256,512,1024)
        images={size:png(size) for size in sizes}
        save(root,"generic/icon.png",images[512])
        save(root,"generic/icon.ico",ico([(size,images[size]) for size in (16,32,48,256)]))
        save(root,"generic/icon.icns",icns([(tag,images[size]) for tag,size in
            (("icp4",16),("icp5",32),("icp6",64),("ic07",128),("ic08",256),("ic09",512),("ic10",1024))]))
        # Full-bleed 1024 PNG, ready for Icon Composer -> .icon export.
        save(root,"apple/AppIcon-1024.png",images[1024])
    if kind in ("all","apple"):
        for size in (76,120,180,1024):
            save(root,"apple/AppIcon-"+str(size)+".png",png(size,"apple"))
    if kind in ("all","desktop","web"):
        favicon_ico,favicon_png=favicon_assets()
        save(root,"web/favicon.ico",favicon_ico)
        save(root,"web/favicon.png",favicon_png)
    if kind in ("all","android"):
        for suffix,size in (("mdpi",48),("hdpi",72),("xhdpi",96),("xxhdpi",144),("xxxhdpi",192)):
            for name in ("ic_launcher","ic_launcher_round"):
                save(root,"android/res/mipmap-"+suffix+"/"+name+".png",png(size,"android"))
        save(root,"android/res/drawable-nodpi/ic_launcher_foreground.png",png(432,"foreground"))
        save(root,"android/res/drawable-nodpi/ic_launcher_monochrome.png",png(432,"mono"))
        save(root,"android/res/drawable-nodpi/kotonoha_splash_mark.png",png(256,"apple"))
        for name in ("ic_launcher","ic_launcher_round"):
            content=('<?xml version="1.0" encoding="utf-8"?>\n'
                '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
                '  <background android:drawable="@color/kotonoha_icon_background"/>\n'
                '  <foreground android:drawable="@drawable/ic_launcher_foreground"/>\n'
                '  <monochrome android:drawable="@drawable/ic_launcher_monochrome"/>\n'
                '</adaptive-icon>\n')
            save(root,"android/res/mipmap-anydpi-v26/"+name+".xml",content)
    if kind in ("all","desktop","apple","android","web"):
        save(root,"splash/SPLASH.PNG",png(1024,"splash"))

def source_digest():
    return hashlib.sha256(Path(__file__).read_bytes()).hexdigest()

def safe_generate(root,kind):
    # CMake/Xcode/Gradle can invoke the generator concurrently and all read
    # the same versioned design/generated/ tree. Use a lock and a source stamp
    # (mtime + digest) so no build reads a partially rendered ICNS and any
    # script edit always regenerates its outputs.
    root.mkdir(parents=True,exist_ok=True)
    lock=root/".kotonoha-brand.lock"
    for attempt in range(3000):
        try:
            lock.mkdir()
            break
        except FileExistsError:
            try:
                if time.time()-lock.stat().st_mtime>600:
                    lock.rmdir()  # Recover only an empty, abandoned lock.
                    continue
            except OSError:
                pass
            time.sleep(.1)
    else:
        raise RuntimeError("Timed out acquiring Kotonoha brand output lock")
    try:
        stamp=root/(".kotonoha-"+kind+".complete")
        digest=source_digest()
        if (stamp.is_file()
                and stamp.stat().st_mtime>=Path(__file__).stat().st_mtime
                and stamp.read_text(encoding="ascii").strip()==digest):
            return
        generate(root,kind)
        stamp.write_text(digest+"\n",encoding="ascii")
    finally:
        lock.rmdir()

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--kind",choices=("all","desktop","apple","android","web"),default="all")
    parser.add_argument("--out",type=Path,default=DEFAULT_OUTPUT,
                        help="output root (default: design/generated/)")
    args=parser.parse_args()
    safe_generate(args.out,args.kind)
    print("[Kotonoha/Brand]",args.kind,args.out)

if __name__=="__main__":
    main()
