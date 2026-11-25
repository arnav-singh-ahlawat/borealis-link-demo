#include "demo_port.h"
#include "images.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(borealis_tx, LOG_LEVEL_INF);
K_MSGQ_DEFINE(selections, sizeof(uint8_t), 1, 1);

/* The input callback and delayable work share the system work queue in
 * synchronous GPIO-keys input mode.
 */
static uint8_t presses;
static atomic_t accepting;

static void finish_selection(struct k_work *work)
{
    (void)work;
    uint8_t choice = presses;
    presses = 0;
    if (choice && atomic_cas(&accepting, 1, 0)) {
        if (k_msgq_put(&selections, &choice, K_NO_WAIT) != 0) {
            atomic_set(&accepting, 1);
        }
    }
}

K_WORK_DELAYABLE_DEFINE(press_window, finish_selection);

void demo_button_pressed(void)
{
    if (!atomic_get(&accepting)) {
        return;
    }
    ++presses;
    if (presses == DEMO_IMAGE_COUNT) {
        k_work_cancel_delayable(&press_window);
        finish_selection(NULL);
    } else {
        k_work_reschedule(&press_window, K_MSEC(CONFIG_BOREALIS_PRESS_WINDOW_MS));
    }
}

static int send_image(uint8_t slot)
{
    const struct demo_image *image = &demo_images[slot - 1];
    struct demo_frame_key key = {
        .transfer = sys_rand32_get(),
        .total = image->count,
        .image = slot,
    };
    uint8_t wire[DEMO_MAX_DATAGRAM];
    LOG_INF("Image %u, %u packets, session %08x",
            (unsigned)slot, (unsigned)key.total, (unsigned)key.transfer);

    for (key.frame = 0; key.frame < key.total; ++key.frame) {
        const uint8_t *packet = image->packets + DEMO_SSDV_BYTES * key.frame;
        for (uint8_t index = 0; index < DEMO_FRAGMENTS; ++index) {
            size_t length = demo_build_datagram(wire, &key, index, packet);
            int error = demo_radio_send(wire, length);
            if (error) {
                return error;
            }
            k_sleep(K_MSEC(CONFIG_BOREALIS_PACKET_GAP_MS));
        }
    }
    return 0;
}

int main(void)
{
    int error = demo_controls_init();
    if (!error) {
        error = demo_radio_configure(true);
    }
    if (error) {
        LOG_ERR("Transmitter initialization failed: %d", error);
        return error;
    }

    atomic_set(&accepting, 1);
    LOG_INF("Ready: press the user button one, two or three times");
    for (;;) {
        uint8_t slot;
        k_msgq_get(&selections, &slot, K_FOREVER);
        demo_led_set(true);
        error = send_image(slot);
        demo_led_set(false);
        if (error) {
            LOG_ERR("Image %u stopped: LoRa error %d", (unsigned)slot, error);
        }
        atomic_set(&accepting, 1);
    }
}
