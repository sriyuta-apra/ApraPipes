#include "Frame.h"
#include "FrameMetadata.h"
#include "OpencvWebcam.h"

cv::VideoCapture vid_capture;
class OpencvWebcam::Detail
{
public:
	typedef std::function<void(frame_sp &)> SendFrame;
	Detail(SendFrame sendFrame, std::function<frame_sp(size_t)> _makeFrame) : mMakeFrame(_makeFrame), mSendFrame(sendFrame)
	{
	}
	~Detail()
	{
	}

	bool initWebCam(int vidCapWidth, int vidCapHeight)
	{

		vid_capture.set(cv::CAP_PROP_FRAME_WIDTH, vidCapWidth);
		vid_capture.set(cv::CAP_PROP_FRAME_HEIGHT, vidCapHeight);
		return true;
	}

	bool retrieveImage()
	{

		bool isSuccess = vid_capture.read(img);
		if (isSuccess == false)
		{
			cout << "Stream disconnected" << endl;
		}
		if (isSuccess == true)
		{
			size_t t = static_cast<size_t>(img.step[0] * img.rows);
			auto frame = mMakeFrame(t);
			memcpy(frame->data(), &img.data[0], frame->size());
			mSendFrame(frame);
		}
		return true;
	}

	bool termWebCam()
	{
		vid_capture.release();
		return true;
	}

private:
	
	std::function<frame_sp(size_t)> mMakeFrame;
	SendFrame mSendFrame;
	cv::Mat img;
};

OpencvWebcam::OpencvWebcam(OpencvWebcamProps _props) : Module(SOURCE, "OpencvWebcam", _props), mProps(_props)
{
	auto outputMetadata = framemetadata_sp(new RawImageMetadata(_props.vidCapWidth, _props.vidCapHeight, ImageMetadata::BGR, CV_8UC3, size_t(0), CV_8U, FrameMetadata::MemType::HOST, true));
	std::string mOutputPinId = addOutputPin(outputMetadata);
	int deviceID = 0;
	int apiID = cv::CAP_ANY;
	vid_capture.open(-1);
	// mDetail->vid_capture.open(-1);
	mDetail.reset(new Detail([&, mOutputPinId](frame_sp &frame) -> void
							 {
								 frame_container frames;
								 frames.insert(make_pair(mOutputPinId, frame));
								 send(frames);
							 },
							 [&](size_t size) -> frame_sp
							 {
								 return makeFrame(size);
							 }));
}

OpencvWebcam::~OpencvWebcam() {}

bool OpencvWebcam::validateOutputPins()
{
	if (getNumberOfOutputPins() != 1)
	{
		LOG_ERROR << "<" << getId() << ">::validateOutputPin size is expected to be 1. Actual<" << getNumberOfInputPins() << ">";
		return false;
	}
	framemetadata_sp metadata = getFirstOutputMetadata();
	FrameMetadata::FrameType frameType = metadata->getFrameType();
	if (frameType != FrameMetadata::RAW_IMAGE)
	{
		LOG_ERROR << "<" << getId() << ">::validateOutputPins input frameType is expected to be RAW_IMAGE. Actual<" << frameType << ">";
		return false;
	}

	return true;
}

bool OpencvWebcam::produce()
{
	mDetail->retrieveImage();
	return true;
}

bool OpencvWebcam::init()
{
	return (mDetail->initWebCam(mProps.vidCapWidth, mProps.vidCapHeight)) && (Module::init());
}

bool OpencvWebcam::term()
{
	return (mDetail->termWebCam()) && (Module::term());
}