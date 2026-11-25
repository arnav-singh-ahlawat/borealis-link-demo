#include "demo_port.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>

static const struct device *const serial = DEVICE_DT_GET(DT_CHOSEN(borealis_telemetry_uart));

int demo_serial_init(void)
{
    return device_is_ready(serial) ? 0 : -ENODEV;
}

void demo_serial_write(const char *bytes, size_t count, void *context)
{
    (void)context;
    for (size_t i = 0; i < count; ++i) {
        uart_poll_out(serial, (unsigned char)bytes[i]);
    }
}
