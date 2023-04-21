/**
 * ��Ҫ˼·��
 * 1����ʼ����������Ϣ
 * 2�����ļ����������ɣ���ȡ�������ݵ�ÿ��AVFrame��
 * 3����ÿ��AVFrameͨ�����������ɱ�������ݵ�AVPacket
 * 4��ʹ��AVFormatContext��װ���ļ��н������
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

/* check that a given sample format is supported by the encoder */
static int check_sample_fmt(const AVCodec* codec, enum AVSampleFormat sample_fmt)
{
    const enum AVSampleFormat* p = codec->sample_fmts;

    while (*p != AV_SAMPLE_FMT_NONE) {
        if (*p == sample_fmt)
            return 1;
        p++;
    }
    return 0;
}

/* just pick the highest supported samplerate */
static int select_sample_rate(const AVCodec* codec)
{
    const int* p;
    int best_samplerate = 0;

    p = codec->supported_samplerates;

    if (*p == 0)
        return 44100;

    while (*p) {
        if (!best_samplerate || abs(44100 - *p) < abs(44100 - best_samplerate))
            best_samplerate = *p;
        p++;
    }
    return best_samplerate;
}

/* select layout with the highest channel count */
static int select_channel_layout(const AVCodec* codec)
{
    const uint64_t* p;
    uint64_t best_ch_layout = 0;
    int best_nb_channels = 0;


    if (!codec->channel_layouts)
        return AV_CH_LAYOUT_STEREO;

    p = codec->channel_layouts;
    

    while (*p) {
        int nb_channels = av_get_channel_layout_nb_channels(*p);

        if (nb_channels > best_nb_channels) {
            best_ch_layout = *p;
            best_nb_channels = nb_channels;
        }
        p++;
    }
    return best_ch_layout;
}

static int f_index = 0;

static void encode(AVCodecContext* ctx, AVFrame* frame, AVPacket* pkt,
    FILE* output, AVFormatContext* ofmt_ctx)
{
    int ret;

    /* send the frame for encoding */
    ret = avcodec_send_frame(ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending the frame to the encoder\n");
        exit(1);
    }

    /* read all the available output packets (in general there may be any
     * number of them */
    while (ret >= 0) {
        ret = avcodec_receive_packet(ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error encoding audio frame\n");
            exit(1);
        }

        //����װ��������д�룩
        //Convert PTS/DTS
        pkt->pts = f_index * 100;
        pkt->dts = f_index * 100;
        pkt->pos = -1;
        pkt->stream_index = 0;

        printf("Write 1 Packet. size:%5d\tpts:%lld\n", pkt->size, pkt->pts);
        //Write
        if (av_interleaved_write_frame(ofmt_ctx, pkt) < 0) {
            printf("Error muxing packet\n");
            break;
        }
        f_index++;

        av_packet_unref(pkt);
    }
}

int main(int argc, char** argv)
{
    const char* outFilename, * inFilename;
    const AVCodec* codec;
    AVCodecContext* c = NULL;
    AVFrame* frame;
    AVPacket* pkt;
    int i, j, k, ret;
    FILE* outFile, * inFile;

    inFilename = "./48000_2_f32le.pcm";
    outFilename = "./encde2_48000_2_f32le.aac";
    

    remove(outFilename);

    //��ȡ AVCodec
    codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }
    //��ȡAVCodecContext
    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate audio codec context\n");
        exit(1);
    }

   
    /* select other audio parameters supported by the encoder */
    c->sample_rate = 48000;//select_sample_rate(codec);
    c->channel_layout = select_channel_layout(codec);
    c->channels = av_get_channel_layout_nb_channels(c->channel_layout);

    //���ò�����ʽ
    c->sample_fmt = AV_SAMPLE_FMT_FLTP;
    //��Ƶ����⵱ǰ�������Ƿ�֧�ֲ�����ʽ
    if (!check_sample_fmt(codec, c->sample_fmt)) {
        fprintf(stderr, "Encoder does not support sample format %s",
            av_get_sample_fmt_name(c->sample_fmt));
        exit(1);
    }

    //���ñ�����
    c->bit_rate = 128 * 1024;



    //��ʼ��AVCodecContext
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    inFile = fopen(inFilename, "rb");
    if (!inFile) {
        fprintf(stderr, "Could not open %s\n", inFilename);
        exit(1);
    }

    outFile = fopen(outFilename, "wb+");
    if (!outFile) {
        fprintf(stderr, "Could not open %s\n", outFilename);
        exit(1);
    }

    /* packet for holding encoded output */
    pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "could not allocate the packet\n");
        exit(1);
    }

    /* frame containing input raw audio */
    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate audio frame\n");
        exit(1);
    }

    //���� AVFrame�Ĳ���
    frame->nb_samples = c->frame_size;
    frame->format = c->sample_fmt;
    frame->channel_layout = c->channel_layout;

    /* allocate the data buffers */
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate audio data buffers\n");
        exit(1);
    }


    /***************************************************************************/
    /*****************      �����Ƕ�PCM������AVPacket�����ٽ��з�װ��AAC      ******/
    /***************************************************************************/
    //����װ������ʼ����
    //Output
    AVFormatContext* ofmt_ctx = NULL;
    AVOutputFormat* ofmt = NULL;
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, outFilename);
    if (!ofmt_ctx) {
        printf("Could not create output context\n");
        return -1;
    }
    ofmt = ofmt_ctx->oformat;
    AVStream* out_stream = avformat_new_stream(ofmt_ctx, codec);
    //Copy the settings of AVCodecContext
    if (avcodec_parameters_from_context(out_stream->codecpar, c) < 0) {
        printf("Failed to copy context from input to output stream codec context\n");
        return -1;
    }
    out_stream->codecpar->codec_tag = 0;
    if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
       // out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    /* open the output file, if needed */
    if (!(ofmt->flags & AVFMT_NOFILE)) {
        if (avio_open(&ofmt_ctx->pb, outFilename, AVIO_FLAG_WRITE)) {
            fprintf(stderr, "Could not open '%s': \n", outFilename);
            return -1;
        }
    }

    //Write file header //����װ�����ļ�ͷ��
    if (avformat_write_header(ofmt_ctx, NULL) < 0) {
        fprintf(stderr, "Error occurred when opening output file: \n");
        return -1;
    }



    //ÿ��ͨ��ռ���ֽ���
    int data_size = av_get_bytes_per_sample(c->sample_fmt);

    while (!feof(inFile)) {
        /* make sure the frame is writable -- makes a copy if the encoder
         * kept a reference internally */
        ret = av_frame_make_writable(frame);

        if (ret < 0)
            exit(1);

        //��ȡpackedģʽ������planarģʽ�洢
        for (i = 0; i < c->frame_size; i++)
            for (j = 0; j < c->channels; j++)
                fread(frame->data[j] + data_size * i, 1, data_size, inFile);

        //����
        encode(c, frame, pkt, outFile, ofmt_ctx);
    }

    /* flush the encoder */
    encode(c, NULL, pkt, outFile, ofmt_ctx);

    printf("Write file trailer.\n");

    //Write file trailer//����װ�����ļ�β��
    av_write_trailer(ofmt_ctx);


    fclose(outFile);

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&c);
    avformat_free_context(ofmt_ctx);

    return 0;
}