#include "tvbgone_xremote.h"
#include <string.h>

void fs_list_dir(
    Storage* storage,
    const char* path,
    bool dirs_only,
    const char* ext_filter,
    FsEntryCb on_entry,
    void* ctx) {
    furi_assert(storage);
    furi_assert(path);
    furi_assert(on_entry);

    File* dir = storage_file_alloc(storage);
    if(!storage_dir_open(dir, path)) {
        storage_dir_close(dir);
        storage_file_free(dir);
        return;
    }

    FileInfo info;
    char name[256];
    uint32_t idx = 0;
    while(storage_dir_read(dir, &info, name, sizeof(name))) {
        // Skip dot-entries and any empty result.
        if(name[0] == '\0' || name[0] == '.') continue;

        bool is_dir = file_info_is_dir(&info);
        if(dirs_only && !is_dir) continue;
        if(!dirs_only && is_dir) continue;

        if(!dirs_only && ext_filter) {
            size_t nlen = strlen(name);
            size_t elen = strlen(ext_filter);
            if(nlen < elen) continue;
            if(strcasecmp(name + nlen - elen, ext_filter) != 0) continue;
        }

        if(!on_entry(ctx, name, idx)) break;
        idx++;
    }

    storage_dir_close(dir);
    storage_file_free(dir);
}
