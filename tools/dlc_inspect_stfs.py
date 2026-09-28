#!/usr/bin/env python3
import argparse, csv, json, struct, sys
from pathlib import Path

HEADER_SIZE = 0x971A
META_OFF = 0x344
META_SIZE = 0x93D6

CONTENT_TYPES = {
    0x00000001: "SavedGame",
    0x00000002: "MarketplaceContent",
    0x00000003: "Publisher",
    0x00001000: "Xbox360Title",
    0x00007000: "GamesOnDemand",
}
LANGS = [("english",1),("japanese",2),("german",3),("french",4),
         ("spanish",5),("italian",6),("korean",7),("traditional_chinese",8),
         ("portuguese",9),("simplified_chinese",10),("polish",11),("russian",12)]

def be32(b,o): return struct.unpack_from(">I", b, o)[0]
def be64(b,o): return struct.unpack_from(">Q", b, o)[0]

def u16be_z(buf):
    out=bytearray()
    for i in range(0,len(buf)-1,2):
        p=buf[i:i+2]
        if p==b"\x00\x00": break
        out.extend(p)
    return out.decode("utf-16-be", errors="replace")

def lang(meta, lang_id, desc=False):
    if 1 <= lang_id <= 9:
        base = 0x9CD if desc else 0x0CD
        i = lang_id - 1
    elif 10 <= lang_id <= 12:
        base = 0x90D6 if desc else 0x50D6
        i = lang_id - 10
    else:
        return ""
    return u16be_z(meta[base+i*256:base+(i+1)*256])

def xex_version(v):
    return f"{(v>>28)&0xF}.{(v>>24)&0xF}.{(v>>8)&0xFFFF}.{v&0xFF}"

def inspect(path):
    with path.open("rb") as f:
        hdr=f.read(HEADER_SIZE)
    if len(hdr) < HEADER_SIZE:
        raise ValueError("file too small for STFS header")
    magic=hdr[:4].decode("ascii", errors="replace")
    if magic not in ("LIVE","PIRS","CON "):
        raise ValueError(f"invalid STFS magic: {magic!r}")
    m=hdr[META_OFF:META_OFF+META_SIZE]
    ctype=be32(m,0x00)
    ver=be32(m,0x14)
    base=be32(m,0x18)
    names={k:lang(m,n) for k,n in LANGS if lang(m,n)}
    descs={k:lang(m,n,True) for k,n in LANGS if lang(m,n,True)}
    return {
        "file": path.name,
        "size_bytes": path.stat().st_size,
        "magic": magic.strip(),
        "content_type": f"0x{ctype:08X}",
        "content_type_name": CONTENT_TYPES.get(ctype,"Unknown"),
        "metadata_version": be32(m,0x04),
        "declared_content_size": be64(m,0x08),
        "media_id": f"{be32(m,0x10):08X}",
        "version": xex_version(ver),
        "base_version": xex_version(base),
        "title_id": f"{be32(m,0x1C):08X}",
        "publisher": u16be_z(m[0x12CD:0x134D]),
        "title_name": u16be_z(m[0x134D:0x13CD]),
        "display_names": names,
        "descriptions": descs,
    }

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("target", nargs="?", default="Data/DLC")
    ap.add_argument("--json", default="DLC_REPORT.json")
    ap.add_argument("--csv", default="DLC_REPORT.csv")
    a=ap.parse_args()
    p=Path(a.target)
    files=[p] if p.is_file() else sorted(x for x in p.iterdir() if x.is_file())
    out=[]
    for f in files:
        try:
            r=inspect(f); out.append(r)
            print(f"[OK] {r['file']}")
            print(f"  {r['display_names'].get('english','(no English name)')}")
            print(f"  TitleID={r['title_id']} Type={r['content_type_name']} Size={r['size_bytes']}")
        except Exception as e:
            print(f"[ERR] {f.name}: {e}", file=sys.stderr)
    Path(a.json).write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding="utf-8")
    with open(a.csv,"w",encoding="utf-8-sig",newline="") as fh:
        cols=["file","size_bytes","magic","content_type","content_type_name","metadata_version",
              "declared_content_size","media_id","version","base_version","title_id",
              "publisher","title_name","display_en","display_es"]
        w=csv.DictWriter(fh,fieldnames=cols); w.writeheader()
        for r in out:
            w.writerow({**{k:r.get(k,"") for k in cols},
                        "display_en":r["display_names"].get("english",""),
                        "display_es":r["display_names"].get("spanish","")})
    return 0 if out else 1

if __name__=="__main__":
    raise SystemExit(main())
