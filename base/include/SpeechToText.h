#pragma once

#include "Module.h"
#include <vector>
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
    size_t BufferSize;
    int src_rate;
    int64_t src_ch_layout;
    AVSampleFormat src_sample_fmt;
};

{
public:
    SpeechToText(SpeechToTextProps _props);
    ~SpeechToText();
    
    virtual bool init();
    virtual bool term();

protected:
    bool process(frame_container &frames);
    ModelState *ctx;
    StreamingState *sCtx;
    size_t tBufferSize;
    int count;
    SwrContext *swr;
    bool validateInputPins();
    bool validateOutputPins();
    std::vector<uint8_t> fVector;
    const uint8_t *src_data;
    void addInputPin(framemetadata_sp &metadata, string &pinId);

private:
    class Detail;
    boost::shared_ptr<Detail> mDetail;
    std::string mOutputPinId;
    SpeechToTextProps props;    
};