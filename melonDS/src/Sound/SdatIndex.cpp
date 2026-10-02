/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

/*
    RealtimeBGM: static SDAT index and sequence classifier.
    Port of tools/sdat_scan.py. Everything read from the ROM is treated as
    untrusted: all reads are bounds-checked and counts/recursion are capped.
*/
#include <ctype.h>
#include "SdatIndex.h"
#include "../CRC32.h"
#include "../Platform.h"

namespace melonDS::Sound
{

using Platform::Log;
using Platform::LogLevel;

const char* SeqClassName(SeqClass c)
{
    switch (c)
    {
    case SeqClass::BGM: return "BGM";
    case SeqClass::Jingle: return "Jingle";
    case SeqClass::Ambient: return "Ambient";
    case SeqClass::SFX: return "SFX";
    case SeqClass::Cry: return "Cry";
    default: return "Unknown";
    }
}

namespace
{

// Bounds-checked view into the ROM. Out-of-range reads return 0, which every
// caller treats as "absent".
struct Span
{
    const u8* Data = nullptr;
    u32 Len = 0;

    bool Has(u32 off, u32 n) const { return off <= Len && n <= Len - off; }
    u8 R8(u32 off) const { return Has(off, 1) ? Data[off] : 0; }
    u16 R16(u32 off) const { return Has(off, 2) ? (u16)(Data[off] | (Data[off+1] << 8)) : 0; }
    u32 R32(u32 off) const
    {
        return Has(off, 4) ? (u32)(Data[off] | (Data[off+1] << 8) | (Data[off+2] << 16) | ((u32)Data[off+3] << 24)) : 0;
    }
    bool Magic(u32 off, const char* m) const
    {
        return Has(off, 4) && Data[off] == m[0] && Data[off+1] == m[1] && Data[off+2] == m[2] && Data[off+3] == m[3];
    }
    Span Sub(u32 off, u32 n) const
    {
        if (!Has(off, n)) return {};
        return {Data + off, n};
    }
    std::string Str(u32 off) const
    {
        std::string s;
        while (off < Len && Data[off] && s.size() < 255)
            s += (char)Data[off++];
        return s;
    }
};

constexpr u32 MaxFntEntries = 0x10000;
constexpr u32 MaxFntDepth = 32;
constexpr u32 MaxSdats = 64;
constexpr u32 MaxSeqsPerSdat = 0x10000;

struct FntWalker
{
    Span Fnt;
    u32 NumFiles = 0;
    u32 Budget = MaxFntEntries;
    std::vector<std::pair<std::string, u32>> Sdats;   // path, file id

    void Walk(u16 dirId, const std::string& path, u32 depth)
    {
        if (depth > MaxFntDepth) return;
        u32 e = (dirId & 0xFFF) * 8;
        if (!Fnt.Has(e, 8)) return;
        u32 p = Fnt.R32(e);
        u32 fid = Fnt.R16(e + 4);
        while (Budget > 0 && p < Fnt.Len)
        {
            Budget--;
            u8 t = Fnt.R8(p++);
            if (t == 0) break;
            u32 n = t & 0x7F;
            if (!Fnt.Has(p, n)) return;
            std::string name((const char*)Fnt.Data + p, n);
            p += n;
            if (t & 0x80)
            {
                if (!Fnt.Has(p, 2)) return;
                u16 d = Fnt.R16(p);
                p += 2;
                Walk(d, path + "/" + name, depth + 1);
            }
            else
            {
                if (n >= 5 && Sdats.size() < MaxSdats)
                {
                    std::string ext = name.substr(n - 5);
                    for (char& c : ext) c = (char)tolower((unsigned char)c);
                    if (ext == ".sdat" && fid < NumFiles)
                        Sdats.emplace_back(path + "/" + name, fid);
                }
                fid++;
            }
        }
    }
};

}

SeqStats AnalyzeMML(const u8* mml, u32 len)
{
    SeqStats st;
    if (!mml) return st;

    u32 p = 0;
    while (p < len)
    {
        u8 op = mml[p++];
        st.Events++;

        // Prefixes: 0xA0 replaces the last argument with s16 min + s16 max,
        // 0xA1 with a u8 variable number, 0xA2 (IF) leaves it unchanged.
        u32 lastArg = 0;   // 0 = normal, else fixed byte count
        while (op >= 0xA0 && op <= 0xA2)
        {
            if (op == 0xA0) lastArg = 4;
            else if (op == 0xA1) lastArg = 1;
            if (p >= len) return st;
            op = mml[p++];
        }

        auto skipLast = [&](u32 normal) -> bool
        {
            if (lastArg) normal = lastArg;
            if (normal == 0)   // variable-length
            {
                while (p < len && (mml[p] & 0x80)) p++;
                p++;
                return p <= len;
            }
            if (len - p < normal) return false;
            p += normal;
            return true;
        };

        if (op < 0x80)
        {
            st.Notes++;
            if (p >= len) break;
            p++;   // velocity
            if (!skipLast(0)) break;
        }
        else if (op == 0x80 || op == 0x81)
        {
            if (!skipLast(0)) break;
        }
        else if (op == 0x93)
        {
            if (len - p < 4) break;
            p += 4;
        }
        else if (op == 0x94 || op == 0x95)
        {
            if (len - p < 3) break;
            u32 tgt = mml[p] | (mml[p+1] << 8) | (mml[p+2] << 16);
            p += 3;
            if (op == 0x94 && !lastArg && tgt < p)
                st.Loops = true;
        }
        else if ((op >= 0xB0 && op <= 0xB6) || (op >= 0xB8 && op <= 0xBD))
        {
            if (p >= len) break;
            p++;   // variable number
            if (!skipLast(2)) break;
        }
        else if (op >= 0xC0 && op <= 0xD7)
        {
            if (!skipLast(1)) break;
        }
        else if (op == 0xE0 || op == 0xE1 || op == 0xE3)
        {
            if (!skipLast(2)) break;
        }
        else if (op == 0xFE)
        {
            if (len - p < 2) break;
            p += 2;
        }
        else if (op == 0xFC || op == 0xFD || op == 0xFF)
        {
            // No arguments. 0xFF ends one track, but the following tracks'
            // data comes after it, so keep decoding linearly.
        }
        else
            break;
    }
    return st;
}

SeqClass ClassifySeq(const SeqStats& st, u16 chanMask)
{
    u32 nch = 0;
    for (u16 m = chanMask; m; m &= m - 1) nch++;
    if (!chanMask) nch = 8;
    if (st.Notes <= 4 && st.Events < 16) return SeqClass::Cry;
    if (st.Loops && (st.Notes >= 150 || nch >= 8)) return SeqClass::BGM;
    if (st.Loops) return SeqClass::Ambient;
    if (st.Notes >= 30 && nch >= 8) return SeqClass::Jingle;
    return SeqClass::SFX;
}

void SdatIndex::Clear()
{
    Seqs.clear();
    Names.clear();
    ByKey.clear();
}

void SdatIndex::Build(const u8* rom, u32 romLen)
{
    Clear();
    Span r{rom, rom ? romLen : 0};
    if (!r.Has(0, 0x50)) return;

    u32 fntOff = r.R32(0x40), fntLen = r.R32(0x44);
    u32 fatOff = r.R32(0x48), fatLen = r.R32(0x4C);
    if (!r.Has(fntOff, fntLen) || !r.Has(fatOff, fatLen) || fntLen < 8) return;

    FntWalker w;
    w.Fnt = r.Sub(fntOff, fntLen);
    w.NumFiles = fatLen / 8;
    w.Walk(0xF000, "", 0);

    for (auto& [path, fid] : w.Sdats)
    {
        u32 start = r.R32(fatOff + fid * 8);
        u32 end = r.R32(fatOff + fid * 8 + 4);
        if (end <= start || !r.Has(start, end - start) || !r.Magic(start, "SDAT")) continue;

        Names.push_back(path);
        ParseSdat(rom, romLen, start, end - start, path);
    }
}

void SdatIndex::ParseSdat(const u8* rom, u32 romLen, u32 off, u32 len, const std::string& name)
{
    Span sdat = Span{rom, romLen}.Sub(off, len);
    if (!sdat.Data) return;

    u32 symbOff = sdat.R32(0x10), symbLen = sdat.R32(0x14);
    u32 infoOff = sdat.R32(0x18), infoLen = sdat.R32(0x1C);
    u32 fatOff = sdat.R32(0x20), fatLen = sdat.R32(0x24);
    Span info = sdat.Sub(infoOff, infoLen);
    Span fat = sdat.Sub(fatOff, fatLen);
    Span symb = symbOff ? sdat.Sub(symbOff, symbLen) : Span{};
    if (!info.Data || !fat.Data) return;

    u32 numFat = fat.R32(8);
    auto fileSpan = [&](u32 fileId) -> Span
    {
        if (fileId >= numFat) return {};
        return sdat.Sub(fat.R32(12 + 16 * fileId), fat.R32(12 + 16 * fileId + 4));
    };

    // INFO/SYMB record: u32 count followed by u32 entry offsets.
    auto recCount = [](const Span& blk, u32 rec) -> u32
    {
        u32 n = blk.R32(rec);
        if (!blk.Has(rec, 4) || n > (blk.Len - rec - 4) / 4) return 0;
        return n;
    };
    auto symbName = [&](u32 rec, u32 i) -> std::string
    {
        if (!symb.Data || !rec || i >= recCount(symb, rec)) return {};
        u32 no = symb.R32(rec + 4 + 4 * i);
        return no ? symb.Str(no) : std::string();
    };

    // PLAYER table: only the channel masks are needed for classification
    std::vector<u16> chanMasks;
    u32 plyRec = info.R32(8 + 4 * 4);
    u32 numPly = plyRec ? recCount(info, plyRec) : 0;
    if (numPly > 256) numPly = 256;
    chanMasks.resize(numPly);
    for (u32 i = 0; i < numPly; i++)
    {
        u32 po = info.R32(plyRec + 4 + 4 * i);
        if (!po || !info.Has(po, 8)) continue;
        chanMasks[i] = info.R16(po + 2);
    }

    // SEQ record
    u32 counts[6] = {};
    u32 symbSeqRec = symb.Data ? symb.R32(8) : 0;
    u32 seqRec = info.R32(8);
    u32 numSeq = seqRec ? recCount(info, seqRec) : 0;
    if (numSeq > MaxSeqsPerSdat) numSeq = MaxSeqsPerSdat;
    u32 numSseq = 0;
    for (u32 i = 0; i < numSeq; i++)
    {
        u32 so = info.R32(seqRec + 4 + 4 * i);
        if (!so || !info.Has(so, 12)) continue;

        Span f = fileSpan(info.R16(so));
        if (!f.Magic(0, "SSEQ")) continue;
        u32 dataOff = f.R32(0x18);
        if (dataOff < 0x1C || dataOff > f.Len) continue;

        SeqInfo si;
        const u8* mml = f.Data + dataOff;
        u8 player = info.R8(so + 9);
        si.MMLLen = f.Len - dataOff;
        si.CRC = CRC32(mml, (int)si.MMLLen);
        si.Cls = ClassifySeq(AnalyzeMML(mml, si.MMLLen), player < chanMasks.size() ? chanMasks[player] : 0);
        si.Name = symbName(symbSeqRec, i);
        counts[(int)si.Cls]++;
        ByKey.emplace(((u64)si.CRC << 32) | si.MMLLen, (u32)Seqs.size());
        Seqs.push_back(std::move(si));
        numSseq++;
    }

    Log(LogLevel::Info, "RealtimeBGM: SDAT %s: %u seqs (%u BGM, %u jingle, %u ambient, %u sfx, %u cry)\n",
        name.c_str(), numSseq,
        counts[(int)SeqClass::BGM], counts[(int)SeqClass::Jingle], counts[(int)SeqClass::Ambient],
        counts[(int)SeqClass::SFX], counts[(int)SeqClass::Cry]);
}

const SeqInfo* SdatIndex::Lookup(u32 crc, u32 mmlLen) const
{
    auto it = ByKey.find(((u64)crc << 32) | mmlLen);
    return it == ByKey.end() ? nullptr : &Seqs[it->second];
}

}
