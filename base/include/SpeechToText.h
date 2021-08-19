#pragma once

#include "Module.h"
#include <vector>
#include <string>
#include <iostream>

extern "C"{
#include "deepspeech.h"
#include<libavutil/samplefmt.h>
#include<libavutil/channel_layout.h>
#include<libswresample/swresample.h>
}

class SpeechToTextProps : public ModuleProps
{
public:
    SpeechToTextProps( size_t _BufferSize = 480000, int _src_rate = 48000, int64_t _src_ch_layout = AV_CH_LAYOUT_MONO, AVSampleFormat _src_sample_format = AV_SAMPLE_FMT_S16)
    {
        
        BufferSize = _BufferSize;
        src_rate = _src_rate;
        src_ch_layout = _src_ch_layout;
        src_sample_fmt = _src_sample_format;
    }
    const char* model_path;
    const char* scorer_path;
    size_t BufferSize;
    int src_rate;
    int64_t src_ch_layout;
    AVSampleFormat src_sample_fmt;
};

class SpeechToText : public Module
{
public:
    SpeechToText(SpeechToTextProps _props);
    ~SpeechToText();
    
    virtual bool init();
    virtual bool term();

protected:
    bool process(frame_container &frames);
    bool validateInputPins();
    bool validateOutputPins();
    void addInputPin(framemetadata_sp &metadata, string &pinId);
    ModelState *ctx;
    StreamingState *sCtx;
    size_t tBufferSize;
    SwrContext *swr;
    bool fState,scoreIncreasing;
    float tStart,maxScoreForCurrentStream;
    int count,numberOfFramesScoreSame;
    string sentence;
    vector<uint16_t> fVector;
    const uint8_t *src_data;

private:
    class Detail;
    boost::shared_ptr<Detail> mDetail;
    string mOutputPinId;
    SpeechToTextProps props;
    
};