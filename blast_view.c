#include "tvbgone_xremote.h"
#include <gui/elements.h>
#include <input/input.h>

#define INTER_SIGNAL_DELAY_MS 150

typedef struct {
    uint32_t sent;
    char last_name[MAX_NAME_LEN];
    bool running;
    bool finished;
    bool aborted;
} BlastModel;

struct IrBlast {
    View* view;
    FuriThread* thread;
    Storage* storage;
    volatile bool stop_flag;
    void (*back_cb)(void*);
    void* back_ctx;
};

static void blast_draw(Canvas* canvas, void* _m) {
    BlastModel* m = _m;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 4, AlignCenter, AlignTop, "TV-B-Gone Blast");

    canvas_set_font(canvas, FontSecondary);
    // Size to fit any prefix + a full MAX_NAME_LEN (64) input + null,
    // so -Wformat-truncation can prove safety.
    char line[MAX_NAME_LEN + 32];
    snprintf(line, sizeof(line), "Signals sent: %lu", (unsigned long)m->sent);
    canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignTop, line);

    if(m->last_name[0]) {
        snprintf(line, sizeof(line), "Last: %s", m->last_name);
        canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignTop, line);
    }

    if(m->finished) {
        canvas_draw_str_aligned(
            canvas, 64, 48, AlignCenter, AlignTop,
            m->aborted ? "Aborted - Back to exit" : "Done - Back to exit");
    } else if(m->running) {
        canvas_draw_str_aligned(canvas, 64, 48, AlignCenter, AlignTop, "Hold Back to stop");
    }
}

static bool blast_input(InputEvent* event, void* ctx) {
    IrBlast* b = ctx;
    if(event->type == InputTypeShort && event->key == InputKeyBack) {
        bool finished = false;
        with_view_model(
            b->view, BlastModel * m, { finished = m->finished; }, false);
        if(finished && b->back_cb) b->back_cb(b->back_ctx);
        return true;
    }
    if(event->type == InputTypeLong && event->key == InputKeyBack) {
        b->stop_flag = true;
        return true;
    }
    return false;
}

static bool on_progress(void* ctx, uint32_t n, const char* button_name) {
    IrBlast* b = ctx;
    with_view_model(
        b->view, BlastModel * m,
        {
            m->sent = n;
            strncpy(m->last_name, button_name, sizeof(m->last_name) - 1);
            m->last_name[sizeof(m->last_name) - 1] = '\0';
        },
        true);
    return !b->stop_flag;
}

static int32_t blast_thread(void* ctx) {
    IrBlast* b = ctx;

    // Stock universal first (highest hit rate, smallest list)
    ir_blast_all_power(
        b->storage, UNIVERSAL_TV_PATH, INTER_SIGNAL_DELAY_MS, on_progress, b);

    // Then the extended list (everything sync_tv_codes.py harvested from IRDB).
    // If the file doesn't exist, ir_blast_all_power just returns 0 quietly.
    if(!b->stop_flag) {
        ir_blast_all_power(
            b->storage, EXTENDED_TV_PATH, INTER_SIGNAL_DELAY_MS, on_progress, b);
    }

    with_view_model(
        b->view, BlastModel * m,
        {
            m->finished = true;
            m->aborted = b->stop_flag;
            m->running = false;
        },
        true);
    return 0;
}

static void blast_enter(void* ctx) {
    IrBlast* b = ctx;
    b->stop_flag = false;
    with_view_model(
        b->view, BlastModel * m,
        {
            m->sent = 0;
            m->last_name[0] = '\0';
            m->running = true;
            m->finished = false;
            m->aborted = false;
        },
        true);
    furi_thread_start(b->thread);
}

static void blast_exit(void* ctx) {
    IrBlast* b = ctx;
    b->stop_flag = true;
    furi_thread_join(b->thread);
}

IrBlast* blast_view_alloc(Storage* storage) {
    IrBlast* b = malloc(sizeof(IrBlast));
    b->storage = storage;
    b->stop_flag = false;
    b->back_cb = NULL;
    b->back_ctx = NULL;

    b->view = view_alloc();
    view_allocate_model(b->view, ViewModelTypeLocking, sizeof(BlastModel));
    view_set_context(b->view, b);
    view_set_draw_callback(b->view, blast_draw);
    view_set_input_callback(b->view, blast_input);
    view_set_enter_callback(b->view, blast_enter);
    view_set_exit_callback(b->view, blast_exit);

    b->thread = furi_thread_alloc_ex("TVBGoneBlast", 2048, blast_thread, b);
    return b;
}

void blast_view_free(IrBlast* b) {
    furi_thread_free(b->thread);
    view_free(b->view);
    free(b);
}

View* blast_view_get(IrBlast* b) {
    return b->view;
}

void blast_view_set_back_callback(IrBlast* b, void (*cb)(void*), void* ctx) {
    b->back_cb = cb;
    b->back_ctx = ctx;
}
