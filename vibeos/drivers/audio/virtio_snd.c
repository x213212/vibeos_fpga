#include "virtio.h"
#include "virtio_snd.h"
#include "gbemu_app.h"
#include "os.h"
#include "string.h"

#define R_SND(addr) ((volatile uint32 *)(VIRTIO_SND_BASE + (addr)))
#define SND_QUEUES 4
#define SND_TX_SLOTS 2
#define SND_PERIOD_FRAMES 735
#define SND_CHANNELS 2
#define SND_SAMPLE_BYTES 2
#define SND_PERIOD_BYTES (SND_PERIOD_FRAMES * SND_CHANNELS * SND_SAMPLE_BYTES)
#define SND_BUFFER_BYTES (SND_PERIOD_BYTES * SND_TX_SLOTS)

typedef struct {
    char pages[2 * PGSIZE] __attribute__((aligned(PGSIZE)));
    virtq_desc_t *desc;
    virtq_avail_t *avail;
    virtq_used_t *used;
    uint16_t used_idx;
    uint16_t num;
} snd_queue_t;

typedef struct {
    uint8_t pcm[SND_PERIOD_BYTES];
    struct virtio_snd_pcm_xfer xfer;
    struct virtio_snd_pcm_status status;
    int busy;
} snd_slot_t;

static struct {
    snd_queue_t q[SND_QUEUES];
    snd_slot_t slot[SND_TX_SLOTS];
    uint16_t tx_free[SND_TX_SLOTS];
    uint16_t tx_free_head;
    uint16_t tx_free_tail;
    int ready;
    int started;
    int init_done;
} snd;

static inline void snd_free_push(uint16_t id) {
    snd.tx_free[snd.tx_free_tail % SND_TX_SLOTS] = id;
    snd.tx_free_tail++;
}

static inline int snd_free_pop(uint16_t *id) {
    if (snd.tx_free_head == snd.tx_free_tail) return 0;
    *id = snd.tx_free[snd.tx_free_head % SND_TX_SLOTS];
    snd.tx_free_head++;
    return 1;
}

static void snd_queue_setup(int qid) {
    snd_queue_t *q = &snd.q[qid];
    uint32_t qmax;
    *R_SND(VIRTIO_MMIO_QUEUE_SEL) = qid;
    qmax = *R_SND(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (qmax == 0) return;
    q->num = (qmax < NUM) ? (uint16_t)qmax : (uint16_t)NUM;
    memset(q->pages, 0, sizeof(q->pages));
    q->desc = (virtq_desc_t *)q->pages;
    q->avail = (virtq_avail_t *)(q->pages + q->num * sizeof(virtq_desc_t));
    q->used = (virtq_used_t *)(q->pages + PGSIZE);
    q->used_idx = 0;
    *R_SND(VIRTIO_MMIO_QUEUE_NUM) = q->num;
    *R_SND(VIRTIO_MMIO_QUEUE_ALIGN) = PGSIZE;
    *R_SND(VIRTIO_MMIO_QUEUE_PFN) = ((uint32)(uintptr_t)q->pages) / PGSIZE;
}

static int snd_wait_queue_used(int qid, uint16_t used_idx_before) {
    snd_queue_t *q = &snd.q[qid];
    uint32_t spins = 0;
    while (q->used->idx == used_idx_before && spins++ < 1000000U) {
        virtio_snd_poll();
    }
    return q->used->idx != used_idx_before;
}

static int snd_ctrl_exec(const void *req, uint32_t req_len, void *resp, uint32_t resp_len) {
    snd_queue_t *q = &snd.q[VIRTIO_SND_VQ_CONTROL];
    q->desc[0].addr = (uint64)(uint32)(uintptr_t)req;
    q->desc[0].len = req_len;
    q->desc[0].flags = VRING_DESC_F_NEXT;
    q->desc[0].next = 1;
    q->desc[1].addr = (uint64)(uint32)(uintptr_t)resp;
    q->desc[1].len = resp_len;
    q->desc[1].flags = VRING_DESC_F_WRITE;
    q->desc[1].next = 0;
    q->avail->ring[q->avail->idx % q->num] = 0;
    __sync_synchronize();
    q->avail->idx++;
    *R_SND(VIRTIO_MMIO_QUEUE_NOTIFY) = VIRTIO_SND_VQ_CONTROL;
    if (!snd_wait_queue_used(VIRTIO_SND_VQ_CONTROL, q->used_idx)) return -1;
    q->used_idx = q->used->idx;
    return 0;
}

static int snd_submit_period(uint16_t slot_id) {
    snd_queue_t *q = &snd.q[VIRTIO_SND_VQ_TX];
    snd_slot_t *slot = &snd.slot[slot_id];
    uint16_t head = (uint16_t)(slot_id * 3);
    slot->xfer.stream_id = 0;
    gbemu_audio_render_pcm_s16_stereo((int16_t *)slot->pcm, SND_PERIOD_FRAMES);
    q->desc[head + 0].addr = (uint64)(uint32)(uintptr_t)&slot->xfer;
    q->desc[head + 0].len = sizeof(slot->xfer);
    q->desc[head + 0].flags = VRING_DESC_F_NEXT;
    q->desc[head + 0].next = head + 1;
    q->desc[head + 1].addr = (uint64)(uint32)(uintptr_t)slot->pcm;
    q->desc[head + 1].len = SND_PERIOD_BYTES;
    q->desc[head + 1].flags = VRING_DESC_F_NEXT;
    q->desc[head + 1].next = head + 2;
    q->desc[head + 2].addr = (uint64)(uint32)(uintptr_t)&slot->status;
    q->desc[head + 2].len = sizeof(slot->status);
    q->desc[head + 2].flags = VRING_DESC_F_WRITE;
    q->desc[head + 2].next = 0;
    q->avail->ring[q->avail->idx % q->num] = head;
    __sync_synchronize();
    q->avail->idx++;
    slot->busy = 1;
    *R_SND(VIRTIO_MMIO_QUEUE_NOTIFY) = VIRTIO_SND_VQ_TX;
    return 0;
}

static void snd_recycle_tx(void) {
    snd_queue_t *q = &snd.q[VIRTIO_SND_VQ_TX];
    while (q->used_idx != q->used->idx) {
        uint16_t head = (uint16_t)q->used->ring[q->used_idx % q->num].id;
        uint16_t slot_id = head / 3;
        if (slot_id < SND_TX_SLOTS) {
            snd.slot[slot_id].busy = 0;
            snd_free_push(slot_id);
        }
        q->used_idx++;
    }
}

static int snd_start_stream(void) {
    struct virtio_snd_pcm_set_params params;
    struct virtio_snd_hdr req;
    struct virtio_snd_hdr resp;
    memset(&params, 0, sizeof(params));
    params.hdr.hdr.code = VIRTIO_SND_R_PCM_SET_PARAMS;
    params.hdr.stream_id = 0;
    params.buffer_bytes = SND_BUFFER_BYTES;
    params.period_bytes = SND_PERIOD_BYTES;
    params.features = 0;
    params.channels = 2;
    params.format = VIRTIO_SND_PCM_FMT_S16;
    params.rate = VIRTIO_SND_PCM_RATE_44100;
    memset(&resp, 0, sizeof(resp));
    if (snd_ctrl_exec(&params, sizeof(params), &resp, sizeof(resp)) != 0) return -1;

    memset(&req, 0, sizeof(req));
    req.code = VIRTIO_SND_R_PCM_PREPARE;
    memset(&resp, 0, sizeof(resp));
    if (snd_ctrl_exec(&req, sizeof(req), &resp, sizeof(resp)) != 0) return -1;

    memset(&req, 0, sizeof(req));
    req.code = VIRTIO_SND_R_PCM_START;
    memset(&resp, 0, sizeof(resp));
    if (snd_ctrl_exec(&req, sizeof(req), &resp, sizeof(resp)) != 0) return -1;

    snd.started = 1;
    return 0;
}

void virtio_snd_poll(void) {
    if (!snd.ready) return;
    snd_recycle_tx();
    if (!snd.started) return;
    while (snd.tx_free_head != snd.tx_free_tail && snd.q[VIRTIO_SND_VQ_TX].num > 0) {
        uint16_t slot_id;
        if (!snd_free_pop(&slot_id)) break;
        if (snd.slot[slot_id].busy) continue;
        snd_submit_period(slot_id);
        if ((snd.tx_free_tail - snd.tx_free_head) <= 0) break;
        if ((snd.q[VIRTIO_SND_VQ_TX].avail->idx - snd.q[VIRTIO_SND_VQ_TX].used->idx) >= 2) break;
    }
}

void virtio_snd_isr(void) {
    if (snd.ready) virtio_snd_poll();
    *R_SND(VIRTIO_MMIO_INTERRUPT_ACK) = *R_SND(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;
}

void virtio_snd_init(void) {
    uint32_t device_id;
    uint32_t status;
    struct virtio_snd_pcm_info info;
    struct virtio_snd_query_info query;

    if (*R_SND(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976) return;
    device_id = *R_SND(VIRTIO_MMIO_DEVICE_ID);
    if (device_id != 25) return;

    *R_SND(VIRTIO_MMIO_STATUS) = 0;
    status = VIRTIO_CONFIG_S_ACKNOWLEDGE;
    *R_SND(VIRTIO_MMIO_STATUS) = status;
    status |= VIRTIO_CONFIG_S_DRIVER;
    *R_SND(VIRTIO_MMIO_STATUS) = status;
    *R_SND(VIRTIO_MMIO_DRIVER_FEATURES) = 0;
    status |= VIRTIO_CONFIG_S_FEATURES_OK;
    *R_SND(VIRTIO_MMIO_STATUS) = status;
    *R_SND(VIRTIO_MMIO_GUEST_PAGE_SIZE) = PGSIZE;

    for (int i = 0; i < SND_QUEUES; i++) snd_queue_setup(i);

    snd.tx_free_head = 0;
    snd.tx_free_tail = 0;
    for (int i = 0; i < SND_TX_SLOTS; i++) {
        snd.slot[i].busy = 0;
        snd_free_push((uint16_t)i);
    }

    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    *R_SND(VIRTIO_MMIO_STATUS) = status;

    memset(&query, 0, sizeof(query));
    query.hdr.code = VIRTIO_SND_R_PCM_INFO;
    query.start_id = 0;
    query.count = 1;
    query.size = sizeof(info);
    memset(&info, 0, sizeof(info));
    info.direction = VIRTIO_SND_D_OUTPUT;
    info.channels_min = 2;
    info.channels_max = 2;
    info.formats = (1ULL << VIRTIO_SND_PCM_FMT_S16);
    info.rates = (1ULL << VIRTIO_SND_PCM_RATE_44100);
    snd_ctrl_exec(&query, sizeof(query), &info, sizeof(info));

    snd.ready = 1;
    snd.started = 0;
    snd.init_done = 1;
    if (snd_start_stream() == 0) {
        virtio_snd_poll();
    }
}
