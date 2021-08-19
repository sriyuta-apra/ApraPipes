#include "stdafx.h"
#include <boost/test/unit_test.hpp>
#include <boost/filesystem.hpp>

#include "Module.h"
#include "PipeLine.h"
#include "Logger.h"
#include "AIPExceptions.h"
#include "FrameMetadata.h"
#include "FrameMetadataFactory.h"
#include "Frame.h"

#include "SoundRecord.h"
#include "Denoise.h"
#include "SpeechToText.h"
#include "FileWriterModule.h"

#include "test_utils.h" 

BOOST_AUTO_TEST_SUITE(SpeechToText_test)

BOOST_AUTO_TEST_CASE(test_0)
{
    Logger::setLogLevel(boost::log::trivial::severity_level::info);
    SoundRecordProps sourceProps; //not passing anything to the constructor
    auto source = boost::shared_ptr<Module>(new SoundRecord(sourceProps));

    auto fileWriter_without_denoise = boost::shared_ptr<Module>(new FileWriterModule(FileWriterModuleProps("../test_mono_original.wav", true)));
    auto fileWriter_with_deepspeechAudio = boost::shared_ptr<Module>(new FileWriterModule(FileWriterModuleProps("../test_stt.wav", true)));

    DenoiseProps denoiseProps;
    auto denoiser = boost::shared_ptr<Module>(new Denoise(denoiseProps));
    source->setNext(denoiser);
    source->setNext(fileWriter_without_denoise);

    // //
    auto fileWriter_with_denoise = boost::shared_ptr<Module>(new FileWriterModule(FileWriterModuleProps("../test_mono_denoise.wav", true)));
    auto p1 = denoiser->getAllOutputPinsByType(FrameMetadata::AUDIO);
    LOG_INFO << p1[0].size();
    // LOG_INFO << p1[1].size();
    std::vector<string> pins,pins1;
    pins.push_back(p1[0]);
    pins1.push_back(p1[1]);
    SpeechToTextProps sttProps;
    auto stt = boost::shared_ptr<Module>(new SpeechToText(sttProps));
    denoiser->setNext(stt, pins);
    denoiser->setNext(fileWriter_with_denoise,pins);
    stt->setNext(fileWriter_with_deepspeechAudio);

    PipeLine stt_p("test_0");
    stt_p.appendModule(source);
    stt_p.init();
    stt_p.run_all_threaded();
    boost::this_thread::sleep_for(boost::chrono::seconds(20));
    stt_p.stop();
    stt_p.term();
    stt_p.wait_for_all();
}
BOOST_AUTO_TEST_SUITE_END()
