// Copyright 2018 Espressif Systems (Shanghai) PTE LTD
// All rights reserved.

#include <string.h>
#include "esp_log.h"
#include "audio_error.h"
#include "audio_mem.h"
#include "audio_element.h"
#include "iir_pipeline_element.h"
#include "audio_type_def.h"

static const char *TAG = "IIRPIPELINE";

#define BUF_SIZE (128)

// this file is based on the nullpipe sample 
typedef struct nullpipe {
    int  samplerate;
    int  channel;
    unsigned char *buf;
    int  byte_num;
    int  at_eof;
} nullpipe_t;

typedef union {
	short audiosample16; // 16bit int
	struct audiosample16_bytes{
		unsigned h	:8;
		unsigned l	:8;
	} audiosample16_bytes;
} audiosample16_t;

#ifdef DEBUG_NULLPIPE_ENC_ISSUE
static FILE *infile;
#endif

// Filters: maybe calculate them directly in code in the future...
struct iir_filt {
   float in_z1;
   float in_z2;
   float out_z1;
   float out_z2;
   float a0;
   float a1;
   float a2;
   float b1;
   float b2;  
};
	

// SAKPC EA Filters, generated with https://www.earlevel.com/main/2021/09/02/biquad-calculator-v3/
// calculator upgraded with Q value for high shelf filter (see comments)
// Filter 1: ON HPQ Fc 69 Hz Q 1.2
static struct iir_filt conf_filter1 = {
.a0 = 0.9958965322266935,
.a1 = -1.991793064453387,
.a2 = 0.9958965322266935,
.b1 = -1.9917449393185656,
.b2 = 0.9918411895882085
};
// Filter 2: ON HPQ Fc 59 Hz Q 1.1
static struct iir_filt conf_filter2 = {
.a0 = 0.9961760470664148,
.a1 = -1.9923520941328297,
.a2 = 0.9961760470664148,
.b1 = -1.9923168977692702,
.b2 = 0.992387290496389
};
// Filter 3: ON HSC 24 dB Fc 133 Hz Gain -3.4 dB
static struct iir_filt conf_filter3 = {
.a0 = 0.6772322964303608,
.a1 = -1.341511835475585,
.a2 = 0.6645204271385686,
.b1 = -1.9842991144319317,
.b2 = 0.984540002525276
};
// Filter 4: ON HSC 24 dB Fc 1099 Hz Gain -3.2 dB
static struct iir_filt conf_filter4 = {
.a0 = 0.7015953688090338,
.a1 = -1.2857715758269515,
.a2 = 0.6001008642111104,
.b1 = -1.86205210961961,
.b2 = 0.8779767668128031
};
// Filter 5: ON PK Fc 8987 Hz Gain -5.1 dB Q 3.8
static struct iir_filt conf_filter5 = {
.a0 = 0.9179038018599048,
.a1 = -0.46675276296451695,
.a2 = 0.7123732296851493,
.b1 = -0.46675276296451695,
.b2 = 0.6302770315450541
};
// Filter 6: ON PK Fc 11499 Hz Gain -1.3 dB Q 8.4
static struct iir_filt conf_filter6 = {
.a0 = 0.9910305489063711,
.a1 = 0.12625581444310055,
.a2 = 0.8799184014524579,
.b1 = 0.12625581444310055,
.b2 = 0.8709489503588289
};
// Filter 7: ON PK Fc 14394 Hz Gain -3.6 dB Q 6
static struct iir_filt conf_filter7 = {
.a0 = 0.9658590781370593,
.a1 = 0.8306320472127507,
.a2 = 0.8329015313326626,
.b1 = 0.8306320472127507,
.b2 = 0.7987606094697218
};
// Filter 8: ON PK Fc 2244 Hz Gain -1.4 dB Q 1.6
static struct iir_filt conf_filter8 = {
.a0 = 0.9845991491933594,
.a1 = -1.7022204630882498,
.a2 = 0.8084863291832074,
.b1 = -1.7022204630882498,
.b2 = 0.7930854783765668
};
// Filter 9: ON PK Fc 152 Hz Gain -3.3 dB Q 1.2
static struct iir_filt conf_filter9 = {
.a0 = 0.9958841831522053,
.a1 = -1.9734949359028797,
.a2 = 0.978073624451928,
.b1 = -1.9734949359028797,
.b2 = 0.9739578076041333
};

static esp_err_t is_valid_nullpipe_samplerate(int samplerate)
{
    if ((samplerate != 11025)
        && (samplerate != 22050)
        && (samplerate != 44100)
        && (samplerate != 48000)) {
        ESP_LOGE(TAG, "The sample rate should be only 11025Hz, 22050Hz, 44100Hz, 48000Hz, here is %dHz. (line %d)", samplerate, __LINE__);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t is_valid_nullpipe_channel(int channel)
{
    if ((channel != 1)
        && (channel != 2)) {
        ESP_LOGE(TAG, "The number of channels should be only 1 or 2, here is %d. (line %d)", channel, __LINE__);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t nullpipe_set_info(audio_element_handle_t self, int rate, int ch)
{
    nullpipe_t *nullpipe = (nullpipe_t *)audio_element_getdata(self);
    if (nullpipe->samplerate == rate && nullpipe->channel == ch) {
        return ESP_OK;
    }
    if ((is_valid_nullpipe_samplerate(rate) != ESP_OK)
        || (is_valid_nullpipe_channel(ch) != ESP_OK)) {
        return ESP_ERR_INVALID_ARG;
    } else {
        ESP_LOGI(TAG, "The reset sample rate and channel of audio stream are %d %d.", rate, ch);
    
        nullpipe->samplerate = rate;
        nullpipe->channel = ch;
        
    }
    return ESP_OK;
}

static esp_err_t nullpipe_destroy(audio_element_handle_t self)
{
    nullpipe_t *nullpipe = (nullpipe_t *)audio_element_getdata(self);
    audio_free(nullpipe);
    return ESP_OK;
}

static esp_err_t nullpipe_open(audio_element_handle_t self)
{
#ifdef NULLPIPE_MEMORY_ANALYSIS
    AUDIO_MEM_SHOW(TAG);
#endif
    ESP_LOGD(TAG, "nullpipe_open");
    nullpipe_t *nullpipe = (nullpipe_t *)audio_element_getdata(self);
    audio_element_info_t info = {0};
    audio_element_getinfo(self, &info);
    if (info.sample_rates
        && info.channels) {
        nullpipe->samplerate = info.sample_rates;
        nullpipe->channel = info.channels;
    }
    nullpipe->at_eof = 0;
    if (is_valid_nullpipe_samplerate(nullpipe->samplerate) != ESP_OK
        || is_valid_nullpipe_channel(nullpipe->channel) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    nullpipe->buf = (unsigned char *)calloc(1, BUF_SIZE);
    if (nullpipe->buf == NULL) {
        ESP_LOGE(TAG, "calloc buffer failed. (line %d)", __LINE__);
        return ESP_ERR_NO_MEM;
    }
    memset(nullpipe->buf, 0, BUF_SIZE);

#ifdef DEBUG_NULLPIPE_ENC_ISSUE
    char fileName[100] = {'//', 's', 'd', 'c', 'a', 'r', 'd', '//', 't', 'e', 's', 't', '.', 'p', 'c', 'm', '\0'};
    infile = fopen(fileName, "rb");
    if (!infile) {
        perror(fileName);
        return ESP_FAIL;
    }
#endif

    return ESP_OK;
}

static esp_err_t nullpipe_close(audio_element_handle_t self)
{
    ESP_LOGD(TAG, "nullpipe_close");
    nullpipe_t *nullpipe = (nullpipe_t *)audio_element_getdata(self);
    if(nullpipe->buf == NULL){
        audio_free(nullpipe->buf);
        nullpipe->buf = NULL;
    }   

#ifdef NULLPIPE_MEMORY_ANALYSIS
    AUDIO_MEM_SHOW(TAG);
#endif
#ifdef DEBUG_NULLPIPE_ENC_ISSUE
    fclose(infile);
#endif

    return ESP_OK;
}

static float process_iir (float inSampleF, struct iir_filt * config) {
	float outSampleF =
	(* config).a0 * inSampleF
	+ (* config).a1 * (* config).in_z1
	+ (* config).a2 * (* config).in_z2
	- (* config).b1 * (* config).out_z1
	- (* config).b2 * (* config).out_z2;
	(* config).in_z2 = (* config).in_z1;
	(* config).in_z1 = inSampleF;
	(* config).out_z2 = (* config).out_z1;
	(* config).out_z1 = outSampleF;
	return outSampleF;
}

static int nullpipe_process(audio_element_handle_t self, char *in_buffer, int in_len)
{
    nullpipe_t *nullpipe = (nullpipe_t *)audio_element_getdata(self);
    int ret = 0;

    int r_size = 0;    
    if (nullpipe->at_eof == 0) {
#ifdef DEBUG_NULLPIPE_ENC_ISSUE
        r_size = fread((char *)nullpipe->buf, 1, BUF_SIZE, infile);
#else
        r_size = audio_element_input(self, (char *)nullpipe->buf, BUF_SIZE);
#endif
    }
    if (r_size > 0) {
        if (r_size != BUF_SIZE) {
            nullpipe->at_eof = 1;
        }
        nullpipe->byte_num += r_size;

        unsigned char *pbuf = nullpipe->buf;
        
        for(int i=0;i<r_size;i+=2){
            audiosample16_t audiosample;
            audiosample.audiosample16_bytes.h = *pbuf;
            audiosample.audiosample16_bytes.l = *(pbuf+1);

            // process audio samples here e.g. scale by 1/16 (attenuation):
            // audiosample.audiosample16 = audiosample.audiosample16>>4;


            // we need float for IIR filters (64bit/double would be even better for more precision)
            float sample = ((float) audiosample.audiosample16) / 32767.0f;
            // assuming sample is between -1 and +1...

            // do IIR here
            sample = process_iir(sample, &conf_filter1);
            sample = process_iir(sample, &conf_filter2);
            sample = process_iir(sample, &conf_filter3);
            sample = process_iir(sample, &conf_filter4);
            sample = process_iir(sample, &conf_filter5);
            sample = process_iir(sample, &conf_filter6);
            sample = process_iir(sample, &conf_filter7);
            sample = process_iir(sample, &conf_filter8);
            sample = process_iir(sample, &conf_filter9);

            // convert float back to short
            audiosample.audiosample16 = (short)(sample * 32767.0f);

            *pbuf = audiosample.audiosample16_bytes.h;
            *(pbuf+1) = audiosample.audiosample16_bytes.l;
            pbuf+=2;
        }
        ret = audio_element_output(self, (char *)nullpipe->buf, BUF_SIZE);
    } else {
        ret = r_size;
    }
    return ret;
}

audio_element_handle_t nullpipe_init(nullpipe_cfg_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "config is NULL. (line %d)", __LINE__);
        return NULL;
    }
    nullpipe_t *nullpipe = audio_calloc(1, sizeof(nullpipe_t));
    AUDIO_MEM_CHECK(TAG, nullpipe, return NULL);     
    if (nullpipe == NULL) {
        ESP_LOGE(TAG, "audio_calloc failed for nullpipe. (line %d)", __LINE__);
        return NULL;
    }
    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    cfg.destroy = nullpipe_destroy;
    cfg.process = nullpipe_process;
    cfg.open = nullpipe_open;
    cfg.close = nullpipe_close;
    cfg.buffer_len = 0;
    cfg.tag = "nullpipe";
    cfg.task_stack = config->task_stack;
    cfg.task_prio = config->task_prio;
    cfg.task_core = config->task_core;
    cfg.out_rb_size = config->out_rb_size;
    audio_element_handle_t el = audio_element_init(&cfg);
    AUDIO_MEM_CHECK(TAG, el, {audio_free(nullpipe); return NULL;});
    nullpipe->samplerate = config->samplerate;
    nullpipe->channel = config->channel;
    audio_element_setdata(el, nullpipe);
    audio_element_info_t info = {0};
    audio_element_setinfo(el, &info);
    ESP_LOGD(TAG, "nullpipe_init");
    return el;
}
