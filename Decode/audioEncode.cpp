/**
*
*          ��Ƶ����
*           �ӱ��ض�ȡPCM���ݽ���AAC����
*           1. ����PCM��ʽ���⣬ͨ��AVCodec��sample_fmts������ȡ����ĸ�ʽ֧��
*           ��1��Ĭ�ϵ�aac�����������PCM��ʽΪ:AV_SAMPLE_FMT_FLTP
*           ��2��libfdk_aac�����������PCM��ʽΪAV_SAMPLE_FMT_S16.
*           2. ֧�ֵĲ����ʣ�ͨ��AVCodec��supported_samplerates���Ի�ȡ
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include <libavcodec/avcodec.h>

#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libavutil/opt.h>
}
/* ���ñ������Ƿ�֧�ָò�����ʽ */
static int check_sample_fmt(const AVCodec* codec, enum AVSampleFormat sample_fmt)
{
    const enum  AVSampleFormat* p = codec->sample_fmts;

    while (*p != AV_SAMPLE_FMT_NONE) { // ͨ��AV_SAMPLE_FMT_NONE��Ϊ������

        if (*p == sample_fmt)
            return 1;
        p++;
    }
    return 0;
}

/* ���ñ������Ƿ�֧�ָò����� */
static int check_sample_rate(const AVCodec* codec, const int sample_rate)
{
    const int* p = codec->supported_samplerates;
    while (*p != 0) {// 0��Ϊ�˳�����������libfdk-aacenc.c��aac_sample_rates
        printf("%s support %dhz\n", codec->name, *p);
        if (*p == sample_rate)
            return 1;
        p++;
    }
    return 0;
}

/* ���ñ������Ƿ�֧�ָò�����, �ú���ֻ�����ο� */
static int check_channel_layout(const AVCodec* codec, const uint64_t channel_layout)
{
    // ����ÿ��codec������֧�ֵ�channel_layout
    const uint64_t* p = codec->channel_layouts;
    if (!p) {
        printf("the codec %s no set channel_layouts\n", codec->name);
        return 1;
    }
    while (*p != 0) { // 0��Ϊ�˳�����������libfdk-aacenc.c��aac_channel_layout
        printf("%s support channel_layout %d\n", codec->name, *p);
        if (*p == channel_layout)
            return 1;
        p++;
    }
    return 0;
}

static int check_codec(AVCodec* codec, AVCodecContext* codec_ctx)
{

    if (!check_sample_fmt(codec, codec_ctx->sample_fmt)) {
        fprintf(stderr, "Encoder does not support sample format %s",
            av_get_sample_fmt_name(codec_ctx->sample_fmt));
        return 0;
    }
    if (!check_sample_rate(codec, codec_ctx->sample_rate)) {
        fprintf(stderr, "Encoder does not support sample rate %d", codec_ctx->sample_rate);
        return 0;
    }
    if (!check_channel_layout(codec, codec_ctx->channel_layout)) {
        fprintf(stderr, "Encoder does not support channel layout %lu", codec_ctx->channel_layout);
        return 0;
    }

    printf("\n\nAudio encode config\n");
    printf("bit_rate:%ldkbps\n", codec_ctx->bit_rate / 1024);
    printf("sample_rate:%d\n", codec_ctx->sample_rate);
    printf("sample_fmt:%s\n", av_get_sample_fmt_name(codec_ctx->sample_fmt));
    printf("channels:%d\n", codec_ctx->channels);
    // frame_size����avcodec_open2����й���
    printf("1 frame_size:%d\n", codec_ctx->frame_size);

    return 1;
}


static void get_adts_header(AVCodecContext* ctx, uint8_t* adts_header, int aac_length)
{
    uint8_t freq_idx = 0;    //0: 96000 Hz  3: 48000 Hz 4: 44100 Hz
    switch (ctx->sample_rate) {
    case 96000: freq_idx = 0; break;
    case 88200: freq_idx = 1; break;
    case 64000: freq_idx = 2; break;
    case 48000: freq_idx = 3; break;
    case 44100: freq_idx = 4; break;
    case 32000: freq_idx = 5; break;
    case 24000: freq_idx = 6; break;
    case 22050: freq_idx = 7; break;
    case 16000: freq_idx = 8; break;
    case 12000: freq_idx = 9; break;
    case 11025: freq_idx = 10; break;
    case 8000: freq_idx = 11; break;
    case 7350: freq_idx = 12; break;
    default: freq_idx = 4; break;
    }
    uint8_t chanCfg = ctx->channels;
    uint32_t frame_length = aac_length + 7;
    adts_header[0] = 0xFF;
    adts_header[1] = 0xF1;
    adts_header[2] = ((ctx->profile) << 6) + (freq_idx << 2) + (chanCfg >> 2);
    adts_header[3] = (((chanCfg & 3) << 6) + (frame_length >> 11));
    adts_header[4] = ((frame_length & 0x7FF) >> 3);
    adts_header[5] = (((frame_length & 7) << 5) + 0x1F);
    adts_header[6] = 0xFC;
}
/*
*
*/
static int encode(AVCodecContext* ctx, AVFrame* frame, AVPacket* pkt, FILE* output)
{
    int ret;

    /* send the frame for encoding */
    ret = avcodec_send_frame(ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending the frame to the encoder\n");
        return -1;
    }

    /* read all the available output packets (in general there may be any number of them */
    // ����ͽ��붼��һ���ģ�����send 1�Σ�Ȼ��receive���, ֱ��AVERROR(EAGAIN)����AVERROR_EOF
    while (ret >= 0) {
        ret = avcodec_receive_packet(ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0;
        }
        else if (ret < 0) {
            fprintf(stderr, "Error encoding audio frame\n");
            return -1;
        }
        uint8_t aac_header[7];
        get_adts_header(ctx, aac_header, pkt->size);

        size_t len = 0;
        len = fwrite(aac_header, 1, 7, output);
        if (len != 7) {
            fprintf(stderr, "fwrite aac_header failed\n");
            return -1;
        }
        len = fwrite(pkt->data, 1, pkt->size, output);
        if (len != pkt->size) {
            fprintf(stderr, "fwrite aac data failed\n");
            return -1;
        }
        /* �Ƿ���Ҫ�ͷ�����? avcodec_receive_packet��һ�����õľ��� av_packet_unref
        * �������ǲ����ֶ�ȥ�ͷţ������и����⣬���ܽ�pktֱ�Ӳ��뵽���У���Ϊ���������ͷ�����
        * �����·���һ��pkt, Ȼ��ʹ��av_packet_move_refת��pkt��Ӧ��buffer
        */
        av_packet_unref(pkt);
    }
    return -1;
}

/*
 * ����ֻ֧��2ͨ����ת��
*/
void f32le_convert_to_fltp(float* f32le, float* fltp, int nb_samples) {
    float* fltp_l = fltp;   // ��ͨ��
    float* fltp_r = fltp + nb_samples;   // ��ͨ��
    for (int i = 0; i < nb_samples; i++) {
        fltp_l[i] = f32le[i * 2];     // 0 1   - 2 3
        fltp_r[i] = f32le[i * 2 + 1];   // ���Գ���ע��������������������������
    }
}
/*
 * ��ȡ�����ļ���
 * 
 * flt��ʽ��ffmpeg -i buweishui.aac -ar 48000 -ac 2 -f f32le 48000_2_f32le.pcm
 * ffmpegֻ����ȡpacked��ʽ��PCM���ݣ��ڱ���ʱ���������ҪΪfltp����Ҫ����ת��
 */

int main2(int argc, char** argv)
{
    const char* in_pcm_file = "./48000_2_f32le.pcm";      // ����PCM�ļ�
    const char* out_aac_file = "./encode_48000_2_f32le.aac";     // �����AAC�ļ�
    AVCodecID codec_id = AV_CODEC_ID_AAC;
    // 1.���ұ�����
    AVCodec* codec = (AVCodec*)avcodec_find_encoder(codec_id); // ��ID������ȱʡ��aac encodeΪaacenc.c
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    // 2.�����ڴ�
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        fprintf(stderr, "Could not allocate audio codec context\n");
        exit(1);
    }
    codec_ctx->codec_id = codec_id;
    codec_ctx->codec_type = AVMEDIA_TYPE_AUDIO;
    codec_ctx->bit_rate = 128 * 1024;
    codec_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
    codec_ctx->sample_rate = 48000;
    codec_ctx->channels = av_get_channel_layout_nb_channels(codec_ctx->channel_layout);
    codec_ctx->profile = FF_PROFILE_AAC_LOW;    //
    codec_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;



    // 3.���֧�ֲ�����ʽ֧�����
    if (!check_codec(codec, codec_ctx)) {
        exit(1);
    }

    // 4.�������������ĺͱ��������й���
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }
    printf("2 frame_size:%d\n\n", codec_ctx->frame_size); // ����ÿ�ε����Ͷ��ٸ�������

    // 5.�����������ļ�
    FILE* infile = fopen(in_pcm_file, "rb");
    if (!infile) {
        fprintf(stderr, "Could not open %s\n", in_pcm_file);
        exit(1);
    }
    FILE* outfile = fopen(out_aac_file, "wb");
    if (!outfile) {
        fprintf(stderr, "Could not open %s\n", out_aac_file);
        exit(1);
    }

    // 6.����packet
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "could not allocate the packet\n");
        exit(1);
    }

    // 7.����frame
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate audio frame\n");
        exit(1);
    }
    /* ÿ���Ͷ������ݸ��������ɣ�
     *  (1)frame_size(ÿ֡����ͨ���Ĳ�������);
     *  (2)sample_fmt(�������ʽ);
     *  (3)channel_layout(ͨ���������);
     * 3Ҫ�ؾ���
     */
    frame->nb_samples = codec_ctx->frame_size;
    frame->format = codec_ctx->sample_fmt;
    frame->channel_layout = codec_ctx->channel_layout;
    frame->channels = av_get_channel_layout_nb_channels(frame->channel_layout);
    printf("frame nb_samples:%d\n", frame->nb_samples);
    printf("frame sample_fmt:%d\n", frame->format);
    printf("frame channel_layout:%lu\n", frame->channel_layout);
    printf("frame channels_num:%lu\n\n", frame->channels);
    // 8.Ϊframe����buffer
    int ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate audio data buffers\n");
        exit(1);
    }

    // 9.ѭ����ȡ����
    // �����ÿһ֡������ ������������ֽ� * ͨ����Ŀ * ÿ֡����������
    int frame_bytes = av_get_bytes_per_sample((AVSampleFormat)frame->format) \
        * frame->channels \
        * frame->nb_samples;
    printf("frame_bytes %d\n", frame_bytes);
    uint8_t* pcm_buf = (uint8_t*)malloc(frame_bytes);
    if (!pcm_buf) {
        printf("pcm_buf malloc failed\n");
        return 1;
    }
    uint8_t* pcm_temp_buf = (uint8_t*)malloc(frame_bytes);
    if (!pcm_temp_buf) {
        printf("pcm_temp_buf malloc failed\n");
        return 1;
    }

    //ÿ��ͨ��ռ���ֽ���
    int data_size = av_get_bytes_per_sample((AVSampleFormat)frame->format);

    int64_t pts = 0;
    printf("start enode\n");
    while (!feof(infile))
    {

        // 10.ȷ����frame��д, ����������ڲ��������ڴ�ο�����������Ҫ���¿���һ������ Ŀ������д������ݺͱ�������������ݲ��ܲ�����ͻ
        ret = av_frame_make_writable(frame);
        if (ret != 0)
            printf("av_frame_make_writable failed, ret = %d\n", ret);

        //��ȡpackedģʽ������planarģʽ�洢
        for (int i = 0; i < frame->nb_samples; i++)
            for (int j = 0; j < frame->channels; j++)
                fread(frame->data[j] + data_size * i, 1, data_size, infile);

        //memset(pcm_buf, 0, frame_bytes);
        //size_t read_bytes = fread(pcm_buf, 1, frame_bytes, infile);
        //if (read_bytes <= 0) {
        //    printf("read file finish\n");
        //    break;
        //}

        //// 11.�����Ƶ֡
        //if (AV_SAMPLE_FMT_S16 == frame->format) {
        //    // ����ȡ����PCM������䵽frameȥ����Ҫע���ʽ��ƥ��, ��planar����packed��Ҫ�������
        //    ret = av_samples_fill_arrays(frame->data, frame->linesize,
        //        pcm_buf, frame->channels,
        //        frame->nb_samples, (AVSampleFormat)frame->format, 0);
        //}
        //else {
        //    // ����ȡ����PCM������䵽frameȥ����Ҫע���ʽ��ƥ��, ��planar����packed��Ҫ�������
        //    // �����ص�f32le packedģʽ������תΪfloat palanar
        //    memset(pcm_temp_buf, 0, frame_bytes);
        //    f32le_convert_to_fltp((float*)pcm_buf, (float*)pcm_temp_buf, frame->nb_samples);
        //    ret = av_samples_fill_arrays(frame->data, frame->linesize,
        //        pcm_temp_buf, frame->channels,
        //        frame->nb_samples, (AVSampleFormat)frame->format, 0);
        //}

        // 12.����
        pts += frame->nb_samples;
        frame->pts = pts;       // ʹ�ò�������Ϊpts�ĵ�λ�����廻����� pts*1/������
        ret = encode(codec_ctx, frame, pkt, outfile);
        if (ret < 0) {
            printf("encode failed\n");
            break;
        }
    }

    // 13.��ˢ������
    encode(codec_ctx, NULL, pkt, outfile);

    // 14.�ر��ļ�
    fclose(infile);
    fclose(outfile);

    // 15.�ͷ��ڴ�
    if (pcm_buf) {
        free(pcm_buf);
    }
    if (pcm_temp_buf) {
        free(pcm_temp_buf);
    }
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codec_ctx);
    printf("main finish, please enter Enter and exit\n");
    getchar();
    return 0;
}