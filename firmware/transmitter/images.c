#include "images.h"

/* The linker puts these const SSDV image streams in on-chip flash. */
static const uint8_t slot1[] = { /* Concordia EV Building */
#include "slot1.inc"
};
static const uint8_t slot2[] = { /* Concordia Hall Building */
#include "slot2.inc"
};
static const uint8_t slot3[] = { /* Concordia Loyola Campus */
#include "slot3.inc"
};

#define PACKETS(array) (sizeof(array) / DEMO_SSDV_BYTES)
#define CHECK_IMAGE(array) \
    _Static_assert(sizeof(array) > 0 && sizeof(array) % DEMO_SSDV_BYTES == 0 && \
                   PACKETS(array) <= DEMO_MAX_FRAMES, "Invalid SSDV image size")

CHECK_IMAGE(slot1);
CHECK_IMAGE(slot2);
CHECK_IMAGE(slot3);

const struct demo_image demo_images[DEMO_IMAGE_COUNT] = {
    {slot1, PACKETS(slot1)}, {slot2, PACKETS(slot2)}, {slot3, PACKETS(slot3)},
};
