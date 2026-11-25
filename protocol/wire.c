#include "demo_protocol.h"

/* Manchester convention for this demo: MSB first, 0 -> 01 and 1 -> 10. */
static const uint8_t manchester_nibble[16] = {
    0x55, 0x56, 0x59, 0x5a, 0x65, 0x66, 0x69, 0x6a,
    0x95, 0x96, 0x99, 0x9a, 0xa5, 0xa6, 0xa9, 0xaa
};

void demo_manchester_encode(const uint8_t *bytes, size_t count, uint8_t *chips)
{
    for (size_t i = 0; i < count; ++i) {
        chips[2 * i] = manchester_nibble[bytes[i] >> 4];
        chips[2 * i + 1] = manchester_nibble[bytes[i] & 0x0f];
    }
}

uint16_t demo_manchester_decode(const uint8_t *chips, size_t count, uint8_t *bytes)
{
    uint16_t invalid = 0;

    for (size_t i = 0; i < count; ++i) {
        uint16_t symbols = ((uint16_t)chips[2 * i] << 8) | chips[2 * i + 1];
        uint8_t value = 0;
        for (int shift = 14; shift >= 0; shift -= 2) {
            unsigned pair = (symbols >> shift) & 3u;
            value = (uint8_t)((value << 1) | (pair == 2u));
            invalid += pair == 0u || pair == 3u;
        }
        bytes[i] = value;
    }
    return invalid;
}

static size_t fragment_bytes(uint8_t index)
{
    size_t offset = index * DEMO_FRAGMENT_BYTES;
    size_t remaining = DEMO_SSDV_BYTES - offset;
    return remaining < DEMO_FRAGMENT_BYTES ? remaining : DEMO_FRAGMENT_BYTES;
}

static bool valid_key(const struct demo_frame_key *key)
{
    return key->image >= 1u && key->image <= DEMO_IMAGE_COUNT &&
           key->total <= DEMO_MAX_FRAMES && key->frame < key->total;
}

static uint16_t read16(const uint8_t *bytes)
{
    return ((uint16_t)bytes[0] << 8) | bytes[1];
}

static uint32_t read32(const uint8_t *bytes)
{
    return ((uint32_t)read16(bytes) << 16) | read16(bytes + 2);
}

static void write16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

size_t demo_build_datagram(uint8_t out[DEMO_MAX_DATAGRAM],
                           const struct demo_frame_key *key, uint8_t index,
                           const uint8_t ssdv[DEMO_SSDV_BYTES])
{
    if (!valid_key(key) || index >= DEMO_FRAGMENTS) {
        return 0;
    }

    size_t bytes = fragment_bytes(index);
    out[0] = 'B';
    out[1] = 'L';
    out[2] = 1;
    out[3] = key->image;
    write16(out + 4, (uint16_t)(key->transfer >> 16));
    write16(out + 6, (uint16_t)key->transfer);
    write16(out + 8, key->frame);
    write16(out + 10, key->total);
    out[12] = index;
    demo_manchester_encode(ssdv + index * DEMO_FRAGMENT_BYTES, bytes,
                           out + DEMO_HEADER_BYTES);
    return DEMO_HEADER_BYTES + 2 * bytes;
}

bool demo_parse_datagram(const uint8_t *wire, size_t length, struct demo_fragment *fragment)
{
    if (length < DEMO_HEADER_BYTES || wire[0] != 'B' || wire[1] != 'L' ||
        wire[2] != 1u || wire[12] >= DEMO_FRAGMENTS) {
        return false;
    }

    struct demo_frame_key key = {
        .image = wire[3],
        .transfer = read32(wire + 4),
        .frame = read16(wire + 8),
        .total = read16(wire + 10),
    };
    size_t bytes = fragment_bytes(wire[12]);
    if (!valid_key(&key) || length != DEMO_HEADER_BYTES + 2 * bytes) {
        return false;
    }

    *fragment = (struct demo_fragment) {
        .key = key, .index = wire[12], .chips = wire + DEMO_HEADER_BYTES, .bytes = bytes,
    };
    return true;
}

