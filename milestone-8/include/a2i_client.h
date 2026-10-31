// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef A2I_CLIENT_H
#define A2I_CLIENT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Reverse-Engineered A2I (Audio to IPC) Client Definitions
 * Target: Harman Becker / BMW NBT Evo (QNX Neutrino)
 * Library: /opt/mm/lib/liba2i_client.so
 */

/* Endpoint Paths */
#define A2I_ENDPOINT_MEDIA_MAIN      "/input_stereo_AirPlayMain" /* 44.1kHz Stereo Music */
#define A2I_ENDPOINT_MEDIA_FALLBACK  "/input_stereo_4"           /* Fallback stereo channel */
#define A2I_ENDPOINT_ALERT           "/input_stereo_4"           /* Factory CarPlay maps audioType "alert" here */
#define A2I_ENDPOINT_MEDIA_CLIENT    "input_stereo_0"            /* MainAudio client-write probe */
#define A2I_ENDPOINT_NAV_ALT         "/input_stereo_AirPlayAlt"  /* 44.1kHz Stereo Navigation Prompts */
#define A2I_ENDPOINT_SPEECH_DL       "/input_mono_AirPlayDL"     /* 16/24kHz Mono Downlink Voice Out */
#define A2I_ENDPOINT_MIC_UL          "/output_mono_AirPlayUL"    /* 16/24kHz Mono Echo-Cancelled Mic In */

/* Stream Directions */
#define A2I_DIR_OUTPUT_DOWNLINK      0 /* Audio Output to Vehicle Speakers */
#define A2I_DIR_INPUT_UPLINK         1 /* Audio Input / Capture from Vehicle Mic */

/* Bit Depths */
#define A2I_BITS_16                  0
#define A2I_BITS_24                  1
#define A2I_BITS_32                  3

/* Sample Rates */
#define A2I_SAMPLE_RATE_16K          16000
#define A2I_SAMPLE_RATE_24K          24000
#define A2I_SAMPLE_RATE_44K          44100
#define A2I_SAMPLE_RATE_48K          48000
#define A2I_SAMPLE_RATE_48K_CLIENT   0x90000900u /* A2I BYCLIENT encoded 48kHz */

/* Channels */
#define A2I_CHANNELS_MONO            1
#define A2I_CHANNELS_STEREO          2

/* Buffer Size Defaults */
#define A2I_BUFFER_SIZE_STEREO       0x400 /* 1024 bytes */
#define A2I_BUFFER_SIZE_MONO         0x200 /* 512 bytes */

/* Return codes observed in the factory CarPlay A2I wrapper (libconn_carplay_audio_a2i.so) */
#define A2I_OK                       0
#define A2I_ERR_NO_BUFFER            0x14  /* GetBuffer: no free buffer right now */

/* cfg.field_34 values. The project has always opened with the legacy values; the
 * factory CarPlay code passes 0x0ABABABA for every stream. Open tries both. */
#define A2I_COOKIE_LEGACY_PLAYBACK   0x05555555u
#define A2I_COOKIE_LEGACY_CAPTURE    0x00000000u
#define A2I_COOKIE_FACTORY           0x0ABABABAu
#define A2I_COOKIE_NONE              0xFFFFFFFFu

typedef void* a2i_client_handle_t;

/* Exact 60-byte struct layout from AudioStreamStart stack frame */
#pragma pack(push, 4)
typedef struct {
    uint32_t    magic;            /* 0x00: 0x02000000 */
    uint32_t    direction;        /* 0x04: 0 = Downlink (Speakers), 1 = Uplink (Mic) */
    uint32_t    field_08;         /* 0x08: 1 */
    uint32_t    mode;             /* 0x0C: 2 */
    uint32_t    buffer_size;      /* 0x10: 0x400 (Stereo) or 0x200 (Mono) */
    uint32_t    channels;         /* 0x14: 1 = Mono, 2 = Stereo */
    uint32_t    bytes_per_sample; /* 0x18: 2 (16-bit), 3 (24-bit), 4 (32-bit) */
    uint32_t    sample_rate;      /* 0x1C: 16000, 24000, 44100, 48000 */
    uint32_t    field_20;         /* 0x20: 0 */
    const char* endpoint_name;    /* 0x24: e.g. "/input_stereo_AirPlayMain" */
    uint32_t    channel_type;     /* 0x28: 0 (Mono), 1 (Stereo) */
    uint32_t    field_2c;         /* 0x2C: 0 */
    uint32_t    sample_bits;      /* 0x30: 0 (16-bit), 1 (24-bit), 3 (32-bit) */
    uint32_t    field_34;         /* 0x34: 0 */
    uint32_t    timeout_ms;       /* 0x38: 2000 */
    uint32_t    reserved[4];      /* 0x3C - 0x48: Pad to full 0x4C (76 bytes) matching A2I_Client_Open memcpy */
} a2i_client_config_t;
#pragma pack(pop)

/* Core A2I Client C API from /opt/mm/lib/liba2i_client.so */
int A2I_Client_Open(a2i_client_handle_t* handle, const a2i_client_config_t* config);
int A2I_Client_Start(a2i_client_handle_t handle);
int A2I_Client_Stop(a2i_client_handle_t handle);
int A2I_Client_Close(a2i_client_handle_t* handle);
int A2I_Client_GetNotification(a2i_client_handle_t handle);
int A2I_Client_GetBuffer(a2i_client_handle_t handle, void** buffer_ptr, size_t* buffer_size);
int A2I_Client_Out_Write(a2i_client_handle_t handle, const void* buffer, size_t size, uint32_t flags);
const char* A2I_Client_GetErrorDescription(int error_code);

#ifdef __cplusplus
}
#endif

#endif /* A2I_CLIENT_H */
