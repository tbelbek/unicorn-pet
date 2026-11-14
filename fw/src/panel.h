#pragma once
#include <stdint.h>
// Pixels to the AMOLED over QSPI with the byte swap and DMA overlapped (see panel.cpp). The Arduino_GFX
// bus must be built as a shared interface, so it releases SPI2 between its own writes.
bool panel_fast_begin(int cs, uint32_t hz);   // after lcd->begin(); false: keep using the library's path
bool panel_fast_ok();
void panel_fast_send(const uint16_t* px, uint32_t n);   // after the window and RAMWR are set; n even

// Band by band: draw into panel_band(k) (BAND_PX pixels, internal DMA RAM), swap it to the panel's byte
// order, set the window, then send; it goes out while the next band is drawn into the other buffer.
static const int PANEL_BAND_PX = 8192;
uint16_t* panel_band(int i);
void panel_band_send(const uint16_t* px, uint32_t n);   // n <= PANEL_BAND_PX, window and RAMWR already set
void panel_band_wait();                                 // the band on the wire is done (CS up, bus free)
