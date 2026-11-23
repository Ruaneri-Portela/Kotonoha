#!/usr/bin/env python3
"""Generate brand PNG, ICO, ICNS, Android resources and universal black splash."""
import argparse
import hashlib
import math
import struct
import zlib
from pathlib import Path

NAVY = (13,20,46)
VIOLET = (106,91,231)
PINK = (255,143,189)
CYAN = (0,214,255)
WHITE = (255,255,255)

def blend(a,b,t):
    return tuple(int(max(0,min(255,a[i]*(1-t)+b[i]*t))) for i in range(3))

def distance(x,y,ax,ay,bx,by):
    dx,dy=bx-ax,by-ay
    t=max(0,min(1,((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy)))
    return math.hypot(x-ax-t*dx,y-ay-t*dy)

def k(x,y,n):
    lines=((.375,.31,.375,.69),(.615,.31,.385,.51),(.395,.50,.635,.69))
    d=min(distance(x,y,*line) for line in lines)
    return max(0,min(1,(.041+1/n-d)*n/2))

def flower(x,y,cx,cy,r):
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
        fx=(x-.5)/.55+.5
        fy=(y-.5)/.55+.5
        glow=max(0,1-math.hypot(fx-.5,fy-.5)*1.65)
        rgb=blend((0,0,0),VIOLET,glow*.13)
        return (*blend(rgb,WHITE,k(fx,fy,n*.55)),255)
    if variant in ("foreground","mono"):
        rgb=(255,255,255)
        alpha=0
    else:
        rgb=blend(NAVY,accent,.22+.26*x+.12*(1-y))
        glow=max(0,1-math.hypot(x-.49,y-.52)*1.6)
        rgb=blend(rgb,PINK,glow*.21)
        if y>.78+math.sin(7*x)*.04:
            rgb=blend(rgb,PINK,.29)
        if variant=="apple":
            shine=max(0,1-math.hypot(x-.08,y-.06)*1.4)
            rgb=blend(rgb,CYAN,shine*.45)
            edge=min(x,y,1-x,1-y)
            if .018<edge<.028:
                rgb=blend(rgb,PINK if y>.5 else CYAN,.7)
        alpha=255
        if variant=="generic":
            radius=.19
            dx=max(0,abs(x-.5)-(.5-radius))
            dy=max(0,abs(y-.5)-(.5-radius))
            alpha=int(max(0,min(1,(radius-math.hypot(dx,dy))*n+.5))*255)
    if variant!="mono" and (flower(x,y,.81,.82,.11) or flower(x,y,.93,.89,.063)):
        return (*PINK,255)
    amount=k(x,y,n)
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
    if kind in ("all","apple"):
        for size in (76,120,180,1024):
            save(root,"apple/AppIcon-"+str(size)+".png",png(size,"apple"))
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
    if kind in ("all","desktop","apple","android"):
        save(root,"splash/SPLASH.PNG",png(1024,"splash"))

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--kind",choices=("all","desktop","apple","android"),default="all")
    parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args()
    generate(args.out,args.kind)
    print("[Kotonoha/Brand]",args.kind,args.out)

if __name__=="__main__":
    main()
