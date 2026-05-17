#include "tvbgone_xremote.h"
#include <string.h>
#include <stdlib.h>

// Alt-name table — IRDB submissions are inconsistent (POWER / power / shutdown /
// off / on / standby — same button). Map canonical names onto alternatives.
static const struct {
    const char* canonical;
    const char* alts[6];
} ALT_NAMES[] = {
    {"Power",   {"POWER", "power", "shutdown", "off", "on", "standby"}},
    {"Mute",    {"MUTE", "mute", "silence", "unmute", NULL, NULL}},
    {"Vol_up",  {"Vol+", "VOL+", "vol+", "volume+", "VolumeUp", "VOL_UP"}},
    {"Vol_dn",  {"Vol-", "VOL-", "vol-", "volume-", "VolumeDown", "VOL_DN"}},
    {"Ch_next", {"Ch+", "CH+", "ch+", "channel+", "ChUp", "CH_UP"}},
    {"Ch_prev", {"Ch-", "CH-", "ch-", "channel-", "ChDown", "CH_DN"}},
    {"Up",      {"UP", "up", "uparrow", "Cursor_Up", NULL, NULL}},
    {"Down",    {"DOWN", "down", "downarrow", "Cursor_Down", NULL, NULL}},
    {"Left",    {"LEFT", "left", "leftarrow", "Cursor_Left", NULL, NULL}},
    {"Right",   {"RIGHT", "right", "rightarrow", "Cursor_Right", NULL, NULL}},
    {"Ok",      {"OK", "ok", "Enter", "ENTER", "Select", "SELECT"}},
    {"Back",    {"BACK", "back", "Return", "RETURN", "Exit", "EXIT"}},
    {"Menu",    {"MENU", "menu", "Home", "HOME", "OSD", NULL}},
    {"Input",   {"INPUT", "input", "Source", "SOURCE", "AV", "TV_AV"}},
};
#define ALT_NAMES_COUNT (sizeof(ALT_NAMES) / sizeof(ALT_NAMES[0]))

static bool name_matches(const char* file_name, const char* wanted) {
    if(strcasecmp(file_name, wanted) == 0) return true;
    for(size_t i = 0; i < ALT_NAMES_COUNT; i++) {
        if(strcasecmp(ALT_NAMES[i].canonical, wanted) != 0) continue;
        for(size_t j = 0; j < 6; j++) {
            if(ALT_NAMES[i].alts[j] == NULL) break;
            if(strcasecmp(ALT_NAMES[i].alts[j], file_name) == 0) return true;
        }
    }
    return false;
}

// Read & transmit one signal block from an open FlipperFormat. Returns true if
// a signal was read (regardless of whether it was actually transmittable).
// On success, also returns whether `name` matched `wanted` AND it was actually
// sent — via `out_matched_and_sent`.
//
// We use the public infrared_send / infrared_send_raw_ext API so we don't need
// the (non-exported) InfraredSignal type.
static bool read_and_maybe_send(
    FlipperFormat* ff,
    const char* wanted_name,
    FuriString* out_name,
    bool* out_matched_and_sent) {
    *out_matched_and_sent = false;

    if(!flipper_format_read_string(ff, "name", out_name)) return false;

    FuriString* type = furi_string_alloc();
    if(!flipper_format_read_string(ff, "type", type)) {
        furi_string_free(type);
        return false;
    }

    bool wanted = (wanted_name == NULL) ||
                  name_matches(furi_string_get_cstr(out_name), wanted_name);

    if(furi_string_cmp_str(type, "parsed") == 0) {
        FuriString* protocol = furi_string_alloc();
        uint8_t addr_bytes[4] = {0}, cmd_bytes[4] = {0};

        bool ok =
            flipper_format_read_string(ff, "protocol", protocol) &&
            flipper_format_read_hex(ff, "address", addr_bytes, 4) &&
            flipper_format_read_hex(ff, "command", cmd_bytes, 4);

        if(ok && wanted) {
            InfraredMessage msg = {
                .protocol = infrared_get_protocol_by_name(furi_string_get_cstr(protocol)),
                .address = (uint32_t)addr_bytes[0] |
                           ((uint32_t)addr_bytes[1] << 8) |
                           ((uint32_t)addr_bytes[2] << 16) |
                           ((uint32_t)addr_bytes[3] << 24),
                .command = (uint32_t)cmd_bytes[0] |
                           ((uint32_t)cmd_bytes[1] << 8) |
                           ((uint32_t)cmd_bytes[2] << 16) |
                           ((uint32_t)cmd_bytes[3] << 24),
            };
            if(msg.protocol != InfraredProtocolUnknown) {
                infrared_send(&msg, 1);
                *out_matched_and_sent = true;
            }
        }
        furi_string_free(protocol);
    } else if(furi_string_cmp_str(type, "raw") == 0) {
        uint32_t freq = 0;
        float duty = 0.33f;
        flipper_format_read_uint32(ff, "frequency", &freq, 1);
        flipper_format_read_float(ff, "duty_cycle", &duty, 1);

        uint32_t count = 0;
        if(flipper_format_get_value_count(ff, "data", &count) && count > 0 && count < 1024) {
            uint32_t* buf = malloc(count * sizeof(uint32_t));
            if(buf) {
                if(flipper_format_read_uint32(ff, "data", buf, count) && wanted) {
                    infrared_send_raw_ext(
                        buf,
                        count,
                        true, // start_from_mark — Flipper raw format always begins with mark
                        freq ? freq : 38000,
                        duty);
                    *out_matched_and_sent = true;
                }
                free(buf);
            }
        }
    }
    // Other types: silently skip (e.g. unknown future formats)

    furi_string_free(type);
    return true;
}

bool ir_transmit_named(Storage* storage, const char* path, const char* button_name) {
    furi_assert(storage);
    furi_assert(path);
    furi_assert(button_name);

    FlipperFormat* ff = flipper_format_buffered_file_alloc(storage);
    bool sent = false;

    do {
        if(!flipper_format_buffered_file_open_existing(ff, path)) break;

        FuriString* header = furi_string_alloc();
        uint32_t version = 0;
        bool ok_header = flipper_format_read_header(ff, header, &version);
        furi_string_free(header);
        if(!ok_header) break;

        FuriString* name = furi_string_alloc();
        bool matched = false;
        while(read_and_maybe_send(ff, button_name, name, &matched)) {
            if(matched) {
                sent = true;
                break;
            }
        }
        furi_string_free(name);
    } while(false);

    flipper_format_free(ff);
    return sent;
}

uint32_t ir_blast_all_power(
    Storage* storage,
    const char* path,
    uint32_t inter_signal_delay_ms,
    IrBlastProgress on_progress,
    void* ctx) {
    furi_assert(storage);
    furi_assert(path);

    FlipperFormat* ff = flipper_format_buffered_file_alloc(storage);
    uint32_t sent_count = 0;

    do {
        if(!flipper_format_buffered_file_open_existing(ff, path)) break;

        FuriString* header = furi_string_alloc();
        uint32_t version = 0;
        bool ok_header = flipper_format_read_header(ff, header, &version);
        furi_string_free(header);
        if(!ok_header) break;

        FuriString* name = furi_string_alloc();
        bool matched = false;
        while(read_and_maybe_send(ff, "Power", name, &matched)) {
            if(matched) {
                sent_count++;
                if(on_progress) {
                    bool keep = on_progress(ctx, sent_count, furi_string_get_cstr(name));
                    if(!keep) break;
                }
                furi_delay_ms(inter_signal_delay_ms);
            }
        }
        furi_string_free(name);
    } while(false);

    flipper_format_free(ff);
    return sent_count;
}
