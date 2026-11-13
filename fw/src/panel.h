#pragma once
#include <stdint.h>
// Pixels to the AMOLED over QSPI with the byte swap and DMA overlapped (see panel.cpp). The Arduino_GFX
// bus must be built as a shared interface, so it releases SPI2 between its own writes.
bool panel_fast_begin(int cs, uint32_t hz);   // after lcd->begin(); false: keep using the library's path
bool panel_fast_ok();
void panel_fast_send(const uint16_t* px, uint32_t n);   // after the window and RAMWR are set; n even
