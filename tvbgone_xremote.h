#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <storage/storage.h>
#include <infrared_transmit.h>
#include <infrared.h>
#include <flipper_format/flipper_format.h>

#define TAG "TVBGoneXRemote"

// Flipper-IRDB layout (sync tool drops files here)
#define IRDB_TV_PATH       EXT_PATH("infrared/TVs")
// Stock universal TV asset shipped with the firmware
#define UNIVERSAL_TV_PATH  EXT_PATH("infrared/assets/tv.ir")
// Extended blast list built by the sync tool from every Power signal in IRDB
#define EXTENDED_TV_PATH   EXT_PATH("apps_data/tvbgone_xremote/tv_extended.ir")

#define MAX_BRANDS         128
#define MAX_MODELS         256
#define MAX_PATH_LEN       512
#define MAX_NAME_LEN       64

typedef enum {
    ViewIdMainMenu,
    ViewIdBrandSelect,
    ViewIdModelSelect,
    ViewIdBlast,
    ViewIdRemote,
    ViewIdAbout,
} ViewId;

typedef enum {
    MainMenuBlast,
    MainMenuBrowse,
    MainMenuAbout,
} MainMenuIndex;

typedef struct IrBlast IrBlast;
typedef struct IrRemoteView IrRemoteView;

typedef struct {
    Gui* gui;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    Submenu* main_menu;
    Submenu* brand_menu;
    Submenu* model_menu;
    Widget* about;
    IrBlast* blast;
    IrRemoteView* remote;

    // Browse state (kept so Back from remote → model list goes to right brand)
    char current_brand[MAX_NAME_LEN];
    char current_model_path[MAX_PATH_LEN];
} App;

// ---- IR helpers (tvbgone_ir.c) -----------------------------------------------
typedef bool (*IrBlastProgress)(void* ctx, uint32_t signal_num, const char* button_name);

// Transmit a single named button from an .ir file. Uses alt-name fallback so
// "POWER", "shutdown", "off", etc. all match a request for "Power".
bool ir_transmit_named(Storage* storage, const char* path, const char* button_name);

// Walk an .ir file and transmit every signal whose name matches "Power"
// (via alt-names). Returns total signals sent. Pauses inter_signal_delay_ms
// between sends. If on_progress returns false, the walk aborts early.
uint32_t ir_blast_all_power(
    Storage* storage,
    const char* path,
    uint32_t inter_signal_delay_ms,
    IrBlastProgress on_progress,
    void* ctx);

// ---- FS helper (tvbgone_fs.c) ------------------------------------------------
typedef bool (*FsEntryCb)(void* ctx, const char* name, uint32_t index);

void fs_list_dir(
    Storage* storage,
    const char* path,
    bool dirs_only,
    const char* ext_filter,
    FsEntryCb on_entry,
    void* ctx);

// ---- views -------------------------------------------------------------------
IrBlast* blast_view_alloc(Storage* storage);
void blast_view_free(IrBlast* b);
View* blast_view_get(IrBlast* b);
void blast_view_set_back_callback(IrBlast* b, void (*cb)(void*), void* ctx);

IrRemoteView* remote_view_alloc(Storage* storage);
void remote_view_free(IrRemoteView* r);
View* remote_view_get(IrRemoteView* r);
void remote_view_set_model(IrRemoteView* r, const char* path);
void remote_view_set_back_callback(IrRemoteView* r, void (*cb)(void*), void* ctx);

// ---- scene transitions (tvbgone_xremote.c) ----------------------------------
void scene_show_main_menu(App* app);
void scene_show_brand_select(App* app);
void scene_show_model_select(App* app, const char* brand);
void scene_show_blast(App* app);
void scene_show_remote(App* app, const char* model_path);
void scene_show_about(App* app);
