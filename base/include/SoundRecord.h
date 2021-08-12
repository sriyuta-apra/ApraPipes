#pragma once

#include "Module.h"

class SoundRecordProps: public ModuleProps
{
public:
    SoundRecordProps(int _samplerate = 48000, int _channel=1, int _byteDepth = 2, int _device = 0) : ModuleProps()
   {
      sampleRate = _samplerate;
      channel = _channel;
      byteDepth = _byteDepth;
      device = _device;
   }
   int sampleRate;
   int channel;
   int byteDepth;
   int device;
};

class SoundRecord : public Module {
public:
	SoundRecord(SoundRecordProps _props);
	virtual ~SoundRecord() {}

	virtual bool init();
	virtual bool term();

protected:	
	bool validateOutputPins();	
    bool produce();

private:
    class Detail;
	boost::shared_ptr<Detail> mDetail;
    std::string mOutputRawAudio;
    std::string mOutputPinId;
};