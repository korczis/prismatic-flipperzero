/* Canvas API over the firmware's u8g2, mirroring applications/services/gui/canvas.c of
 * firmware 1.5.1 in behaviour: font mode 1 (transparent), the same four system fonts, text
 * drawn with u8g2_DrawUTF8 at the baseline, haxrcorp4089's height reported one pixel taller. */
#include <gui/canvas.h>

#include <stdlib.h>
#include <u8g2.h>

extern const uint8_t u8g2_font_helvB08_tr[];
extern const uint8_t u8g2_font_haxrcorp4089_tr[];
extern const uint8_t u8g2_font_profont11_mr[];
extern const uint8_t u8g2_font_profont22_tn[];

struct Canvas {
    u8g2_t fb;
    uint8_t buf[1024];
};

/* A display with nothing to talk to: 128x64, 16x8 tiles, every message accepted. */
static const u8x8_display_info_t sim_info = {
    .chip_enable_level = 0,
    .chip_disable_level = 1,
    .post_chip_enable_wait_ns = 0,
    .pre_chip_disable_wait_ns = 0,
    .reset_pulse_width_ms = 0,
    .post_reset_wait_ms = 0,
    .sda_setup_time_ns = 0,
    .sck_pulse_width_ns = 0,
    .sck_clock_hz = 1000000,
    .spi_mode = 0,
    .i2c_bus_clock_100kHz = 4,
    .data_setup_time_ns = 0,
    .write_pulse_width_ns = 0,
    .tile_width = 16,
    .tile_height = 8,
    .default_x_offset = 0,
    .flipmode_x_offset = 0,
    .pixel_width = 128,
    .pixel_height = 64,
};

static uint8_t sim_display(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr) {
    (void)arg_int;
    (void)arg_ptr;
    if(msg == U8X8_MSG_DISPLAY_SETUP_MEMORY) u8x8_d_helper_display_setup_memory(u8x8, &sim_info);
    return 1;
}

static uint8_t sim_noop(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr) {
    (void)u8x8;
    (void)msg;
    (void)arg_int;
    (void)arg_ptr;
    return 1;
}

Canvas* sim_canvas_alloc(void) {
    Canvas* c = calloc(1, sizeof(Canvas));
    u8g2_SetupDisplay(&c->fb, sim_display, u8x8_cad_empty, sim_noop, sim_noop);
    u8g2_SetupBuffer(&c->fb, c->buf, 8, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
    u8g2_InitDisplay(&c->fb);
    u8g2_ClearBuffer(&c->fb);
    canvas_set_color(c, ColorBlack);
    canvas_set_font(c, FontSecondary);
    return c;
}

void sim_canvas_free(Canvas* c) {
    free(c);
}

const uint8_t* sim_canvas_frame(Canvas* c) {
    return c->buf;
}

size_t canvas_width(const Canvas* c) {
    (void)c;
    return 128;
}

size_t canvas_height(const Canvas* c) {
    (void)c;
    return 64;
}

size_t canvas_current_font_height(const Canvas* c) {
    size_t h = u8g2_GetMaxCharHeight((u8g2_t*)&c->fb);
    if(c->fb.font == u8g2_font_haxrcorp4089_tr) h += 1;
    return h;
}

void canvas_clear(Canvas* c) {
    u8g2_ClearBuffer(&c->fb);
}

void canvas_set_color(Canvas* c, Color color) {
    u8g2_SetDrawColor(&c->fb, color);
}

void canvas_invert_color(Canvas* c) {
    c->fb.draw_color = !c->fb.draw_color;
}

void canvas_set_font(Canvas* c, Font font) {
    static const uint8_t* const fonts[] = {
        u8g2_font_helvB08_tr,
        u8g2_font_haxrcorp4089_tr,
        u8g2_font_profont11_mr,
        u8g2_font_profont22_tn,
    };
    if(font >= FontTotalNumber) abort();
    u8g2_SetFontMode(&c->fb, 1);
    u8g2_SetFont(&c->fb, fonts[font]);
}

void canvas_set_custom_u8g2_font(Canvas* c, const uint8_t* font) {
    u8g2_SetFontMode(&c->fb, 1);
    u8g2_SetFont(&c->fb, font);
}

void canvas_draw_str(Canvas* c, int32_t x, int32_t y, const char* str) {
    if(str) u8g2_DrawUTF8(&c->fb, x, y, str);
}

void canvas_draw_str_aligned(Canvas* c, int32_t x, int32_t y, Align h, Align v, const char* str) {
    if(!str) return;
    if(h == AlignRight) x -= u8g2_GetUTF8Width(&c->fb, str);
    if(h == AlignCenter) x -= u8g2_GetUTF8Width(&c->fb, str) / 2;
    if(v == AlignTop) y += u8g2_GetAscent(&c->fb);
    if(v == AlignCenter) y += u8g2_GetAscent(&c->fb) / 2;
    u8g2_DrawUTF8(&c->fb, x, y, str);
}

uint16_t canvas_string_width(Canvas* c, const char* str) {
    return str ? u8g2_GetUTF8Width(&c->fb, str) : 0;
}

size_t canvas_glyph_width(Canvas* c, uint16_t symbol) {
    return u8g2_GetGlyphWidth(&c->fb, symbol);
}

/* XBM: rows of LSB-first bytes; set bits drawn in the draw colour, clear bits left alone. */
void canvas_draw_xbm(Canvas* c, int32_t x, int32_t y, size_t w, size_t h, const uint8_t* bitmap) {
    size_t stride = (w + 7) / 8;
    for(size_t r = 0; r < h; r++) {
        for(size_t col = 0; col < w; col++) {
            if(bitmap[r * stride + col / 8] & (1u << (col % 8))) {
                u8g2_DrawPixel(&c->fb, (u8g2_uint_t)(x + (int32_t)col), (u8g2_uint_t)(y + (int32_t)r));
            }
        }
    }
}

void canvas_draw_dot(Canvas* c, int32_t x, int32_t y) {
    u8g2_DrawPixel(&c->fb, x, y);
}

void canvas_draw_box(Canvas* c, int32_t x, int32_t y, size_t w, size_t h) {
    u8g2_DrawBox(&c->fb, x, y, w, h);
}

void canvas_draw_frame(Canvas* c, int32_t x, int32_t y, size_t w, size_t h) {
    u8g2_DrawFrame(&c->fb, x, y, w, h);
}

void canvas_draw_line(Canvas* c, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    u8g2_DrawLine(&c->fb, x1, y1, x2, y2);
}

void canvas_draw_rframe(Canvas* c, int32_t x, int32_t y, size_t w, size_t h, size_t r) {
    u8g2_DrawRFrame(&c->fb, x, y, w, h, r);
}

void canvas_draw_rbox(Canvas* c, int32_t x, int32_t y, size_t w, size_t h, size_t r) {
    u8g2_DrawRBox(&c->fb, x, y, w, h, r);
}

void canvas_draw_disc(Canvas* c, int32_t x, int32_t y, size_t r) {
    u8g2_DrawDisc(&c->fb, x, y, r, U8G2_DRAW_ALL);
}

