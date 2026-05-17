#ifndef __VIRTIO_H__
#define __VIRTIO_H__

#include "types.h"

// --- VirtIO MMIO Base Addresses ---
#define VIRTIO_DISK_BASE   0x10001000UL
#define VIRTIO_NET_BASE    0x10002000UL
#define VIRTIO_KBD_BASE    0x10003000UL
#define VIRTIO_MOUSE_BASE  0x10004000UL
#define VIRTIO_SND_BASE    0x10005000UL

// --- VirtIO MMIO Registers ---
#define VIRTIO_MMIO_MAGIC_VALUE      0x000
#define VIRTIO_MMIO_VERSION          0x004
#define VIRTIO_MMIO_DEVICE_ID        0x008
#define VIRTIO_MMIO_VENDOR_ID        0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES  0x010
#define VIRTIO_MMIO_DRIVER_FEATURES  0x020
#define VIRTIO_MMIO_GUEST_PAGE_SIZE  0x028
#define VIRTIO_MMIO_QUEUE_SEL        0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX    0x034
#define VIRTIO_MMIO_QUEUE_NUM        0x038
#define VIRTIO_MMIO_QUEUE_ALIGN      0x03c
#define VIRTIO_MMIO_QUEUE_PFN        0x040
#define VIRTIO_MMIO_QUEUE_READY      0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY     0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK    0x064
#define VIRTIO_MMIO_STATUS           0x070

#define VIRTIO_MMIO_QueueDescLow     0x080
#define VIRTIO_MMIO_QueueAvailLow    0x090
#define VIRTIO_MMIO_QueueUsedLow     0x0a0

// --- Status & Flags ---
#define VIRTIO_CONFIG_S_ACKNOWLEDGE  1
#define VIRTIO_CONFIG_S_DRIVER       2
#define VIRTIO_CONFIG_S_DRIVER_OK    4
#define VIRTIO_CONFIG_S_FEATURES_OK  8
#define VRING_DESC_F_NEXT            1
#define VRING_DESC_F_WRITE           2
#define NUM                          8
#define PGSIZE                       4096

typedef struct virtq_desc { uint64 addr; uint32 len; uint16 flags; uint16 next; } virtq_desc_t;
typedef struct virtq_avail { uint16 flags; uint16 idx; uint16 ring[NUM]; } virtq_avail_t;
typedef struct virtq_used_elem { uint32 id; uint32 len; } virtq_used_elem_t;
typedef struct virtq_used { uint16 flags; uint16 idx; struct virtq_used_elem ring[NUM]; } virtq_used_t;

// --- Disk Specific ---
#define BSIZE 4096
#define VIRTIO_BLK_T_IN  0
#define VIRTIO_BLK_T_OUT 1
struct blk { uint32 dev; uint32 blockno; lock_t lock; volatile int disk; unsigned char data[BSIZE]; };
typedef struct { uint32 type; uint32 reserved; uint64 sector; } virtio_blk_req_t;

void virtio_disk_init();
void virtio_disk_rw(struct blk *b, int write);
void virtio_disk_isr();

// --- Net Specific ---
void virtio_net_init();
void virtio_net_rx_loop2();
void virtio_net_interrupt_handler();
int virtio_net_has_pending_irq();
int virtio_net_has_rx_ready(void);

// --- Input Specific ---
struct virtio_input_event { uint16 type; uint16 code; uint32 value; };
void virtio_keyboard_init();
void virtio_keyboard_isr();
void virtio_mouse_init();
void virtio_mouse_isr();
void virtio_input_poll(void);

// --- Sound Specific ---
void virtio_snd_init(void);
void virtio_snd_poll(void);
void virtio_snd_isr(void);

#endif
