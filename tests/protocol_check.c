#include "demo_protocol.h"
#include "demo_telemetry.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void stdout_write(const char *bytes, size_t count, void *context)
{
    (void)context;
    assert(fwrite(bytes, 1, count, stdout) == count);
}

static void check_codec(const struct demo_rs *rs, const uint8_t *original)
{
    uint8_t full[256], decoded[256], chips[512];
    for (unsigned i = 0; i < 256; ++i) {
        full[i] = (uint8_t)i;
    }
    demo_manchester_encode(full, sizeof(full), chips);
    assert(chips[0] == 0x55 && chips[1] == 0x55);
    assert(chips[510] == 0xaa && chips[511] == 0xaa);
    assert(demo_manchester_decode(chips, sizeof(decoded), decoded) == 0);
    assert(memcmp(decoded, full, sizeof(full)) == 0);
    chips[0] = 0;
    assert(demo_manchester_decode(chips, sizeof(decoded), decoded) == 4);

    for (unsigned errors = 0; errors <= 16; ++errors) {
        memcpy(decoded, original, sizeof(decoded));
        for (unsigned i = 0; i < errors; ++i) {
            decoded[1 + (errors + 33 * i) % 255] ^= 0xa7;
        }
        assert(demo_ssdv_repair(rs, decoded) == (int)errors);
        assert(memcmp(decoded, original, sizeof(decoded)) == 0);
    }
    memcpy(decoded, original, sizeof(decoded));
    for (unsigned i = 1; i <= 17; ++i) {
        decoded[i * 13] ^= 0xb3;
    }
    assert(demo_ssdv_repair(rs, decoded) == -1);
}

static void simulate(const struct demo_writer *out, const struct demo_rs *rs,
                     uint8_t slot, const char *path, uint32_t *seq)
{
    FILE *input = fopen(path, "rb");
    assert(input != NULL);
    uint8_t original[DEMO_SSDV_BYTES];
    assert(fseek(input, 0, SEEK_END) == 0);
    long length = ftell(input);
    assert(length > 0 && length % DEMO_SSDV_BYTES == 0);
    assert(length / DEMO_SSDV_BYTES <= DEMO_MAX_FRAMES);
    rewind(input);

    struct demo_frame_key key = {
        .transfer = 0xC011EC70u + slot,
        .image = slot,
        .total = (uint16_t)(length / DEMO_SSDV_BYTES),
    };
    struct demo_assembler assembler = {0};
    for (key.frame = 0; key.frame < key.total; ++key.frame) {
        assert(fread(original, 1, sizeof(original), input) == sizeof(original));
        if (key.frame == 0) {
            check_codec(rs, original);
        }
        for (uint8_t index = 0; index < DEMO_FRAGMENTS; ++index) {
            uint8_t wire[DEMO_MAX_DATAGRAM];
            struct demo_fragment fragment;
            struct demo_frame frame;
            size_t size = demo_build_datagram(wire, &key, index, original);
            assert(size <= DEMO_RADIO_CAPACITY);
            if (slot == 2 && key.frame == 0 && index == 0) {
                wire[DEMO_HEADER_BYTES + 16] ^= 0xc0; /* valid but inverted pair */
                wire[DEMO_HEADER_BYTES + 20] ^= 0x80; /* illegal pair */
            }
            ++*seq;
            demo_report_radio(out, *seq, *seq * 115u, -62 - slot, 8, wire, size);
            assert(!demo_parse_datagram(wire, size - 1, &fragment));
            assert(demo_parse_datagram(wire, size, &fragment));
            bool complete = demo_assemble(&assembler, rs, &fragment, &frame);
            assert(complete == (index == DEMO_FRAGMENTS - 1));
            assert(!demo_assemble(&assembler, rs, &fragment, &frame));
            if (complete) {
                assert(frame.status == DEMO_FRAME_OK);
                assert(memcmp(frame.ssdv, original, sizeof(original)) == 0);
                if (slot == 2 && key.frame == 0) {
                    assert(frame.invalid_pairs == 1 && frame.corrected_symbols >= 1);
                    assert(memcmp(frame.decoded, original, sizeof(original)) != 0);
                }
                demo_report_frame(out, *seq, &frame);
            }
        }
    }
    fclose(input);
}

int main(int argc, char **argv)
{
    assert(argc == 4);
    struct demo_rs rs;
    demo_rs_init(&rs);
    const struct demo_writer writer = { .write = stdout_write };
    demo_report_status(&writer, "listening", 915000000);
    uint32_t seq = 0;
    for (uint8_t slot = 1; slot <= 3; ++slot) {
        simulate(&writer, &rs, slot, argv[slot], &seq);
    }
}
