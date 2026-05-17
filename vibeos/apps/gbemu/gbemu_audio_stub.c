#include "user_internal.h"
#include <string.h>
#include <stdint.h>  // 新增：確保標準整數型態可用

/* * GameBoy Audio Engine - OPTIMIZED VERSION
 * Includes: Switch-based IO mapping, Dropping Test Sound, Fixed-Point Math,
 * and pre-calculated frequency/volume caching.
 */

#define SAMPLE_RATE 48000
#define MAX_VOLUME 8192
#define P_SHIFT 20
#define P_ONE (1 << P_SHIFT)
#define P_MASK (P_ONE - 1)

typedef struct {
    uint8_t NR10, NR11, NR12, NR13, NR14;
    uint8_t NR21, NR22, NR23, NR24;
    uint8_t NR30, NR31, NR32, NR33, NR34;
    uint8_t NR41, NR42, NR43, NR44;
    uint8_t NR50, NR51, NR52;
    uint8_t wave_ram[16];
} AudioRegisters;

static AudioRegisters audio_regs;
static int audio_ready = 0;
static int32_t test_sound_counter = 0; 
static uint32_t test_phase = 0;

static uint32_t phase_ch1 = 0, phase_ch2 = 0, phase_ch3 = 0;
static uint16_t lfsr = 0x7FFF;
static int32_t ch4_timer_fp = 0;
static int16_t ch4_last_sample = 0;

// --- 新增：快取變數，避免在迴圈內做除法 ---
static uint32_t step_ch1 = 0, step_ch2 = 0;
static int32_t vol_ch1 = 0, vol_ch2 = 0;

static const int16_t waveform_ch3[32] = {
    0, 1, 0, -1, 0, 1, 0, -1, 1, 1, 1, -1, -1, -1, -1, 1,
    0, 0, 0, 0, 1, 1, 1, -1, -1, -1, -1, -1, 0, 0, 0, 0
};

// --- 新增：輔助函式用於更新快取 ---
static void update_ch1_cache() {
    uint16_t f1 = audio_regs.NR13 | ((audio_regs.NR14 & 7) << 8);
    step_ch1 = (f1 < 2048) ? (uint32_t)(((uint64_t)(131072 / (2048 - f1)) << P_SHIFT) / SAMPLE_RATE) : 0;
    vol_ch1 = ((audio_regs.NR12 >> 4) * MAX_VOLUME) / 15;
}

static void update_ch2_cache() {
    uint16_t f2 = audio_regs.NR23 | ((audio_regs.NR24 & 7) << 8);
    step_ch2 = (f2 < 2048) ? (uint32_t)(((uint64_t)(131072 / (2048 - f2)) << P_SHIFT) / SAMPLE_RATE) : 0;
    vol_ch2 = ((audio_regs.NR22 >> 4) * MAX_VOLUME) / 15;
}

uint8_t audio_read(uint16_t address) {
    // 快速排除非音訊區段，減少 switch 的開銷
    if (address < 0xFF10 || address > 0xFF3F) return 0xFF;

    switch (address) {
        case 0xFF10: return audio_regs.NR10; case 0xFF11: return audio_regs.NR11;
        case 0xFF12: return audio_regs.NR12; case 0xFF13: return audio_regs.NR13;
        case 0xFF14: return audio_regs.NR14;
        case 0xFF16: return audio_regs.NR21; case 0xFF17: return audio_regs.NR22;
        case 0xFF18: return audio_regs.NR23; case 0xFF19: return audio_regs.NR24;
        case 0xFF1A: return audio_regs.NR30; case 0xFF1B: return audio_regs.NR31;
        case 0xFF1C: return audio_regs.NR32; case 0xFF1D: return audio_regs.NR33;
        case 0xFF1E: return audio_regs.NR34;
        case 0xFF20: return audio_regs.NR41; case 0xFF21: return audio_regs.NR42;
        case 0xFF22: return audio_regs.NR43;
        case 0xFF23: return (audio_regs.NR44 & 0xC0) | (audio_regs.NR41 & 0x3F);
        case 0xFF24: return audio_regs.NR50; case 0xFF25: return audio_regs.NR51;
        case 0xFF26: return audio_regs.NR52;
        default:
            if (address >= 0xFF30 && address <= 0xFF3F) return audio_regs.wave_ram[address - 0xFF30];
            return 0xFF;
    }
}

void audio_write(uint16_t address, uint8_t value) {
    if (address < 0xFF10 || address > 0xFF3F) return;

    switch (address) {
        // Channel 1
        case 0xFF10: audio_regs.NR10 = value; break;
        case 0xFF11: audio_regs.NR11 = value; break;
        case 0xFF12: audio_regs.NR12 = value; update_ch1_cache(); break;
        case 0xFF13: audio_regs.NR13 = value; update_ch1_cache(); break;
        case 0xFF14: 
            audio_regs.NR14 = value; 
            update_ch1_cache(); 
            if (value & 0x80) phase_ch1 = 0; 
            break;
            
        // Channel 2
        case 0xFF16: audio_regs.NR21 = value; break;
        case 0xFF17: audio_regs.NR22 = value; update_ch2_cache(); break;
        case 0xFF18: audio_regs.NR23 = value; update_ch2_cache(); break;
        case 0xFF19: 
            audio_regs.NR24 = value; 
            update_ch2_cache(); 
            if (value & 0x80) phase_ch2 = 0; 
            break;

        // Channel 3
        case 0xFF1A: audio_regs.NR30 = value; break;
        case 0xFF1B: audio_regs.NR31 = value; break;
        case 0xFF1C: audio_regs.NR32 = value; break;
        case 0xFF1D: audio_regs.NR33 = value; break;
        case 0xFF1E: audio_regs.NR34 = value; if (value & 0x80) phase_ch3 = 0; break;
        
        // Channel 4
        case 0xFF20: audio_regs.NR41 = value; break;
        case 0xFF21: audio_regs.NR42 = value; break;
        case 0xFF22: audio_regs.NR43 = value; break;
        case 0xFF23:
            audio_regs.NR44 = value;
            if (value & 0x80) {
                lfsr = (audio_regs.NR43 & 0x08) ? 0x007F : 0x7FFF;
                ch4_timer_fp = 0;
            }
            break;
            
        // Control & Wave RAM
        case 0xFF26: audio_regs.NR52 = (value & 0x80) | (audio_regs.NR52 & 0x7F); break;
        default:
            if (address >= 0xFF30 && address <= 0xFF3F) audio_regs.wave_ram[address - 0xFF30] = value;
            break;
    }
}

int audio_init(void) {
    memset(&audio_regs, 0, sizeof(audio_regs));
    audio_regs.NR52 = 0x80;
    
    // 初始化快取
    update_ch1_cache();
    update_ch2_cache();
    
    test_sound_counter = 0; // 關閉測試音
    audio_ready = 1;
    return 0;
}

void audio_update(void) {}
void gamepad_init(void) {}

void gbemu_audio_render_pcm_s16_stereo(int16_t *out, int frames) {
    if (!out || !audio_ready) return;

    for (int i = 0; i < frames; i++) {
        int32_t mixed = 0;

        if (test_sound_counter > 0) {
            /* Play Dropping Sound for 2 seconds */
            uint32_t freq = 100 + (test_sound_counter / 10);
            test_phase += (freq << 16) / SAMPLE_RATE;
            mixed = ((test_phase >> 15) & 1) ? 3000 : -3000;
            test_sound_counter--;
        } else if (audio_regs.NR52 & 0x80) {
            /* Render actual GB Audio Tracks (使用預先計算的快取變數) */
            
            // Channel 1
            if (vol_ch1 && (audio_regs.NR14 & 0x80)) {
                phase_ch1 = (phase_ch1 + step_ch1) & P_MASK;
                mixed += (phase_ch1 < (P_ONE >> 1)) ? vol_ch1 : -vol_ch1;
            }

            // Channel 2
            if (vol_ch2 && (audio_regs.NR24 & 0x80)) {
                phase_ch2 = (phase_ch2 + step_ch2) & P_MASK;
                mixed += (phase_ch2 < (P_ONE >> 1)) ? vol_ch2 : -vol_ch2;
            }
        }

        // Fast clamping (避免分支預測失敗)
        if (mixed > 32767) mixed = 32767;
        else if (mixed < -32768) mixed = -32768;
        
        out[i * 2 + 0] = (int16_t)mixed;
        out[i * 2 + 1] = (int16_t)mixed;
    }
}