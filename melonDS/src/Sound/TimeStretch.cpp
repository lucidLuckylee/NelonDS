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

#include "TimeStretch.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace melonDS::Sound
{

static s16 Saturate(float v)
{
    long s = std::lround(v);
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    return (s16)s;
}

TimeStretch::TimeStretch()
{
    SetRate(SampleRate);
}

void TimeStretch::SetRate(double hz)
{
    SampleRate = hz;

    FrameSize = (int)std::lround(hz * 0.025); // ~25ms analysis window
    FrameSize &= ~1;
    if (FrameSize < 64) FrameSize = 64;
    SynthesisHop = FrameSize / 2; // periodic Hann at 50% overlap sums to unity

    SearchRadius = (int)std::lround(hz * 0.0075); // +/- ~7.5ms
    if (SearchRadius < SynthesisHop) SearchRadius = SynthesisHop;

    Window.resize(FrameSize);
    for (int i = 0; i < FrameSize; i++)
        Window[i] = (float)(0.5 * (1.0 - std::cos((2.0 * M_PI * i) / FrameSize)));

    Reset();
}

void TimeStretch::Reset()
{
    BufL.clear();
    BufR.clear();
    BufMono.clear();
    BufBase = 0;
    TotalPushed = 0;

    AnalysisPos = 0;
    NaturalPos = 0;
    Primed = false;

    AccL.assign(FrameSize, 0.0f);
    AccR.assign(FrameSize, 0.0f);
    RefScratch.resize(SynthesisHop);

    OutQueue.clear();
    OutQueueHead = 0;
}

int TimeStretch::Process(const s16* in, int inFrames, s16* out, int outFrames, double ratio)
{
    for (int i = 0; i < inFrames; i++)
    {
        s16 l = in[(i*2)+0];
        s16 r = in[(i*2)+1];
        BufL.push_back(l);
        BufR.push_back(r);
        BufMono.push_back(0.5f * ((float)l + (float)r));
    }
    TotalPushed += inFrames;

    int written = 0;
    auto drain = [&]()
    {
        int availFrames = ((int)OutQueue.size()/2) - OutQueueHead;
        int n = std::min(availFrames, outFrames - written);
        if (n <= 0) return;
        std::memcpy(out + (written*2), OutQueue.data() + (OutQueueHead*2), n*2*sizeof(s16));
        OutQueueHead += n;
        written += n;
    };

    drain();

    if (!Primed && CanSynthesise())
        SynthesiseHop(ratio, false);

    while ((written < outFrames) && CanSynthesise())
    {
        SynthesiseHop(ratio, true);
        drain();
    }

    if (OutQueueHead > 0)
    {
        OutQueue.erase(OutQueue.begin(), OutQueue.begin() + (OutQueueHead*2));
        OutQueueHead = 0;
    }
    Compact();

    return written;
}

bool TimeStretch::CanSynthesise() const
{
    s64 frameEnd = AnalysisPos + FrameSize;
    s64 naturalEnd = NaturalPos + SynthesisHop;
    return (TotalPushed >= frameEnd) && (TotalPushed >= naturalEnd);
}

void TimeStretch::SynthesiseHop(double ratio, bool output)
{
    int hop = (int)std::lround(SynthesisHop * ratio);
    if (hop < 1) hop = 1;

    s64 chosen = Primed ? FindBestOffset() : AnalysisPos;
    Primed = true;

    s64 bufLen = (s64)BufL.size();
    for (int i = 0; i < FrameSize; i++)
    {
        s64 idx = std::clamp<s64>(chosen + i - BufBase, 0, bufLen - 1);
        float w = Window[i];
        AccL[i] += w * (float)BufL[idx];
        AccR[i] += w * (float)BufR[idx];
    }

    if (output)
    {
        size_t base = OutQueue.size();
        OutQueue.resize(base + (SynthesisHop*2));
        for (int i = 0; i < SynthesisHop; i++)
        {
            OutQueue[base + (i*2) + 0] = Saturate(AccL[i]);
            OutQueue[base + (i*2) + 1] = Saturate(AccR[i]);
        }
    }

    // shift the accumulator left by one hop, the way successive overlapping
    // windows do, and clear the freshly exposed tail
    std::memmove(AccL.data(), AccL.data() + SynthesisHop, (FrameSize - SynthesisHop) * sizeof(float));
    std::memmove(AccR.data(), AccR.data() + SynthesisHop, (FrameSize - SynthesisHop) * sizeof(float));
    std::fill(AccL.begin() + (FrameSize - SynthesisHop), AccL.end(), 0.0f);
    std::fill(AccR.begin() + (FrameSize - SynthesisHop), AccR.end(), 0.0f);

    // the nominal pointer advances by hop alone; advancing from chosen would
    // make consumption hop + E[bestK], which the ratio control can't see
    NaturalPos = chosen + SynthesisHop;
    AnalysisPos += hop;
}

s64 TimeStretch::FindBestOffset()
{
    s64 oldest = BufBase;
    if (NaturalPos < oldest) return AnalysisPos;

    double* ref = RefScratch.data();
    double refEnergy = 0.0;
    s64 bufLen = (s64)BufMono.size();
    for (int i = 0; i < SynthesisHop; i++)
    {
        s64 idx = std::clamp<s64>(NaturalPos + i - BufBase, 0, bufLen - 1);
        double v = BufMono[idx];
        ref[i] = v;
        refEnergy += v * v;
    }

    int lowestK = -SearchRadius;
    if ((AnalysisPos + lowestK) < oldest)
        lowestK = (int)(oldest - AnalysisPos);

    int highestK = SearchRadius;
    s64 latest = TotalPushed - FrameSize;
    if ((AnalysisPos + highestK) > latest)
        highestK = (int)(latest - AnalysisPos);
    if (highestK < lowestK) highestK = lowestK;

    int bestK = std::clamp(0, lowestK, highestK);
    double bestScore = Score(AnalysisPos + bestK, ref, refEnergy);

    for (int k = lowestK; k <= highestK; k += CoarseStride)
    {
        double s = Score(AnalysisPos + k, ref, refEnergy);
        if (s > bestScore) { bestScore = s; bestK = k; }
    }

    int lo = std::max(lowestK, bestK - FineRadius);
    int hi = std::min(highestK, bestK + FineRadius);
    for (int k = lo; k <= hi; k++)
    {
        double s = Score(AnalysisPos + k, ref, refEnergy);
        if (s > bestScore) { bestScore = s; bestK = k; }
    }

    return AnalysisPos + bestK;
}

double TimeStretch::Score(s64 pos, const double* ref, double refEnergy) const
{
    double dot = 0.0, energy = 0.0;
    s64 bufLen = (s64)BufMono.size();
    for (int i = 0; i < SynthesisHop; i++)
    {
        s64 idx = std::clamp<s64>(pos + i - BufBase, 0, bufLen - 1);
        double a = BufMono[idx];
        dot += a * ref[i];
        energy += a * a;
    }
    return dot / std::sqrt((energy * refEnergy) + 1.0e-9);
}

void TimeStretch::Compact()
{
    s64 safe = std::min(AnalysisPos, NaturalPos) - (SearchRadius + FrameSize);
    s64 drop = safe - BufBase;
    if (drop <= 0) return;
    if (drop > (s64)BufL.size()) drop = (s64)BufL.size();

    BufL.erase(BufL.begin(), BufL.begin() + drop);
    BufR.erase(BufR.begin(), BufR.begin() + drop);
    BufMono.erase(BufMono.begin(), BufMono.begin() + drop);
    BufBase += drop;
}

}
