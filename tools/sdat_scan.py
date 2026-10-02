#!/usr/bin/env python3
"""Scan an NDS ROM for SDAT archives, list every SSEQ with its PLAYER/bank info and a
heuristic BGM/SFX class. Usage: sdat_scan.py game.nds > out.txt"""
import struct, sys, collections

rom = open(sys.argv[1],'rb').read()
u32 = lambda o: struct.unpack_from('<I', rom, o)[0]
u16 = lambda o: struct.unpack_from('<H', rom, o)[0]
print("game:", rom[0:12].decode('ascii','replace').strip('\0'), rom[12:16].decode())

# --- NitroFS: walk FNT/FAT to find *.sdat
fnt_off, fnt_len, fat_off, fat_len = u32(0x40), u32(0x44), u32(0x48), u32(0x4C)
def walk(dir_id, path, out):
    e = fnt_off + (dir_id & 0xFFF) * 8
    sub = u32(e); first = u16(e+4)
    p = fnt_off + sub; fid = first
    while True:
        t = rom[p]; p += 1
        if t == 0: break
        n = t & 0x7F; name = rom[p:p+n].decode('ascii','replace'); p += n
        if t & 0x80:
            d = u16(p); p += 2; walk(d, path+'/'+name, out)
        else:
            out.append((path+'/'+name, fid)); fid += 1
files=[]; walk(0xF000,'',files)
sdats=[(n,i) for n,i in files if n.lower().endswith('.sdat')]
print("files:",len(files)," sdat:",sdats)

for name, fid in sdats:
    s = u32(fat_off+fid*8); e = u32(fat_off+fid*8+4)
    print(f"\n== {name} @ {s:#x} len {e-s:#x}")
    base = s
    assert rom[base:base+4]==b'SDAT'
    symb_off,symb_len,info_off,info_len,fat_o,fat_l,file_o,file_l = struct.unpack_from('<8I', rom, base+0x10)
    def rec(blk, off): return blk+off
    info = base+info_off
    # INFO block: 8 record offsets (SEQ, SEQARC, BANK, WAVEARC, PLAYER, GROUP, PLAYER2, STRM)
    recs = struct.unpack_from('<8I', rom, info+8)
    def entries(r):
        o = info+r; n=u32(o); return [u32(o+4+4*i) for i in range(n)]
    names = {}
    if symb_off:
        symb = base+symb_off
        srecs = struct.unpack_from('<8I', rom, symb+8)
        def snames(r):
            o = symb+r; n=u32(o); out=[]
            for i in range(n):
                no=u32(o+4+4*i)
                if no==0: out.append(None); continue
                q=symb+no; z=rom.index(b'\0',q); out.append(rom[q:z].decode('ascii','replace'))
            return out
        names['seq']=snames(srecs[0]); names['bank']=snames(srecs[2]); names['wa']=snames(srecs[3])
    fat = base+fat_o
    def fatent(i): return u32(fat+12+16*i), u32(fat+12+16*i+4)
    seqs = entries(recs[0]); banks=entries(recs[2]); players=entries(recs[4])
    print("SEQ:",len(seqs)," BANK:",len(banks)," WAVEARC:",len(entries(recs[3]))," PLAYER:",len(players)," STRM:",len(entries(recs[7])))
    # PLAYER info: u8 maxSeqs, u8 pad, u16 channelMask, u32 heapSize
    for pi,po in enumerate(players):
        if po==0: continue
        o=info+po
        print(f"  player {pi}: maxSeqs={rom[o]} chanMask={u16(o+2):#06x} heap={u32(o+4):#x}")
    # SEQ info: u16 fileId, u16 pad, u16 bnk, u8 vol, u8 cpr, u8 ppr, u8 ply, u16 pad
    # SSEQ parse for classification
    def analyze_sseq(data):
        # header 0x1C; DATA chunk at 0x10: 'DATA', size, dataOffset
        if data[:4]!=b'SSEQ': return None
        start = u32_b(data,0x18)
        p=start; n=len(data)
        tracks=1; events=0; loops=False; jumps=0; calls=0; notes=0; tempo=None; var=False; rnd=False
        # single pass linear decode (ignores control flow), robust enough for stats
        max_pos=0
        argbytes = {0x80:'v',0x81:'v',0x93:'4',0x94:'3',0x95:'3',0xA0:'rand',0xA1:'var',0xA2:'if',
                    0xB0:'2',0xB1:'2',0xB2:'2',0xB3:'2',0xB4:'2',0xB5:'2',0xB6:'2',0xB8:'2',0xB9:'2',0xBA:'2',0xBB:'2',0xBC:'2',0xBD:'2',
                    0xC0:'1',0xC1:'1',0xC2:'1',0xC3:'1',0xC4:'1',0xC5:'1',0xC6:'1',0xC7:'1',0xC8:'1',0xC9:'1',0xCA:'1',0xCB:'1',0xCC:'1',0xCD:'1',0xCE:'1',0xCF:'1',
                    0xD0:'1',0xD1:'1',0xD2:'1',0xD3:'1',0xD4:'1',0xD5:'1',0xD6:'1',0xE0:'2',0xE1:'2',0xE3:'2',0xFC:'0',0xFD:'0',0xFE:'2',0xFF:'0'}
        def rdvar(p):
            while p<n and data[p]&0x80: p+=1
            return p+1
        while p<n:
            op=data[p]; p+=1; events+=1
            if op<0x80:
                notes+=1; p+=1; p=rdvar(p)
                if p>n: break
                continue
            k=argbytes.get(op)
            if k is None or p+4>n+4: break
            if k=='v': p=rdvar(p)
            elif k=='4':
                p+=4
                if op==0x94: jumps+=1
                if op==0x93: tracks+=1
            elif k=='3':
                tgt = data[p]|data[p+1]<<8|data[p+2]<<16; p+=3
                if op==0x94:
                    jumps+=1
                    if tgt < (p-start): loops=True
                else: calls+=1
            elif k=='rand': p+=1; p+=4; rnd=True
            elif k=='var': p+=1; p+=1; var=True
            elif k=='if': continue
            elif k=='2':
                if op==0xE1: tempo=u16_b(data,p)
                if op==0xFE: tracks = bin(u16_b(data,p)).count('1')
                p+=2
            elif k=='1': p+=1
            elif k=='0':
                if op==0xFF: pass
        return dict(events=events,notes=notes,tracks=tracks,loops=loops,jumps=jumps,calls=calls,tempo=tempo,var=var,rnd=rnd,size=len(data))
    def u32_b(b,o): return struct.unpack_from('<I',b,o)[0]
    def u16_b(b,o): return struct.unpack_from('<H',b,o)[0]
    # player stats: heap size + channel mask + number of seqs on that player
    pinfo={}
    for pi,po in enumerate(players):
        if po: o=info+po; pinfo[pi]=(rom[o],u16(o+2),u32(o+4))
    def classify(ply, st):
        """Heuristic: no names, no per-game data. Uses PLAYER record + SSEQ structure."""
        if st is None: return '?'
        maxseq,chmask,heap = pinfo.get(ply,(1,0,0))
        nch = bin(chmask).count('1')
        if st['notes'] <= 4 and st['events'] < 16: return 'cry/trigger'
        if st['loops'] and (st['notes'] >= 150 or nch >= 8): return 'BGM'
        if st['loops'] and st['notes'] < 150: return 'ambient-loop' if nch < 8 else 'BGM'
        if not st['loops'] and st['notes'] >= 30 and nch >= 8: return 'jingle'
        return 'SFX'
    rows=[]
    for si,so in enumerate(seqs):
        if so==0: continue
        o=info+so
        fileId,_,bnk,vol,cpr,ppr,ply = struct.unpack_from('<HHHBBBB',rom,o)
        fo,fl = fatent(fileId)
        st = analyze_sseq(rom[base+fo:base+fo+fl])
        nm = names.get('seq',[None]*9999)[si] if names else None
        rows.append((si,nm,bnk,vol,ply,ppr,cpr,st,classify(ply,st)))
    byplayer=collections.Counter(r[4] for r in rows)
    print("  sequences per player:",dict(byplayer))
    print("  class per player:")
    for p in sorted(byplayer):
        print("   ",p,dict(collections.Counter(r[8] for r in rows if r[4]==p)))
    print(f"  {'id':>4} {'name':34} {'bnk':>3} {'vol':>3} {'ply':>3} {'ppr':>3} {'cpr':>3} {'trk':>3} {'notes':>6} {'ev':>6} loop tempo size")
    for si,nm,bnk,vol,ply,ppr,cpr,st,cls in rows:
        if st is None: print(f"  {si:4} {nm!s:34} (unparsed)"); continue
        print(f"  {si:4} {nm!s:34} {bnk:3} {vol:3} {ply:3} {ppr:3} {cpr:3} {st['tracks']:3} {st['notes']:6} {st['events']:6} {'L' if st['loops'] else '-'}{'V' if st['var'] else ''}{'R' if st['rnd'] else ''} {st['tempo']!s:>5} {st['size']:6} {cls}")
