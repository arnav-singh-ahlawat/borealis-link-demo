#ifndef BOREALIS_DEMO_PORT_H
#define BOREALIS_DEMO_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Zephyr radio data is valid until this callback returns. Copy it before queuing. */
typedef void (*demo_radio_handler)(const uint8_t *wire, size_t length, int16_t rssi, int8_t snr);

int demo_radio_configure(bool tx);
int demo_radio_send(uint8_t *wire, size_t length);
int demo_radio_listen(demo_radio_handler handler);

/* The button callback runs on the Zephyr system work queue after GPIO debounce. */
int demo_controls_init(void);
void demo_button_pressed(void);
void demo_led_set(bool on);

int demo_serial_init(void);
void demo_serial_write(const char *bytes, size_t count, void *context);

#endif
