#ifndef BOREALIS_DEMO_PROTOCOL_H
#define BOREALIS_DEMO_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEMO_SSDV_BYTES 256u
#define DEMO_SSDV_PARITY 32u
#define DEMO_MAX_FRAMES 512u
#define DEMO_IMAGE_COUNT 3u
#define DEMO_HEADER_BYTES 13u
#define DEMO_FRAGMENT_BYTES 96u
#define DEMO_FRAGMENTS 3u
#define DEMO_MAX_DATAGRAM (DEMO_HEADER_BYTES + 2u * DEMO_FRAGMENT_BYTES)
#define DEMO_RADIO_CAPACITY 255u

struct demo_frame_key {
    uint32_t transfer;
    uint16_t frame;
    uint16_t total;
    uint8_t image;
};

struct demo_fragment {
    struct demo_frame_key key;
    uint8_t index;
    const uint8_t *chips;
    size_t bytes;
};

struct demo_rs {
    uint8_t exponent[510];
    uint8_t logarithm[256];
};

struct demo_assembler {
    struct demo_frame_key key;
    uint8_t fragments;
    uint16_t invalid_pairs;
    uint8_t decoded[DEMO_SSDV_BYTES];
};

enum demo_frame_status {
    DEMO_FRAME_OK,
    DEMO_FRAME_UNCORRECTABLE,
    DEMO_FRAME_METADATA
};

struct demo_frame {
    struct demo_frame_key key;
    enum demo_frame_status status;
    uint16_t invalid_pairs;
    int corrected_symbols;
    uint8_t decoded[DEMO_SSDV_BYTES];
    uint8_t ssdv[DEMO_SSDV_BYTES];
};

void demo_manchester_encode(const uint8_t *bytes, size_t count, uint8_t *chips);
/* Returns the number of invalid chip pairs. An invalid pair decodes to zero. */
uint16_t demo_manchester_decode(const uint8_t *chips, size_t count, uint8_t *bytes);

size_t demo_build_datagram(uint8_t out[DEMO_MAX_DATAGRAM],
                           const struct demo_frame_key *key, uint8_t index,
                           const uint8_t ssdv[DEMO_SSDV_BYTES]);
bool demo_parse_datagram(const uint8_t *wire, size_t length, struct demo_fragment *fragment);

void demo_rs_init(struct demo_rs *rs);
/* Returns the number of repaired RS symbols, or -1 for an invalid packet. */
int demo_ssdv_repair(const struct demo_rs *rs, uint8_t ssdv[DEMO_SSDV_BYTES]);

/* Feed fragments from demo_parse_datagram for a single ordered radio stream.
 * A new frame replaces an incomplete frame.
 */
bool demo_assemble(struct demo_assembler *assembler, const struct demo_rs *rs,
                   const struct demo_fragment *fragment, struct demo_frame *frame);

#endif
