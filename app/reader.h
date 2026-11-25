/* Reader: a small structured-text view for the 128x64 display.
 *
 * A document is a list of styled blocks (title, section heading, meta line, paragraph, bullet,
 * note, link). The reader wraps them to the real glyph widths of the fonts, draws a title bar,
 * underlined headings, hanging-indent bullets, quoted notes and selectable links, and scrolls
 * by line. Controls: Up/Down scroll, Left/Right move between links, OK opens the focused
 * link, Back leaves; with a button bar, Left/OK/Right press its buttons instead. Buffers are
 * allocated once; nothing is allocated while drawing.
 */
#pragma once

#include <gui/view.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ReaderTitle,
    ReaderHeading,
    ReaderMeta,
    ReaderParagraph,
    ReaderBullet,
    ReaderNote,
    ReaderLink,
} ReaderStyle;

typedef struct Reader Reader;
typedef void (*ReaderLinkCallback)(void* context, uint32_t target);
/* button: 0 left, 1 center, 2 right; long is true for a long press of the center button */
typedef void (*ReaderButtonCallback)(void* context, uint8_t button, bool long_press);

Reader* reader_alloc(void);
void reader_free(Reader* reader);
View* reader_get_view(Reader* reader);
void reader_set_link_callback(Reader* reader, ReaderLinkCallback callback, void* context);
void reader_set_button_callback(Reader* reader, ReaderButtonCallback callback, void* context);
/* A bottom button bar (NULL hides a button). While shown, Left/OK/Right press the buttons;
 * Up/Down still scroll. reader_reset() removes it. */
void reader_set_buttons(Reader* reader, const char* left, const char* center, const char* right);

/* Building a document: reset, add blocks, show. Text is copied; a full buffer drops the rest
 * and ends the document with a "(truncated)" note rather than failing. */
void reader_reset(Reader* reader);
void reader_add(Reader* reader, ReaderStyle style, const char* text, uint32_t link_target);
void reader_show(Reader* reader);
