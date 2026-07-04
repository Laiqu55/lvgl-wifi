/*
 * wifi_ui.h — LVGL-based WiFi settings UI interface
 *
 * All UI objects live on a single LVGL screen.  Call wifi_ui_create()
 * once after lv_init(); the screen becomes active immediately.
 */

#ifndef WIFI_UI_H
#define WIFI_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Create and display the WiFi settings screen.
 *
 * Must be called after lv_init() and after the display / input
 * drivers have been registered.
 */
void wifi_ui_create(void);

/**
 * @brief  Periodic update tick – call from the main loop.
 *
 * Checks whether a background scan / connect operation has
 * finished and refreshes the UI accordingly.
 */
void wifi_ui_update(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_UI_H */
