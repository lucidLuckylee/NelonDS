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
    TimeStretch: WSOLA (Waveform Similarity Overlap-Add) time-stretcher for
    interleaved s16 stereo audio, used to keep sound effects at their original
    pitch while the emulator is fast-forwarded or slowed down.

    The approach (fixed-size analysis window, Hann overlap-add, a coarse+fine
    cross-correlation search for the best-matching splice point on a mono
    downmix) follows the WSOLA stretcher written by Davey Hughes for melonDS
    PR #2738 ("Speedup/slowdown audio filter"); this is a from-scratch
    reimplementation of the same algorithm as a pull-style, single-threaded
    class so it can live in the core and be shared by every frontend.
*/

#ifndef TIMESTRETCH_H
#define TIMESTRETCH_H

#include <vector>
#include "../types.h"

namespace melonDS::Sound
{

class TimeStretch
{
public:
    TimeStretch();

    // (Re)configures the analysis window/search sizes for the given output
    // sample rate and resets all state.
    void SetRate(double hz);

    // Drops all buffered input/output and accumulator state (e.g. on a seek,
    // a pause/resume, or a large jump in the speed ratio).
    void Reset();

    // Appends inFrames of interleaved stereo input, then synthesises up to
    // outFrames of interleaved stereo output into `out`. `ratio` is the
    // input/output speed (2.0 plays input twice as fast at the same pitch).
    // Returns the number of frames actually written, which is less than
    // outFrames only when there isn't enough input buffered yet to keep up.
    int Process(const s16* in, int inFrames, s16* out, int outFrames, double ratio);

private:
    bool CanSynthesise() const;
    void SynthesiseHop(double ratio, bool output);
    s64 FindBestOffset();
    double Score(s64 pos, const double* ref, double refEnergy) const;
    void Compact();

    double SampleRate = 32824.0;
    int FrameSize = 0;        // analysis window, frames (~25ms)
    int SynthesisHop = 0;     // FrameSize/2: periodic Hann at 50% overlap sums to unity
    int SearchRadius = 0;     // how far either side of the natural continuation to search (~7.5ms)
    int CoarseStride = 4;
    int FineRadius = 3;

    std::vector<float> Window;

    // input history, indexed [0, Buf*.size()) with BufBase added to translate
    // to/from absolute frame indices
    std::vector<s16> BufL, BufR;
    std::vector<float> BufMono;
    s64 BufBase = 0;
    s64 TotalPushed = 0;

    s64 AnalysisPos = 0;   // next nominal read position (absolute frame index)
    s64 NaturalPos = 0;    // where AnalysisPos would be without any splicing
    bool Primed = false;

    std::vector<float> AccL, AccR;
    std::vector<double> RefScratch; // scratch space for FindBestOffset, sized to SynthesisHop

    // output frames synthesised but not yet handed back to the caller
    std::vector<s16> OutQueue;
    int OutQueueHead = 0;
};

}

#endif // TIMESTRETCH_H
