#ifndef BOREALIS_DEMO_IMAGES_H
#define BOREALIS_DEMO_IMAGES_H

#include "demo_protocol.h"

struct demo_image {
    const uint8_t *packets;
    uint16_t count;
};

extern const struct demo_image demo_images[DEMO_IMAGE_COUNT];

#endif
