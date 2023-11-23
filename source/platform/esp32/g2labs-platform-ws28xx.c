/*
 * MIT License
 *
 * Copyright (c) 2023 Grzegorz Grzęda
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "g2labs-platform-ws28xx.h"
#include <stddef.h>
#include <stdint.h>
#include "driver/rmt_tx.h"

#define RMT_LED_STRIP_RESOLUTION_HZ (10000000)
#define RMT_BLOCK_SIZE (64)
#define RMT_CLOCK_SOURCE (RMT_CLK_SRC_DEFAULT)
#define RMT_MAX_PENDING_TRANSACTIONS_COUNT (4)
#define RMT_LED_ENDIANESS (1)
#define RMT_STATE_SEND_DATA (0)
#define RMT_STATE_SEND_RESET (1)
#define T0L_LEVEL (1)
#define T0L_TIME_US ((0.3) * RMT_LED_STRIP_RESOLUTION_HZ / 1000000)
#define T0H_LEVEL (0)
#define T0H_TIME_US ((0.9) * RMT_LED_STRIP_RESOLUTION_HZ / 1000000)
#define T1L_LEVEL (1)
#define T1L_TIME_US ((0.9) * RMT_LED_STRIP_RESOLUTION_HZ / 1000000)
#define T1H_LEVEL (0)
#define T1H_TIME_US ((0.3) * RMT_LED_STRIP_RESOLUTION_HZ / 1000000)
#define TRL_LEVEL (0)
#define TRL_TIME_US (50 / 2 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000)
#define TRH_LEVEL (0)
#define TRH_TIME_US (50 / 2 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000)
#define COLOR_COMPONENT_COUNT (3)
#define COLOR_RED_IN_BUFFER_OFFSET (1)
#define COLOR_GREEN_IN_BUFFER_OFFSET (0)
#define COLOR_BLUE_IN_BUFFER_OFFSET (2)

static rmt_channel_handle_t led_chan = NULL;
static rmt_encoder_handle_t led_encoder = NULL;
static uint8_t* led_buffer = NULL;
static size_t led_buffer_size = 0;
static size_t total_led_count = 0;

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t* bytes_encoder;
    rmt_encoder_t* copy_encoder;
    int state;
    rmt_symbol_word_t reset_code;
} rmt_led_strip_encoder_t;

static size_t rmt_encode_led_strip(rmt_encoder_t* encoder,
                                   rmt_channel_handle_t channel,
                                   const void* primary_data,
                                   size_t data_size,
                                   rmt_encode_state_t* ret_state) {
    rmt_led_strip_encoder_t* led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_handle_t bytes_encoder = led_encoder->bytes_encoder;
    rmt_encoder_handle_t copy_encoder = led_encoder->copy_encoder;
    rmt_encode_state_t session_state = 0;
    rmt_encode_state_t state = 0;
    size_t encoded_symbols = 0;
    switch (led_encoder->state) {
        case RMT_STATE_SEND_DATA:
            encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data, data_size, &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                led_encoder->state = RMT_STATE_SEND_RESET;
            }
            if (session_state & RMT_ENCODING_MEM_FULL) {
                state |= RMT_ENCODING_MEM_FULL;
                goto out;
            }
        // fall-through
        case RMT_STATE_SEND_RESET:
            encoded_symbols += copy_encoder->encode(copy_encoder, channel, &led_encoder->reset_code,
                                                    sizeof(led_encoder->reset_code), &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                led_encoder->state = RMT_STATE_SEND_DATA;
                state |= RMT_ENCODING_COMPLETE;
            }
            if (session_state & RMT_ENCODING_MEM_FULL) {
                state |= RMT_ENCODING_MEM_FULL;
                goto out;
            }
    }
out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t rmt_del_led_strip_encoder(rmt_encoder_t* encoder) {
    rmt_led_strip_encoder_t* led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_del_encoder(led_encoder->bytes_encoder);
    rmt_del_encoder(led_encoder->copy_encoder);
    free(led_encoder);
    return ESP_OK;
}

static esp_err_t rmt_led_strip_encoder_reset(rmt_encoder_t* encoder) {
    rmt_led_strip_encoder_t* led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_reset(led_encoder->bytes_encoder);
    rmt_encoder_reset(led_encoder->copy_encoder);
    led_encoder->state = 0;
    return ESP_OK;
}

static esp_err_t rmt_new_led_strip_encoder(rmt_encoder_handle_t* ret_encoder) {
    if (!ret_encoder) {
        return ESP_FAIL;
    }
    rmt_led_strip_encoder_t* led_encoder = calloc(1, sizeof(rmt_led_strip_encoder_t));
    if (!led_encoder) {
        return ESP_FAIL;
    }
    led_encoder->base.encode = rmt_encode_led_strip;
    led_encoder->base.del = rmt_del_led_strip_encoder;
    led_encoder->base.reset = rmt_led_strip_encoder_reset;
    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 =
            {
                .level0 = T0L_LEVEL,
                .duration0 = T0L_TIME_US,
                .level1 = T0H_LEVEL,
                .duration1 = T0H_TIME_US,
            },
        .bit1 =
            {
                .level0 = T1L_LEVEL,
                .duration0 = T1L_TIME_US,
                .level1 = T1H_LEVEL,
                .duration1 = T1H_TIME_US,
            },
        .flags.msb_first = RMT_LED_ENDIANESS,
    };
    if (rmt_new_bytes_encoder(&bytes_encoder_config, &led_encoder->bytes_encoder) != ESP_OK) {
        goto err;
    }
    rmt_copy_encoder_config_t copy_encoder_config = {};
    if (rmt_new_copy_encoder(&copy_encoder_config, &led_encoder->copy_encoder)) {
        goto err;
    }
    led_encoder->reset_code = (rmt_symbol_word_t){
        .level0 = TRL_LEVEL,
        .duration0 = TRL_TIME_US,
        .level1 = TRH_LEVEL,
        .duration1 = TRH_TIME_US,
    };
    *ret_encoder = &led_encoder->base;
    return ESP_OK;
err:
    if (led_encoder) {
        if (led_encoder->bytes_encoder) {
            rmt_del_encoder(led_encoder->bytes_encoder);
        }
        if (led_encoder->copy_encoder) {
            rmt_del_encoder(led_encoder->copy_encoder);
        }
        free(led_encoder);
    }
    return ESP_FAIL;
}

static void transmit_buffer_to_leds(void) {
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,  // no transfer loop
    };
    rmt_transmit(led_chan, led_encoder, led_buffer, led_buffer_size, &tx_config);
}

void platform_ws28xx_initialize(uint8_t pin, size_t led_count) {
    led_buffer = calloc(led_count * COLOR_COMPONENT_COUNT, sizeof(uint8_t));
    if (!led_buffer) {
        return;
    }
    total_led_count = led_count;
    led_buffer_size = total_led_count * COLOR_COMPONENT_COUNT;

    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLOCK_SOURCE,
        .gpio_num = pin,
        .mem_block_symbols = RMT_BLOCK_SIZE,
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = RMT_MAX_PENDING_TRANSACTIONS_COUNT,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&led_encoder));
    ESP_ERROR_CHECK(rmt_enable(led_chan));
    transmit_buffer_to_leds();
}

static void set_buffer_led_value(size_t led_number, uint8_t red, uint8_t green, uint8_t blue) {
    size_t index = led_number * COLOR_COMPONENT_COUNT;
    led_buffer[index + COLOR_RED_IN_BUFFER_OFFSET] = red;
    led_buffer[index + COLOR_GREEN_IN_BUFFER_OFFSET] = green;
    led_buffer[index + COLOR_BLUE_IN_BUFFER_OFFSET] = blue;
}

void platform_ws28xx_set_led_color(size_t led_number, uint8_t red, uint8_t green, uint8_t blue) {
    if (led_number >= total_led_count) {
        return;
    }
    set_buffer_led_value(led_number, red, green, blue);
    transmit_buffer_to_leds();
}

void platform_ws28xx_set_every_led_color(uint8_t red, uint8_t green, uint8_t blue) {
    for (size_t i = 0; i < total_led_count; i++) {
        set_buffer_led_value(i, red, green, blue);
    }
    transmit_buffer_to_leds();
}
