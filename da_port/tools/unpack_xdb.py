import struct, sys, os

N=4096; F=60; THRESHOLD=2
N_CHAR=256-THRESHOLD+F; T=N_CHAR*2-1; R=T-1; MAX_FREQ=0x4000  # эталон: xrCore/LzHuf.cpp. Было 0x8000 — вдвое больше, из-за чего
# перестройка дерева срабатывала не в тот момент, и таблица портилась с 21% потока.

d_code=[0]*256; d_len=[0]*256
# build d_code/d_len per Okumura table
_spec=[(0,32,0,3),(32,16,1,4)]  # placeholder, replaced below
# Use exact tables:
_dc=(
[0x00]*32+[0x01]*16+[0x02]*16+[0x03]*16+[0x04]*8+[0x05]*8+[0x06]*8+[0x07]*8+
[0x08]*8+[0x09]*8+[0x0A]*8+[0x0B]*8+[0x0C]*4+[0x0D]*4+[0x0E]*4+[0x0F]*4+
[0x10]*4+[0x11]*4+[0x12]*4+[0x13]*4+[0x14]*4+[0x15]*4+[0x16]*4+[0x17]*4+
[0x18]*2+[0x19]*2+[0x1A]*2+[0x1B]*2+[0x1C]*2+[0x1D]*2+[0x1E]*2+[0x1F]*2+
[0x20]*2+[0x21]*2+[0x22]*2+[0x23]*2+[0x24]*2+[0x25]*2+[0x26]*2+[0x27]*2+
[0x28]*2+[0x29]*2+[0x2A]*2+[0x2B]*2+[0x2C]*2+[0x2D]*2+[0x2E]*2+[0x2F]*2+
list(range(0x30,0x40)))
_dl=[3]*32+[4]*48+[5]*64+[6]*48+[7]*48+[8]*16
assert len(_dc)==256 and len(_dl)==256, (len(_dc),len(_dl))
d_code=_dc; d_len=_dl

class Lzhuf:
    def __init__(self, src):
        self.src=src; self.sp=0; self.getbuf=0; self.getlen=0
        self.freq=[0]*(T+1); self.prnt=[0]*(T+N_CHAR); self.son=[0]*T
        self.text_buf=bytearray(N+F-1); self.StartHuff()
    def _fill(self):
        while self.getlen<=8:
            c=self.src[self.sp] if self.sp<len(self.src) else 0
            self.sp+=1; self.getbuf=(self.getbuf|(c<<(8-self.getlen)))&0xFFFF; self.getlen+=8
    def GetBit(self):
        self._fill(); i=self.getbuf; self.getbuf=(self.getbuf<<1)&0xFFFF; self.getlen-=1
        return 1 if (i&0x8000) else 0
    def GetByte(self):
        self._fill(); i=self.getbuf; self.getbuf=(self.getbuf<<8)&0xFFFF; self.getlen-=8
        return (i>>8)&0xFF
    def StartHuff(self):
        for i in range(N_CHAR):
            self.freq[i]=1; self.son[i]=i+T; self.prnt[i+T]=i
        i=0; j=N_CHAR
        while j<=R:
            self.freq[j]=self.freq[i]+self.freq[i+1]; self.son[j]=i
            self.prnt[i]=self.prnt[i+1]=j; i+=2; j+=1
        self.freq[T]=0xffff; self.prnt[R]=0
    def reconst(self):
        j=0
        for i in range(T):
            if self.son[i]>=T:
                self.freq[j]=(self.freq[i]+1)//2; self.son[j]=self.son[i]; j+=1
        i=0; j=N_CHAR
        while j<T:
            f=self.freq[j]=self.freq[i]+self.freq[i+1]
            k=j-1
            while f<self.freq[k]: k-=1
            k+=1; l=j-k
            self.freq[k+1:k+1+l]=self.freq[k:k+l]; self.freq[k]=f
            self.son[k+1:k+1+l]=self.son[k:k+l]; self.son[k]=i
            i+=2; j+=1
        for i in range(T):
            k=self.son[i]
            if k>=T: self.prnt[k]=i
            else: self.prnt[k]=self.prnt[k+1]=i
    def update(self,c):
        if self.freq[R]==MAX_FREQ: self.reconst()
        c=self.prnt[c+T]
        while True:
            self.freq[c]+=1; k=self.freq[c]; l=c+1
            if k>self.freq[l]:
                while k>self.freq[l]: l+=1
                l-=1
                self.freq[c]=self.freq[l]; self.freq[l]=k
                i=self.son[c]; self.prnt[i]=l
                if i<T: self.prnt[i+1]=l
                jj=self.son[l]; self.son[l]=i; self.prnt[jj]=c
                if jj<T: self.prnt[jj+1]=c
                self.son[c]=jj; c=l
            c=self.prnt[c]
            if c==0: break
    def DecodeChar(self):
        c=self.son[R]
        while c<T: c=self.son[c+self.GetBit()]
        c-=T; self.update(c); return c
    def DecodePosition(self):
        i=self.GetByte(); c=d_code[i]<<6; j=d_len[i]-2
        while j>0:
            i=((i<<1)&0xFF)|self.GetBit(); j-=1
        return c|(i&0x3f)
    def decode(self, textsize):
        out=bytearray()
        for i in range(N-F): self.text_buf[i]=0x20
        r=N-F; count=0
        while count<textsize:
            c=self.DecodeChar()
            if c<256:
                out.append(c); self.text_buf[r]=c; r=(r+1)&(N-1); count+=1
            else:
                i=(r-self.DecodePosition()-1)&(N-1); j=c-255+THRESHOLD
                for k in range(j):
                    cc=self.text_buf[(i+k)&(N-1)]
                    out.append(cc); self.text_buf[r]=cc; r=(r+1)&(N-1); count+=1
                    if count>=textsize: break
        return bytes(out)

def read_chunks(path):
    """Return (header_text, data_chunk_offset_in_file, data_chunk_size, table_compressed_bytes)."""
    fsz=os.path.getsize(path); f=open(path,"rb")
    typ,size=struct.unpack("<II",f.read(8)); hdr=f.read(size)
    dpos=8+size; f.seek(dpos); dtyp,dsize=struct.unpack("<II",f.read(8))
    data_off=dpos+8
    f.seek(data_off+dsize)
    rest=f.read()
    o=0; table=None
    while o+8<=len(rest):
        ct,cs=struct.unpack_from("<II",rest,o)
        cdata=rest[o+8:o+8+cs]
        if (ct & 0x7fffffff)==1:
            table=cdata
        o+=8+cs
    return hdr.decode('latin1'), data_off, dsize, table

def decompress_table(table):
    textsize=struct.unpack("<I",table[:4])[0]
    return Lzhuf(table[4:]).decode(textsize)

def parse_fat(dec):
    """Yield dict per entry: name,size_real,size_compr,crc,ptr"""
    o=0; n=len(dec); out=[]
    while o+2<=n:
        start=o
        buf_size=struct.unpack_from("<H",dec,o)[0]
        if buf_size<16 or o+2+buf_size>n: break
        size_real,size_compr,crc=struct.unpack_from("<III",dec,o+2)
        name_len=buf_size-16
        name=dec[o+2+12:o+2+12+name_len].decode('latin1')
        ptr=struct.unpack_from("<I",dec,o+2+12+name_len)[0]
        o+=2+buf_size
        out.append(dict(off=start,name=name,size_real=size_real,size_compr=size_compr,crc=crc,ptr=ptr))
    return out

import lzo1x
def extract_file(fp, data_off, e):
    # NOTE: ptr is absolute from file start; compressed files use LZO1X (dest=size_real)
    fp.seek(e['ptr'])
    raw=fp.read(e['size_compr'])
    if e['size_compr']==e['size_real']:
        return raw
    return lzo1x.lzo1x_decompress(raw, e['size_real'])

DB_DIR=r"D:/Dead Air/Dead Air/database"

def load_fat(path):
    hdr,data_off,dsize,table=read_chunks(path)
    return parse_fat(decompress_table(table))

if __name__=="__main__":
    import sys, glob
    args=sys.argv[1:]
    # optional --db <archive>; default scan all *.xdb0..9 + .db
    db=None
    if '--db' in args:
        i=args.index('--db'); db=args[i+1]; del args[i:i+2]
    mode=args[0] if args else "dump"
    archives=[db] if db else sorted(glob.glob(os.path.join(DB_DIR,"*.xdb*"))+glob.glob(os.path.join(DB_DIR,"*.db")))

    if mode=="find":
        sub=args[1].lower()
        for arc in archives:
            try: fat=load_fat(arc)
            except Exception as ex: continue
            hits=[e for e in fat if sub in e['name'].lower() and e['size_real']>0]
            if hits:
                print(f"### {os.path.basename(arc)}: {len(hits)} hit(s)")
                for e in hits[:80]:
                    print(f"   real={e['size_real']:>8} compr={e['size_compr']:>8}  {e['name']}")
    elif mode=="extract":
        sub=args[1].lower(); outroot=r"D:/Dead Air/extracted"
        total=0
        for arc in archives:
            try: fat=load_fat(arc)
            except Exception: continue
            fp=open(arc,"rb")
            for e in fat:
                if sub not in e['name'].lower() or e['size_real']==0: continue
                data=extract_file(fp,None,e)
                dest=os.path.join(outroot,e['name'].replace('\\','/'))
                os.makedirs(os.path.dirname(dest),exist_ok=True)
                with open(dest,"wb") as w: w.write(data)
                total+=1
        print("extracted",total,"files to",outroot)
    else:  # dump configs.xdb0 FAT
        path=os.path.join(DB_DIR,"configs.xdb0")
        hdr,data_off,dsize,table=read_chunks(path); dec=decompress_table(table); fat=parse_fat(dec)
        with open(r"D:/Dead Air/fat_dump.txt","w",encoding="utf-8") as w:
            for e in fat:
                w.write(f"ptr={e['ptr']:>10} real={e['size_real']:>9} compr={e['size_compr']:>9}  name={e['name']!r}\n")
        print("wrote fat_dump.txt entries=",len(fat))
