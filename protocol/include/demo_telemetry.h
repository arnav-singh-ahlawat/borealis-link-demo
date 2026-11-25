#ifndef BOREALIS_DEMO_TELEMETRY_H
#define BOREALIS_DEMO_TELEMETRY_H

#include "demo_protocol.h"

typedef void (*demo_write_fn)(const char *bytes, size_t count, void *context);

struct demo_writer {
    demo_write_fn write;
    void *context;
};

void demo_report_radio(const struct demo_writer *out, uint32_t sequence, uint32_t ms,
                       int16_t rssi, int8_t snr, const uint8_t *wire, size_t length);
void demo_report_frame(const struct demo_writer *out, uint32_t sequence,
                       const struct demo_frame *frame);
void demo_report_status(const struct demo_writer *out, const char *state, int32_t value);

#endif
