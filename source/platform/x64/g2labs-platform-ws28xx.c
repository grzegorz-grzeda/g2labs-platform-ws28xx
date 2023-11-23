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
#include <stdio.h>
#include <stdlib.h>

#define COLOR_COMPONENT_COUNT (3)
#define COLOR_RED_IN_BUFFER_OFFSET (2)
#define COLOR_GREEN_IN_BUFFER_OFFSET (0)
#define COLOR_BLUE_IN_BUFFER_OFFSET (1)

static uint8_t* led_buffer = NULL;
static size_t led_buffer_size = 0;
static size_t total_led_count = 0;

static void transmit_buffer_to_leds(void) {
    printf("WS28XX - transmitting buffer to LEDS\n");
}

void platform_ws28xx_initialize(uint8_t pin, size_t led_count) {
    led_buffer = calloc(led_count * COLOR_COMPONENT_COUNT, sizeof(uint8_t));
    if (!led_buffer) {
        return;
    }
    total_led_count = led_count;
    led_buffer_size = total_led_count * COLOR_COMPONENT_COUNT;
    transmit_buffer_to_leds();
}

static void set_buffer_led_value(size_t led_number, uint8_t red, uint8_t green, uint8_t blue) {
    printf("WS28XX - set LED [%lu] to color: %u, %u, %u\n", led_number, red, green, blue);
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
