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
    Scans a Nitro ROM for *.sdat archives, records every SSEQ with a CRC32 of
    its MML bytes (file minus the 0x1C header) so a runtime START_SEQ can be
    matched back to a named, classified sequence.
*/
#ifndef SDATINDEX_H
#define SDATINDEX_H

#include <string>
#include <vector>
#include <unordered_map>
#include "../types.h"

namespace melonDS::Sound
{

enum class SeqClass : u8
{
    Unknown = 0,
    BGM,      // looping music: rendered at 1x during fast-forward
    Jingle,   // non-looping fanfare / music-ending: 1x by default (policy)
    Ambient,  // short looping environmental sound on an SFX player: stays on emulation clock
    SFX,      // one-shot effect
    Cry,      // tiny trigger sequence (Pokemon voice): stays on emulation clock
};

const char* SeqClassName(SeqClass c);

struct SeqStats
{
    bool Loops = false;   // backward JUMP (0x94) seen
    u32 Notes = 0;
    u32 Events = 0;
};

struct SeqInfo
{
    u32 CRC = 0;          // CRC32 (zlib polynomial, same as melonDS CRC32()) of MML bytes
    u32 MMLLen = 0;       // SSEQ file size - 0x1C
    std::string Name;     // from SYMB, empty if the SDAT has no symbols
    SeqClass Cls = SeqClass::Unknown;
};

// Heuristic classifier shared by the static index and the runtime fallback.
// mml/len: sequence data (no header). chanMask: channels of the PLAYER record that will
// play it, or from ALLOCATABLE_CHANNEL at runtime (0 if unknown).
SeqStats AnalyzeMML(const u8* mml, u32 len);
SeqClass ClassifySeq(const SeqStats& st, u16 chanMask);

class SdatIndex
{
public:
    // Scans the whole ROM image (Nitro FNT/FAT) for SDAT files and indexes them.
    // Safe to call with any ROM; a ROM without SDAT yields an empty index.
    void Build(const u8* rom, u32 romLen);
    void Clear();

    const SeqInfo* Lookup(u32 crc, u32 mmlLen) const;      // nullptr if unknown
    const std::vector<SeqInfo>& Sequences() const { return Seqs; }
    const std::vector<std::string>& SdatNames() const { return Names; }

private:
    void ParseSdat(const u8* rom, u32 romLen, u32 off, u32 len, const std::string& name);

    std::vector<SeqInfo> Seqs;
    std::vector<std::string> Names;
    std::unordered_map<u64, u32> ByKey;   // (CRC<<32 | MMLLen) -> index into Seqs
};

}
#endif
