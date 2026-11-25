#include "demo_protocol.h"

#include <string.h>

/* SSDV normal packets contain a CCSDS RS(255,223) codeword after the sync byte. */
#define FIELD_ORDER 255u
#define FIELD_POLYNOMIAL 0x187u
#define FIRST_ROOT 112u
#define ROOT_STEP 11u
#define CORRECTABLE (DEMO_SSDV_PARITY / 2u)
#define CRC_OFFSET 220u

void demo_rs_init(struct demo_rs *rs)
{
    unsigned value = 1;
    rs->logarithm[0] = 0;
    for (unsigned i = 0; i < FIELD_ORDER; ++i) {
        rs->exponent[i] = rs->exponent[i + FIELD_ORDER] = (uint8_t)value;
        rs->logarithm[value] = (uint8_t)i;
        value <<= 1;
        if (value & 0x100u) {
            value ^= FIELD_POLYNOMIAL;
        }
    }
}

static uint8_t multiply(const struct demo_rs *rs, uint8_t a, uint8_t b)
{
    return a && b ? rs->exponent[rs->logarithm[a] + rs->logarithm[b]] : 0;
}

static uint8_t divide(const struct demo_rs *rs, uint8_t a, uint8_t b)
{
    return a ? rs->exponent[FIELD_ORDER + rs->logarithm[a] - rs->logarithm[b]] : 0;
}

static uint8_t evaluate(const struct demo_rs *rs, const uint8_t *coefficients,
                        size_t count, uint8_t x)
{
    uint8_t value = 0;
    for (size_t i = 0; i < count; ++i) {
        value = multiply(rs, value, x) ^ coefficients[i];
    }
    return value;
}

static bool syndromes(const struct demo_rs *rs, const uint8_t *word, uint8_t *out)
{
    uint8_t nonzero = 0;
    for (unsigned i = 0; i < DEMO_SSDV_PARITY; ++i) {
        uint8_t root = rs->exponent[((FIRST_ROOT + i) * ROOT_STEP) % FIELD_ORDER];
        out[i] = evaluate(rs, word, FIELD_ORDER, root);
        nonzero |= out[i];
    }
    return nonzero != 0;
}

/* Berlekamp-Massey with the locator coefficients in ascending order. */
static unsigned locate(const struct demo_rs *rs, const uint8_t *syndrome,
                        uint8_t *locator)
{
    uint8_t previous[DEMO_SSDV_PARITY + 1] = {1};
    uint8_t saved[DEMO_SSDV_PARITY + 1];
    uint8_t last_gap = 1;
    unsigned degree = 0;
    unsigned shift = 1;
    locator[0] = 1;

    for (unsigned step = 0; step < DEMO_SSDV_PARITY; ++step) {
        uint8_t gap = syndrome[step];
        for (unsigned i = 1; i <= degree; ++i) {
            gap ^= multiply(rs, locator[i], syndrome[step - i]);
        }
        if (!gap) {
            ++shift;
            continue;
        }

        memcpy(saved, locator, sizeof(saved));
        uint8_t scale = divide(rs, gap, last_gap);
        for (unsigned i = 0; i + shift <= DEMO_SSDV_PARITY; ++i) {
            locator[i + shift] ^= multiply(rs, scale, previous[i]);
        }
        if (2 * degree <= step) {
            degree = step + 1 - degree;
            memcpy(previous, saved, sizeof(previous));
            last_gap = gap;
            shift = 1;
        } else {
            ++shift;
        }
    }
    return degree;
}

static unsigned error_positions(const struct demo_rs *rs, const uint8_t *locator,
                                unsigned degree, uint8_t *positions, uint8_t *roots)
{
    unsigned found = 0;
    for (unsigned p = 0; p < FIELD_ORDER && found < degree; ++p) {
        unsigned power = (ROOT_STEP * (FIELD_ORDER - 1 - p)) % FIELD_ORDER;
        uint8_t inverse = rs->exponent[FIELD_ORDER - power];
        uint8_t value = locator[degree];
        for (unsigned i = degree; i > 0; --i) {
            value = multiply(rs, value, inverse) ^ locator[i - 1];
        }
        if (!value) {
            positions[found] = (uint8_t)p;
            roots[found++] = rs->exponent[power];
        }
    }
    return found;
}

/* Solve the first n syndrome equations for the n located error magnitudes. */
static bool fix_symbols(const struct demo_rs *rs, uint8_t *word, const uint8_t *syndrome,
                        const uint8_t *positions, const uint8_t *roots, unsigned n)
{
    uint8_t matrix[CORRECTABLE][CORRECTABLE + 1];

    for (unsigned col = 0; col < n; ++col) {
        uint8_t power = 1;
        for (unsigned row = 0; row < n; ++row) {
            matrix[row][col] = power;
            power = multiply(rs, power, roots[col]);
        }
    }
    for (unsigned row = 0; row < n; ++row) {
        matrix[row][n] = syndrome[row];
    }

    for (unsigned col = 0; col < n; ++col) {
        unsigned pivot = col;
        while (pivot < n && !matrix[pivot][col]) {
            ++pivot;
        }
        if (pivot == n) {
            return false;
        }
        for (unsigned k = col; k <= n; ++k) {
            uint8_t swap = matrix[col][k];
            matrix[col][k] = matrix[pivot][k];
            matrix[pivot][k] = swap;
        }
        uint8_t scale = matrix[col][col];
        for (unsigned k = col; k <= n; ++k) {
            matrix[col][k] = divide(rs, matrix[col][k], scale);
        }
        for (unsigned row = 0; row < n; ++row) {
            if (row != col) {
                uint8_t factor = matrix[row][col];
                for (unsigned k = col; k <= n; ++k) {
                    matrix[row][k] ^= multiply(rs, factor, matrix[col][k]);
                }
            }
        }
    }

    for (unsigned i = 0; i < n; ++i) {
        unsigned power = (FIRST_ROOT * rs->logarithm[roots[i]]) % FIELD_ORDER;
        word[positions[i]] ^= divide(rs, matrix[i][n], rs->exponent[power]);
    }
    return true;
}

static uint32_t crc32(const uint8_t *bytes, size_t length)
{
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            uint32_t polynomial = (crc & 1u) ? 0xedb88320u : 0;
            crc = (crc >> 1) ^ polynomial;
        }
    }
    return ~crc;
}

int demo_ssdv_repair(const struct demo_rs *rs, uint8_t ssdv[DEMO_SSDV_BYTES])
{
    uint8_t syndrome[DEMO_SSDV_PARITY];
    unsigned corrections = 0;
    uint8_t *word = ssdv + 1;

    if (syndromes(rs, word, syndrome)) {
        uint8_t locator[DEMO_SSDV_PARITY + 1] = {0};
        uint8_t positions[CORRECTABLE], roots[CORRECTABLE];
        corrections = locate(rs, syndrome, locator);
        if (!corrections || corrections > CORRECTABLE ||
            error_positions(rs, locator, corrections, positions, roots) != corrections ||
            !fix_symbols(rs, word, syndrome, positions, roots, corrections) ||
            syndromes(rs, word, syndrome)) {
            return -1;
        }
    }

    uint32_t expected = 0;
    for (unsigned i = 0; i < 4; ++i) {
        expected = (expected << 8) | ssdv[CRC_OFFSET + i];
    }
    if (ssdv[1] != 0x66u || crc32(word, CRC_OFFSET - 1u) != expected) {
        return -1;
    }

    /* The LoRa fragment header supplies the frame boundary for the sync byte. */
    ssdv[0] = 0x55;
    return (int)corrections;
}
