/* Host stand-in for the firmware's <gui/canvas.h>: the subset of the canvas API that the
 * portable UI (app/ui) uses, with the SDK's signatures and enum values. tests/sim/canvas.c
 * implements it over the firmware's own u8g2 (tests/sim/fetch.sh), with the firmware's font
 * mapping, so the simulator draws the same pixels as the device. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ColorWhite = 0x00,
    ColorBlack = 0x01,
    ColorXOR = 0x02,
} Color;

typedef enum {
    FontPrimary,
    FontSecondary,
    FontKeyboard,
    FontBigNumbers,
    FontTotalNumber,
} Font;

typedef enum {
    AlignLeft,
    AlignRight,
    AlignTop,
    AlignBottom,
    AlignCenter,
} Align;

typedef struct Canvas Canvas;

size_t canvas_width(const Canvas* canvas);
size_t canvas_height(const Canvas* canvas);
size_t canvas_current_font_height(const Canvas* canvas);
void canvas_clear(Canvas* canvas);
void canvas_set_color(Canvas* canvas, Color color);
void canvas_invert_color(Canvas* canvas);
void canvas_set_font(Canvas* canvas, Font font);
void canvas_set_custom_u8g2_font(Canvas* canvas, const uint8_t* font);
void canvas_draw_str(Canvas* canvas, int32_t x, int32_t y, const char* str);
void canvas_draw_str_aligned(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    Align horizontal,
    Align vertical,
    const char* str);
uint16_t canvas_string_width(Canvas* canvas, const char* str);
size_t canvas_glyph_width(Canvas* canvas, uint16_t symbol);
void canvas_draw_xbm(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    size_t width,
    size_t height,
    const uint8_t* bitmap);
void canvas_draw_dot(Canvas* canvas, int32_t x, int32_t y);
void canvas_draw_box(Canvas* canvas, int32_t x, int32_t y, size_t width, size_t height);
void canvas_draw_frame(Canvas* canvas, int32_t x, int32_t y, size_t width, size_t height);
void canvas_draw_line(Canvas* canvas, int32_t x1, int32_t y1, int32_t x2, int32_t y2);
void canvas_draw_rframe(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    size_t width,
    size_t height,
    size_t radius);
void canvas_draw_rbox(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    size_t width,
    size_t height,
    size_t radius);
void canvas_draw_disc(Canvas* canvas, int32_t x, int32_t y, size_t radius);

/* Host only: create a 128x64 canvas, and read its frame in the display's page layout
 * (the same 1024 bytes the device streams over RPC). */
Canvas* sim_canvas_alloc(void);
void sim_canvas_free(Canvas* canvas);
const uint8_t* sim_canvas_frame(Canvas* canvas);
