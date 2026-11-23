#!/usr/bin/env python3
"""Per-game installable PWA icons, preserving the canonical Kotonoha K."""
import hashlib
import sys
from pathlib import Path
from generate_brand_assets import favicon_assets, png

PALETTE=((37,99,235),(14,165,163),(124,58,237),(220,69,104),
         (21,128,129),(202,111,33),(68,105,174),(130,75,135))

def main():
    if len(sys.argv)!=3 or not sys.argv[1].strip():
        print("Usage: generate_pwa_icons.py <target> <output-directory>",file=sys.stderr)
        return 2
    target,directory=sys.argv[1],Path(sys.argv[2])
    directory.mkdir(parents=True,exist_ok=True)
    color=PALETTE[hashlib.sha256(target.encode("utf8")).digest()[0]%len(PALETTE)]
    for size in (180,192,512):
        (directory/("pwa-icon-"+str(size)+".png")).write_bytes(png(size,"generic",color))
    # Browser tab/shortcut icons use the canonical Kotonoha logo from
    # design/generated/web/ (favicon_assets is the same generator used there),
    # not the per-game PWA accent, and match the shell's rel="icon" links.
    # Each PWA output directory gets its own copy; design/ is never packaged.
    favicon_ico,favicon_png=favicon_assets()
    (directory/"favicon.ico").write_bytes(favicon_ico)
    (directory/"favicon.png").write_bytes(favicon_png)
    print("[Kotonoha PWA] Branded icons for",target)
    return 0

if __name__=="__main__":
    sys.exit(main())
