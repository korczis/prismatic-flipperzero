/* Prismatic for Flipper Zero: First Light.
 *
 * One native dialog that proves the application is installed and runs: the Prismatic mark,
 * the name, the milestone and the version. Continue shows the firmware the app is running
 * on, read from the device at runtime; Back or Exit leaves.
 */
#include <furi.h>
#include <dialogs/dialogs.h>
#include <toolbox/version.h>

#include "version.h"
#include <prismatic_icons.h>

static DialogMessageButton prismatic_show_splash(DialogsApp* dialogs, DialogMessage* msg) {
    dialog_message_set_header(msg, "PRISMATIC", 86, 2, AlignCenter, AlignTop);
    dialog_message_set_text(
        msg, "Flipper Zero\nFirst Light\nv" PRISMATIC_VERSION, 86, 18, AlignCenter, AlignTop);
    dialog_message_set_icon(msg, &I_prism_40x40, 2, 12);
    dialog_message_set_buttons(msg, "Exit", NULL, "Continue");
    return dialog_message_show(dialogs, msg);
}

static void prismatic_show_firmware(DialogsApp* dialogs, DialogMessage* msg) {
    const char* fw = version_get_version(NULL);
    FuriString* text = furi_string_alloc_printf(
        "Firmware %s\nApp v%s\nBack to exit", fw ? fw : "UNKNOWN", PRISMATIC_VERSION);
    dialog_message_set_icon(msg, NULL, 0, 0);
    dialog_message_set_header(msg, "Running on", 64, 2, AlignCenter, AlignTop);
    dialog_message_set_text(msg, furi_string_get_cstr(text), 64, 18, AlignCenter, AlignTop);
    dialog_message_set_buttons(msg, NULL, NULL, NULL);
    dialog_message_show(dialogs, msg);
    furi_string_free(text);
}

int32_t prismatic_app(void* p) {
    UNUSED(p);
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessage* msg = dialog_message_alloc();

    FURI_LOG_I("Prismatic", "first light v%s", PRISMATIC_VERSION);
    if(prismatic_show_splash(dialogs, msg) == DialogMessageButtonRight) {
        prismatic_show_firmware(dialogs, msg);
    }

    dialog_message_free(msg);
    furi_record_close(RECORD_DIALOGS);
    return 0;
}
