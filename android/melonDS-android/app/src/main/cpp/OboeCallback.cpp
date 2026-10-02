#include "OboeCallback.h"
#include "types.h"
#include "Platform.h"
#include "SPU.h"

using namespace melonDS;

#define INTERNAL_FRAME_RATE 59.8260982880808f

// set by MelonDSAndroidJNI.cpp's emu thread
extern bool isFastForwardEnabled;
extern int targetFps;
extern float fps;

OboeCallback::OboeCallback(int volume, void (*onErrorCallback)(void), std::ostream* recordingStream) : _volume(volume), onErrorCallback(onErrorCallback), _recordingStream(recordingStream) {
    audioSampleFrac = 0;
}

oboe::DataCallbackResult
OboeCallback::onAudioReady(oboe::AudioStream *stream, void *audioData, int32_t numFrames) {
    auto currentInstance = activeInstance.lock();

    if (!currentInstance)
    {
        memset(audioData, 0, numFrames * sizeof(u16) * 2);
        return oboe::DataCallbackResult::Continue;
    }

    int len = numFrames;

    double skew = std::clamp(60.0 / INTERNAL_FRAME_RATE, 0.995, 1.005);
    currentInstance->setAudioOutputSkew(skew);

    int len_in = getNumSamplesOut(len);
    if (len_in > numFrames) len_in = numFrames;

    // keep sound effects at their original pitch while fast-forwarded: targetFps (0 = unlimited)
    // reflects the configured multiplier, and fps is the measured achieved rate for that case
    double speedRatio = 1.0;
    if (isFastForwardEnabled)
    {
        speedRatio = (targetFps > 0) ? (targetFps / 60.0) : (fps / 60.0);
        speedRatio = std::clamp(speedRatio, 1.0, 8.0);
    }

    int num_in = currentInstance->readAudioOutput((s16*) audioData, len_in, stream->getSampleRate(), speedRatio, _volume);

    if (num_in < 1)
    {
        memset(audioData, 0, len * sizeof(s16) * 2);
        return oboe::DataCallbackResult::Continue;
    }

    if (num_in < len_in)
    {
        int last = num_in - 1;

        for (int i = num_in; i < len_in; i++)
            ((u32*)audioData)[i] = ((u32*)audioData)[last];
    }

    if (_recordingStream) [[unlikely]]
        _recordingStream->write((char*) audioData, numFrames * sizeof(s16) * 2);

    return oboe::DataCallbackResult::Continue;
}

void OboeCallback::onErrorAfterClose(oboe::AudioStream* stream, oboe::Result result)
{
    if (result == oboe::Result::ErrorDisconnected && onErrorCallback != nullptr) {
        onErrorCallback();
    }
}

int OboeCallback::getNumSamplesOut(int len)
{
    // TODO: adjust to game speed
    float f_len_in = len /* * (curFPS/60.0)*/;
    f_len_in += audioSampleFrac;
    int len_in = (int) floor(f_len_in);
    audioSampleFrac = f_len_in - len_in;

    return len_in;
}