#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <string.h>
#include <dlfcn.h>
#include <vector>

#include "deepspeech.h"
#include "SpeechToText.h"
#include "Logger.h"
#include "AIPExceptions.h"

extern "C"
{
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

class SpeechToText::Detail
{
public:
    Detail(SpeechToTextProps props)
    {
    }
    double jaro_distance(string s1, string s2) 
    {
        // If the strings are equal
        if (s1 == s2)
            return 1.0;

        // Length of two strings
        int len1 = s1.length(),
            len2 = s2.length();

        // Maximum distance upto which matching
        // is allowed
        int max_dist = floor(max(len1, len2) / 2) - 1;

        // Count of matches
        int match = 0;

        // Hash for matches
        int hash_s1[s1.length()] = { 0 },
            hash_s2[s2.length()] = { 0 };

        // Traverse through the first string
        for (int i = 0; i < len1; i++) {
        
            // Check if there is any matches
            for (int j = max(0, i - max_dist);
                    j < min(len2, i + max_dist + 1); j++)

                // If there is a match
                if (s1[i] == s2[j] && hash_s2[j] == 0) {
                    hash_s1[i] = 1;
                    hash_s2[j] = 1;
                    match++;
                    break;
                }
        }

        // If there is no match
        if (match == 0)
            return 0.0;

        // Number of transpositions
        double t = 0;

        int point = 0;

        // Count number of occurrences
        // where two characters match but
        // there is a third matched character
        // in between the indices
        for (int i = 0; i < len1; i++)
            if (hash_s1[i]) {
            
                // Find the next matched character
                // in second string
                while (hash_s2[point] == 0)
                    point++;

                if (s1[i] != s2[point++])
                    t++;
            }

        t /= 2;

        // Return the Jaro Similarity
        return (((double)match) / ((double)len1)
                + ((double)match) / ((double)len2)
                + ((double)match - t) / ((double)match))
                / 3.0;
    }
    ~Detail() {}
};

//Constructor
SpeechToText::SpeechToText(SpeechToTextProps _props) : Module(TRANSFORM, "SpeechToText", _props), tBufferSize(0)
{
    mDetail.reset(new Detail(_props));
    auto mText = framemetadata_sp(new FrameMetadata(FrameMetadata::FrameType::GENERAL)); //need to change FrameType
    mOutputPinId = addOutputPin(mText);
    count = 0;
    int status = DS_CreateModel("/home/developer/development/aprapipes_audio/data/deepspeech-0.9.3-models.pbmm", &ctx);
    status = DS_EnableExternalScorer(ctx, "/home/developer/Downloads/native_client.amd64.tflite.linux/bluebox.scorer");
    // status = DS_EnableExternalScorer(ctx, "/home/developer/development/aprapipes_audio/data/deepspeech-0.9.3-models.scorer");
    
    try
    {
      
        swr = swr_alloc_set_opts(NULL,
                                 AV_CH_LAYOUT_MONO,
                                 AV_SAMPLE_FMT_S16,
                                 16000,
                                 props.src_ch_layout,
                                 props.src_sample_fmt,
                                 props.src_rate,
                                 0,
                                 NULL);
        swr_init(swr);
    }
    catch (...)
    {
        throw AIPException(AIP_FATAL, "Failed to init STT");
    }
}

//Destructor
SpeechToText::~SpeechToText()
{
    swr_free(&swr);
    DS_FreeModel(ctx);
}

//adding and validating input output pins
bool SpeechToText::validateInputPins()
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

bool SpeechToText::validateOutputPins()
{
    framemetadata_sp metadata = getFirstOutputMetadata();
    auto mOutputFrameType = metadata->getFrameType();
    if (mOutputFrameType != FrameMetadata::GENERAL)
    {
        LOG_ERROR << "<" << getId() << ">::validateOutputPins input frameType is expected to be general. Actual<" << mOutputFrameType << ">";
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
void SpeechToText::addInputPin(framemetadata_sp &metadata, string &pinId)
{
    Module::addInputPin(metadata, pinId);
}

//process
bool SpeechToText::process(frame_container &frames)
{
    LOG_INFO << 3;
    auto frame = frames.cbegin()->second; // taking the data from the mapping
    if (isFrameEmpty(frame))
    {
        return true;
    }
    else
    {
        if(tBufferSize <= props.BufferSize)
        {
            fVector.insert(fVector.end(), static_cast<uint16_t*>(frame->data()), static_cast<uint16_t*>(frame->data())+frame->size());
            tBufferSize+= frame->size();
        }
        else
        {
            fVector.insert(fVector.end(), static_cast<uint16_t*>(frame->data()), static_cast<uint16_t*>(frame->data())+frame->size());
            auto outFrame = makeFrame(fVector.size());
            memcpy(outFrame->data(), &fVector[0], fVector.size());
            frames.insert(make_pair(mOutputPinId, outFrame));
            send(frames);
            LOG_INFO << "frames sent";
            tBufferSize=0;
            fVector.clear();
        }
    }
    // else
    // { 
    //     if(tBufferSize < props.BufferSize)
    //     {
    //         //initializing the stream
    //         if(tBufferSize ==0)
    //         {
    //             int status = DS_CreateStream(ctx, &sCtx);
    //         }
    //         //register the audio to a vector before resampling

    //         //do resampling
    //         const uint8_t* src_data =  (const uint8_t*)frame->data(); 
    //         uint8_t *dst_data;                             
    //         int dst_linesize;     
    //         int src_nb_samples = frame->size() / (2*av_get_channel_layout_nb_channels(props.src_ch_layout)); 

    //         int dst_nb_channels = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_MONO);
    //         int dst_nb_samples = av_rescale_rnd(swr_get_delay(swr, props.src_rate) + src_nb_samples, 16000, props.src_rate, AV_ROUND_UP);
    //         av_samples_alloc(&dst_data, &dst_linesize, dst_nb_channels, dst_nb_samples, AV_SAMPLE_FMT_S16, 1);
    //         int ret = swr_convert(swr, &dst_data, dst_nb_samples, &src_data, src_nb_samples);
    //         int dst_bufsize = av_samples_get_buffer_size(&dst_linesize, dst_nb_channels,ret, AV_SAMPLE_FMT_S16, 1);
            
    //         //feed the resampled frames to the stream
    //         DS_FeedAudioContent(sCtx, (const short*)dst_data, (unsigned int)ret);

    //         //do intermediate decode for the samples
    //         Metadata* partialMetadata = DS_IntermediateDecodeWithMetadata(sCtx,1);

    //         //get the entire sentence from the metadata 
    //         const CandidateTranscript* InterTrans = &partialMetadata->transcripts[0];
    //         std::string sentence;
    //         for (int i = 0; i < InterTrans->num_tokens; i++)
    //             sentence.append(InterTrans->tokens[i].text);
            
    //         LOG_INFO << sentence;
            
    //         //do jaro similarity test to the sentence
    //         double score = mDetail->jaro_distance(sentence, "hello blue box my name is ");

    //         //set threshold (temporarily inside here, later outside) = 0.77
    //         if(score > 0.5 && score < 1)
    //         {
    //             LOG_INFO << "Jaro score above threshold " << "for this sentence : " << sentence;
    //             //reset the tBufferSize
    //             tBufferSize =0;
    //             sentence = "";
    //             DS_FreeStream(sCtx);
    //             DS_FreeMetadata(partialMetadata);
    //         }
    //         else
    //         {
    //             LOG_INFO << "Jaro score below threshold " << "for this sentence : " << sentence;
    //             tBufferSize+= dst_bufsize;
    //             sentence = "";
    //         }            
    //     }
    //     else
    //     {
    //         DS_FreeStream(sCtx); //or we can do DS_FinishStream here for the last frame
    //         tBufferSize =0;
            
    //     }
    // }
    return true;
}

//init and term
bool SpeechToText::init()
{
    return Module::init();
}
bool SpeechToText::term()
{            
    return Module::term();
}