#include "demo_protocol.h"

#include <string.h>

static bool same_frame(const struct demo_frame_key *a, const struct demo_frame_key *b)
{
    return a->transfer == b->transfer && a->image == b->image &&
           a->frame == b->frame && a->total == b->total;
}

bool demo_assemble(struct demo_assembler *assembler, const struct demo_rs *rs,
                   const struct demo_fragment *fragment, struct demo_frame *frame)
{
    unsigned mask = 1u << fragment->index;
    if (!assembler->fragments || !same_frame(&assembler->key, &fragment->key)) {
        assembler->key = fragment->key;
        assembler->fragments = 0;
        assembler->invalid_pairs = 0;
    }
    if (assembler->fragments & mask) {
        return false;
    }

    assembler->invalid_pairs += demo_manchester_decode(
        fragment->chips, fragment->bytes,
        assembler->decoded + fragment->index * DEMO_FRAGMENT_BYTES);
    assembler->fragments |= (uint8_t)mask;
    if (assembler->fragments != (1u << DEMO_FRAGMENTS) - 1u) {
        return false;
    }

    frame->key = assembler->key;
    frame->invalid_pairs = assembler->invalid_pairs;
    memcpy(frame->decoded, assembler->decoded, DEMO_SSDV_BYTES);
    memcpy(frame->ssdv, assembler->decoded, DEMO_SSDV_BYTES);
    frame->corrected_symbols = demo_ssdv_repair(rs, frame->ssdv);
    uint16_t ssdv_index = ((uint16_t)frame->ssdv[7] << 8) | frame->ssdv[8];
    bool eoi = (frame->ssdv[11] & 0x04u) != 0;
    if (frame->corrected_symbols < 0) {
        frame->status = DEMO_FRAME_UNCORRECTABLE;
    } else if (frame->ssdv[6] != frame->key.image || ssdv_index != frame->key.frame ||
               eoi != (frame->key.frame + 1u == frame->key.total)) {
        frame->status = DEMO_FRAME_METADATA;
    } else {
        frame->status = DEMO_FRAME_OK;
    }
    return true;
}
