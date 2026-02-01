#ifndef CUPIDIMAGE_WEBP_TABLES_H
#define CUPIDIMAGE_WEBP_TABLES_H

#include <stdint.h>

extern const uint8_t cupidimage_vp8_coeff_probs[4][8][3][11];
extern const uint8_t cupidimage_vp8_kf_bmode_prob[10][10][9];
extern const uint8_t cupidimage_vp8_coeff_update_probs[4][8][3][11];
extern const int16_t cupidimage_vp8_dc_qlookup[128];
extern const int16_t cupidimage_vp8_ac_qlookup[128];

#endif /* CUPIDIMAGE_WEBP_TABLES_H */
