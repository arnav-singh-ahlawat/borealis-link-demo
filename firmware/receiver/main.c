#include "demo_port.h"
#include "demo_protocol.h"
#include "demo_telemetry.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

struct radio_datagram {
    uint32_t sequence;
    uint32_t ms;
    size_t length;
    int16_t rssi;
    int8_t snr;
    uint8_t bytes[DEMO_RADIO_CAPACITY];
};

K_MSGQ_DEFINE(incoming, sizeof(struct radio_datagram), 8, 4);
static atomic_t dropped;
static atomic_t arrival_count;
static struct demo_rs rs;
static struct demo_assembler assembler;
static const struct demo_writer telemetry = { .write = demo_serial_write };

static void receive_radio(const uint8_t *wire, size_t length, int16_t rssi, int8_t snr)
{
    if (length > DEMO_RADIO_CAPACITY) {
        atomic_inc(&dropped);
        return;
    }
    struct radio_datagram datagram = {
        .sequence = (uint32_t)atomic_inc(&arrival_count) + 1,
        .ms = k_uptime_get_32(),
        .length = length,
        .rssi = rssi,
        .snr = snr,
    };
    memcpy(datagram.bytes, wire, length);
    if (k_msgq_put(&incoming, &datagram, K_NO_WAIT) != 0) {
        atomic_inc(&dropped);
    }
}

int main(void)
{
    int error = demo_serial_init();
    if (error) {
        return error;
    }
    demo_rs_init(&rs);
    error = demo_radio_configure(false);
    if (!error) {
        error = demo_radio_listen(receive_radio);
    }
    if (error) {
        for (;;) {
            demo_report_status(&telemetry, "init_error", error);
            k_sleep(K_SECONDS(2));
        }
    }

    demo_report_status(&telemetry, "listening", CONFIG_BOREALIS_FREQUENCY_HZ);
    for (;;) {
        struct radio_datagram datagram;
        struct demo_fragment fragment;
        struct demo_frame frame;
        if (k_msgq_get(&incoming, &datagram, K_SECONDS(2)) == 0) {
            demo_report_radio(&telemetry, datagram.sequence, datagram.ms, datagram.rssi,
                              datagram.snr, datagram.bytes, datagram.length);
            if (demo_parse_datagram(datagram.bytes, datagram.length, &fragment) &&
                demo_assemble(&assembler, &rs, &fragment, &frame)) {
                demo_report_frame(&telemetry, datagram.sequence, &frame);
            }
        } else {
            demo_report_status(&telemetry, "listening", CONFIG_BOREALIS_FREQUENCY_HZ);
        }
        atomic_val_t lost = atomic_set(&dropped, 0);
        if (lost) {
            demo_report_status(&telemetry, "queue_overrun", (int32_t)lost);
        }
    }
}
