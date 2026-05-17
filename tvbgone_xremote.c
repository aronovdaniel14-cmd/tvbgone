#include "tvbgone_xremote.h"
#include <string.h>

// Tracks which view is currently displayed, so the navigation callback can
// decide between "go back one level" and "exit the app".
static ViewId g_current_view = ViewIdMainMenu;

// ---------------------------------------------------------------------------
// Main menu
// ---------------------------------------------------------------------------

static void main_menu_cb(void* ctx, uint32_t index) {
    App* app = ctx;
    switch(index) {
    case MainMenuBlast:  scene_show_blast(app); break;
    case MainMenuBrowse: scene_show_brand_select(app); break;
    case MainMenuAbout:  scene_show_about(app); break;
    }
}

void scene_show_main_menu(App* app) {
    submenu_reset(app->main_menu);
    submenu_set_header(app->main_menu, "TV-B-Gone XRemote");
    submenu_add_item(app->main_menu, "Blast All TVs",   MainMenuBlast,  main_menu_cb, app);
    submenu_add_item(app->main_menu, "Browse by Brand", MainMenuBrowse, main_menu_cb, app);
    submenu_add_item(app->main_menu, "About",           MainMenuAbout,  main_menu_cb, app);
    g_current_view = ViewIdMainMenu;
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewIdMainMenu);
}

// ---------------------------------------------------------------------------
// Brand select
// ---------------------------------------------------------------------------

typedef struct {
    char names[MAX_BRANDS][MAX_NAME_LEN];
    uint32_t count;
} BrandList;

static BrandList g_brands;

static bool brand_collect(void* ctx, const char* name, uint32_t idx) {
    BrandList* bl = ctx;
    if(idx >= MAX_BRANDS) return false;
    strncpy(bl->names[idx], name, MAX_NAME_LEN - 1);
    bl->names[idx][MAX_NAME_LEN - 1] = '\0';
    bl->count = idx + 1;
    return true;
}

static void brand_clicked(void* ctx, uint32_t index) {
    App* app = ctx;
    if(index >= g_brands.count) return;
    scene_show_model_select(app, g_brands.names[index]);
}

void scene_show_brand_select(App* app) {
    g_brands.count = 0;
    fs_list_dir(app->storage, IRDB_TV_PATH, true, NULL, brand_collect, &g_brands);

    submenu_reset(app->brand_menu);
    submenu_set_header(app->brand_menu, "Pick Brand");

    if(g_brands.count == 0) {
        // Show what path we checked so the user can find their typo. The submenu
        // truncates long entries but the start is what matters.
        submenu_add_item(app->brand_menu, "(empty - copy IRDB to:)", 0, NULL, NULL);
        submenu_add_item(app->brand_menu, IRDB_TV_PATH, 1, NULL, NULL);
    } else {
        for(uint32_t i = 0; i < g_brands.count; i++) {
            submenu_add_item(app->brand_menu, g_brands.names[i], i, brand_clicked, app);
        }
    }
    g_current_view = ViewIdBrandSelect;
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewIdBrandSelect);
}

// ---------------------------------------------------------------------------
// Model select
// ---------------------------------------------------------------------------

typedef struct {
    char brand_path[MAX_PATH_LEN];
    char names[MAX_MODELS][MAX_NAME_LEN];
    uint32_t count;
} ModelList;

static ModelList g_models;

static bool model_collect(void* ctx, const char* name, uint32_t idx) {
    ModelList* ml = ctx;
    if(idx >= MAX_MODELS) return false;
    strncpy(ml->names[idx], name, MAX_NAME_LEN - 1);
    ml->names[idx][MAX_NAME_LEN - 1] = '\0';
    ml->count = idx + 1;
    return true;
}

static void model_clicked(void* ctx, uint32_t index) {
    App* app = ctx;
    if(index >= g_models.count) return;

    // Destination is sized to MAX_PATH_LEN + MAX_NAME_LEN + 2 so gcc's static
    // -Wformat-truncation analysis can prove "%s/%s" always fits regardless of
    // input lengths. We still bounds-check the return value defensively.
    char path[MAX_PATH_LEN + MAX_NAME_LEN + 2];
    int n = snprintf(
        path, sizeof(path), "%s/%s", g_models.brand_path, g_models.names[index]);
    if(n <= 0 || (size_t)n >= sizeof(path)) return;
    scene_show_remote(app, path);
}

void scene_show_model_select(App* app, const char* brand) {
    strncpy(app->current_brand, brand, sizeof(app->current_brand) - 1);
    app->current_brand[sizeof(app->current_brand) - 1] = '\0';

    g_models.count = 0;
    int n = snprintf(
        g_models.brand_path, sizeof(g_models.brand_path),
        "%s/%s", IRDB_TV_PATH, brand);
    if(n <= 0 || (size_t)n >= sizeof(g_models.brand_path)) {
        // Brand path too long — bail back to brand select.
        scene_show_brand_select(app);
        return;
    }

    fs_list_dir(
        app->storage, g_models.brand_path, false, ".ir", model_collect, &g_models);

    submenu_reset(app->model_menu);
    submenu_set_header(app->model_menu, brand);

    if(g_models.count == 0) {
        submenu_add_item(app->model_menu, "(no models)", 0, NULL, NULL);
    } else {
        for(uint32_t i = 0; i < g_models.count; i++) {
            // Display label without the ".ir" suffix for readability
            char display[MAX_NAME_LEN];
            strncpy(display, g_models.names[i], sizeof(display) - 1);
            display[sizeof(display) - 1] = '\0';
            char* dot = strrchr(display, '.');
            if(dot) *dot = '\0';
            submenu_add_item(app->model_menu, display, i, model_clicked, app);
        }
    }
    g_current_view = ViewIdModelSelect;
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewIdModelSelect);
}

// ---------------------------------------------------------------------------
// Blast / Remote / About scene transitions
// ---------------------------------------------------------------------------

static void back_to_main(void* ctx) {
    scene_show_main_menu((App*)ctx);
}

static void back_to_model_list(void* ctx) {
    App* app = ctx;
    scene_show_model_select(app, app->current_brand);
}

void scene_show_blast(App* app) {
    blast_view_set_back_callback(app->blast, back_to_main, app);
    g_current_view = ViewIdBlast;
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewIdBlast);
}

void scene_show_remote(App* app, const char* model_path) {
    strncpy(app->current_model_path, model_path, sizeof(app->current_model_path) - 1);
    app->current_model_path[sizeof(app->current_model_path) - 1] = '\0';

    remote_view_set_model(app->remote, model_path);
    remote_view_set_back_callback(app->remote, back_to_model_list, app);
    g_current_view = ViewIdRemote;
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewIdRemote);
}

void scene_show_about(App* app) {
    widget_reset(app->about);
    widget_add_string_multiline_element(
        app->about, 64, 4, AlignCenter, AlignTop, FontPrimary, "TV-B-Gone XRemote");
    widget_add_string_multiline_element(
        app->about, 4, 22, AlignLeft, AlignTop, FontSecondary,
        "Blast: walks every Power\n"
        "signal in the universal TV\n"
        "DB and transmits each one.\n\n"
        "Browse: pick a brand and\n"
        "model from Flipper-IRDB to\n"
        "use as a full remote.");
    g_current_view = ViewIdAbout;
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewIdAbout);
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

static bool back_event(void* ctx) {
    App* app = ctx;
    switch(g_current_view) {
    case ViewIdMainMenu:
        // Already at top: returning false stops the dispatcher and the app exits.
        return false;
    case ViewIdBrandSelect:
    case ViewIdAbout:
    case ViewIdBlast:
    case ViewIdRemote:
        scene_show_main_menu(app);
        return true;
    case ViewIdModelSelect:
        scene_show_brand_select(app);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// App lifecycle
// ---------------------------------------------------------------------------

static App* app_alloc(void) {
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(*app));

    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(
        app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, back_event);

    app->main_menu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, ViewIdMainMenu, submenu_get_view(app->main_menu));

    app->brand_menu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, ViewIdBrandSelect, submenu_get_view(app->brand_menu));

    app->model_menu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, ViewIdModelSelect, submenu_get_view(app->model_menu));

    app->about = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, ViewIdAbout, widget_get_view(app->about));

    app->blast = blast_view_alloc(app->storage);
    view_dispatcher_add_view(
        app->view_dispatcher, ViewIdBlast, blast_view_get(app->blast));

    app->remote = remote_view_alloc(app->storage);
    view_dispatcher_add_view(
        app->view_dispatcher, ViewIdRemote, remote_view_get(app->remote));

    return app;
}

static void app_free(App* app) {
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdMainMenu);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdBrandSelect);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdModelSelect);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdAbout);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdBlast);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdRemote);

    submenu_free(app->main_menu);
    submenu_free(app->brand_menu);
    submenu_free(app->model_menu);
    widget_free(app->about);
    blast_view_free(app->blast);
    remote_view_free(app->remote);

    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    free(app);
}

int32_t tvbgone_xremote_app(void* p) {
    UNUSED(p);
    App* app = app_alloc();
    scene_show_main_menu(app);
    view_dispatcher_run(app->view_dispatcher);
    app_free(app);
    return 0;
}
