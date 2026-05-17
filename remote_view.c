#include "tvbgone_xremote.h"
#include <gui/elements.h>
#include <input/input.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Pages — mirrors XRemote: General / Control / Navigation / Playback
// ---------------------------------------------------------------------------
typedef enum {
    PageGeneral    = 0,
    PageControl    = 1,
    PageNavigation = 2,
    PagePlayback   = 3,
    PageCount      = 4,
} RemotePage;

static const char* const PAGE_NAMES[PageCount] = {
    "General",
    "Control",
    "Navigation",
    "Playback",
};

// Each page has 6 slots: Up, Left, Center(OK), Right, Down, Hold
// The Hold slot is shown separately at the right side with a HOLD label
typedef struct {
    const char* ir_name;  // passed to ir_transmit_named
    const char* label;    // short text drawn inside the button box
} SlotDef;

static const SlotDef PAGE_SLOTS[PageCount][6] = {
    // General: Power center, Input left, Setup down, Menu right, Mute up, hard-power hold
    [PageGeneral] = {
        {"Mute",   "MUTE" },  // up
        {"Input",  "INPUT"},  // left
        {"Power",  "PWR"  },  // center (OK)
        {"Menu",   "MENU" },  // right
        {"Setup",  "SETUP"},  // down
        {"Power",  "PWR"  },  // hold
    },
    // Control: Ch+/- and Vol+/- around play/pause center, Mute hold
    [PageControl] = {
        {"Ch_next", "CH+" },   // up
        {"Vol_dn",  "VOL-"},   // left
        {"Ok",      ">||" },   // center
        {"Vol_up",  "VOL+"},   // right
        {"Ch_prev", "CH-" },   // down
        {"Mute",    "MUTE"},   // hold
    },
    // Navigation: arrow cross with OK center, Back hold
    [PageNavigation] = {
        {"Up",    "UP"   },  // up
        {"Left",  "LEFT" },  // left
        {"Ok",    "OK"   },  // center
        {"Right", "RIGHT"},  // right
        {"Down",  "DOWN" },  // down
        {"Back",  "BACK" },  // hold
    },
    // Playback: skip-fwd up, rew left, play center, ffw right, skip-back down, stop hold
    [PagePlayback] = {
        {"Next",    "|>>" },  // up
        {"Rewind",  "<<"  },  // left
        {"Play",    ">"   },  // center
        {"FastFwd", ">>"  },  // right
        {"Prev",    "<<|" },  // down
        {"Stop",    "[]"  },  // hold
    },
};

// ---------------------------------------------------------------------------
// View model
// ---------------------------------------------------------------------------
typedef struct {
    char       model_name[MAX_NAME_LEN];
    RemotePage page;
    char       last_action[MAX_NAME_LEN];
    bool       last_ok;
    bool       show_flash;
} RemoteModel;

struct IrRemoteView {
    View*    view;
    Storage* storage;
    char     path[MAX_PATH_LEN];
    void   (*back_cb)(void*);
    void*    back_ctx;
};

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// Rounded button box with centred label — matches XRemote button style
static void draw_btn(Canvas* c, int x, int y, int w, int h, const char* lbl) {
    canvas_draw_rframe(c, x, y, w, h, 3);
    canvas_set_font(c, FontSecondary);
    // Centre text vertically in the box (FontSecondary baseline ≈ 7px above bottom)
    canvas_draw_str_aligned(c, x + w / 2, y + (h / 2) - 3, AlignCenter, AlignTop, lbl);
}

static void remote_draw(Canvas* canvas, void* _m) {
    RemoteModel* m = _m;
    canvas_clear(canvas);

    // ------------------------------------------------------------------
    // Header (top-right) — "XRemote" bold + page name below, like screenshots
    // ------------------------------------------------------------------
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 127, 0, AlignRight, AlignTop, "XRemote");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 127, 11, AlignRight, AlignTop, PAGE_NAMES[m->page]);

    // ------------------------------------------------------------------
    // 5-button cross (left side) — same geometry as XRemote screenshots
    //   Button size: 20 wide × 13 tall, gap: 3px
    //   Cross occupies columns 0-65, rows 0-42
    // ------------------------------------------------------------------
    const int BW = 20, BH = 13, GAP = 3;
    const int col0 = 0;
    const int col1 = col0 + BW + GAP;  // centre column x
    const int col2 = col1 + BW + GAP;  // right column x
    const int row0 = 1;
    const int row1 = row0 + BH + GAP;  // middle row y
    const int row2 = row1 + BH + GAP;  // bottom row y

    const SlotDef* s = PAGE_SLOTS[m->page];

    draw_btn(canvas, col1, row0, BW, BH, s[0].label);  // up
    draw_btn(canvas, col0, row1, BW, BH, s[1].label);  // left
    draw_btn(canvas, col1, row1, BW, BH, s[2].label);  // center
    draw_btn(canvas, col2, row1, BW, BH, s[3].label);  // right
    draw_btn(canvas, col1, row2, BW, BH, s[4].label);  // down

    // ------------------------------------------------------------------
    // Hold indicator (right side) — box + "HOLD" label below, like XRemote
    // ------------------------------------------------------------------
    const int hx = 88, hy = 20;
    canvas_draw_rframe(canvas, hx, hy, 28, 13, 3);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, hx + 14, hy + 2, AlignCenter, AlignTop, s[5].label);
    canvas_draw_str_aligned(canvas, hx + 14, hy + 16, AlignCenter, AlignTop, "HOLD");

    // ------------------------------------------------------------------
    // Bottom bar — flash feedback OR "< Press to exit"
    // ------------------------------------------------------------------
    canvas_set_font(canvas, FontSecondary);
    if(m->show_flash && m->last_action[0]) {
        char line[MAX_NAME_LEN + 16];
        snprintf(line, sizeof(line), "%s %s",
                 m->last_action, m->last_ok ? "sent" : "FAIL");
        canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, line);
    } else {
        canvas_draw_str(canvas, 1, 63, "< Press to exit");
    }

    // ------------------------------------------------------------------
    // Page indicator dots (centre-bottom) — filled = active
    // ------------------------------------------------------------------
    for(int i = 0; i < PageCount; i++) {
        int px = 52 + i * 7;
        int py = 57;
        if(i == (int)m->page) {
            canvas_draw_box(canvas, px, py, 4, 4);
        } else {
            canvas_draw_frame(canvas, px, py, 4, 4);
        }
    }
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

static void transmit_slot(IrRemoteView* r, int slot_idx) {
    RemotePage page = PageGeneral;
    with_view_model(r->view, RemoteModel * m, { page = m->page; }, false);

    const SlotDef* s = &PAGE_SLOTS[page][slot_idx];
    bool ok = ir_transmit_named(r->storage, r->path, s->ir_name);

    with_view_model(
        r->view, RemoteModel * m,
        {
            strncpy(m->last_action, s->ir_name, sizeof(m->last_action) - 1);
            m->last_action[sizeof(m->last_action) - 1] = '\0';
            m->last_ok  = ok;
            m->show_flash = true;
        },
        true);
}

static bool remote_input(InputEvent* event, void* ctx) {
    IrRemoteView* r = ctx;

    if(event->type == InputTypeShort) {
        switch(event->key) {
        case InputKeyUp:    transmit_slot(r, 0); return true;
        case InputKeyLeft:  transmit_slot(r, 1); return true;
        case InputKeyOk:    transmit_slot(r, 2); return true;
        case InputKeyRight: transmit_slot(r, 3); return true;
        case InputKeyDown:  transmit_slot(r, 4); return true;
        case InputKeyBack:
            if(r->back_cb) r->back_cb(r->back_ctx);
            return true;
        default: break;
        }
    } else if(event->type == InputTypeLong) {
        switch(event->key) {
        // Any long-press on a directional or OK → fire the HOLD slot
        case InputKeyUp:
        case InputKeyLeft:
        case InputKeyOk:
        case InputKeyRight:
        case InputKeyDown:
            transmit_slot(r, 5);
            return true;
        // Long Back = cycle to next page (like XRemote's page switching)
        case InputKeyBack:
            with_view_model(
                r->view, RemoteModel * m,
                {
                    m->page = (RemotePage)((m->page + 1) % PageCount);
                    m->show_flash = false;
                    m->last_action[0] = '\0';
                },
                true);
            return true;
        default: break;
        }
    } else if(event->type == InputTypeRepeat) {
        // Auto-repeat on directionals for held buttons
        switch(event->key) {
        case InputKeyUp:    transmit_slot(r, 0); return true;
        case InputKeyLeft:  transmit_slot(r, 1); return true;
        case InputKeyRight: transmit_slot(r, 3); return true;
        case InputKeyDown:  transmit_slot(r, 4); return true;
        default: break;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

IrRemoteView* remote_view_alloc(Storage* storage) {
    IrRemoteView* r = malloc(sizeof(IrRemoteView));
    r->storage  = storage;
    r->path[0]  = '\0';
    r->back_cb  = NULL;
    r->back_ctx = NULL;

    r->view = view_alloc();
    view_allocate_model(r->view, ViewModelTypeLocking, sizeof(RemoteModel));
    view_set_context(r->view, r);
    view_set_draw_callback(r->view, remote_draw);
    view_set_input_callback(r->view, remote_input);
    return r;
}

void remote_view_free(IrRemoteView* r) {
    view_free(r->view);
    free(r);
}

View* remote_view_get(IrRemoteView* r) {
    return r->view;
}

void remote_view_set_model(IrRemoteView* r, const char* path) {
    strncpy(r->path, path, sizeof(r->path) - 1);
    r->path[sizeof(r->path) - 1] = '\0';

    const char* slash = strrchr(path, '/');
    const char* base  = slash ? slash + 1 : path;

    with_view_model(
        r->view, RemoteModel * m,
        {
            strncpy(m->model_name, base, sizeof(m->model_name) - 1);
            m->model_name[sizeof(m->model_name) - 1] = '\0';
            char* dot = strrchr(m->model_name, '.');
            if(dot) *dot = '\0';
            m->page        = PageGeneral;
            m->last_action[0] = '\0';
            m->last_ok     = false;
            m->show_flash  = false;
        },
        true);
}

void remote_view_set_back_callback(IrRemoteView* r, void (*cb)(void*), void* ctx) {
    r->back_cb  = cb;
    r->back_ctx = ctx;
}
