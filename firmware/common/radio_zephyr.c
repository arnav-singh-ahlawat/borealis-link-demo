#include "demo_port.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/lora.h>

static const struct device *const radio = DEVICE_DT_GET(DT_ALIAS(lora0));
static demo_radio_handler receive_handler;

int demo_radio_configure(bool tx)
{
    if (!device_is_ready(radio)) {
        return -ENODEV;
    }
    struct lora_modem_config settings = {
        .frequency = CONFIG_BOREALIS_FREQUENCY_HZ,
        .bandwidth = BW_500_KHZ,
        .datarate = SF_7,
        .coding_rate = CR_4_5,
        .preamble_len = 8,
        .tx_power = CONFIG_BOREALIS_TX_POWER_DBM,
        .tx = tx,
        .iq_inverted = false,
        .public_network = false,
    };
    return lora_config(radio, &settings);
}

int demo_radio_send(uint8_t *wire, size_t length)
{
    return lora_send(radio, wire, length);
}

static void radio_received(const struct device *dev, uint8_t *wire, uint16_t length,
                           int16_t rssi, int8_t snr, void *context)
{
    (void)dev;
    (void)context;
    receive_handler(wire, length, rssi, snr);
}

int demo_radio_listen(demo_radio_handler handler)
{
    receive_handler = handler;
    return lora_recv_async(radio, radio_received, NULL);
}
