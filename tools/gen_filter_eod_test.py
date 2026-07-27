#!/usr/bin/env python3
"""
Generate tests/filter_eod_test.ps — a self-contained regression test for the
"a decoder must consume its EOD so a fresh filter on the same currentfile stream
stays in sync" bug class (the dvips/colorimage scanline desync).

Each scanline uses the exact dvips idiom, with the reading operator LAST on its
line and the encoded data on the next line -- so that when `readstring` runs,
currentfile is positioned exactly at the data (xpost tokenises currentfile in
step, so an operator mid-line would read from mid-line):
    currentfile /ASCII85Decode filter [/InnerDecode filter] <len> string readstring
    <ASCII85(inner_encode(scanline))~>>
    pop <expected> ck
The buffer is sized to the scanline's exact length, so readstring stops on a full
buffer WITHOUT reading the EOD -- staying in sync then depends entirely on eager
EOD consumption inside each decoder.  After the last scanline a plain token is read
straight from currentfile; any residual desync makes the scanner choke on it.

Sizes and contents hug the boundaries where the desync bit in practice:
  - ASCII85 'z' zero-group abbreviation (all-zero scanlines)          -> a85 z-path
  - ASCII85 full 5-char group at a 4-byte boundary                    -> a85 group path
  - Flate/LZW/DCT 4096-byte output buffer and its multiples           -> trailer/EOD
  - RunLength 128-byte run cap and repeat runs                        -> run EOD
Emitted data is printable ASCII85, so the file is text-clean and needs no external
data.  Regenerate with: python3 tools/gen_filter_eod_test.py
"""
import base64, zlib, subprocess, sys

def a85(d): return base64.a85encode(d) + b"~>"

def rle(d):
    out=bytearray(); i=0; n=len(d)
    while i<n:
        run=1
        while i+run<n and d[i+run]==d[i] and run<128: run+=1
        if run>=2: out.append(257-run); out.append(d[i]); i+=run
        else:
            j=i; lit=bytearray()
            while j<n and len(lit)<128:
                if j+2<n and d[j]==d[j+1]==d[j+2]: break
                lit.append(d[j]); j+=1
            out.append(len(lit)-1); out+=lit; i=j
    out.append(128); return bytes(out)

def flate(d): return zlib.compress(d,6)

def lzw_gs(d):
    inb="/tmp/_lzwgen.bin"; open(inb,"wb").write(d)
    prog=("/i (%s)(r)file def /o (%%stdout)(w)file def /f o /LZWEncode filter def "
          "{ i read { f exch write } { exit } ifelse } loop f closefile o flushfile quit"%inb)
    r=subprocess.run(["gs","-q","-dNOSAFER","-dBATCH","-dNODISPLAY","-c",prog],capture_output=True)
    if r.returncode!=0 or not r.stdout:
        raise RuntimeError("gs LZWEncode failed: "+r.stderr.decode('latin1','replace'))
    return r.stdout

def content(kind,n,seed):
    # zero/ff/markers carry the byte values that stress content-dependent paths and
    # are used only at small sizes (cheap to embed).  'mix' stays in the printable
    # range so large scanlines embed as compact literals -- the decoded bytes are
    # opaque to the EOD logic, only the *encoded* stream's terminator matters.
    if kind=='zero': return bytes(n)
    if kind=='ff':   return bytes([0xFF])*n
    if kind=='markers':
        m=bytes([0x7e,0x3e,0x80,0x00,0x20,0x0a,0x7a,0xff,0x21,0x09,0x0d])
        return bytes(m[(seed*7+k*5)%len(m)] for k in range(n))
    return bytes(0x21 + ((seed*37+k*3) % (0x7e-0x21)) for k in range(n))  # 'mix'

def inner_bytes(chain,d):
    if chain=="":                        return d
    if "ASCIIHexDecode"  in chain:       return d.hex().encode()+b">"
    if "RunLengthDecode" in chain:       return rle(d)
    if "FlateDecode"     in chain:       return flate(d)
    if "LZWDecode"       in chain:       return lzw_gs(d)
    raise ValueError(chain)

# (size, content-kind) per chain -- targeted at each decoder's EOD boundary
CHAINS=[
 ("plain ASCII85", "", [
    (4,'zero'),(64,'zero'),(4096,'zero'),                 # z-abbreviation path
    (4,'mix'),(8,'mix'),(64,'mix'),(4096,'mix'),          # full 5-char groups
    (5,'mix'),(65,'mix'),(129,'mix'),(7,'markers')]),     # partial final groups
 ("ASCII85+ASCIIHex", " /ASCIIHexDecode filter", [
    (1,'mix'),(2,'mix'),(64,'mix'),(65,'mix'),(128,'markers')]),
 ("ASCII85+RunLength", " /RunLengthDecode filter", [
    (1,'mix'),(2,'mix'),(127,'mix'),(128,'mix'),(129,'mix'),(256,'mix'),
    (300,'zero'),(256,'ff'),(128,'ff')]),                 # long/repeat runs, 128 cap
 ("ASCII85+Flate", " /FlateDecode filter", [
    (1,'mix'),(128,'mix'),(4095,'mix'),(4096,'mix'),(4097,'mix'),(8192,'mix'),
    (4096,'zero')]),                                       # decompressed len == buffer multiple
 ("ASCII85+LZW", " /LZWDecode filter", [
    (1,'mix'),(2,'mix'),(3,'mix'),(5,'mix'),(64,'mix'),(127,'mix'),(128,'mix'),
    (255,'mix'),(256,'mix'),(257,'mix'),(4096,'mix')]),
]

def pslit(b):
    out=bytearray(b"(")
    for c in b:
        if c==0x28: out+=b"\\("
        elif c==0x29: out+=b"\\)"
        elif c==0x5c: out+=b"\\\\"
        elif 0x20<=c<0x7f: out.append(c)
        else: out+=("\\%03o"%c).encode()
    out+=b")"; return bytes(out)

def gen():
    L=["% GENERATED by tools/gen_filter_eod_test.py -- do not edit by hand.",
       "% Each decoder must consume its EOD so the next fresh filter reading the same",
       "% currentfile stream stays in sync (the dvips/colorimage scanline idiom).",
       "% Prints SUCCESS iff every scanline round-trips and the trailing token resyncs.",
       "/failed false def",
       "/ck { eq not { /failed true def } if } bind def",""]
    n=0; seed=1
    for title,chain,rows in CHAINS:
        L.append(f"% ---- {title} ----")
        for size,kind in rows:
            seed+=1
            data=content(kind,size,seed)
            payload=a85(inner_bytes(chain,data)).decode("latin1")
            L.append(f"/exp{n} {pslit(data).decode('latin1')} def")
            L.append(f"currentfile /ASCII85Decode filter{chain} {size} string readstring")
            L.append(payload)
            L.append(f"pop exp{n} ck")
            n+=1
        L.append("")
    L.append("% ---- sentinel: the scanner must resync cleanly after the last filter ----")
    L.append("currentfile token")
    L.append("987654321")
    L.append("pop 987654321 eq not { /failed true def } if")
    L.append("")
    L.append(f"% {n} scanlines across {len(CHAINS)} filter chains")
    L.append("failed { (FAILURES in filter EOD checks) = }{ (SUCCESS) = } ifelse")
    return "\n".join(L)+"\n"

if __name__=="__main__":
    dst=sys.argv[1] if len(sys.argv)>1 else "tests/filter_eod_test.ps"
    open(dst,"w").write(gen())
    import os; print("wrote",dst,os.path.getsize(dst),"bytes")
