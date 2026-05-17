#include "ac97_audio.h"
#include "gbemu_app.h"
#include "lib.h"
#include "string.h"
#include "types.h"

#define AC97_PCI_ECAM_BASE 0x30000000UL
#define AC97_PCI_PIO_WINDOW 0x03000000UL
#define AC97_BAR0_VAL 0xC000
#define AC97_BAR1_VAL 0xC100

#define AC97_RING_SIZE 32
#define AC97_FRAMES_PER_CHUNK 512
#define AC97_SAMPLES_PER_CHUNK (AC97_FRAMES_PER_CHUNK * 2)

typedef struct {
    uint32_t addr;
    uint16_t samples;
    uint16_t flags;
} ac97_bdl_entry_t;

typedef struct {
    int ready;
    uint8_t dev;
    uint32_t bar0, bar1;
    uint8_t last_cp;
    ac97_bdl_entry_t bdl[AC97_RING_SIZE] __attribute__((aligned(16)));
    int16_t pcm[AC97_RING_SIZE][AC97_SAMPLES_PER_CHUNK] __attribute__((aligned(16)));
} ac97_state_t;

static ac97_state_t ac97;

static inline uint32_t ac97_cfg_addr(uint8_t d, uint16_t o) { return AC97_PCI_ECAM_BASE | ((uint32_t)d << 15) | (o & 0x0ffc); }
static inline uint32_t ac97_cfg_read32(uint8_t d, uint16_t o) { return *(volatile uint32_t *)(uintptr_t)ac97_cfg_addr(d,o); }
static inline void ac97_cfg_write32(uint8_t d, uint16_t o, uint32_t v) { *(volatile uint32_t *)(uintptr_t)ac97_cfg_addr(d,o) = v; }
static inline void ac97_io_write16(uint32_t b, uint32_t o, uint16_t v) { *(volatile uint16_t *)(uintptr_t)(b + o) = v; }
static inline void ac97_io_write8(uint32_t b, uint32_t o, uint8_t v) { *(volatile uint8_t *)(uintptr_t)(b + o) = v; }
static inline uint8_t ac97_io_read8(uint32_t b, uint32_t o) { return *(volatile uint8_t *)(uintptr_t)(b + o); }

static void ac97_fill_chunk(uint8_t slot) {
    gbemu_audio_render_pcm_s16_stereo(ac97.pcm[slot], AC97_FRAMES_PER_CHUNK);
    ac97.bdl[slot].addr = (uint32_t)(uintptr_t)ac97.pcm[slot];
    ac97.bdl[slot].samples = AC97_SAMPLES_PER_CHUNK;
    ac97.bdl[slot].flags = 0x8000;
}
int ac97_audio_init(void) {
    if (ac97.ready) return 0;
    memset(&ac97, 0, sizeof(ac97));
    ac97.dev = 2; 

    ac97.bar0 = AC97_PCI_PIO_WINDOW + AC97_BAR0_VAL;
    ac97.bar1 = AC97_PCI_PIO_WINDOW + AC97_BAR1_VAL;

    ac97_cfg_write32(ac97.dev, 0x10, AC97_BAR0_VAL | 1);
    ac97_cfg_write32(ac97.dev, 0x14, AC97_BAR1_VAL | 1);
    ac97_cfg_write32(ac97.dev, 0x04, 0x05);

    *(volatile uint32_t *)(uintptr_t)(ac97.bar1 + 0x2c) = 2; lib_delay(5);
    ac97_io_write16(ac97.bar0, 0x02, 0x0808); 
    ac97_io_write16(ac97.bar0, 0x18, 0x0808);

    // 修正：不要填滿 32 格，只預先填 4 格就好
    for (int i=0; i<4; i++) ac97_fill_chunk(i);
    ac97.last_cp = 4; // 寫入指針停在第 4 格

    *(volatile uint32_t *)(uintptr_t)(ac97.bar1 + 0x10) = (uint32_t)(uintptr_t)ac97.bdl;
    ac97_io_write8(ac97.bar1, 0x15, 3); // 修正：LVI 設定在第 3 格 (0, 1, 2, 3)
    ac97_io_write8(ac97.bar1, 0x1b, 1);  

    ac97.ready = 1;
    return 0;
}
int ac97_audio_pump(void) {
    if (!ac97.ready) return 1;
    uint8_t civ = ac97_io_read8(ac97.bar1, 0x14) & 0x1F;
    
    // 盡可能填滿，但不等待，這能讓 FPS 衝到最高
    while (ac97.last_cp != civ) {
        ac97_fill_chunk(ac97.last_cp);
        ac97_io_write8(ac97.bar1, 0x15, ac97.last_cp); 
        ac97.last_cp = (ac97.last_cp + 1) & 31;
    }
    return 1;
}
