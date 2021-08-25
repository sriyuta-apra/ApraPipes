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

/* 1. clearing out fVector by discarding every other by comparing string
2. 


*/
class SpeechToText::Detail
{
public:
    Detail(SpeechToTextProps _props)
    {
        props = _props;
    }
    double jaro_distance(string s1, string s2)
    {
        int missing = 0;
        // Length of two strings
        int len1 = s1.length(), len2 = s2.length();
        // If the strings are equal
        if (s1 == s2)
        {
            return 1.0;
        }
        string string1 = s1;

        // Maximum distance upto which matching
        // is allowed
        int max_dist = floor(max(len1, len2) / 2) - 1;

        // Count of matches
        int match = 0;

        // Hash for matches
        int hash_s1[s1.length()] = {0},
            hash_s2[s2.length()] = {0};

        // Traverse through the first string
        for (int i = 0; i < len1; i++)
        {

            // Check if there is any matches
            for (int j = max(0, i - max_dist);
                 j < min(len2, i + max_dist + 1); j++)

                // If there is a match
                if (s1[i] == s2[j] && hash_s2[j] == 0)
                {
                    hash_s1[i] = 1;
                    hash_s2[j] = 1;
                    match++;
                    break;
                }
        }

        // If there is no match
        if (match == 0)
        {
            return 0.0;
        }

        // Number of transpositions
        double t = 0;

        int point = 0;

        for (int i = 0; i < len1; i++)
            if (hash_s1[i])
            {

                while (hash_s2[point] == 0)
                    point++;

                if (s1[i] != s2[point++])
                    t++;
            }

        t /= 2;

        missing = len2 - match + t;
        double score = (((double)match) / ((double)len1) + ((double)match) / ((double)len2) + ((double)match - t) / ((double)match)) / 3.0;

        score = score - double(missing / len2);
        // Return the Jaro Similarity
        return score;
    }

    float getStartTimefromCandidateTranscript(CandidateTranscript)

    ~Detail() {}

private:
    SpeechToTextProps props;
};

//Constructor
SpeechToText::SpeechToText(SpeechToTextProps _props) : Module(TRANSFORM, "SpeechToText", _props), tBufferSize(0)
{
    mDetail.reset(new Detail(_props));
    auto mAudio = framemetadata_sp(new FrameMetadata(FrameMetadata::FrameType::AUDIO));
    mOutputPinId = addOutputPin(mAudio);
    count = 0;
    maxScoreForCurrentStream = 0.0;
    fState = false;
    scoreIncreasing = false;
    numberOfFramesScoreSame = 0;
    LOG_INFO << "Constructor";
    int status = DS_CreateModel("/home/developer/development/aprapipes_audio/data/deepspeech-0.9.3-models.pbmm", &ctx);
    status = DS_EnableExternalScorer(ctx, "/home/developer/Downloads/native_client.amd64.tflite.linux/bluebox.scorer");

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
    if (mOutputFrameType != FrameMetadata::AUDIO)
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
    uint8_t *src_data;
    size_t src_nb_samples;
    auto frame = frames.cbegin()->second;
    if (isFrameEmpty(frame))
    {
        return true;
    }
    //VAD
    else if (tBufferSize > props.BufferSize)
    {
        DS_FreeStream(sCtx);
        LOG_INFO << "ending stream";
        tBufferSize = 0;
        fVector.insert(fVector.end(), static_cast<uint16_t *>(frame->data()), static_cast<uint16_t *>(frame->data() + frame->size()));
        maxScoreForCurrentStream = 0;
    }
    else
    {
        if (fState == false)
        {
            if (tBufferSize == 0)
            {
                //Create Stream and check for previous samples
                int status = DS_CreateStream(ctx, &sCtx);
                LOG_INFO << "starting stream";
                maxScoreForCurrentStream = 0;
                sentence = "";
                numberOfFrameErased =0;
            }
            LOG_INFO << "fVector size before " << fVector.size();
            LOG_INFO << "frame size " << frame->size() / 2;
            fVector.insert(fVector.end(), static_cast<uint16_t *>(frame->data()), static_cast<uint16_t *>(frame->data() + frame->size()));

            //Update src_data as per fVector after updating tStart from the number of frames erased
            if(tBufferSize==0 && !fVector.empty())
            {
                LOG_INFO<<"tStart "<<tStart<<" number of frames erased  "<<numberOfFrameErased<<" fVector Size "<<fVector.size();
                tStart = (tStart*props.src_rate-numberOfFrameErased)/props.src_rate ;
                src_data = (uint8_t *)(fVector.data()+(int)tStart*props.src_rate*2); //make it not 2 and try
                src_nb_samples = tStart*props.src_rate;
                tStart = 0;        
            }
            else{
                //if nothing comes from previous stream then src_data = current frame
                src_data = (uint8_t *)frame->data();
                src_nb_samples = frame->size()/2;
            }

            tBufferSize += frame->size();
            LOG_INFO << "fVector size " << fVector.size();

            //resample
            uint8_t *dst_data;
            int dst_linesize;
            int dst_nb_channels = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_MONO);
            int dst_nb_samples = av_rescale_rnd(swr_get_delay(swr, props.src_rate) + src_nb_samples, 16000, props.src_rate, AV_ROUND_UP);
            av_samples_alloc(&dst_data, &dst_linesize, dst_nb_channels, dst_nb_samples, AV_SAMPLE_FMT_S16, 1);
            int ret = swr_convert(swr, &dst_data, dst_nb_samples, (const uint8_t **)&src_data, src_nb_samples);
            int dst_bufsize = av_samples_get_buffer_size(&dst_linesize, dst_nb_channels, ret, AV_SAMPLE_FMT_S16, 1);

            //feed the audio to the stream
            DS_FeedAudioContent(sCtx, (const short *)dst_data, (unsigned int)ret);

            //get the intermediate result
            Metadata *partialMetadata = DS_IntermediateDecodeWithMetadata(sCtx, 1);

            //get the entire sentence from the metadata
            const CandidateTranscript *InterTrans = &partialMetadata->transcripts[0];

            for (int i = 0; i < InterTrans->num_tokens; i++)
            {
                sentence.append(InterTrans->tokens[i].text);
            }
            LOG_INFO << sentence;

            //do jaro similarity test to the sentence
            double score = mDetail->jaro_distance(sentence, "hello blue box my name is");
            LOG_INFO << "score: " << score;

            //check if the score is increasing or not
            if (score > maxScoreForCurrentStream)
            {
                maxScoreForCurrentStream = score;
                size = 0;
                counter = 0;
            }
            else //decreasing or same score case
            {
                counter++; //log the consecutive number of frames for which the score is not increasing
                size += frame->size(); //log the total size of the consecutive frames for which the score is not increasing
            }

            if (score >= 0.9)
            {
                for (int i = 0; i < InterTrans->num_tokens; i++)
                {
                    if (strcmp(InterTrans->tokens[i].text, " ")) //compare for all tokens with space
                    {
                        tStart = InterTrans->tokens[i].start_time; //if it is the first time log it in tStart
                        break;
                    }
                }
                fState = true;
                DS_FreeStream(sCtx);
                DS_FreeMetadata(partialMetadata);
                LOG_INFO << "ending stream";
                tBufferSize = 0;
            }
            else if ((0.5 < score) && (score < 0.9)  && counter < 3 ) //counter < 3 means the score is increasing
            {
                //it means not a prefect match
                //logging the start and end non space character's start time
                for (int i = 0; i < InterTrans->num_tokens; i++)
                {
                    if (strcmp(InterTrans->tokens[i].text, " ")) //compare for all tokens with space
                    {
                        tStart = InterTrans->tokens[i].start_time; //if it is the first time log it in tStart
                        break;
                    }
                }
            }
            else
            {
                //if nothing is logged then remove the first 0.5 secs from fVector once it crosses 2.5 secs worth of buffer
                if(fVector.size()>48000*2.5){
                    fVector.erase(fVector.begin(), (fVector.begin() + 24000) );
                    numberOfFrameErased += 24000; //log the number of frames removed so that we can update the tStart using this
                }
            }
            
        }
        else
        {
            //reached final State
            //accumulate the next 0.5 secs
            if (count <= 48000)
            {
                LOG_INFO << "accumulating the last 0.5 secs of audio now";
                fVector.insert(fVector.end(), static_cast<uint16_t *>(frame->data()), static_cast<uint16_t *>(frame->data() + frame->size()));
                count += frame->size();
            }
            else
            {
                tBufferSize = 0;
                count = 0;
                LOG_INFO << "sending out frames now";
                auto outFrame = makeFrame(2 * (fVector.size()));
                memcpy(outFrame->data(), fVector.data() , 2 * (fVector.size()));
                frames.insert(make_pair(mOutputPinId, outFrame));
                send(frames);
                fVector.clear();
                //add frame to fVector after clearing the vector
                fVector.insert(fVector.end(), static_cast<uint16_t *>(frame->data()), static_cast<uint16_t *>(frame->data() + frame->size()));
                fState = false;
            }
        }
    }
    sentence = "";
    return true;
}     

bool SpeechToText::init()
{
    return Module::init();
}

bool SpeechToText::term()
{            
    return Module::term();
}