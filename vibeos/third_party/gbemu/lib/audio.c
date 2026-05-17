#include <stdint.h>
/* STUBBED to prevent floating point crashes on rv32imac */
void audio_init_lib() {}
void audio_update_lib() {}
uint8_t audio_read_lib(uint16_t address) { return 0; }
void audio_write_lib(uint16_t address, uint8_t value) {}
