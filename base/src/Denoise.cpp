#include "Denoise.h"
// #include "opencv2/core/core.hpp"
// #include "opencv2/highgui/highgui.hpp"
// #include "opencv2/imgproc/imgproc.hpp"

Denoise::Denoise(DenoiseProps _props) : Module(TRANSFORM, "Denoise", _props), mPrevSampleSize(0)
{
	st = rnnoise_create(NULL);
	auto mDenoiseRawAudio = framemetadata_sp(new FrameMetadata(FrameMetadata::FrameType::AUDIO));
	mOutputPinId = addOutputPin(mDenoiseRawAudio);
	mOutputPinId1 = addOutputPin(mDenoiseRawAudio);
}

Denoise::~Denoise()
{
	rnnoise_destroy(st);
}
bool Denoise::validateInputPins()
{
	framemetadata_sp metadata = getFirstInputMetadata();
	FrameMetadata::FrameType frameType = metadata->getFrameType();
	if (frameType != FrameMetadata::AUDIO)
	{
		LOG_ERROR << "<" << getId() << ">::validateInputPins input frameType is expected to be AUDIO. Actual<" << frameType << ">";
		return false;
	}

	FrameMetadata::MemType memType = metadata->getMemType();
	if (memType != FrameMetadata::MemType::HOST)
	{
		LOG_ERROR << "<" << getId() << ">::validateInputPins input memType is expected to be host. Actual<" << memType << ">";
		return false;
	}
	if (getNumberOfInputPins() != 1)
	{
		LOG_ERROR << "<" << getId() << ">::validateOutputPins size is expected to be 1. Actual<" << getNumberOfInputPins() << ">";
		return false;
	}

	return true;
}

bool Denoise::validateInputOutputPins() //validateINputoutput
{
	if (getNumberOfOutputPins() != 2)
	{
		LOG_ERROR << "<" << getId() << ">::validateOutputPins size is expected to be 2. Actual<" << getNumberOfOutputPins() << ">";
		return false;
	}
	return true;
}

bool Denoise::validateOutputPins()
{
	framemetadata_sp metadata = getFirstOutputMetadata();
	auto mOutputFrameType = metadata->getFrameType();
	if (mOutputFrameType != FrameMetadata::AUDIO)
	{
		LOG_ERROR << "<" << getId() << ">::validateOutputPins input frameType is expected to be AUDIO. Actual<" << mOutputFrameType << ">";
		return false;
	}

	FrameMetadata::MemType memType = metadata->getMemType();
	if (memType != FrameMetadata::MemType::HOST)
	{
		LOG_ERROR << "<" << getId() << ">::validateOutputPins input memType is expected to be host. Actual<" << memType << ">";
		return false;
	}

	return true;
}
void Denoise::addInputPin(framemetadata_sp &metadata, string &pinId)
{
	Module::addInputPin(metadata, pinId);
}

bool Denoise::process(frame_container &frames)
{
	//take all incomming input frames and beak into 480 chunks
	auto frame = frames.cbegin()->second;
	auto extraSampleSize = (mPrevSampleSize + frame->size()) % (480 * sizeof(int16_t));
	auto a = frame->size();
	auto outSize = (mPrevSampleSize + frame->size()) - (extraSampleSize);
	auto outFrame = makeFrame(outSize);
	// LOG_INFO << frame->size();
	int chunks = (mPrevSampleSize + frame->size()) / CHUNK_SIZE;

	if (mPrevSampleSize != 0)
	{
		memcpy(outFrame->data(), mTempAudio->data(), mPrevSampleSize);
	}
	memcpy(outFrame->data() + mPrevSampleSize, frame->data(), frame->size() - extraSampleSize);

	//A float frame
	auto outFrame_float = makeFrame(outSize * (sizeof(float) - sizeof(int16_t)));
	auto denoise_size = (outFrame->data() + mPrevSampleSize);

	//convert int16 to float
	// cv::Mat M1(1, (outFrame->size()) / 2, CV_16S, outFrame->data());
	// cv::Mat M2(1, (outFrame->size()) / 2, CV_32F, outFrame_float->data());
	// M1.convertTo(M2, CV_32F);
	int16_t finalOutput[CHUNK_COUNT * chunks];

	for (int i = 0; i < chunks; i++)
	{
		int16_t input[CHUNK_COUNT];
		float x[CHUNK_COUNT];
		memcpy(input, outFrame->data() + (i * CHUNK_SIZE), CHUNK_SIZE);
		for (int index = 0; index < CHUNK_COUNT; index++)
		{
			x[index] = input[index];
		}
		//rnnoise_process_frame(st, static_cast<float *>(outFrame_float->data()) + i * CHUNK_COUNT, static_cast<float *>(outFrame_float->data()) + i * CHUNK_COUNT);
		rnnoise_process_frame(st, x, x);
		for (int index = 0; index < CHUNK_COUNT; index++)
		{
			finalOutput[index + i * CHUNK_COUNT] = x[index];
		}
	}
	memcpy(outFrame->data(), finalOutput, CHUNK_SIZE * chunks);

	// M2.convertTo(M1, CV_16S);

	// COPY extra samples
	memcpy(mTempAudio->data(), frame->data() + (frame->size() - extraSampleSize), extraSampleSize);
	mPrevSampleSize = extraSampleSize;
	frames.insert(make_pair(mOutputPinId, outFrame)); //send denoised frame
	frames.insert(make_pair(mOutputPinId1, frame));	  //send normal frame
	send(frames);

	return true;
}

void Denoise::setMetadata(framemetadata_sp &metadata)
{
	//auto samplerate =  get.samplerate();
	//auto channel = get.channel();
	//auto bitdepth = get.bit();
}

bool Denoise::init()
{
	mTempAudio = makeFrame(480 * 2);
	auto size = sizeof(float) * 2400000;
	fiveSecFrame = makeFrame(size);
	return Module::init();
}

bool Denoise::term()
{
	return Module::term();
}
