#ifndef KAJEN_LEVEL_UTIL_H_
#define KAJEN_LEVEL_UTIL_H_

#include <stdint.h>

#include "kajen_error_codes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Matter Level (0..254) to percent (0..100), rounded. */
uint8_t kajen_level254_to_percent(uint8_t level254);

/** Clamp unsigned 16-bit value to inclusive [min, max]. */
uint16_t kajen_clamp_u16(uint16_t value, uint16_t min, uint16_t max);

/** Permille (0..1000) to percent (0..100), rounded. */
uint8_t kajen_permille_to_percent(uint16_t permille);

/** Validate permille is within 0..1000. */
kajen_status_t kajen_validate_permille(uint16_t permille);

#ifdef __cplusplus
}
#endif

#endif /* KAJEN_LEVEL_UTIL_H_ */
