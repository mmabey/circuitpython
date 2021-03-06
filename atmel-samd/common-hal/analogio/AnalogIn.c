/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Scott Shawcroft for Adafruit Industries
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
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "common-hal/analogio/AnalogIn.h"

#include <string.h>

#include "py/gc.h"
#include "py/nlr.h"
#include "py/runtime.h"
#include "py/binary.h"
#include "py/mphal.h"
#include "shared-bindings/analogio/AnalogIn.h"

#include "asf/sam0/drivers/adc/adc.h"
#include "samd21_pins.h"

// Number of active ADC channels.
volatile uint8_t active_channel_count;

// Shared between all the instances. Allocated only when needed.
struct adc_module *adc_instance = NULL;
struct adc_config *config_adc = NULL;

void common_hal_analogio_analogin_construct(analogio_analogin_obj_t* self,
        const mcu_pin_obj_t *pin) {
    if (!pin->has_adc) {
        // No ADC function on that pin
        mp_raise_ValueError("Pin does not have ADC capabilities");
    }
    claim_pin(pin);

    self->pin = pin;

    if (adc_instance == NULL) {
        // Allocate strucs on the heap so we only use the memory when we
        // need it.
        adc_instance = gc_alloc(sizeof(struct adc_module), false);
        config_adc = gc_alloc(sizeof(struct adc_config), false);

        adc_get_config_defaults(config_adc);

        config_adc->reference = ADC_REFERENCE_INTVCC1;
        config_adc->gain_factor = ADC_GAIN_FACTOR_DIV2;
        config_adc->positive_input = self->pin->adc_input;
        config_adc->resolution = ADC_RESOLUTION_16BIT;
        config_adc->clock_prescaler = ADC_CLOCK_PRESCALER_DIV128;

        adc_init(adc_instance, ADC, config_adc);
    }

    self->adc_instance = adc_instance;
    self->config_adc = config_adc;
    active_channel_count++;
}

void common_hal_analogio_analogin_deinit(analogio_analogin_obj_t *self) {
    active_channel_count--;
    if (active_channel_count == 0) {
        adc_reset(adc_instance);
        gc_free(adc_instance);
        gc_free(config_adc);
        // Set our references to NULL so the GC doesn't mistakenly see the
        // pointers in memory.
        adc_instance = NULL;
        config_adc = NULL;
    }
    reset_pin(self->pin->pin);
}

void analogin_reset() {
    if (adc_instance != NULL) {
        adc_reset(adc_instance);
        adc_instance = NULL;
    }
    active_channel_count = 0;
}

uint16_t common_hal_analogio_analogin_get_value(analogio_analogin_obj_t *self) {
    // Something else might have used the ADC in a different way,
    // so we have to completely re-initialize it.
    // ADC must have been disabled before adc_init() is called.
    adc_init(adc_instance, ADC, config_adc);
    config_adc->positive_input = self->pin->adc_input;

    adc_enable(adc_instance);

    // Read twice and discard first result, as recommended in section 14 of
    // http://www.atmel.com/images/Atmel-42645-ADC-Configurations-with-Examples_ApplicationNote_AT11481.pdf
    // "Discard the first conversion result whenever there is a change in ADC configuration
    // like voltage reference / ADC channel change"
    // Empirical observation shows the first reading is quite different than subsequent ones.

    uint16_t data;
    enum status_code status;

    adc_start_conversion(adc_instance);
    do {
      status = adc_read(adc_instance, &data);
    }  while (status == STATUS_BUSY); 
    if (status == STATUS_ERR_OVERFLOW) {
      // TODO(tannewt): Throw an error.
    }

    adc_start_conversion(adc_instance);
    do {
        status = adc_read(adc_instance, &data);
    } while (status == STATUS_BUSY);
    if (status == STATUS_ERR_OVERFLOW) {
      // TODO(tannewt): Throw an error.
    }

    adc_disable(adc_instance);
    return data;
}

float common_hal_analogio_analogin_get_reference_voltage(analogio_analogin_obj_t *self) {
    return 3.3f;
}
