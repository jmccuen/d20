/*
 * widgets.h — date ribbon and the HP / torch stat row.
 *
 * Familiar art for the ribbon corner and the six class silhouettes for the
 * journey token will live alongside these in later phases.
 */

#pragma once

#include <pebble.h>

/* Load + cache the bitmap resources used by ribbon and stat-row draw
 * functions. Must be called once after the window is created and
 * before the first paint. widgets_deinit releases them. */
void widgets_init(void);
void widgets_deinit(void);

void widgets_draw_ribbon(GContext *ctx, GRect bounds,
                         int16_t day_of_year, const char *date_str,
                         bool bluetooth);

void widgets_draw_stats(GContext *ctx, GRect bounds,
                        int16_t heart_rate, uint8_t battery_pct);
