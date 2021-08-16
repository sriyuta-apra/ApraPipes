#pragma once

#include "Module.h"
#include "/home/developer/ApraPipes-local/ApraPipes/thirdparty/rnnoise/include/rnnoise.h"

class DenoiseProps : public ModuleProps
{
public:
    DenoiseProps(int _samplerate = 48000, int _channel = 1, int _byteDepth = 2) : ModuleProps()
    {
        sampleRate = _samplerate;
        channel = _channel;
        byteDepth = _byteDepth;
    }
    int sampleRate;
    int channel;
    int byteDepth;
};

class Denoise : public Module
{
public:
    Denoise(DenoiseProps _props);
    virtual ~Denoise();
    virtual bool init();
    virtual bool term();

protected:
#define FRAME_SIZE 480
    bool process(frame_container &frames);
    void addInputPin(framemetadata_sp &metadata, string &pinId);
    bool validateInputPins();
    bool validateInputOutputPins();
    bool validateOutputPins();

private:
    void setMetadata(framemetadata_sp &metadata);
    class Detail;
    boost::shared_ptr<Detail> mDetail;
    DenoiseProps props;
    std::string mDenoiseRawAudio;
    std::string mOutputPinId;
    std::string mOutputPinId1;
    const size_t CHUNK_SIZE = 480 * 2;
    DenoiseState *st;
    frame_sp mTempAudio;
    frame_sp fiveSecFrame;
    size_t mPrevSampleSize;
    size_t CHUNK_COUNT = 480;
    int counter = 0; 
    int check = counter + 30;
};