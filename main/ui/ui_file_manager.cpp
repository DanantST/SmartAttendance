/**
 * @file ui_file_manager.cpp
 * @brief File manager application screen with full subdirectory navigation
 *        and pagination to prevent LVGL WDT crashes on large directories.
 */

#include "ui_file_manager.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "esp_log.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display/lcd_driver.h"
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static const char* TAG = "UI_FILE_MGR";
static lv_obj_t* s_file_manager_screen = NULL;
static lv_obj_t* s_file_list = NULL;
static lv_obj_t* s_title_label = NULL;
static lv_obj_t* s_page_label = NULL;

static char s_current_dir[512] = "/sdcard";

/* Pagination: show at most PAGE_SIZE entries at a time */
#define MAX_FILE_ENTRIES  200
#define PAGE_SIZE          20

struct FileEntry {
    char name[128];
    bool is_dir;
    long size_kb;
};

/* Heap-allocated entry cache populated by the loader task */
static FileEntry* s_entries     = NULL;
static int        s_entry_count = 0;
static int        s_page        = 0;   /* current page (0-based) */

/* Static name storage for LVGL event user-data (must outlive the widget) */
static char s_entry_names[PAGE_SIZE + 2][128];  /* +2 for ".." and page buttons */

/* ---- Forward declarations ---- */
static void render_current_page(void);
static lv_obj_t* add_list_item(lv_obj_t* parent, const char* icon, const char* text);
static void file_manager_load_task(void* param);

/* ---- Directory-click handler ---- */
static void dir_click_handler(lv_event_t* e) {
    const char* name = (const char*)lv_event_get_user_data(e);
    if (!name) return;

    if (strcmp(name, "..") == 0) {
        char* last_slash = strrchr(s_current_dir, '/');
        if (last_slash && last_slash != s_current_dir && strcmp(s_current_dir, "/sdcard") != 0) {
            *last_slash = '\0';
        } else {
            strcpy(s_current_dir, "/sdcard");
        }
    } else {
        char new_path[1024];
        snprintf(new_path, sizeof(new_path), "%s/%s", s_current_dir, name);
        strncpy(s_current_dir, new_path, sizeof(s_current_dir) - 1);
        s_current_dir[sizeof(s_current_dir) - 1] = '\0';
    }

    /* Reset page and reload directory */
    s_page = 0;
    if (s_entries) { free(s_entries); s_entries = NULL; }
    s_entry_count = 0;

    if (ui_lock()) {
        if (s_title_label)
            lv_label_set_text_fmt(s_title_label, "File Manager: %s", s_current_dir);
        if (s_file_list) {
            lv_obj_clean(s_file_list);
            lv_obj_t* loading = lv_label_create(s_file_list);
            lv_label_set_text(loading, "Loading files...");
            lv_obj_set_style_text_color(loading, lv_color_hex(0xBBBBBB), 0);
        }
        ui_unlock();
    }

    xTaskCreate([](void* p) {
        FileEntry* entries = (FileEntry*)malloc(sizeof(FileEntry) * MAX_FILE_ENTRIES);
        int count = 0;
        bool success = false;

        if (entries) {
            DIR* dir = opendir(s_current_dir);
            if (dir) {
                success = true;
                struct dirent* ent;
                while ((ent = readdir(dir)) != NULL && count < MAX_FILE_ENTRIES) {
                    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
                    strncpy(entries[count].name, ent->d_name, sizeof(entries[0].name) - 1);
                    entries[count].name[sizeof(entries[0].name) - 1] = '\0';
                    char path[1024];
                    snprintf(path, sizeof(path), "%s/%s", s_current_dir, ent->d_name);
                    struct stat st; memset(&st, 0, sizeof(st));
                    if (stat(path, &st) == 0) {
                        entries[count].is_dir  = S_ISDIR(st.st_mode);
                        entries[count].size_kb = (long)(st.st_size / 1024);
                    } else {
                        entries[count].is_dir  = false;
                        entries[count].size_kb = 0;
                    }
                    count++;
                }
                closedir(dir);
            }
        }

        if (!success && entries) { free(entries); entries = NULL; }
        s_entries     = entries;
        s_entry_count = success ? count : 0;

        if (ui_lock()) {
            if (s_file_list) {
                lv_obj_clean(s_file_list);
                if (!success) {
                    lv_obj_t* err = lv_label_create(s_file_list);
                    lv_label_set_text_fmt(err, "Failed to open folder:\n%s", s_current_dir);
                    lv_obj_set_style_text_color(err, lv_color_hex(0xFF4444), 0);
                } else {
                    render_current_page();
                }
            }
            ui_unlock();
        }
        vTaskDelete(NULL);
    }, "fm_dir_nav", 8192, NULL, 5, NULL);
}

/* ---- Pagination button handlers ---- */
static void next_page_handler(lv_event_t* e) {
    int total_pages = (s_entry_count + PAGE_SIZE - 1) / PAGE_SIZE;
    if (s_page + 1 < total_pages) {
        s_page++;
        if (ui_lock()) {
            lv_obj_clean(s_file_list);
            render_current_page();
            ui_unlock();
        }
    }
}

static void prev_page_handler(lv_event_t* e) {
    if (s_page > 0) {
        s_page--;
        if (ui_lock()) {
            lv_obj_clean(s_file_list);
            render_current_page();
            ui_unlock();
        }
    }
}

static void delete_click_handler(lv_event_t* e) {
    const char* name = (const char*)lv_event_get_user_data(e);
    if (!name) return;

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", s_current_dir, name);

    ESP_LOGI(TAG, "Deleting path: %s", path);

    struct stat st;
    if (stat(path, &st) == 0) {
        int ret;
        if (S_ISDIR(st.st_mode)) {
            ret = rmdir(path);
        } else {
            ret = remove(path);
        }
        if (ret == 0) {
            ui_show_notification(NOTIFY_SUCCESS, "Deleted", "Item deleted successfully", 2000);
        } else {
            ui_show_notification(NOTIFY_ERROR, "Delete Failed", "Could not delete (folder might not be empty)", 3000);
        }
    } else {
        ui_show_notification(NOTIFY_ERROR, "Delete Failed", "Item does not exist", 2000);
    }

    /* Reload current directory/page */
    s_page = 0;
    if (s_entries) { free(s_entries); s_entries = NULL; }
    s_entry_count = 0;

    if (s_file_list) {
        lv_obj_clean(s_file_list);
        lv_obj_t* loading = lv_label_create(s_file_list);
        lv_label_set_text(loading, "Refreshing...");
        lv_obj_set_style_text_color(loading, lv_color_hex(0xBBBBBB), 0);
    }

    // Trigger loader task again to refresh list
    xTaskCreate(file_manager_load_task, "fm_loader", 8192, NULL, 5, NULL);
}

/* ---- Render one page of entries (call under ui_lock) ---- */
static void render_current_page(void) {
    if (!s_file_list) return;

    /* "Go Up" button */
    if (strcmp(s_current_dir, "/sdcard") != 0) {
        lv_obj_t* btn = add_list_item(s_file_list, LV_SYMBOL_UP, "Go Up (..)");
        lv_obj_add_event_cb(btn, dir_click_handler, LV_EVENT_CLICKED, (void*)"..");
    }

    int total_pages = (s_entry_count + PAGE_SIZE - 1) / PAGE_SIZE;
    if (s_entry_count == 0) {
        lv_obj_t* lbl = lv_label_create(s_file_list);
        lv_label_set_text(lbl, "No files or folders found.");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);
    } else {
        int start = s_page * PAGE_SIZE;
        int end   = start + PAGE_SIZE;
        if (end > s_entry_count) end = s_entry_count;

        /* Render only this page's slice — keeps LVGL render time short */
        for (int i = start; i < end; i++) {
            int slot = i - start;  /* index into s_entry_names (0..PAGE_SIZE-1) */
            const char* icon = s_entries[i].is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE;
            strncpy(s_entry_names[slot], s_entries[i].name, sizeof(s_entry_names[0]) - 1);
            s_entry_names[slot][sizeof(s_entry_names[0]) - 1] = '\0';

            if (s_entries[i].is_dir) {
                lv_obj_t* btn = add_list_item(s_file_list, icon, s_entries[i].name);
                lv_obj_add_event_cb(btn, dir_click_handler, LV_EVENT_CLICKED, (void*)s_entry_names[slot]);
                
                // Set flex grow on the text label to push the delete button to the right
                lv_obj_t* text_lbl = lv_obj_get_child(btn, 1);
                if (text_lbl) lv_obj_set_flex_grow(text_lbl, 1);

                // Add small red delete button on the right
                lv_obj_t* del_btn = lv_btn_create(btn);
                lv_obj_set_size(del_btn, 40, 36);
                lv_obj_set_style_bg_color(del_btn, lv_color_hex(0xFF4444), 0);
                lv_obj_t* del_lbl = lv_label_create(del_btn);
                lv_label_set_text(del_lbl, LV_SYMBOL_TRASH);
                lv_obj_center(del_lbl);
                lv_obj_add_event_cb(del_btn, delete_click_handler, LV_EVENT_CLICKED, (void*)s_entry_names[slot]);
            } else {
                char title_str[256];
                snprintf(title_str, sizeof(title_str), "%s (%ld KB)", s_entries[i].name, s_entries[i].size_kb);
                lv_obj_t* btn = add_list_item(s_file_list, icon, title_str);
                
                // Set flex grow on the text label to push the delete button to the right
                lv_obj_t* text_lbl = lv_obj_get_child(btn, 1);
                if (text_lbl) lv_obj_set_flex_grow(text_lbl, 1);

                // Add small red delete button on the right
                lv_obj_t* del_btn = lv_btn_create(btn);
                lv_obj_set_size(del_btn, 40, 36);
                lv_obj_set_style_bg_color(del_btn, lv_color_hex(0xFF4444), 0);
                lv_obj_t* del_lbl = lv_label_create(del_btn);
                lv_label_set_text(del_lbl, LV_SYMBOL_TRASH);
                lv_obj_center(del_lbl);
                lv_obj_add_event_cb(del_btn, delete_click_handler, LV_EVENT_CLICKED, (void*)s_entry_names[slot]);
            }
        }
    }

    /* Pagination navigation row */
    if (total_pages > 1) {
        /* Prev button */
        lv_obj_t* prev_btn = add_list_item(s_file_list, LV_SYMBOL_LEFT,
            s_page > 0 ? "< Prev Page" : "(No previous page)");
        if (s_page > 0)
            lv_obj_add_event_cb(prev_btn, prev_page_handler, LV_EVENT_CLICKED, NULL);

        /* Page counter label */
        if (s_page_label) {
            lv_label_set_text_fmt(s_page_label, "Page %d / %d  (%d files)",
                s_page + 1, total_pages, s_entry_count);
        }

        /* Next button */
        lv_obj_t* next_btn = add_list_item(s_file_list, LV_SYMBOL_RIGHT,
            (s_page + 1 < total_pages) ? "Next Page >" : "(No next page)");
        if (s_page + 1 < total_pages)
            lv_obj_add_event_cb(next_btn, next_page_handler, LV_EVENT_CLICKED, NULL);
    } else if (s_page_label) {
        lv_label_set_text_fmt(s_page_label, "%d file(s)", s_entry_count);
    }
}

/* ---- Helper: add list item using plain lv_btn instead of unstable lv_list ---- */
static lv_obj_t* add_list_item(lv_obj_t* parent, const char* icon, const char* text) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, lv_pct(100), 50);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Minimal styling to avoid complex render buffers
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_pad_all(btn, 8, 0);
    lv_obj_set_style_pad_column(btn, 12, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    if (icon) {
        lv_obj_t* icon_label = lv_label_create(btn);
        lv_label_set_text(icon_label, icon);
        lv_obj_set_style_text_color(icon_label, lv_color_hex(0xFFB74D), 0); // Warm amber
    }

    lv_obj_t* text_label = lv_label_create(btn);
    lv_label_set_text(text_label, text);
    lv_obj_set_style_text_color(text_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(text_label, &lv_font_montserrat_14, 0);

    return btn;
}

/* ---- Initial load task (stack: 8192) ---- */
static void file_manager_load_task(void* param) {
    FileEntry* entries = (FileEntry*)malloc(sizeof(FileEntry) * MAX_FILE_ENTRIES);
    int count   = 0;
    bool success = false;

    if (entries) {
        DIR* dir = opendir(s_current_dir);
        if (dir) {
            success = true;
            struct dirent* ent;
            while ((ent = readdir(dir)) != NULL && count < MAX_FILE_ENTRIES) {
                if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
                strncpy(entries[count].name, ent->d_name, sizeof(entries[0].name) - 1);
                entries[count].name[sizeof(entries[0].name) - 1] = '\0';
                char path[1024];
                snprintf(path, sizeof(path), "%s/%s", s_current_dir, ent->d_name);
                struct stat st; memset(&st, 0, sizeof(st));
                if (stat(path, &st) == 0) {
                    entries[count].is_dir  = S_ISDIR(st.st_mode);
                    entries[count].size_kb = (long)(st.st_size / 1024);
                } else {
                    entries[count].is_dir  = false;
                    entries[count].size_kb = 0;
                }
                count++;
            }
            closedir(dir);
        }
    }

    if (!success && entries) { free(entries); entries = NULL; }
    s_entries     = entries;
    s_entry_count = success ? count : 0;

    if (ui_lock()) {
        if (s_file_list) {
            lv_obj_clean(s_file_list);
            if (!success) {
                lv_obj_t* err = lv_label_create(s_file_list);
                lv_label_set_text_fmt(err, "Failed to open folder:\n%s", s_current_dir);
                lv_obj_set_style_text_color(err, lv_color_hex(0xFF4444), 0);
            } else {
                render_current_page();
            }
        }
        ui_unlock();
    }
    vTaskDelete(NULL);
}

/* ---- Public: Open File Manager ---- */
extern "C" void ui_show_file_manager_screen(void) {
    strcpy(s_current_dir, "/sdcard");
    s_page = 0;
    if (s_entries) { free(s_entries); s_entries = NULL; }
    s_entry_count = 0;

    if (s_file_manager_screen) {
        if (s_title_label)
            lv_label_set_text_fmt(s_title_label, "File Manager: %s", s_current_dir);
        lv_scr_load(s_file_manager_screen);
        /* Re-launch load for fresh listing */
        xTaskCreate(file_manager_load_task, "fm_loader", 8192, NULL, 5, NULL);
        return;
    }
    ESP_LOGI(TAG, "Opening File Manager");

    s_file_manager_screen = lv_obj_create(NULL);
    ui_add_double_tap_to_screen(s_file_manager_screen);
    lv_obj_set_style_bg_color(s_file_manager_screen, lv_color_hex(0x121212), 0);

    /* Title bar */
    lv_obj_t* title_bar = lv_obj_create(s_file_manager_screen);
    lv_obj_set_size(title_bar, lv_pct(100), 50);
    lv_obj_set_pos(title_bar, 0, 40);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 0, 0);

    s_title_label = lv_label_create(title_bar);
    lv_label_set_text_fmt(s_title_label, "File Manager: %s", s_current_dir);
    lv_obj_set_pos(s_title_label, 20, 12);
    lv_obj_set_style_text_color(s_title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_14, 0);

    /* Page info label (below title bar) */
    s_page_label = lv_label_create(s_file_manager_screen);
    lv_label_set_text(s_page_label, "Loading...");
    lv_obj_set_pos(s_page_label, 20, 96);
    lv_obj_set_style_text_color(s_page_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(s_page_label, &lv_font_montserrat_12, 0);

    /* File list widget: Use plain lv_obj with flex to avoid unstable lv_list */
    s_file_list = lv_obj_create(s_file_manager_screen);
    lv_obj_set_size(s_file_list, 1024 - 40, 600 - 180);
    lv_obj_set_pos(s_file_list, 20, 115);
    lv_obj_set_flex_flow(s_file_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(s_file_list, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_width(s_file_list, 0, 0);
    lv_obj_set_style_pad_all(s_file_list, 10, 0);
    lv_obj_set_style_pad_row(s_file_list, 6, 0);
    lv_obj_set_style_radius(s_file_list, 8, 0);

    lv_obj_t* loading_lbl = lv_label_create(s_file_list);
    lv_label_set_text(loading_lbl, "Loading files from SD card...");
    lv_obj_set_style_text_color(loading_lbl, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_font(loading_lbl, &lv_font_montserrat_14, 0);

    /* Launch background load at safe stack size */
    xTaskCreate(file_manager_load_task, "fm_loader", 8192, NULL, 5, NULL);

    lv_scr_load(s_file_manager_screen);
}

/* ---- Public: Close File Manager ---- */
extern "C" void ui_close_file_manager_screen(void) {
    if (!s_file_manager_screen) return;

    /* Free entry cache */
    if (s_entries) { free(s_entries); s_entries = NULL; }
    s_entry_count = 0;

    s_file_list   = NULL;
    s_title_label = NULL;
    s_page_label  = NULL;
    lv_obj_del(s_file_manager_screen);
    s_file_manager_screen = NULL;

    ui_return_to_main();
}
