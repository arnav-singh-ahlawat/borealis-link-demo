#include "demo_telemetry.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void json(const struct demo_writer *out, const char *text)
{
    out->write(text, strlen(text), out->context);
}

static void fields(const struct demo_writer *out, const char *format, ...)
{
    char line[224];
    va_list args;
    va_start(args, format);
    int size = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (size > 0 && (size_t)size < sizeof(line)) {
        out->write(line, (size_t)size, out->context);
    }
}

static void hex(const struct demo_writer *out, const uint8_t *bytes, size_t count)
{
    static const char alphabet[] = "0123456789abcdef";
    char text[64];
    for (size_t offset = 0; offset < count; ) {
        size_t length = count - offset;
        if (length > sizeof(text) / 2) {
            length = sizeof(text) / 2;
        }
        for (size_t i = 0; i < length; ++i) {
            uint8_t byte = bytes[offset + i];
            text[2 * i] = alphabet[byte >> 4];
            text[2 * i + 1] = alphabet[byte & 15u];
        }
        out->write(text, 2 * length, out->context);
        offset += length;
    }
}

void demo_report_radio(const struct demo_writer *out, uint32_t sequence, uint32_t ms,
                       int16_t rssi, int8_t snr, const uint8_t *wire, size_t length)
{
    fields(out, "{\"kind\":\"radio\",\"seq\":%" PRIu32 ",\"ms\":%" PRIu32
           ",\"rssi\":%d,\"snr\":%d,\"wire\":\"", sequence, ms, rssi, snr);
    hex(out, wire, length);
    json(out, "\"}\n");
}

void demo_report_frame(const struct demo_writer *out, uint32_t sequence,
                       const struct demo_frame *frame)
{
    const char *status = "ok";
    if (frame->status == DEMO_FRAME_UNCORRECTABLE) {
        status = "uncorrectable";
    } else if (frame->status == DEMO_FRAME_METADATA) {
        status = "metadata_mismatch";
    }
    fields(out, "{\"kind\":\"frame\",\"seq\":%" PRIu32 ",\"transfer\":%" PRIu32
           ",\"image\":%u,\"frame\":%u,\"total\":%u,\"invalid_pairs\":%u,"
           "\"corrected_symbols\":%d,\"status\":\"%s\",\"decoded\":\"",
           sequence, frame->key.transfer, (unsigned)frame->key.image,
           (unsigned)frame->key.frame, (unsigned)frame->key.total,
           (unsigned)frame->invalid_pairs, frame->corrected_symbols, status);
    hex(out, frame->decoded, DEMO_SSDV_BYTES);
    if (frame->status == DEMO_FRAME_OK) {
        json(out, "\",\"ssdv\":\"");
        hex(out, frame->ssdv, DEMO_SSDV_BYTES);
    }
    json(out, "\"}\n");
}

/* state is an application-defined token, with no user-supplied text. */
void demo_report_status(const struct demo_writer *out, const char *state, int32_t value)
{
    fields(out, "{\"kind\":\"status\",\"state\":\"%s\",\"value\":%" PRId32 "}\n",
           state, value);
}
