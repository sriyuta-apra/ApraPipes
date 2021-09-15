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

#include "FileReaderModule.h"
#include "textOverlay.h"

#include "OpencvWebcam.h"
#include "FileWriterModule.h"
#include "ExternalSinkModule.h"
#include "ImageEncoderCV.h"

#include "test_utils.h"

BOOST_AUTO_TEST_SUITE(ApraPipes_ut)

BOOST_AUTO_TEST_CASE(test_5)
{
    OpencvWebcamProps sourceProps(1280, 720);
    auto source = boost::shared_ptr<Module>(new OpencvWebcam(sourceProps));

    auto imageEncoder = boost::shared_ptr<ImageEncoderCV>(new ImageEncoderCV(ImageEncoderCVProps()));
    // source->setNext(imageEncoder);

    auto fileWriter = boost::shared_ptr<Module>(new FileWriterModule(FileWriterModuleProps("/home/developer/ApraPipes-local/ApraPipes/data/cameraModule/WebcamOutput????.raw")));
    source->setNext(fileWriter);

    PipeLine p("test_5");
    p.appendModule(source);
    p.init();
    p.run_all_threaded();
    boost::this_thread::sleep_for(boost::chrono::seconds(20));
    p.stop();
    p.term();
    p.wait_for_all(true);
}

BOOST_AUTO_TEST_CASE(test_6)
{	
	auto fileReader = boost::shared_ptr<FileReaderModule>(new FileReaderModule(FileReaderModuleProps("./data/frame_1280x720_rgb.raw")));
	auto metadata = framemetadata_sp(new RawImageMetadata(1280,720,ImageMetadata::ImageType::RGB, CV_8UC3, 0, CV_8U, FrameMetadata::HOST, true));
	fileReader->addOutputPin(metadata);

	// auto stream = cudastream_sp(new ApraCudaStream);

	auto textOverlayModule = boost::shared_ptr<Module>(new textOverlay(textOverlayProps((std::string)"TRIAL", cv::Point (100,200), 3, 5.0, cv::Scalar (0,255,0), 5,8)));
    fileReader->setNext(textOverlayModule);

	auto sink = boost::shared_ptr<ExternalSinkModule>(new ExternalSinkModule());
    textOverlayModule->setNext(sink);

	BOOST_TEST(fileReader->init());
	BOOST_TEST(textOverlayModule->init());
	// BOOST_TEST(copy->init());
	BOOST_TEST(sink->init());	
	
	fileReader->step();
	textOverlayModule->step();
	// copy->step();
	auto frames = sink->pop();
	BOOST_TEST(frames.size() == 1);
	auto outputFrame = frames.cbegin()->second;
	BOOST_TEST(outputFrame->getMetadata()->getFrameType() == FrameMetadata::RAW_IMAGE);

	Test_Utils::saveOrCompare("./data/testOutput/textOverlay_testOutput.raw", const_cast<const uint8_t*>(static_cast<uint8_t*>(outputFrame->data())), outputFrame->size(), 0);
}
BOOST_AUTO_TEST_CASE(test_8)
{	
	auto fileReader = boost::shared_ptr<FileReaderModule>(new FileReaderModule(FileReaderModuleProps("./data/frame_1280x720_rgb.raw")));
	auto metadata = framemetadata_sp(new RawImageMetadata(1280,720,ImageMetadata::ImageType::RGB, CV_8UC3, 0, CV_8U, FrameMetadata::HOST, true));
	fileReader->addOutputPin(metadata);

	auto imageOverlayModule = boost::shared_ptr<Module>(new imageOverlay(imageOverlayProps());
    fileReader->setNext(textOverlayModule);

	auto sink = boost::shared_ptr<ExternalSinkModule>(new ExternalSinkModule());
    imageOverlayModule->setNext(sink);

	BOOST_TEST(fileReader->init());
	BOOST_TEST(textOverlayModule->init());
	BOOST_TEST(sink->init());	
	
	fileReader->step();
	imageOverlayModule->step();

	auto frames = sink->pop();
	BOOST_TEST(frames.size() == 1);
	auto outputFrame = frames.cbegin()->second;
	BOOST_TEST(outputFrame->getMetadata()->getFrameType() == FrameMetadata::RAW_IMAGE);

	Test_Utils::saveOrCompare("./data/testOutput/imageOverlay_testOutput.raw", const_cast<const uint8_t*>(static_cast<uint8_t*>(outputFrame->data())), outputFrame->size(), 0);
}
BOOST_AUTO_TEST_SUITE_END()
