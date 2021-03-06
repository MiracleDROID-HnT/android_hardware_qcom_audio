/*
* Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#define LOG_TAG "split_a2dp"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0
#include <errno.h>
#include <log/log.h>
#include <dlfcn.h>
#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include <stdlib.h>
#include <cutils/str_parms.h>
#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <cutils/properties.h>

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_A2DP
#include <log_utils.h>
#endif

#ifdef SPLIT_A2DP_ENABLED
#define AUDIO_PARAMETER_A2DP_STARTED "A2dpStarted"
#define BT_IPC_LIB_NAME  "libbthost_if.so"
#define ENC_MEDIA_FMT_NONE                                     0
#define ENC_MEDIA_FMT_AAC                                  0x00010DA6
#define ENC_MEDIA_FMT_APTX                                 0x000131ff
#define ENC_MEDIA_FMT_APTX_HD                              0x00013200
#define ENC_MEDIA_FMT_SBC                                  0x00010BF2
#define MEDIA_FMT_AAC_AOT_LC                               2
#define MEDIA_FMT_AAC_AOT_SBR                              5
#define MEDIA_FMT_AAC_AOT_PS                               29
#define PCM_CHANNEL_L                                      1
#define PCM_CHANNEL_R                                      2
#define PCM_CHANNEL_C                                      3
#define MEDIA_FMT_SBC_CHANNEL_MODE_MONO                    1
#define MEDIA_FMT_SBC_CHANNEL_MODE_STEREO                  2
#define MEDIA_FMT_SBC_CHANNEL_MODE_DUAL_MONO               8
#define MEDIA_FMT_SBC_CHANNEL_MODE_JOINT_STEREO            9
#define MEDIA_FMT_SBC_ALLOCATION_METHOD_LOUDNESS           0
#define MEDIA_FMT_SBC_ALLOCATION_METHOD_SNR                1
#define MIXER_ENC_CONFIG_BLOCK     "SLIM_7_RX Encoder Config"
#define MIXER_ENC_BIT_FORMAT       "AFE Input Bit Format"
#define MIXER_ENC_FMT_SBC          "SBC"
#define MIXER_ENC_FMT_AAC          "AAC"
#define MIXER_ENC_FMT_APTX         "APTX"
#define MIXER_ENC_FMT_APTXHD       "APTXHD"
#define MIXER_ENC_FMT_NONE         "NONE"
#define ENCODER_LATENCY_SBC        10
#define ENCODER_LATENCY_APTX       40
#define ENCODER_LATENCY_APTX_HD    20
#define ENCODER_LATENCY_AAC        70
#define DEFAULT_SINK_LATENCY_SBC       140
#define DEFAULT_SINK_LATENCY_APTX      160
#define DEFAULT_SINK_LATENCY_APTX_HD   180
#define DEFAULT_SINK_LATENCY_AAC       180

typedef int (*audio_stream_open_t)(void);
typedef int (*audio_stream_close_t)(void);
typedef int (*audio_start_stream_t)(void);
typedef int (*audio_stop_stream_t)(void);
typedef int (*audio_suspend_stream_t)(void);
typedef void (*audio_handoff_triggered_t)(void);
typedef void (*clear_a2dpsuspend_flag_t)(void);
typedef void * (*audio_get_codec_config_t)(uint8_t *multicast_status,uint8_t *num_dev,
                               audio_format_t *codec_type);
typedef int (*audio_check_a2dp_ready_t)(void);
typedef uint16_t (*audio_get_a2dp_sink_latency_t)(void);

enum A2DP_STATE {
    A2DP_STATE_CONNECTED,
    A2DP_STATE_STARTED,
    A2DP_STATE_STOPPED,
    A2DP_STATE_DISCONNECTED,
};

/* structure used to  update a2dp state machine
 * to communicate IPC library
 * to store DSP encoder configuration information
 */
struct a2dp_data {
    struct audio_device *adev;
    void *bt_lib_handle;
    audio_stream_open_t audio_stream_open;
    audio_stream_close_t audio_stream_close;
    audio_start_stream_t audio_start_stream;
    audio_stop_stream_t audio_stop_stream;
    audio_suspend_stream_t audio_suspend_stream;
    audio_handoff_triggered_t audio_handoff_triggered;
    clear_a2dpsuspend_flag_t clear_a2dpsuspend_flag;
    audio_get_codec_config_t audio_get_codec_config;
    audio_check_a2dp_ready_t audio_check_a2dp_ready;
    audio_get_a2dp_sink_latency_t audio_get_a2dp_sink_latency;
    enum A2DP_STATE bt_state;
    audio_format_t bt_encoder_format;
    uint32_t enc_sampling_rate;
    bool a2dp_started;
    bool a2dp_suspended;
    int  a2dp_total_active_session_request;
    bool is_a2dp_offload_supported;
    bool is_handoff_in_progress;
};

struct a2dp_data a2dp;

/* START of DSP configurable structures
 * These values should match with DSP interface defintion
 */

/* AAC encoder configuration structure. */
typedef struct aac_enc_cfg_t aac_enc_cfg_t;

/* supported enc_mode are AAC_LC, AAC_SBR, AAC_PS
 * supported aac_fmt_flag are ADTS/RAW
 * supported channel_cfg are Native mode, Mono , Stereo
 */
struct aac_enc_cfg_t {
    uint32_t      enc_format;
    uint32_t      bit_rate;
    uint32_t      enc_mode;
    uint16_t      aac_fmt_flag;
    uint16_t      channel_cfg;
    uint32_t      sample_rate;
} ;

/* SBC encoder configuration structure. */
typedef struct sbc_enc_cfg_t sbc_enc_cfg_t;

/* supported num_subbands are 4/8
 * supported blk_len are 4, 8, 12, 16
 * supported channel_mode are MONO, STEREO, DUAL_MONO, JOINT_STEREO
 * supported alloc_method are LOUNDNESS/SNR
 * supported bit_rate for mono channel is max 320kbps
 * supported bit rate for stereo channel is max 512 kbps
 */
struct sbc_enc_cfg_t{
    uint32_t      enc_format;
    uint32_t      num_subbands;
    uint32_t      blk_len;
    uint32_t      channel_mode;
    uint32_t      alloc_method;
    uint32_t      bit_rate;
    uint32_t      sample_rate;
};


/* supported num_channels are Mono/Stereo
 * supported channel_mapping for mono is CHANNEL_C
 * supported channel mapping for stereo is CHANNEL_L and CHANNEL_R
 * custom size and reserved are not used(for future enhancement)
  */
struct custom_enc_cfg_aptx_t
{
    uint32_t      enc_format;
    uint32_t      sample_rate;
    uint16_t      num_channels;
    uint16_t      reserved;
    uint8_t       channel_mapping[8];
    uint32_t      custom_size;
};

/* TODO: Define the following structures only for O using PLATFORM_VERSION */
/* Information about BT SBC encoder configuration
 * This data is used between audio HAL module and
 * BT IPC library to configure DSP encoder
 */
typedef struct {
    uint32_t subband;    /* 4, 8 */
    uint32_t blk_len;    /* 4, 8, 12, 16 */
    uint16_t sampling_rate; /*44.1khz,48khz*/
    uint8_t  channels;      /*0(Mono),1(Dual_mono),2(Stereo),3(JS)*/
    uint8_t  alloc;         /*0(Loudness),1(SNR)*/
    uint8_t  min_bitpool;   /* 2 */
    uint8_t  max_bitpool;   /*53(44.1khz),51 (48khz) */
    uint32_t bitrate;      /* 320kbps to 512kbps */
} audio_sbc_encoder_config;


/* Information about BT APTX encoder configuration
 * This data is used between audio HAL module and
 * BT IPC library to configure DSP encoder
 */
typedef struct {
    uint16_t sampling_rate;
    uint8_t  channels;
    uint32_t bitrate;
} audio_aptx_encoder_config;


/* Information about BT AAC encoder configuration
 * This data is used between audio HAL module and
 * BT IPC library to configure DSP encoder
 */
typedef struct {
    uint32_t enc_mode; /* LC, SBR, PS */
    uint16_t format_flag; /* RAW, ADTS */
    uint16_t channels; /* 1-Mono, 2-Stereo */
    uint32_t sampling_rate;
    uint32_t bitrate;
} audio_aac_encoder_config;

/*********** END of DSP configurable structures ********************/

/* API to identify DSP encoder captabilities */
static void a2dp_offload_codec_cap_parser(char *value)
{
    char *tok = NULL,*saveptr;

    tok = strtok_r(value, "-", &saveptr);
    while (tok != NULL) {
        if (strcmp(tok, "sbc") == 0) {
            ALOGD("%s: SBC offload supported\n",__func__);
            a2dp.is_a2dp_offload_supported = true;
            break;
        } else if (strcmp(tok, "aptx") == 0) {
            ALOGD("%s: aptx offload supported\n",__func__);
            a2dp.is_a2dp_offload_supported = true;
            break;
        } else if (strcmp(tok, "aptxhd") == 0) {
            ALOGD("%s: aptx HD offload supported\n",__func__);
            a2dp.is_a2dp_offload_supported = true;
            break;
        } else if (strcmp(tok, "aac") == 0) {
            ALOGD("%s: aac offload supported\n",__func__);
            a2dp.is_a2dp_offload_supported = true;
            break;
        }
        tok = strtok_r(NULL, "-", &saveptr);
    };
}

static void update_offload_codec_capabilities()
{
    char value[PROPERTY_VALUE_MAX] = {'\0'};

    property_get("persist.vendor.bt.a2dp_offload_cap", value, "false");
    ALOGD("get_offload_codec_capabilities = %s",value);
    a2dp.is_a2dp_offload_supported =
            property_get_bool("persist.vendor.bt.a2dp_offload_cap", false);
    if (strcmp(value, "false") != 0)
        a2dp_offload_codec_cap_parser(value);
    ALOGD("%s: codec cap = %s",__func__,value);
}

/* API to open BT IPC library to start IPC communication */
static void open_a2dp_output()
{
    int ret = 0;

    ALOGD(" Open A2DP output start ");
    if (a2dp.bt_lib_handle == NULL){
        ALOGD(" Requesting for BT lib handle");
        a2dp.bt_lib_handle = dlopen(BT_IPC_LIB_NAME, RTLD_NOW);

        if (a2dp.bt_lib_handle == NULL) {
            ALOGE("%s: DLOPEN failed for %s", __func__, BT_IPC_LIB_NAME);
            ret = -ENOSYS;
            goto init_fail;
        } else {
            a2dp.audio_stream_open = (audio_stream_open_t)
                          dlsym(a2dp.bt_lib_handle, "audio_stream_open");
            a2dp.audio_start_stream = (audio_start_stream_t)
                          dlsym(a2dp.bt_lib_handle, "audio_start_stream");
            a2dp.audio_get_codec_config = (audio_get_codec_config_t)
                          dlsym(a2dp.bt_lib_handle, "audio_get_codec_config");
            a2dp.audio_suspend_stream = (audio_suspend_stream_t)
                          dlsym(a2dp.bt_lib_handle, "audio_suspend_stream");
            a2dp.audio_handoff_triggered = (audio_handoff_triggered_t)
                          dlsym(a2dp.bt_lib_handle, "audio_handoff_triggered");
            a2dp.clear_a2dpsuspend_flag = (clear_a2dpsuspend_flag_t)
                          dlsym(a2dp.bt_lib_handle, "clear_a2dpsuspend_flag");
            a2dp.audio_stop_stream = (audio_stop_stream_t)
                          dlsym(a2dp.bt_lib_handle, "audio_stop_stream");
            a2dp.audio_stream_close = (audio_stream_close_t)
                          dlsym(a2dp.bt_lib_handle, "audio_stream_close");
            a2dp.audio_check_a2dp_ready = (audio_check_a2dp_ready_t)
                        dlsym(a2dp.bt_lib_handle,"audio_check_a2dp_ready");
            a2dp.audio_get_a2dp_sink_latency = (audio_get_a2dp_sink_latency_t)
                        dlsym(a2dp.bt_lib_handle,"audio_get_a2dp_sink_latency");
        }
    }

    if (a2dp.bt_lib_handle && a2dp.audio_stream_open) {
        if (a2dp.bt_state == A2DP_STATE_DISCONNECTED) {
            ALOGD("calling BT stream open");
            ret = a2dp.audio_stream_open();
            if(ret != 0) {
                ALOGE("Failed to open output stream for a2dp: status %d", ret);
                goto init_fail;
            }
            a2dp.bt_state = A2DP_STATE_CONNECTED;
        } else {
            ALOGD("Called a2dp open with improper state, Ignoring request state %d", a2dp.bt_state);
        }
    } else {
        ALOGE("a2dp handle is not identified, Ignoring open request");
        a2dp.bt_state = A2DP_STATE_DISCONNECTED;
        goto init_fail;
    }

init_fail:
    if(ret != 0 && (a2dp.bt_lib_handle != NULL)) {
        dlclose(a2dp.bt_lib_handle);
        a2dp.bt_lib_handle = NULL;
    }
}

static int close_a2dp_output()
{
    ALOGV("%s\n",__func__);
    if (!(a2dp.bt_lib_handle && a2dp.audio_stream_close)) {
        ALOGE("a2dp handle is not identified, Ignoring close request");
        return -ENOSYS;
    }
    if (a2dp.bt_state != A2DP_STATE_DISCONNECTED) {
        ALOGD("calling BT stream close");
        if(a2dp.audio_stream_close() == false)
            ALOGE("failed close a2dp control path from BT library");
    }
    a2dp.a2dp_started = false;
    a2dp.a2dp_total_active_session_request = 0;
    a2dp.a2dp_suspended = false;
    a2dp.bt_encoder_format = AUDIO_FORMAT_INVALID;
    a2dp.enc_sampling_rate = 48000;
    a2dp.bt_state = A2DP_STATE_DISCONNECTED;

    return 0;
}

/* API to configure SBC DSP encoder */
bool configure_sbc_enc_format(audio_sbc_encoder_config *sbc_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL, *ctrl_bit_format = NULL;
    struct sbc_enc_cfg_t sbc_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if(sbc_bt_cfg == NULL)
        return false;

   ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identifed");
        is_configured = false;
        goto fail;
    }
    a2dp.bt_encoder_format = AUDIO_FORMAT_SBC;
    memset(&sbc_dsp_cfg, 0x0, sizeof(struct sbc_enc_cfg_t));
    sbc_dsp_cfg.enc_format = ENC_MEDIA_FMT_SBC;
    sbc_dsp_cfg.num_subbands = sbc_bt_cfg->subband;
    sbc_dsp_cfg.blk_len = sbc_bt_cfg->blk_len;
    switch(sbc_bt_cfg->channels) {
        case 0:
            sbc_dsp_cfg.channel_mode = MEDIA_FMT_SBC_CHANNEL_MODE_MONO;
            break;
        case 1:
            sbc_dsp_cfg.channel_mode = MEDIA_FMT_SBC_CHANNEL_MODE_DUAL_MONO;
            break;
        case 3:
            sbc_dsp_cfg.channel_mode = MEDIA_FMT_SBC_CHANNEL_MODE_JOINT_STEREO;
            break;
        case 2:
        default:
            sbc_dsp_cfg.channel_mode = MEDIA_FMT_SBC_CHANNEL_MODE_STEREO;
            break;
    }
    if (sbc_bt_cfg->alloc)
        sbc_dsp_cfg.alloc_method = MEDIA_FMT_SBC_ALLOCATION_METHOD_LOUDNESS;
    else
        sbc_dsp_cfg.alloc_method = MEDIA_FMT_SBC_ALLOCATION_METHOD_SNR;
    sbc_dsp_cfg.bit_rate = sbc_bt_cfg->bitrate;
    sbc_dsp_cfg.sample_rate = sbc_bt_cfg->sampling_rate;
    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&sbc_dsp_cfg,
                                    sizeof(struct sbc_enc_cfg_t));
    if (ret != 0) {
        ALOGE("%s: failed to set SBC encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_ENC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        ALOGE(" ERROR bit format CONFIG data mixer control not identifed");
        is_configured = false;
        goto fail;
    }
    ret = mixer_ctl_set_enum_by_string(ctrl_bit_format, "S16_LE");
    if (ret != 0) {
        ALOGE("%s: Failed to set bit format to encoder", __func__);
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.enc_sampling_rate = sbc_bt_cfg->sampling_rate;
    ALOGV("Successfully updated SBC enc format with samplingrate: %d channelmode:%d",
           sbc_dsp_cfg.sample_rate, sbc_dsp_cfg.channel_mode);
fail:
    return is_configured;
}

/* API to configure APTX DSP encoder */
bool configure_aptx_enc_format(audio_aptx_encoder_config *aptx_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL, *ctrl_bit_format = NULL;
    struct custom_enc_cfg_aptx_t aptx_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if(aptx_bt_cfg == NULL)
        return false;

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identifed");
        is_configured = false;
        goto fail;
    }
    a2dp.bt_encoder_format = AUDIO_FORMAT_APTX;
    memset(&aptx_dsp_cfg, 0x0, sizeof(struct custom_enc_cfg_aptx_t));
    aptx_dsp_cfg.enc_format = ENC_MEDIA_FMT_APTX;
    aptx_dsp_cfg.sample_rate = aptx_bt_cfg->sampling_rate;
    aptx_dsp_cfg.num_channels = aptx_bt_cfg->channels;
    switch(aptx_dsp_cfg.num_channels) {
        case 1:
            aptx_dsp_cfg.channel_mapping[0] = PCM_CHANNEL_C;
            break;
        case 2:
        default:
            aptx_dsp_cfg.channel_mapping[0] = PCM_CHANNEL_L;
            aptx_dsp_cfg.channel_mapping[1] = PCM_CHANNEL_R;
            break;
    }
    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aptx_dsp_cfg,
                              sizeof(struct custom_enc_cfg_aptx_t));
    if (ret != 0) {
        ALOGE("%s: Failed to set APTX encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_ENC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        ALOGE("ERROR bit format CONFIG data mixer control not identifed");
        is_configured = false;
        goto fail;
    } else {
        ret = mixer_ctl_set_enum_by_string(ctrl_bit_format, "S16_LE");
        if (ret != 0) {
            ALOGE("%s: Failed to set bit format to encoder", __func__);
            is_configured = false;
            goto fail;
        }
    }
    is_configured = true;
    a2dp.enc_sampling_rate = aptx_bt_cfg->sampling_rate;
    ALOGV("Successfully updated APTX enc format with samplingrate: %d channels:%d",
           aptx_dsp_cfg.sample_rate, aptx_dsp_cfg.num_channels);
fail:
    return is_configured;
}

/* API to configure APTX HD DSP encoder
 */
bool configure_aptx_hd_enc_format(audio_aptx_encoder_config *aptx_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL, *ctrl_bit_format = NULL;
    struct custom_enc_cfg_aptx_t aptx_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if(aptx_bt_cfg == NULL)
        return false;

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identifed");
        is_configured = false;
        goto fail;
    }

    a2dp.bt_encoder_format = AUDIO_FORMAT_APTX_HD;
    memset(&aptx_dsp_cfg, 0x0, sizeof(struct custom_enc_cfg_aptx_t));
    aptx_dsp_cfg.enc_format = ENC_MEDIA_FMT_APTX_HD;
    aptx_dsp_cfg.sample_rate = aptx_bt_cfg->sampling_rate;
    aptx_dsp_cfg.num_channels = aptx_bt_cfg->channels;
    switch(aptx_dsp_cfg.num_channels) {
        case 1:
            aptx_dsp_cfg.channel_mapping[0] = PCM_CHANNEL_C;
            break;
        case 2:
        default:
            aptx_dsp_cfg.channel_mapping[0] = PCM_CHANNEL_L;
            aptx_dsp_cfg.channel_mapping[1] = PCM_CHANNEL_R;
            break;
    }
    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aptx_dsp_cfg,
                              sizeof(struct custom_enc_cfg_aptx_t));
    if (ret != 0) {
        ALOGE("%s: Failed to set APTX HD encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        ALOGE(" ERROR  bit format CONFIG data mixer control not identifed");
        is_configured = false;
        goto fail;
    }
    ret = mixer_ctl_set_enum_by_string(ctrl_bit_format, "S24_LE");
    if (ret != 0) {
        ALOGE("%s: Failed to set APTX HD encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.enc_sampling_rate = aptx_bt_cfg->sampling_rate;
    ALOGV("Successfully updated APTX HD encformat with samplingrate: %d channels:%d",
           aptx_dsp_cfg.sample_rate, aptx_dsp_cfg.num_channels);
fail:
    return is_configured;
}

/* API to configure AAC DSP encoder */
bool configure_aac_enc_format(audio_aac_encoder_config *aac_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL, *ctrl_bit_format = NULL;
    struct aac_enc_cfg_t aac_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if(aac_bt_cfg == NULL)
        return false;

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identifed");
        is_configured = false;
        goto fail;
    }
    a2dp.bt_encoder_format = AUDIO_FORMAT_AAC;
    memset(&aac_dsp_cfg, 0x0, sizeof(struct aac_enc_cfg_t));
    aac_dsp_cfg.enc_format = ENC_MEDIA_FMT_AAC;
    aac_dsp_cfg.bit_rate = aac_bt_cfg->bitrate;
    aac_dsp_cfg.sample_rate = aac_bt_cfg->sampling_rate;
    switch(aac_bt_cfg->enc_mode) {
        case 0:
            aac_dsp_cfg.enc_mode = MEDIA_FMT_AAC_AOT_LC;
            break;
        case 2:
            aac_dsp_cfg.enc_mode = MEDIA_FMT_AAC_AOT_PS;
            break;
        case 1:
        default:
            aac_dsp_cfg.enc_mode = MEDIA_FMT_AAC_AOT_SBR;
            break;
    }
    aac_dsp_cfg.aac_fmt_flag = aac_bt_cfg->format_flag;
    aac_dsp_cfg.channel_cfg = aac_bt_cfg->channels;
    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aac_dsp_cfg,
                              sizeof(struct aac_enc_cfg_t));
    if (ret != 0) {
        ALOGE("%s: failed to set SBC encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_ENC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        is_configured = false;
        ALOGE(" ERROR  bit format CONFIG data mixer control not identifed");
        goto fail;
    }
    ret = mixer_ctl_set_enum_by_string(ctrl_bit_format, "S16_LE");
    if (ret != 0) {
        ALOGE("%s: Failed to set bit format to encoder", __func__);
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.enc_sampling_rate = aac_bt_cfg->sampling_rate;
    ALOGV("Successfully updated AAC enc format with samplingrate: %d channels:%d",
           aac_dsp_cfg.sample_rate, aac_dsp_cfg.channel_cfg);
fail:
    return is_configured;
}

bool configure_a2dp_encoder_format()
{
    void *codec_info = NULL;
    uint8_t multi_cast = 0, num_dev = 1;
    audio_format_t codec_type = AUDIO_FORMAT_INVALID;
    bool is_configured = false;

    if (!a2dp.audio_get_codec_config) {
        ALOGE(" a2dp handle is not identified, ignoring a2dp encoder config");
        return false;
    }
    ALOGD("configure_a2dp_encoder_format start");
    codec_info = a2dp.audio_get_codec_config(&multi_cast, &num_dev,
                               &codec_type);

    switch(codec_type) {
        case AUDIO_FORMAT_SBC:
            ALOGD(" Received SBC encoder supported BT device");
            is_configured =
               configure_sbc_enc_format((audio_sbc_encoder_config *)codec_info);
            break;
        case AUDIO_FORMAT_APTX:
            ALOGD(" Received APTX encoder supported BT device");
            is_configured =
              configure_aptx_enc_format((audio_aptx_encoder_config *)codec_info);
            break;
        case AUDIO_FORMAT_APTX_HD:
            ALOGD(" Received APTX HD encoder supported BT device");
            is_configured =
             configure_aptx_hd_enc_format((audio_aptx_encoder_config *)codec_info);
            break;
        case AUDIO_FORMAT_AAC:
            ALOGD(" Received AAC encoder supported BT device");
            is_configured =
              configure_aac_enc_format((audio_aac_encoder_config *)codec_info);
            break;
        default:
            ALOGD(" Received Unsupported encoder formar");
            is_configured = false;
            break;
    }
    return is_configured;
}

int audio_extn_a2dp_start_playback()
{
    int ret = 0;

    ALOGD("audio_extn_a2dp_start_playback start");

    if(!(a2dp.bt_lib_handle && a2dp.audio_start_stream
       && a2dp.audio_get_codec_config)) {
        ALOGE("a2dp handle is not identified, Ignoring start request");
        return -ENOSYS;
    }

    if(a2dp.a2dp_suspended == true) {
        //session will be restarted after suspend completion
        ALOGD("a2dp start requested during suspend state");
        return -ENOSYS;
    }

    if (!a2dp.a2dp_started && !a2dp.a2dp_total_active_session_request) {
        ALOGD("calling BT module stream start");
        /* This call indicates BT IPC lib to start playback */
        ret =  a2dp.audio_start_stream();
        ALOGE("BT controller start return = %d",ret);
        if (ret != 0 ) {
           ALOGE("BT controller start failed");
           a2dp.a2dp_started = false;
        } else {
           if(configure_a2dp_encoder_format() == true) {
                a2dp.a2dp_started = true;
                ret = 0;
                ALOGD("Start playback successful to BT library");
           } else {
                ALOGD(" unable to configure DSP encoder");
                a2dp.a2dp_started = false;
                ret = -ETIMEDOUT;
           }
        }
    }

    if (a2dp.a2dp_started)
        a2dp.a2dp_total_active_session_request++;

    ALOGD("start A2DP playback total active sessions :%d",
          a2dp.a2dp_total_active_session_request);
    return ret;
}

static void reset_a2dp_enc_config_params()
{
    int ret =0;

    struct mixer_ctl *ctl_enc_config, *ctrl_bit_format;
    struct sbc_enc_cfg_t dummy_reset_config;

    memset(&dummy_reset_config, 0x0, sizeof(struct sbc_enc_cfg_t));
    ctl_enc_config = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                           MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_config) {
        ALOGE(" ERROR  a2dp encoder format mixer control not identifed");
    } else {
        ret = mixer_ctl_set_array(ctl_enc_config, (void *)&dummy_reset_config,
                                        sizeof(struct sbc_enc_cfg_t));
         a2dp.bt_encoder_format = ENC_MEDIA_FMT_NONE;
    }
    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_ENC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        ALOGE(" ERROR  bit format CONFIG data mixer control not identifed");
    } else {
        ret = mixer_ctl_set_enum_by_string(ctrl_bit_format, "S16_LE");
        if (ret != 0) {
            ALOGE("%s: Failed to set bit format to encoder", __func__);
        }
    }
}

int audio_extn_a2dp_stop_playback()
{
    int ret =0;

    ALOGV("audio_extn_a2dp_stop_playback start");
    if(!(a2dp.bt_lib_handle && a2dp.audio_stop_stream)) {
        ALOGE("a2dp handle is not identified, Ignoring start request");
        return -ENOSYS;
    }

    if (a2dp.a2dp_total_active_session_request > 0)
        a2dp.a2dp_total_active_session_request--;

    if ( a2dp.a2dp_started && !a2dp.a2dp_total_active_session_request) {
        ALOGV("calling BT module stream stop");
        ret = a2dp.audio_stop_stream();
        if (ret < 0)
            ALOGE("stop stream to BT IPC lib failed");
        else
            ALOGV("stop steam to BT IPC lib successful");
        reset_a2dp_enc_config_params();
    }
    if(!a2dp.a2dp_total_active_session_request)
       a2dp.a2dp_started = false;
    ALOGD("Stop A2DP playback total active sessions :%d",
          a2dp.a2dp_total_active_session_request);
    return 0;
}

void audio_extn_a2dp_set_parameters(struct str_parms *parms)
{
     int ret, val;
     char value[32]={0};
     struct audio_usecase *uc_info;
     struct listnode *node;

     if(a2dp.is_a2dp_offload_supported == false) {
        ALOGV("no supported encoders identified,ignoring a2dp setparam");
        return;
     }

     ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_CONNECT, value,
                            sizeof(value));
     if( ret >= 0) {
         val = atoi(value);
         if (val & AUDIO_DEVICE_OUT_ALL_A2DP) {
             ALOGV("Received device connect request for A2DP");
             open_a2dp_output();
         }
         goto param_handled;
     }

     ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, value,
                         sizeof(value));

     if( ret >= 0) {
         val = atoi(value);
         if (val & AUDIO_DEVICE_OUT_ALL_A2DP) {
             ALOGV("Received device dis- connect request");
             reset_a2dp_enc_config_params();
             close_a2dp_output();
         }
         goto param_handled;
     }

     ret = str_parms_get_str(parms, "A2dpSuspended", value, sizeof(value));
     if (ret >= 0) {
         if (a2dp.bt_lib_handle && (a2dp.bt_state != A2DP_STATE_DISCONNECTED) ) {
             if ((!strncmp(value,"true",sizeof(value)))) {
                ALOGD("Setting a2dp to suspend state");
                a2dp.a2dp_suspended = true;
                list_for_each(node, &a2dp.adev->usecase_list) {
                    uc_info = node_to_item(node, struct audio_usecase, list);
                    if (uc_info->type == PCM_PLAYBACK &&
                         (uc_info->stream.out->devices & AUDIO_DEVICE_OUT_ALL_A2DP)) {
                        pthread_mutex_unlock(&a2dp.adev->lock);
                        check_a2dp_restore(a2dp.adev, uc_info->stream.out, false);
                        pthread_mutex_lock(&a2dp.adev->lock);
                    }
                }
                reset_a2dp_enc_config_params();
                if(a2dp.audio_suspend_stream)
                   a2dp.audio_suspend_stream();
            } else if (a2dp.a2dp_suspended == true) {
                ALOGD("Resetting a2dp suspend state");
                struct audio_usecase *uc_info;
                struct listnode *node;
                if(a2dp.clear_a2dpsuspend_flag)
                    a2dp.clear_a2dpsuspend_flag();
                a2dp.a2dp_suspended = false;
                /*
                 * It is possible that before suspend,a2dp sessions can be active
                 * for example during music + voice activation concurrency
                 * a2dp suspend will be called & BT will change to sco mode
                 * though music is paused as a part of voice activation
                 * compress session close happens only after pause timeout(10secs)
                 * so if resume request comes before pause timeout as a2dp session
                 * is already active IPC start will not be called from APM/audio_hw
                 * Fix is to call a2dp start for IPC library post suspend
                 * based on number of active session count
                 */
                if (a2dp.a2dp_total_active_session_request > 0) {
                    ALOGD(" Calling IPC lib start post suspend state");
                    if(a2dp.audio_start_stream) {
                        ret =  a2dp.audio_start_stream();
                        if (ret != 0) {
                            ALOGE("BT controller start failed");
                            a2dp.a2dp_started = false;
                        }
                    }
                }
                list_for_each(node, &a2dp.adev->usecase_list) {
                    uc_info = node_to_item(node, struct audio_usecase, list);
                    if (uc_info->type == PCM_PLAYBACK &&
                         (uc_info->stream.out->devices & AUDIO_DEVICE_OUT_ALL_A2DP)) {
                        pthread_mutex_unlock(&a2dp.adev->lock);
                        check_a2dp_restore(a2dp.adev, uc_info->stream.out, true);
                        pthread_mutex_lock(&a2dp.adev->lock);
                    }
                }
            }
        }
        goto param_handled;
     }
param_handled:
     ALOGV("end of a2dp setparam");
}

void audio_extn_a2dp_set_handoff_mode(bool is_on)
{
    a2dp.is_handoff_in_progress = is_on;
}

bool audio_extn_a2dp_is_force_device_switch()
{
    //During encoder reconfiguration mode, force a2dp device switch
    // Or if a2dp device is selected but earlier start failed ( as a2dp
    // was suspended, force retry.
    return a2dp.is_handoff_in_progress || !a2dp.a2dp_started;
}

void audio_extn_a2dp_get_apptype_params(uint32_t *sample_rate,
                                        uint32_t *bit_width)
{
    if(a2dp.bt_encoder_format == AUDIO_FORMAT_APTX_HD)
        *bit_width = 24;
    else
        *bit_width = 16;
    *sample_rate = a2dp.enc_sampling_rate;
}

bool audio_extn_a2dp_is_ready()
{
    bool ret = false;

    if (a2dp.a2dp_suspended)
        return ret;

    if ((a2dp.bt_state != A2DP_STATE_DISCONNECTED) &&
        (a2dp.is_a2dp_offload_supported) &&
        (a2dp.audio_check_a2dp_ready))
           ret = a2dp.audio_check_a2dp_ready();
    return ret;
}

bool audio_extn_a2dp_is_suspended()
{
    return a2dp.a2dp_suspended;
}

void audio_extn_a2dp_init (void *adev)
{
  a2dp.adev = (struct audio_device*)adev;
  a2dp.bt_lib_handle = NULL;
  a2dp.a2dp_started = false;
  a2dp.bt_state = A2DP_STATE_DISCONNECTED;
  a2dp.a2dp_total_active_session_request = 0;
  a2dp.a2dp_suspended = false;
  a2dp.bt_encoder_format = AUDIO_FORMAT_INVALID;
  a2dp.enc_sampling_rate = 48000;
  a2dp.is_a2dp_offload_supported = false;
  a2dp.is_handoff_in_progress = false;
  update_offload_codec_capabilities();
}

uint32_t audio_extn_a2dp_get_encoder_latency()
{
    uint32_t latency = 0;
    int avsync_runtime_prop = 0;
    int sbc_offset = 0, aptx_offset = 0, aptxhd_offset = 0, aac_offset = 0;
    char value[PROPERTY_VALUE_MAX];

    memset(value, '\0', sizeof(char)*PROPERTY_VALUE_MAX);
    avsync_runtime_prop = property_get("vendor.audio.a2dp.codec.latency", value, NULL);
    if (avsync_runtime_prop > 0) {
        if (sscanf(value, "%d/%d/%d/%d",
                  &sbc_offset, &aptx_offset, &aptxhd_offset, &aac_offset) != 4) {
            ALOGI("Failed to parse avsync offset params from '%s'.", value);
            avsync_runtime_prop = 0;
        }
    }

    uint32_t slatency = 0;
    if (a2dp.audio_get_a2dp_sink_latency && a2dp.bt_state != A2DP_STATE_DISCONNECTED) {
        slatency = a2dp.audio_get_a2dp_sink_latency();
    }

    switch(a2dp.bt_encoder_format) {
        case AUDIO_FORMAT_SBC:
            latency = (avsync_runtime_prop > 0) ? sbc_offset : ENCODER_LATENCY_SBC;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_SBC : slatency;
            break;
        case AUDIO_FORMAT_APTX:
            latency = (avsync_runtime_prop > 0) ? aptx_offset : ENCODER_LATENCY_APTX;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_APTX : slatency;
            break;
        case AUDIO_FORMAT_APTX_HD:
            latency = (avsync_runtime_prop > 0) ? aptxhd_offset : ENCODER_LATENCY_APTX_HD;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_APTX_HD : slatency;
            break;
        case AUDIO_FORMAT_AAC:
            latency = (avsync_runtime_prop > 0) ? aac_offset : ENCODER_LATENCY_AAC;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_AAC : slatency;
            break;
        default:
            latency = 200;
            break;
    }
    return latency;
}
#endif // SPLIT_A2DP_ENABLED
