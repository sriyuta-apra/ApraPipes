#include <cstdint>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>
}

#include "RTSPPusher.h"
#include "H264FrameDemuxer.h"
#include "H264Utils.h"
#include "H264ParserUtils.h"
#include "Frame.h"

class RTSPPusher::Detail
{
	/* video output */
	AVFrame *frame;
	AVPicture src_picture, dst_picture;
	AVFormatContext *outContext;
	AVStream *video_st;
	AVCodec *video_codec;
	string mURL;
	string mTitle;
	int frameCount;
	AVRational in_time_base;
	int64_t prev_pts;
	// int64_t prev_mfEnd;

	AVStream *add_stream(AVFormatContext *oc, AVCodec **codec, enum AVCodecID codec_id, int num, int den)
	{
		LOG_TRACE << "add_stream enter";
		AVCodecContext *c;
		AVStream *st = 0;

		/* find the encoder */
		*codec = avcodec_find_encoder(codec_id);
		if (!(*codec))
		{
			// av_log(NULL, AV_LOG_ERROR, "Could not find encoder for '%s'.\n", avcodec_get_name(codec_id));
			LOG_ERROR << "Could not find encoder for ^" << avcodec_get_name(codec_id) << "^"
					  << "\n";
		}
		else
		{
			st = avformat_new_stream(oc, *codec);
			if (!st)
			{
				// av_log(NULL, AV_LOG_ERROR, "Could not allocate stream.\n");
				LOG_ERROR << "Could not allocate stream."
						  << "\n";
			}
			else
			{
				st->id = oc->nb_streams - 1;
				st->time_base.den = /*st->pts.den = */ 90000;
				st->time_base.num = /*st->pts.num = */ 1;

				in_time_base.den = den;
				in_time_base.num = num;
				c = st->codec;
				c->codec_id = codec_id;
				c->bit_rate = (int)bitrate;
				c->width = (int)width;
				c->height = (int)height;
				c->time_base.den = (int)fps_num; //Note: time_base is inverse of fps hence this
				c->time_base.num = (int)fps_den; //Note: time_base is inverse of fps hence this
				c->gop_size = 32;				 /* emit one intra frame every twelve frames at most */
				c->pix_fmt = AV_PIX_FMT_YUV420P;
				c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
			}
		}
		LOG_TRACE << "add_stream exit";
		return st;
	}

	int open_video()
	{
		int ret;
		AVCodecContext *c = video_st->codec;

		//AK: the following line initializes extradata and extradata size
		ret = avcodec_open2(c, video_codec, NULL);
		if (ret < 0)
		{
			// av_log(NULL, AV_LOG_ERROR, "Could not open video codec.\n", avcodec_get_name(c->codec_id));
			LOG_ERROR << "Could not open video codec. ^" << avcodec_get_name(c->codec_id) << "^"
					  << "\n";
		}
		else
		{

			/* allocate and init a re-usable frame */
			frame = av_frame_alloc();
			if (!frame)
			{
				// av_log(NULL, AV_LOG_ERROR, "Could not allocate video frame.\n");
				LOG_ERROR << "Could not allocate video frame."
						  << "\n";
				ret = -1;
			}
			else
			{
				frame->format = c->pix_fmt;
				frame->width = c->width;
				frame->height = c->height;

				/* Allocate the encoded raw picture. */
				ret = avpicture_alloc(&dst_picture, c->pix_fmt, c->width, c->height);
				if (ret < 0)
				{
					// av_log(NULL, AV_LOG_ERROR, "Could not allocate picture.\n");
					LOG_ERROR << "Could not allocate picture."
							  << "\n";
				}
				else
				{
					/* copy data and linesize picture pointers to frame */
					*((AVPicture *)frame) = dst_picture;
				}
			}
		}

		return ret;
	}

	int open_video_precoded()
	{
		AVCodecContext *c = video_st->codec;

		c->extradata = (uint8_t *)(demuxer->getSPS_PPS().data());
		c->extradata_size = (int)demuxer->getSPS_PPS().size();
		lastDiff = pts_adder = lastPTS = 0;
		return 0;
	}

	void fix_pts(int64_t &pts_org)
	{
		LOG_TRACE << "The PTS org is :" << pts_org << "and last PTS : " << lastPTS;
		if (pts_org > lastPTS)
		{
			lastDiff = pts_org - lastPTS;
			lastPTS = pts_org;
		}
		else
		{
			// pts_adder = lastPTS - pts_org + lastDiff;
			pts_adder = pts_adder + lastPTS - pts_org + lastDiff;
			LOG_TRACE << "PTS_ORG: " << pts_org << ": PTS_ADDER: " << pts_adder << ": LAST_PTS:" << lastPTS << ": LAST_DIFF: " << lastDiff;
			// lastPTS = pts_org;
			lastPTS = pts_org;
		}
		pts_org += pts_adder;
	}

	/* Prepare a dummy image. */
	void fill_yuv_image(AVPicture *pict, int frame_index, int width, int height)
	{
		int x, y, i;

		i = frame_index;

		/* Y */
		for (y = 0; y < height; y++)
			for (x = 0; x < width; x++)
				pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;

		/* Cb and Cr */
		for (y = 0; y < height / 2; y++)
		{
			for (x = 0; x < width / 2; x++)
			{
				pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
				pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
			}
		}
	}

	int write_video_frame(AVFormatContext *oc, AVStream *st)
	{
		int ret = 0;
		AVCodecContext *c = st->codec;

		fill_yuv_image(&dst_picture, frameCount, c->width, c->height);

		AVPacket pkt = {0};
		int got_packet;
		av_init_packet(&pkt);

		/* encode the image */
		frame->pts = frameCount;
		ret = avcodec_encode_video2(c, &pkt, frame, &got_packet);
		if (ret < 0)
		{
			// av_log(NULL, AV_LOG_ERROR, "Error encoding video frame.\n");
			LOG_ERROR << "Error encoding video frame."
					  << "\n";
		}
		else
		{
			if (got_packet)
			{
				pkt.stream_index = st->index;
				pkt.pts = av_rescale_q_rnd(pkt.pts, c->time_base, st->time_base, AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
				ret = av_write_frame(oc, &pkt);

				if (ret < 0)
				{
					// av_log(NULL, AV_LOG_ERROR, "Error while writing video frame.\n");
					LOG_ERROR << "Error while writing video frame."
							  << "\n";
				}
			}
		}

		return ret;
	}

	bool init_stream_params()
	{
		LOG_TRACE << "init_stream_params";
		const_buffer sps_pps = demuxer->getSPS_PPS();
		sps_pps_properties p;
		H264ParserUtils::parse_sps(((const char *)sps_pps.data()) + 5, sps_pps.size() > 5 ? sps_pps.size() - 5 : sps_pps.size(), &p);
		width = p.width;
		height = p.height;
		bitrate = 2000000;
		fps_den = 1;
		fps_num = 30;

		return true;
	}

public:
	// We need to pass object reference to refer to the connection status etc
	// bool write_precoded_video_frame(boost::shared_ptr<Frame>& f, RTSPPusher& rtspMod)
	bool write_precoded_video_frame(boost::shared_ptr<Frame> &f)
	{
		mutable_buffer &codedFrame = *(f.get());
		bool isKeyFrame = (f->mFrameType == H264Utils::H264_NAL_TYPE_IDR_SLICE);

		AVCodecContext *c = video_st->codec;
		AVPacket pkt = {0};
		av_init_packet(&pkt);

		/* encode the image */
		frameCount++;
		pkt.stream_index = video_st->index;
		//pkt.duration = f->mFEnd - lastPTS;
		//pkt.pts=lastPTS= f->mFEnd;
		//pkt.pts = AV_NOPTS_VALUE;
		//pkt.pts = av_rescale_q_rnd(frameCount, c->time_base, video_st->time_base, AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.pts = av_rescale_q_rnd(f->mFEnd, in_time_base, video_st->time_base, AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		// LOG_INFO << "frame mfend: "<< f->mFEnd << "- diff is : "<< (f->mFEnd - prev_mfEnd) << " -PTS: "<< pkt.pts;
		// prev_mfEnd = f->mFEnd;
		fix_pts(pkt.pts);

		//LOG_TRACE << "PTS is : " << pkt.pts << " and the diff is :" << (pkt.pts - prev_pts) << "frame index : " << f->fIndex;
		//
		// printf("diff is : %lld and the frame index is %lld\n", (pkt.pts - prev_pts), f->fIndex);
		prev_pts = pkt.pts;

		pkt.data = (uint8_t *)codedFrame.data();
		pkt.size = (int)codedFrame.size();
		if (isKeyFrame)
			pkt.flags |= AV_PKT_FLAG_KEY;

		int ret = av_write_frame(outContext, &pkt);

		if (ret < 0)
		{
			// av_log(NULL, AV_LOG_ERROR, "Error while writing video frame.ret = %d\n", ret);

			char avErrorBuf[500] = {'\0'};
			av_strerror(ret, avErrorBuf, 500);

			LOG_ERROR << "Error while writing video frame : " << ret << ":" << avErrorBuf << ":" << pkt.pts << "\n";

			// On evostream going down the return code is -32 - errno.h says 32 is EPIPE but
			// AVERROR_EOF not coming as -32

			if (ret == -32) // Need to resolve the corresponding enum in AVERROR header files etc
			{
				connectionStatus = CONNECTION_FAILED;
				// emit the event after returning
			}

			return false;
		}
		return true;
	}
	size_t width, height, bitrate, fps_den, fps_num;
	int64_t lastPTS, lastDiff, pts_adder;
	boost::shared_ptr<H264FrameDemuxer> demuxer;
	bool precoded;
	EventType connectionStatus;
	bool isFirstFrame;

	Detail(RTSPPusherProps props) : mURL(props.URL), mTitle(props.title), precoded(true), connectionStatus(CONNECTION_FAILED), isFirstFrame(false)
	{
		demuxer = boost::shared_ptr<H264FrameDemuxer>(new H264FrameDemuxer());
	}
	~Detail()
	{
	}

	bool init()
	{
		const char *url = mURL.c_str();

		int ret = 0;
		frameCount = 0;

		av_log_set_level(AV_LOG_INFO);

		av_register_all();
		avformat_network_init();

		int rc = avformat_alloc_output_context2(&outContext, NULL, "rtsp", url);

		if (rc < 0)
		{
			// av_log(NULL, AV_LOG_FATAL, "Alloc failure in avformat %d\n", rc);
			LOG_FATAL << "Alloc failure in avformat : " << rc << "<>" << url;			
			{
				char errBuf[AV_ERROR_MAX_STRING_SIZE];
				size_t errBufSize = AV_ERROR_MAX_STRING_SIZE;
				av_strerror(rc, errBuf, errBufSize);

				LOG_ERROR << errBuf;			
			}
			return false;
		}

		if (!outContext)
		{
			// av_log(NULL, AV_LOG_FATAL, "Could not allocate an output context for '%s'.\n", url);
			LOG_FATAL << "Could not allocate an output context for : "
					  << "^" << url << "^"
					  << "\n";
			return false;
		}

		if (!outContext->oformat)
		{
			// av_log(NULL, AV_LOG_FATAL, "Could not create the output format for '%s'.\n", url);
			LOG_FATAL << "Could not create the output format for : "
					  << "^" << url << "^"
					  << "\n";
			return false;
		}
		return true;
	}

	bool write_header(int num, int den)
	{
		init_stream_params();
		int ret = 0;
		video_st = add_stream(outContext, &video_codec, AV_CODEC_ID_H264, num, den);

		/* Now that all the parameters are set, we can open the video codec and allocate the necessary encode buffers. */
		if (video_st)
		{
			// av_log(NULL, AV_LOG_DEBUG, "Video stream codec %s.\n ", avcodec_get_name(video_st->codec->codec_id));

			LOG_INFO << "Video stream codec : ^" << avcodec_get_name(video_st->codec->codec_id) << "^"
					 << "\n";

			if (precoded)
				ret = open_video_precoded();
			else
				ret = open_video();
			if (ret < 0)
			{
				// av_log(NULL, AV_LOG_FATAL, "Open video stream failed.\n");
				LOG_FATAL << "Open video stream failed."
						  << "\n";
				return false;
			}
		}
		else
		{
			LOG_FATAL << "Add video stream for the codec : ^" << avcodec_get_name(AV_CODEC_ID_H264) << "^ "
					  << "failed"
					  << "\n";
			return false;
		}

		AVDictionary *options = NULL;

		// string networkMode = configData.getValue("Network_Mode");
		string networkMode = "udp";
		bool isTcp = true;
		LOG_INFO << "Network mode being used :" << networkMode;
		if (networkMode.compare("udp") == 0)
		{
			isTcp = false;
		}

		av_dict_set(&outContext->metadata, "title", mTitle.c_str(), 0);
		av_dict_set(&options, "rtsp_transport", isTcp ? "tcp" : "udp", 0);

		av_dump_format(outContext, 0, mURL.c_str(), 1);

		AVFormatContext *ac[] = {outContext};
		char buf[1024];
		av_sdp_create(ac, 1, buf, 1024);

		ret = avformat_write_header(outContext, &options);

		// Why is it not returning if RTSP server is not available?
		// is there some timeout mechanism that needs to be implemented?
		if (ret != 0)
		{
			// av_log(NULL, AV_LOG_ERROR, "Failed to connect to RTSP server for '%s'.\n", mURL.c_str());

			LOG_ERROR << "Failed to connect to RTSP server for ^" << mURL.c_str() << "^"
					  << "\n";
			return false;
		}
		return true;
	}

	bool term(EventType status)
	{
		if (video_st)
		{
			if (status != CONNECTION_FAILED)
			{
				av_write_trailer(outContext);
			}
			if (precoded)
			{
				video_st->codec->extradata = 0;
				video_st->codec->extradata_size = 0;
			}
			else
			{
				av_free(src_picture.data[0]);
				av_free(dst_picture.data[0]);
				av_frame_free(&frame);
			}
			avcodec_close(video_st->codec);
		}

		if (outContext)
		{
			avformat_free_context(outContext);
			return true;
		}
		else
		{
			return false;
		}

		// if connection has not been established, free context is causing an
		// an unhandled exception  because of heap correuption
		// if we free outContext without freeing up the other members of video_st
	}
};

RTSPPusher::RTSPPusher(RTSPPusherProps props) : Module(SINK, "RTSPPusher", props)
{
	mDetail.reset(new RTSPPusher::Detail(props));

	//handles the frame drops and initial parsing
	adaptQueue(mDetail->demuxer);
}

RTSPPusher::~RTSPPusher()
{
	mDetail.reset();
}

bool RTSPPusher::init()
{
	if (!Module::init())
	{
		return false;
	}

	return mDetail->init();
}

bool RTSPPusher::term()
{
	bool bRC = mDetail->term(mDetail->connectionStatus);
	if (mDetail->connectionStatus == WRITE_FAILED || mDetail->connectionStatus == STREAM_ENDED)
	{
		// self destruct
		// emit_fatal(mDetail->connectionStatus);
	}

	auto res = Module::term();

	return bRC && res;
}

bool RTSPPusher::validateInputPins()
{
	if (getNumberOfInputPins() != 1)
	{
		LOG_ERROR << "<" << getId() << ">::validateInputPins size is expected to be 1. Actual<" << getNumberOfInputPins() << ">";
		return false;
	}

	framemetadata_sp metadata = getFirstInputMetadata();
	FrameMetadata::FrameType frameType = metadata->getFrameType();
	if (frameType != FrameMetadata::H264_DATA)
	{
		LOG_ERROR << "<" << getId() << ">::validateInputPins input frameType is expected to be H264_DATA. Actual<" << frameType << ">";
		return false;
	}

	if (metadata->getMemType() != FrameMetadata::HOST)
	{
		LOG_ERROR << "<" << getId() << ">::validateInputPins input MemType is expected to be HOST. Actual<" << metadata->getMemType() << ">";
		return false;
	}

	return true;
}

bool RTSPPusher::process(frame_container &frames)
{
	auto frame = frames.begin()->second;

	if (mDetail->connectionStatus != CONNECTION_READY)
	{
		return true;
	}

	if(mDetail->isFirstFrame)
	{
		mDetail->isFirstFrame = false;
		return true;
	}

	// non-first frame
	if (!mDetail->write_precoded_video_frame(frame))
	{
		mDetail->connectionStatus = WRITE_FAILED;
		LOG_FATAL << "write_precoded_video_frame failed";

		// return false;
	}

	return true;
}

bool RTSPPusher::processSOS(frame_sp &frame)
{
	LOG_TRACE << "at first frame";
	//stick the sps/pps into extradata
	if (mDetail->write_header(frame->m_num, frame->m_den) && mDetail->write_precoded_video_frame(frame))
	{
		//written header and first frame both.
		mDetail->connectionStatus = CONNECTION_READY;
		mDetail->isFirstFrame = true;
		// emit_event(CONNECTION_READY);
		return true;
	}
	else
	{
		LOG_ERROR << "Could not write stream header... stream will not play !!"
				  << "\n";
		mDetail->connectionStatus = CONNECTION_FAILED;
		// emit_event(CONNECTION_FAILED);
		return false;
	}

	return true;
}

bool RTSPPusher::shouldTriggerSOS()
{
	return mDetail->connectionStatus == CONNECTION_FAILED || mDetail->connectionStatus == STREAM_ENDED;
}

bool RTSPPusher::processEOS(string &pinId)
{
	mDetail->connectionStatus = STREAM_ENDED;

	return true;
}
