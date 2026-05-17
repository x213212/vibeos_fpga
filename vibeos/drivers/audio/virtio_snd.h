#pragma once
#ifndef VIRTIO_SND_H
#define VIRTIO_SND_H

#include "types.h"

struct virtio_snd_config {
    uint32_t jacks;
    uint32_t streams;
    uint32_t chmaps;
} __attribute__((packed));

enum {
    VIRTIO_SND_VQ_CONTROL = 0,
    VIRTIO_SND_VQ_EVENT = 1,
    VIRTIO_SND_VQ_TX = 2,
    VIRTIO_SND_VQ_RX = 3,
    VIRTIO_SND_VQ_MAX = 4,
};

enum {
    VIRTIO_SND_D_OUTPUT = 0,
    VIRTIO_SND_D_INPUT = 1,
};

enum {
    VIRTIO_SND_R_JACK_INFO = 1,
    VIRTIO_SND_R_JACK_REMAP = 2,
    VIRTIO_SND_R_PCM_INFO = 0x0100,
    VIRTIO_SND_R_PCM_SET_PARAMS = 0x0101,
    VIRTIO_SND_R_PCM_PREPARE = 0x0102,
    VIRTIO_SND_R_PCM_RELEASE = 0x0103,
    VIRTIO_SND_R_PCM_START = 0x0104,
    VIRTIO_SND_R_PCM_STOP = 0x0105,
    VIRTIO_SND_R_CHMAP_INFO = 0x0200,
    VIRTIO_SND_S_OK = 0x8000,
    VIRTIO_SND_S_BAD_MSG = 0x8001,
    VIRTIO_SND_S_NOT_SUPP = 0x8002,
    VIRTIO_SND_S_IO_ERR = 0x8003,
};

struct virtio_snd_hdr {
    uint32_t code;
} __attribute__((packed));

struct virtio_snd_query_info {
    struct virtio_snd_hdr hdr;
    uint32_t start_id;
    uint32_t count;
    uint32_t size;
} __attribute__((packed));

struct virtio_snd_info {
    uint32_t hda_fn_nid;
} __attribute__((packed));

struct virtio_snd_pcm_hdr {
    struct virtio_snd_hdr hdr;
    uint32_t stream_id;
} __attribute__((packed));

enum {
    VIRTIO_SND_PCM_FMT_S16 = 5,
};

enum {
    VIRTIO_SND_PCM_RATE_44100 = 6,
};

struct virtio_snd_pcm_info {
    struct virtio_snd_info hdr;
    uint32_t features;
    uint64_t formats;
    uint64_t rates;
    uint8_t direction;
    uint8_t channels_min;
    uint8_t channels_max;
    uint8_t padding[5];
} __attribute__((packed));

struct virtio_snd_pcm_set_params {
    struct virtio_snd_pcm_hdr hdr;
    uint32_t buffer_bytes;
    uint32_t period_bytes;
    uint32_t features;
    uint8_t channels;
    uint8_t format;
    uint8_t rate;
    uint8_t padding;
} __attribute__((packed));

struct virtio_snd_pcm_xfer {
    uint32_t stream_id;
} __attribute__((packed));

struct virtio_snd_pcm_status {
    uint32_t status;
    uint32_t latency_bytes;
} __attribute__((packed));

#endif
