// A faster pixel stream to the AMOLED than Arduino_GFX's writePixels(), which byte-swaps 1024 pixels at a
// time and then waits for each transfer (~44 ms a full frame at 40 MHz, measured). Here the swap is done a
// 32-bit word at a time into one of two DMA buffers while the other is on the wire, so the CPU and the
// QSPI bus overlap. Same protocol as the library: after its window and RAMWR, the pixels go out as quad
// writes (0x32, address 0x003C00 = "memory write continue") inside one CS-low stretch.
#pragma GCC optimize("O2")   // the swap loop runs over every pixel sent
#include "panel.h"
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <string.h>

static const int CHUNK_PX = 8192;              // 16 KB: under the bus's max_transfer_sz (Arduino_ESP32QSPI: 16392)
static spi_device_handle_t dev;
static uint32_t* buf[2];
static int cs_pin = -1;

bool panel_fast_begin(int cs, uint32_t hz) {
    spi_device_interface_config_t cfg = {};
    cfg.command_bits = 8;
    cfg.address_bits = 24;
    cfg.mode = 0;
    cfg.clock_source = SPI_CLK_SRC_DEFAULT;
    cfg.clock_speed_hz = (int)hz;
    cfg.spics_io_num = -1;                     // CS by hand: one CS-low stretch spans many transactions
    cfg.flags = SPI_DEVICE_HALFDUPLEX;
    cfg.queue_size = 2;
    buf[0] = (uint32_t*)heap_caps_malloc(CHUNK_PX * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    buf[1] = (uint32_t*)heap_caps_malloc(CHUNK_PX * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf[0] || !buf[1] || spi_bus_add_device(SPI2_HOST, &cfg, &dev) != ESP_OK) {
        free(buf[0]); free(buf[1]);
        buf[0] = buf[1] = nullptr;
        return false;
    }
    cs_pin = cs;
    return true;
}

bool panel_fast_ok() { return dev != nullptr; }

// little-endian RGB565 pairs -> the panel's big-endian, two pixels per word
static void swap_into(uint32_t* dst, const uint32_t* src, int words) {
    for (int i = 0; i < words; i++) {
        uint32_t w = src[i];
        dst[i] = ((w & 0x00FF00FFu) << 8) | ((w >> 8) & 0x00FF00FFu);
    }
}

void panel_fast_send(const uint16_t* px, uint32_t n) {
    static spi_transaction_ext_t t[2];
    spi_device_acquire_bus(dev, portMAX_DELAY);
    gpio_set_level((gpio_num_t)cs_pin, 0);
    const uint32_t* src = (const uint32_t*)px;     // n is even: whole rows of an even width
    int k = 0, queued = 0;
    bool first = true;
    while (n) {
        uint32_t l = n > (uint32_t)CHUNK_PX ? CHUNK_PX : n;
        if (queued == 2) {                         // both in flight: the older one holds buf[k]
            spi_transaction_t* done;
            spi_device_get_trans_result(dev, &done, portMAX_DELAY);
            queued--;
        }
        swap_into(buf[k], src, l / 2);             // fills while the other buffer is being sent
        spi_transaction_ext_t& x = t[k];
        memset(&x, 0, sizeof x);
        if (first) {
            x.base.flags = SPI_TRANS_MODE_QIO;
            x.base.cmd = 0x32;
            x.base.addr = 0x003C00;
            first = false;
        } else {                                   // a continuation: no command, no address
            x.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR |
                           SPI_TRANS_VARIABLE_DUMMY;
        }
        x.base.tx_buffer = buf[k];
        x.base.length = l * 16;
        spi_device_queue_trans(dev, &x.base, portMAX_DELAY);
        queued++;
        src += l / 2;
        n -= l;
        k ^= 1;
    }
    while (queued--) {
        spi_transaction_t* done;
        spi_device_get_trans_result(dev, &done, portMAX_DELAY);
    }
    gpio_set_level((gpio_num_t)cs_pin, 1);
    spi_device_release_bus(dev);
}
