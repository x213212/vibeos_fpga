#include "virtio.h"
#include "os.h"

#define R(addr) ((volatile uint32 *)(VIRTIO_DISK_BASE + (addr)))
#ifdef FPGA_MINIMAL
#define FPGA_RAMDISK_BLOCKS 512u
#endif
extern int os_debug;

#ifdef FPGA_MINIMAL
static unsigned char fpga_ramdisk[FPGA_RAMDISK_BLOCKS][BSIZE] __attribute__((aligned(BSIZE)));
static struct lock fpga_ramdisk_lock;
#else
static struct disk {
  char pages[2 * PGSIZE];
  virtq_desc_t *desc; virtq_avail_t *avail; volatile virtq_used_t *used;
  char free[NUM]; struct { struct blk *b; char status; } info[NUM];
  volatile uint16 used_idx; virtio_blk_req_t ops[NUM]; struct lock vdisk_lock;
} __attribute__((aligned(PGSIZE))) disk;
#endif

void virtio_disk_init() {
#ifdef FPGA_MINIMAL
  lock_init(&fpga_ramdisk_lock);
  memset(fpga_ramdisk, 0, sizeof(fpga_ramdisk));
  lib_puts("[VIBE] ramdisk: 512 blocks x 4096 bytes\n");
#else
  uint32 status = 0; lock_init(&disk.vdisk_lock);
  if (*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976) panic("virtio disk fail");
  *R(VIRTIO_MMIO_STATUS) = 0;
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE; *R(VIRTIO_MMIO_STATUS) = status;
  status |= VIRTIO_CONFIG_S_DRIVER; *R(VIRTIO_MMIO_STATUS) = status;
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = 0;
  status |= VIRTIO_CONFIG_S_FEATURES_OK; *R(VIRTIO_MMIO_STATUS) = status;
  status |= VIRTIO_CONFIG_S_DRIVER_OK; *R(VIRTIO_MMIO_STATUS) = status;
  *R(VIRTIO_MMIO_GUEST_PAGE_SIZE) = PGSIZE;
  *R(VIRTIO_MMIO_QUEUE_SEL) = 0;
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;
  *R(VIRTIO_MMIO_QUEUE_ALIGN) = PGSIZE;
  memset(disk.pages, 0, sizeof(disk.pages));
  *R(VIRTIO_MMIO_QUEUE_PFN) = ((uint32)disk.pages) / PGSIZE;
  disk.desc = (virtq_desc_t *)disk.pages;
  disk.avail = (virtq_avail_t *)(disk.pages + NUM * sizeof(virtq_desc_t));
  disk.used = (virtq_used_t *)(disk.pages + PGSIZE);
  for (int i = 0; i < NUM; i++) disk.free[i] = 1;
  if (os_debug) lib_puts("Disk Ready\n");
#endif
}

void virtio_disk_rw(struct blk *b_ptr, int write) {
#ifdef FPGA_MINIMAL
  if (b_ptr->blockno >= FPGA_RAMDISK_BLOCKS) panic("ramdisk block out of range");
  lock_acquire(&fpga_ramdisk_lock);
  if (write) memcpy(fpga_ramdisk[b_ptr->blockno], b_ptr->data, BSIZE);
  else memcpy(b_ptr->data, fpga_ramdisk[b_ptr->blockno], BSIZE);
  b_ptr->disk = 0;
  lock_free(&fpga_ramdisk_lock);
#else
  uint64 sector = b_ptr->blockno * (BSIZE / 512);
  lock_acquire(&disk.vdisk_lock);
  disk.ops[0].type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
  disk.ops[0].sector = sector;
  disk.desc[0].addr = (uint64)(uint32)&disk.ops[0];
  disk.desc[0].len = sizeof(virtio_blk_req_t);
  disk.desc[0].flags = VRING_DESC_F_NEXT; disk.desc[0].next = 1;
  disk.desc[1].addr = (uint64)(uint32)b_ptr->data;
  disk.desc[1].len = BSIZE;
  disk.desc[1].flags = write ? 0 : VRING_DESC_F_WRITE;
  disk.desc[1].flags |= VRING_DESC_F_NEXT; disk.desc[1].next = 2;
  disk.info[0].status = 0;
  disk.desc[2].addr = (uint64)(uint32)&disk.info[0].status;
  disk.desc[2].len = 1; disk.desc[2].flags = VRING_DESC_F_WRITE; disk.desc[2].next = 0;
  b_ptr->disk = 1; disk.info[0].b = b_ptr;
  disk.avail->ring[disk.avail->idx % NUM] = 0;
  __sync_synchronize(); disk.avail->idx += 1;
  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;
  while (b_ptr->disk == 1) {
    __sync_synchronize();
    virtio_disk_isr();
  }
  lock_free(&disk.vdisk_lock);
#endif
}

void virtio_disk_isr() {
#ifdef FPGA_MINIMAL
  return;
#else
  *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;
  while (disk.used_idx != disk.used->idx) {
    int id = disk.used->ring[disk.used_idx % NUM].id;
    disk.info[id].b->disk = 0; disk.used_idx += 1;
  }
#endif
}
void virtio_tester(int w) {}
