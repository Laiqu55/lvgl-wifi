/*
 * wifi_ui.c — LVGL v8 WiFi settings screen
 *
 * Screen layout (800 × 480):
 *
 *   ┌──────────────────────────────────────────────────────┐
 *   │  WiFi Settings              [Scan]   [Disconnect]    │  ← header bar
 *   ├──────────────────────────────────────────────────────┤
 *   │  Status: Connected · MySSID · 192.168.1.100          │  ← status label
 *   ├──────────────────────────────────────────────────────┤
 *   │  🔒  HomeNetwork                          ▐▐▐▐ -62  │
 *   │  🔒  OfficeWifi                           ▐▐▐░ -71  │
 *   │      OpenHotspot                          ▐▐░░ -78  │
 *   │  …                                                   │  ← scrollable list
 *   └──────────────────────────────────────────────────────┘
 *
 * Password dialog (modal, shown when a locked AP is tapped):
 *
 *   ┌──────────────────────────────┐
 *   │  Connect to "HomeNetwork"    │
 *   │  Password: [______________]  │
 *   │         [Cancel]  [Connect]  │
 *   └──────────────────────────────┘
 *   [Full-screen LVGL keyboard]
 */

#include "wifi_ui.h"
#include "wifi_manager.h"

#include "lvgl/lvgl.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Display geometry (override via -D flags if needed)                  */
/* ------------------------------------------------------------------ */

#ifndef DISP_HOR_RES
#define DISP_HOR_RES 800
#endif
#ifndef DISP_VER_RES
#define DISP_VER_RES 480
#endif

/* ------------------------------------------------------------------ */
/* Internal state                                                       */
/* ------------------------------------------------------------------ */

/* Shared WiFi info (written by background threads, read on UI thread) */
static wifi_info_t g_wifi_info;

/* Mutex protecting g_wifi_info.ap_list / ap_count between the scan
 * worker thread and the UI refresh function running in the LVGL loop. */
static pthread_mutex_t g_ap_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Password buffer for the current connect operation.
 * Safe without a mutex because the g_connecting flag serialises access:
 * a new connection cannot start until the previous one clears the flag. */
static char g_pass_buf[WIFI_MAX_PASS_LEN];

/* Background operation flags – set by worker threads, cleared by UI */
static volatile int g_scan_done    = 0; /* scan finished         */
static volatile int g_connect_done = 0; /* connect attempt done  */
static volatile int g_connect_ok   = 0; /* result of connect     */
static volatile int g_scanning     = 0; /* scan in progress      */
static volatile int g_connecting   = 0; /* connect in progress   */

/* Currently selected AP for connect attempt */
static wifi_ap_t g_pending_ap;

/* ------------------------------------------------------------------ */
/* LVGL object handles                                                  */
/* ------------------------------------------------------------------ */

static lv_obj_t *g_screen      = NULL;
static lv_obj_t *g_status_label= NULL;
static lv_obj_t *g_scan_btn    = NULL;
static lv_obj_t *g_disco_btn   = NULL;
static lv_obj_t *g_ap_list     = NULL;
static lv_obj_t *g_spinner     = NULL;

/* Password dialog handles */
static lv_obj_t *g_dialog_bg   = NULL;
static lv_obj_t *g_dialog_box  = NULL;
static lv_obj_t *g_pass_ta     = NULL;
static lv_obj_t *g_keyboard    = NULL;

/* ------------------------------------------------------------------ */
/* Colour / style constants                                             */
/* ------------------------------------------------------------------ */

#define COLOR_HEADER    lv_color_hex(0x1E88E5)   /* blue           */
#define COLOR_BG        lv_color_hex(0xF5F5F5)   /* light grey     */
#define COLOR_ITEM_BG   lv_color_hex(0xFFFFFF)   /* white          */
#define COLOR_CONNECTED lv_color_hex(0x43A047)   /* green          */
#define COLOR_WARN      lv_color_hex(0xE53935)   /* red            */
#define COLOR_TEXT      lv_color_hex(0x212121)   /* near-black     */
#define COLOR_SUBTEXT   lv_color_hex(0x757575)   /* mid-grey       */

#define HEADER_H    60
#define STATUS_H    36
#define LIST_TOP    (HEADER_H + STATUS_H + 4)

/* ------------------------------------------------------------------ */
/* Forward declarations                                                 */
/* ------------------------------------------------------------------ */

static void refresh_ap_list(void);
static void refresh_status_label(void);
static void show_password_dialog(const wifi_ap_t *ap);
static void close_password_dialog(void);

/* ------------------------------------------------------------------ */
/* Background worker threads                                            */
/* ------------------------------------------------------------------ */

static void *scan_worker(void *arg)
{
    (void)arg;
    wifi_info_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    wifi_scan(&tmp);

    pthread_mutex_lock(&g_ap_mutex);
    g_wifi_info = tmp;
    pthread_mutex_unlock(&g_ap_mutex);

    g_scan_done = 1;
    g_scanning  = 0;
    return NULL;
}

static void *connect_worker(void *arg)
{
    const char *pass = (const char *)arg;

    /* g_pending_ap.ssid is already set by the caller */
    int rc = wifi_connect(g_pending_ap.ssid,
                          (pass && pass[0]) ? pass : NULL);
    g_connect_ok   = (rc == 0) ? 1 : 0;
    g_connect_done = 1;
    g_connecting   = 0;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Signal-strength indicator text                                       */
/* ------------------------------------------------------------------ */

/**
 * Returns a short text label describing signal quality.
 * Uses LV_SYMBOL_WIFI repeated 1–4 times to indicate bars.
 * An em-dash placeholder aligns entries with fewer bars.
 */
static const char *bars_text(int signal_dbm)
{
    int b = wifi_signal_bars(signal_dbm);
    switch (b) {
        case 4: return LV_SYMBOL_WIFI LV_SYMBOL_WIFI LV_SYMBOL_WIFI LV_SYMBOL_WIFI;
        case 3: return LV_SYMBOL_WIFI LV_SYMBOL_WIFI LV_SYMBOL_WIFI;
        case 2: return LV_SYMBOL_WIFI LV_SYMBOL_WIFI;
        default: return LV_SYMBOL_WIFI;
    }
}

/* ------------------------------------------------------------------ */
/* Event callbacks                                                      */
/* ------------------------------------------------------------------ */

/** Scan button pressed */
static void scan_btn_cb(lv_event_t *e)
{
    (void)e;
    if (g_scanning || g_connecting)
        return;

    g_scanning  = 1;
    g_scan_done = 0;
    g_wifi_info.ap_count = 0;

    /* Clear old list */
    lv_obj_clean(g_ap_list);

    /* Show spinner */
    if (g_spinner) {
        lv_obj_clear_flag(g_spinner, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(g_status_label, "Scanning for networks...");
    lv_obj_add_flag(g_scan_btn, LV_OBJ_FLAG_DISABLED);

    pthread_t tid;
    pthread_create(&tid, NULL, scan_worker, NULL);
    pthread_detach(tid);
}

/** Disconnect button pressed */
static void disco_btn_cb(lv_event_t *e)
{
    (void)e;
    if (g_connecting || g_scanning)
        return;

    wifi_disconnect();
    lv_label_set_text(g_status_label, "Disconnected");

    /* Refresh status after a short delay via the timer */
}

/** AP list item pressed */
static void ap_item_cb(lv_event_t *e)
{
    if (g_connecting || g_scanning)
        return;

    wifi_ap_t *ap = (wifi_ap_t *)lv_event_get_user_data(e);
    if (!ap)
        return;

    /* If encrypted, show password dialog; otherwise connect directly */
    if (ap->encrypted) {
        show_password_dialog(ap);
    } else {
        g_pending_ap = *ap;
        g_connecting  = 1;
        g_connect_done = 0;

        lv_label_set_text(g_status_label, "Connecting...");
        lv_obj_add_flag(g_scan_btn, LV_OBJ_FLAG_DISABLED);
        lv_obj_add_flag(g_disco_btn, LV_OBJ_FLAG_DISABLED);

        pthread_t tid;
        pthread_create(&tid, NULL, connect_worker, NULL);
        pthread_detach(tid);
    }
}

/** "Cancel" button in password dialog */
static void dialog_cancel_cb(lv_event_t *e)
{
    (void)e;
    close_password_dialog();
}

/** "Connect" button in password dialog */
static void dialog_connect_cb(lv_event_t *e)
{
    (void)e;
    if (!g_pass_ta)
        return;

    const char *pass = lv_textarea_get_text(g_pass_ta);
    strncpy(g_pass_buf, pass ? pass : "", WIFI_MAX_PASS_LEN - 1);
    g_pass_buf[WIFI_MAX_PASS_LEN - 1] = '\0';

    close_password_dialog();

    g_connecting   = 1;
    g_connect_done = 0;

    lv_label_set_text(g_status_label, "Connecting...");
    lv_obj_add_flag(g_scan_btn, LV_OBJ_FLAG_DISABLED);
    lv_obj_add_flag(g_disco_btn, LV_OBJ_FLAG_DISABLED);

    pthread_t tid;
    pthread_create(&tid, NULL, connect_worker, (void *)g_pass_buf);
    pthread_detach(tid);
}

/** Keyboard event – route Enter to connect, hide on ready */
static void keyboard_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if (code == LV_EVENT_READY)
            dialog_connect_cb(e);
        else
            dialog_cancel_cb(e);
    }
}

/* ------------------------------------------------------------------ */
/* Password dialog                                                      */
/* ------------------------------------------------------------------ */

static void show_password_dialog(const wifi_ap_t *ap)
{
    if (!ap)
        return;

    g_pending_ap = *ap;

    /* Semi-transparent overlay */
    g_dialog_bg = lv_obj_create(g_screen);
    lv_obj_set_size(g_dialog_bg, DISP_HOR_RES, DISP_VER_RES);
    lv_obj_set_pos(g_dialog_bg, 0, 0);
    lv_obj_set_style_bg_color(g_dialog_bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_dialog_bg, LV_OPA_50, 0);
    lv_obj_set_style_border_width(g_dialog_bg, 0, 0);
    lv_obj_clear_flag(g_dialog_bg, LV_OBJ_FLAG_SCROLLABLE);

    /* Dialog box */
    g_dialog_box = lv_obj_create(g_dialog_bg);
    lv_obj_set_size(g_dialog_box, 440, 180);
    lv_obj_align(g_dialog_box, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_radius(g_dialog_box, 8, 0);
    lv_obj_set_style_pad_all(g_dialog_box, 12, 0);
    lv_obj_clear_flag(g_dialog_box, LV_OBJ_FLAG_SCROLLABLE);

    /* Title */
    lv_obj_t *title = lv_label_create(g_dialog_box);
    char title_buf[64];
    snprintf(title_buf, sizeof(title_buf), "Connect to \"%s\"", ap->ssid);
    lv_label_set_text(title, title_buf);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* "Password:" label */
    lv_obj_t *pass_lbl = lv_label_create(g_dialog_box);
    lv_label_set_text(pass_lbl, "Password:");
    lv_obj_align(pass_lbl, LV_ALIGN_TOP_LEFT, 0, 32);

    /* Password text area */
    g_pass_ta = lv_textarea_create(g_dialog_box);
    lv_textarea_set_one_line(g_pass_ta, true);
    lv_textarea_set_password_mode(g_pass_ta, true);
    lv_textarea_set_placeholder_text(g_pass_ta, "Enter password");
    lv_obj_set_width(g_pass_ta, 420);
    lv_obj_align(g_pass_ta, LV_ALIGN_TOP_LEFT, 0, 56);

    /* Buttons row */
    lv_obj_t *cancel_btn = lv_btn_create(g_dialog_box);
    lv_obj_set_size(cancel_btn, 110, 40);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x9E9E9E), 0);
    lv_obj_add_event_cb(cancel_btn, dialog_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_center(cancel_lbl);

    lv_obj_t *ok_btn = lv_btn_create(g_dialog_box);
    lv_obj_set_size(ok_btn, 110, 40);
    lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(ok_btn, COLOR_HEADER, 0);
    lv_obj_add_event_cb(ok_btn, dialog_connect_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ok_lbl = lv_label_create(ok_btn);
    lv_label_set_text(ok_lbl, "Connect");
    lv_obj_center(ok_lbl);

    /* LVGL keyboard – sits below the dialog box */
    g_keyboard = lv_keyboard_create(g_dialog_bg);
    lv_obj_set_size(g_keyboard, DISP_HOR_RES, DISP_VER_RES / 2);
    lv_obj_align(g_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(g_keyboard, g_pass_ta);
    lv_obj_add_event_cb(g_keyboard, keyboard_cb, LV_EVENT_ALL, NULL);
}

static void close_password_dialog(void)
{
    if (g_dialog_bg) {
        lv_obj_del(g_dialog_bg);
        g_dialog_bg  = NULL;
        g_dialog_box = NULL;
        g_pass_ta    = NULL;
        g_keyboard   = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* AP list rendering                                                    */
/* ------------------------------------------------------------------ */

static void refresh_ap_list(void)
{
    if (!g_ap_list)
        return;

    lv_obj_clean(g_ap_list);

    if (g_wifi_info.ap_count == 0) {
        lv_obj_t *empty = lv_label_create(g_ap_list);
        lv_label_set_text(empty, "No networks found. Press Scan.");
        lv_obj_set_style_text_color(empty, lv_color_hex(0x9E9E9E), 0);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, 0);
        return;
    }

    /* Snapshot the shared AP list under lock into a local stack buffer,
     * then render without holding the mutex. Using a local (non-static)
     * array means each call gets its own copy with no aliasing concerns. */
    wifi_ap_t ap_storage[WIFI_MAX_AP_COUNT];
    int ap_count;

    pthread_mutex_lock(&g_ap_mutex);
    ap_count = g_wifi_info.ap_count;
    memcpy(ap_storage, g_wifi_info.ap_list,
           sizeof(wifi_ap_t) * (size_t)ap_count);
    pthread_mutex_unlock(&g_ap_mutex);

    for (int i = 0; i < ap_count; i++) {
        wifi_ap_t *ap = &ap_storage[i];

        /* Each AP gets a button that spans the full list width */
        lv_obj_t *btn = lv_btn_create(g_ap_list);
        lv_obj_set_size(btn, LV_PCT(100), 52);
        lv_obj_set_style_bg_color(btn, COLOR_ITEM_BG, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xE3F2FD),
                                   LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_pad_all(btn, 8, 0);
        lv_obj_add_event_cb(btn, ap_item_cb, LV_EVENT_CLICKED, ap);

        /* Lock icon */
        lv_obj_t *lock = lv_label_create(btn);
        lv_label_set_text(lock, ap->encrypted ? LV_SYMBOL_LOCK : " ");
        lv_obj_set_style_text_color(lock,
            ap->encrypted ? lv_color_hex(0x757575) : lv_color_white(), 0);
        lv_obj_align(lock, LV_ALIGN_LEFT_MID, 0, 0);

        /* SSID */
        lv_obj_t *ssid_lbl = lv_label_create(btn);
        lv_label_set_text(ssid_lbl, ap->ssid);
        lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ssid_lbl, 480);
        lv_obj_set_style_text_color(ssid_lbl, COLOR_TEXT, 0);
        lv_obj_align(ssid_lbl, LV_ALIGN_LEFT_MID, 28, 0);

        /* Signal strength label (dBm) */
        char sig_buf[32];
        snprintf(sig_buf, sizeof(sig_buf), "%d dBm", ap->signal_dbm);
        lv_obj_t *sig_lbl = lv_label_create(btn);
        lv_label_set_text(sig_lbl, sig_buf);
        lv_obj_set_style_text_color(sig_lbl, COLOR_SUBTEXT, 0);
        lv_obj_align(sig_lbl, LV_ALIGN_RIGHT_MID, -90, 4);

        /* Bar count label */
        lv_obj_t *bars = lv_label_create(btn);
        lv_label_set_text(bars, bars_text(ap->signal_dbm));
        lv_obj_set_style_text_color(bars, COLOR_HEADER, 0);
        lv_obj_align(bars, LV_ALIGN_RIGHT_MID, -4, 0);
    }
}

/* ------------------------------------------------------------------ */
/* Status label update                                                  */
/* ------------------------------------------------------------------ */

static void refresh_status_label(void)
{
    if (!g_status_label)
        return;

    wifi_info_t st;
    memset(&st, 0, sizeof(st));
    wifi_get_status(&st);

    char buf[128];
    switch (st.state) {
        case WIFI_STATE_CONNECTED:
            snprintf(buf, sizeof(buf),
                     LV_SYMBOL_WIFI "  Connected · %s · %s",
                     st.connected_ssid, st.ip_addr);
            lv_obj_set_style_text_color(g_status_label, COLOR_CONNECTED, 0);
            lv_obj_clear_flag(g_disco_btn, LV_OBJ_FLAG_DISABLED);
            break;
        case WIFI_STATE_CONNECTING:
            snprintf(buf, sizeof(buf),
                     LV_SYMBOL_REFRESH "  Connecting to %s…",
                     st.connected_ssid);
            lv_obj_set_style_text_color(g_status_label,
                                         lv_color_hex(0xFB8C00), 0);
            break;
        case WIFI_STATE_DISCONNECTED:
            snprintf(buf, sizeof(buf), LV_SYMBOL_CLOSE "  Not connected");
            lv_obj_set_style_text_color(g_status_label, COLOR_WARN, 0);
            lv_obj_add_flag(g_disco_btn, LV_OBJ_FLAG_DISABLED);
            break;
        default:
            snprintf(buf, sizeof(buf), "WiFi status unknown");
            lv_obj_set_style_text_color(g_status_label, COLOR_SUBTEXT, 0);
            break;
    }
    lv_label_set_text(g_status_label, buf);
}

/* ------------------------------------------------------------------ */
/* Periodic status timer (runs on LVGL task-handler thread)            */
/* ------------------------------------------------------------------ */

static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    /* Check if a scan just finished */
    if (g_scan_done) {
        g_scan_done = 0;
        lv_obj_clear_flag(g_scan_btn, LV_OBJ_FLAG_DISABLED);
        if (g_spinner)
            lv_obj_add_flag(g_spinner, LV_OBJ_FLAG_HIDDEN);
        refresh_ap_list();
        refresh_status_label();
        return;
    }

    /* Check if a connect attempt just finished */
    if (g_connect_done) {
        g_connect_done = 0;
        lv_obj_clear_flag(g_scan_btn, LV_OBJ_FLAG_DISABLED);
        lv_obj_clear_flag(g_disco_btn, LV_OBJ_FLAG_DISABLED);
        refresh_status_label();
        if (!g_connect_ok) {
            lv_label_set_text(g_status_label,
                              LV_SYMBOL_CLOSE "  Connection failed. Wrong password?");
            lv_obj_set_style_text_color(g_status_label, COLOR_WARN, 0);
        }
        return;
    }

    /* Idle – refresh status every ~5 s */
    static uint32_t last_tick = 0;
    uint32_t now = lv_tick_get();
    if ((now - last_tick) >= 5000) {
        last_tick = now;
        if (!g_scanning && !g_connecting)
            refresh_status_label();
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void wifi_ui_create(void)
{
    /* -------- Root screen -------- */
    g_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_screen, COLOR_BG, 0);
    lv_scr_load(g_screen);

    /* -------- Header bar -------- */
    lv_obj_t *header = lv_obj_create(g_screen);
    lv_obj_set_size(header, DISP_HOR_RES, HEADER_H);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, COLOR_HEADER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_shadow_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    /* Title label */
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, LV_SYMBOL_WIFI "  WiFi Settings");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 16, 0);

    /* Scan button */
    g_scan_btn = lv_btn_create(header);
    lv_obj_set_size(g_scan_btn, 100, 38);
    lv_obj_align(g_scan_btn, LV_ALIGN_RIGHT_MID, -120, 0);
    lv_obj_set_style_bg_color(g_scan_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(g_scan_btn, LV_OPA_20, 0);
    lv_obj_set_style_border_color(g_scan_btn, lv_color_white(), 0);
    lv_obj_set_style_border_width(g_scan_btn, 1, 0);
    lv_obj_add_event_cb(g_scan_btn, scan_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *scan_lbl = lv_label_create(g_scan_btn);
    lv_label_set_text(scan_lbl, LV_SYMBOL_REFRESH "  Scan");
    lv_obj_set_style_text_color(scan_lbl, lv_color_white(), 0);
    lv_obj_center(scan_lbl);

    /* Disconnect button */
    g_disco_btn = lv_btn_create(header);
    lv_obj_set_size(g_disco_btn, 120, 38);
    lv_obj_align(g_disco_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(g_disco_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(g_disco_btn, LV_OPA_20, 0);
    lv_obj_set_style_border_color(g_disco_btn, lv_color_white(), 0);
    lv_obj_set_style_border_width(g_disco_btn, 1, 0);
    lv_obj_add_flag(g_disco_btn, LV_OBJ_FLAG_DISABLED);
    lv_obj_add_event_cb(g_disco_btn, disco_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *disco_lbl = lv_label_create(g_disco_btn);
    lv_label_set_text(disco_lbl, LV_SYMBOL_POWER "  Disconnect");
    lv_obj_set_style_text_color(disco_lbl, lv_color_white(), 0);
    lv_obj_center(disco_lbl);

    /* -------- Status bar -------- */
    lv_obj_t *status_bar = lv_obj_create(g_screen);
    lv_obj_set_size(status_bar, DISP_HOR_RES, STATUS_H);
    lv_obj_set_pos(status_bar, 0, HEADER_H);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(0xE3F2FD), 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_set_style_shadow_width(status_bar, 0, 0);
    lv_obj_set_style_pad_hor(status_bar, 16, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    g_status_label = lv_label_create(status_bar);
    lv_label_set_text(g_status_label, "Checking WiFi status…");
    lv_label_set_long_mode(g_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(g_status_label, DISP_HOR_RES - 32);
    lv_obj_set_style_text_color(g_status_label, COLOR_SUBTEXT, 0);
    lv_obj_align(g_status_label, LV_ALIGN_LEFT_MID, 0, 0);

    /* -------- AP list -------- */
    g_ap_list = lv_obj_create(g_screen);
    lv_obj_set_size(g_ap_list,
                    DISP_HOR_RES - 8,
                    DISP_VER_RES - LIST_TOP - 4);
    lv_obj_set_pos(g_ap_list, 4, LIST_TOP);
    lv_obj_set_flex_flow(g_ap_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_ap_list,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(g_ap_list, COLOR_BG, 0);
    lv_obj_set_style_border_width(g_ap_list, 0, 0);
    lv_obj_set_style_radius(g_ap_list, 0, 0);
    lv_obj_set_style_pad_all(g_ap_list, 4, 0);
    lv_obj_set_style_pad_row(g_ap_list, 4, 0);

    /* Placeholder text */
    lv_obj_t *hint = lv_label_create(g_ap_list);
    lv_label_set_text(hint, "Press Scan to search for networks");
    lv_obj_set_style_text_color(hint, COLOR_SUBTEXT, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 0);

    /* -------- Spinner (hidden by default) -------- */
    g_spinner = lv_spinner_create(g_screen, 1000, 60);
    lv_obj_set_size(g_spinner, 48, 48);
    lv_obj_align(g_spinner, LV_ALIGN_TOP_RIGHT, -8, HEADER_H + 4);
    lv_obj_add_flag(g_spinner, LV_OBJ_FLAG_HIDDEN);

    /* -------- Periodic timer -------- */
    lv_timer_create(status_timer_cb, 500, NULL);

    /* Initial status check */
    refresh_status_label();
}

void wifi_ui_update(void)
{
    /* Intentionally empty – the LVGL timer handles all updates.
     * This function is provided as an extension point and can be
     * called from the main loop if needed. */
}
