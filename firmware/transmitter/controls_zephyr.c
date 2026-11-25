#include "demo_port.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>

#define BUTTON DT_ALIAS(sw0)
#define BUTTONS DT_PARENT(BUTTON)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(demo_led), gpios);

static void button_changed(struct input_event *event, void *context)
{
    (void)context;
    if (event->type == INPUT_EV_KEY && event->code == DT_PROP(BUTTON, zephyr_code) &&
        event->value == 1) {
        demo_button_pressed();
    }
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(BUTTONS), button_changed, NULL);

int demo_controls_init(void)
{
    if (!gpio_is_ready_dt(&led) || !device_is_ready(DEVICE_DT_GET(BUTTONS))) {
        return -ENODEV;
    }
    return gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
}

void demo_led_set(bool on)
{
    gpio_pin_set_dt(&led, on);
}
