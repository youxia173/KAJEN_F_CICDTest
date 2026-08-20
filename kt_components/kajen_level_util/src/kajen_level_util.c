#include "kajen_level_util.h"

uint8_t kajen_level254_to_percent(uint8_t level254)
{
  return (uint8_t)(((uint16_t)level254 * 100u + 127u) / 254u);
}

uint16_t kajen_clamp_u16(uint16_t value, uint16_t min, uint16_t max)
{
  if (value < min) {
    return min;
  }
  if (value > max) {
    return max;
  }
  return value;
}

uint8_t kajen_permille_to_percent(uint16_t permille)
{
  permille = kajen_clamp_u16(permille, 0u, 1000u);
  return (uint8_t)((permille * 100u + 500u) / 1000u);
}

kajen_status_t kajen_validate_permille(uint16_t permille)
{
  if (permille > 1000u) {
    return KAJEN_ERROR_BAD_ARGUMENT;
  }
  return KAJEN_SUCCESS;
}
