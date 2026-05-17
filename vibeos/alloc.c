/*  Bitmap allocator for 08-BlockDeviceDriver
 *  - 4KB pages
 *  - 1 bit per page
 *  - 16-byte allocation header so free() can recover block length
 */

#include "os.h"

extern uint32_t TEXT_START;
extern uint32_t TEXT_END;
extern uint32_t DATA_START;
extern uint32_t DATA_END;
extern uint32_t RODATA_START;
extern uint32_t RODATA_END;
extern uint32_t BSS_START;
extern uint32_t BSS_END;
extern uint32_t HEAP_START;
extern uint32_t HEAP_SIZE;

#define PAGE_SIZE 4096
#define PAGE_ORDER 12
#define PAGE_MASK (PAGE_SIZE - 1)
#define BITS_PER_BYTE 8

#define ALLOC_HEADER_SIZE 16
#define ALLOC_MAGIC 0x4d415030u /* "MAP0" */

struct alloc_header
{
  uint32_t magic;
  uint32_t npages;
  uint32_t req_size;
  uint32_t reserved;
};

uint32_t _alloc_start = 0;
static uint32_t _alloc_end = 0;
static uint32_t _heap_total_pages = 0;
uint32_t _alloc_pages = 0;
static uint32_t _bitmap_bytes = 0;
static uint8_t *_bitmap = 0;
static lock_t _alloc_lock;

#ifdef FPGA_MINIMAL
#define FPGA_USB_DMA_CPU_BASE 0x851da000u
#define FPGA_USB_DMA_CPU_SIZE 0x00010000u
#define FPGA_MINIMAL_HEAP_SIZE (32u * 1024u * 1024u)
#define fpga_alloc_mark(value) do { \
  asm volatile("fence rw,rw" ::: "memory"); \
  volatile uint32_t *dbg = (volatile uint32_t *)0x10004000u; \
  dbg[15] = (uint32_t)(value); \
  asm volatile("fence rw,rw" ::: "memory"); \
} while (0)
#endif

// Activity counters
static uint32_t _malloc_calls = 0;
static uint32_t _free_calls = 0;

static inline uint32_t _align_page(uint32_t address)
{
  return (address + PAGE_MASK) & ~PAGE_MASK;
}

static inline uint32_t _page_index_from_addr(uint32_t address)
{
  return (address - HEAP_START) >> PAGE_ORDER;
}

static inline uint8_t _page_bit_mask(uint32_t page_index)
{
  return (uint8_t)(1u << (page_index & (BITS_PER_BYTE - 1)));
}

int _page_is_used(uint32_t page_index)
{
  return (_bitmap[page_index >> 3] & _page_bit_mask(page_index)) != 0;
}

static inline void _page_set_used(uint32_t page_index)
{
  _bitmap[page_index >> 3] |= _page_bit_mask(page_index);
}

static inline void _page_clear_used(uint32_t page_index)
{
  _bitmap[page_index >> 3] &= (uint8_t)~_page_bit_mask(page_index);
}

static void _reserve_page_range(uint32_t start, uint32_t end)
{
  if (end <= start || _alloc_end <= HEAP_START)
  {
    return;
  }

  uint32_t s = start & ~PAGE_MASK;
  uint32_t e = _align_page(end);

  if (e <= HEAP_START || s >= _alloc_end)
  {
    return;
  }
  if (s < HEAP_START)
  {
    s = HEAP_START;
  }
  if (e > _alloc_end)
  {
    e = _alloc_end;
  }

  uint32_t first = (s - HEAP_START) >> PAGE_ORDER;
  uint32_t last = (e - HEAP_START) >> PAGE_ORDER;
  for (uint32_t page = first; page < last; page++)
  {
    _page_set_used(page);
  }
}

static inline uint32_t _pages_for_size(uint32_t size)
{
  return (size + PAGE_SIZE - 1) >> PAGE_ORDER;
}

static inline uint32_t _irq_save(void)
{
  uint32_t state = r_mstatus();
  w_mstatus(state & ~MSTATUS_MIE);
  return state;
}

static inline void _irq_restore(uint32_t state)
{
  w_mstatus(state);
}

void page_init()
{
#ifdef FPGA_MINIMAL
  uint32_t heap_start = HEAP_START;
  uint32_t heap_size = HEAP_SIZE;
  uint32_t total_pages;
  uint32_t bitmap_bytes;
  uint32_t alloc_start;
  uint32_t alloc_end;
  uint32_t alloc_pages;
  uint8_t *bitmap;

  if (heap_size > FPGA_MINIMAL_HEAP_SIZE)
  {
    heap_size = FPGA_MINIMAL_HEAP_SIZE;
  }

  total_pages = heap_size / PAGE_SIZE;
  bitmap_bytes = (total_pages + 7) / 8;
  alloc_start = (heap_start + bitmap_bytes + PAGE_MASK) & ~PAGE_MASK;
  alloc_end = heap_start + (total_pages * PAGE_SIZE);
  alloc_pages = (alloc_end - alloc_start) / PAGE_SIZE;

  if (alloc_start >= alloc_end || alloc_pages == 0)
  {
    for (;;) {
      asm volatile("wfi");
    }
  }

  _heap_total_pages = total_pages;
  bitmap = (uint8_t *)heap_start;
  _bitmap = bitmap;
  _bitmap_bytes = bitmap_bytes;
  _alloc_start = alloc_start;
  _alloc_end = alloc_end;
  _alloc_pages = alloc_pages;

  for (uint32_t i = 0; i < bitmap_bytes; i++)
  {
    bitmap[i] = 0xff;
  }

  for (uint32_t page = (alloc_start - heap_start) / PAGE_SIZE; page < total_pages; page++)
  {
    bitmap[page >> 3] &= (uint8_t)~_page_bit_mask(page);
  }

  _reserve_page_range(FPGA_USB_DMA_CPU_BASE, FPGA_USB_DMA_CPU_BASE + FPGA_USB_DMA_CPU_SIZE);
  return;
#else
  lock_init(&_alloc_lock);

  uint32_t heap_size = HEAP_SIZE;

  _heap_total_pages = heap_size / PAGE_SIZE;
  _bitmap = (uint8_t *)HEAP_START;
  _bitmap_bytes = (_heap_total_pages + 7) / 8;

  _alloc_start = (HEAP_START + _bitmap_bytes + PAGE_MASK) & ~PAGE_MASK;
  _alloc_end = HEAP_START + (_heap_total_pages * PAGE_SIZE);
  _alloc_pages = (_alloc_end - _alloc_start) / PAGE_SIZE;

  if (_alloc_start >= _alloc_end || _alloc_pages == 0)
  {
    panic("page_init: invalid heap layout\n");
  }

  for (uint32_t i = 0; i < _bitmap_bytes; i++)
  {
    _bitmap[i] = 0xff;
  }

  for (uint32_t page = (_alloc_start - HEAP_START) / PAGE_SIZE; page < _heap_total_pages; page++)
  {
    _page_clear_used(page);
  }

  lib_printf("HEAP_START = %x, HEAP_SIZE = %x, total pages = %d\n", HEAP_START, heap_size, _heap_total_pages);
  lib_printf("bitmap bytes = %d, alloc pages = %d\n", _bitmap_bytes, _alloc_pages);
  lib_printf("HEAP:   0x%x -> 0x%x\n", _alloc_start, _alloc_end);
#endif
}

void mem_usage_info(uint32_t *total_pages, uint32_t *used_pages, uint32_t *free_pages, uint32_t *m_calls, uint32_t *f_calls) {
  uint32_t total = _alloc_pages;
  uint32_t used = 0;
  uint32_t first_page = (_alloc_start - HEAP_START) / PAGE_SIZE;
  
  for (uint32_t i = 0; i < total; i++) {
    if (_page_is_used(first_page + i)) {
      used++;
    }
  }
  
  if (total_pages) *total_pages = total;
  if (used_pages) *used_pages = used;
  if (free_pages) *free_pages = total - used;
  if (m_calls) *m_calls = _malloc_calls;
  if (f_calls) *f_calls = _free_calls;
}

void kernel_heap_range_info(uint32_t *start, uint32_t *end) {
  if (start) *start = _alloc_start;
  if (end) *end = _alloc_end;
}

void *malloc(size_t size)
{
  _malloc_calls++;
  if (size == 0)
  {
    size = 1;
  }

  if ((uint32_t)size > 0xffffffffu - ALLOC_HEADER_SIZE)
  {
    return NULL;
  }

  uint32_t npages = _pages_for_size((uint32_t)size + ALLOC_HEADER_SIZE);
  if (npages == 0 || npages > _alloc_pages)
  {
    return NULL;
  }

  uint32_t irq_state = _irq_save();

  uint32_t first_page = (_alloc_start - HEAP_START) / PAGE_SIZE;
  uint32_t last_page = first_page + _alloc_pages;

  for (uint32_t page = first_page; page + npages <= last_page; page++)
  {
    int found = 1;
    for (uint32_t j = 0; j < npages; j++)
    {
      if (_page_is_used(page + j))
      {
        found = 0;
        page += j;
        break;
      }
    }

    if (!found)
    {
      continue;
    }

    for (uint32_t j = 0; j < npages; j++)
    {
      _page_set_used(page + j);
    }

    struct alloc_header *hdr = (struct alloc_header *)(HEAP_START + (page * PAGE_SIZE));
    hdr->magic = ALLOC_MAGIC;
    hdr->npages = npages;
    hdr->req_size = (uint32_t)size;
    hdr->reserved = 0;

    _irq_restore(irq_state);
    return (void *)((uint8_t *)hdr + ALLOC_HEADER_SIZE);
  }

  _irq_restore(irq_state);
  return NULL;
}

void free(void *p)
{
  _free_calls++;
  if (!p)
  {
    return;
  }

  uint32_t user_addr = (uint32_t)p;
  if (user_addr < HEAP_START + ALLOC_HEADER_SIZE || user_addr >= _alloc_end)
  {
    return;
  }

  struct alloc_header *hdr = (struct alloc_header *)((uint8_t *)p - ALLOC_HEADER_SIZE);
  uint32_t hdr_addr = (uint32_t)hdr;
  if (hdr->magic != ALLOC_MAGIC)
  {
    return;
  }

  if (hdr_addr < _alloc_start || hdr_addr >= _alloc_end)
  {
    return;
  }

  if (((hdr_addr - _alloc_start) & PAGE_MASK) != 0)
  {
    return;
  }

  uint32_t start_page = _page_index_from_addr(hdr_addr);
  uint32_t first_page = (_alloc_start - HEAP_START) / PAGE_SIZE;
  uint32_t last_page = first_page + _alloc_pages;

  if (start_page < first_page || start_page >= last_page)
  {
    return;
  }

  if (start_page + hdr->npages > last_page || hdr->npages == 0)
  {
    return;
  }

  uint32_t irq_state = _irq_save();

  for (uint32_t i = 0; i < hdr->npages; i++)
  {
    _page_clear_used(start_page + i);
  }

  hdr->magic = 0;
  hdr->npages = 0;
  hdr->req_size = 0;
  hdr->reserved = 0;

  _irq_restore(irq_state);
}

void page_test()
{
  void *p = malloc(1024);
  lib_printf("p = 0x%x\n", p);

  void *p2 = malloc(512);
  lib_printf("p2 = 0x%x\n", p2);

  void *p3 = malloc(sizeof(int));
  lib_printf("p3 = 0x%x\n", p3);

  free(p);
  free(p2);
  free(p3);
}
