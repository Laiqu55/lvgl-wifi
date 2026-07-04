/*
 * main.c — Application entry point
 *
 * Initialises LVGL, registers the Linux framebuffer display driver and
 * the evdev touch-input driver, creates the WiFi UI, then runs the
 * LVGL task-handler in the main loop.
 *
 * Target: ATK-DLT113IS (Allwinner T113-i), Buildroot 2019, kernel 5.4.61
 */

#include "wifi_manager.h"
#include "wifi_ui.h"

/* LVGL core */
#include "lvgl/lvgl.h"

/* LVGL Linux drivers (from lv_drivers repository) */
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Configuration                                                        */
/* ------------------------------------------------------------------ */

#ifndef DISP_HOR_RES
#define DISP_HOR_RES 800
#endif
#ifndef DISP_VER_RES
#define DISP_VER_RES 480
#endif

/*
 * Double-buffered draw buffers.
 * Each buffer covers 1/10 of the screen (800 × 48 pixels).
 */
#define DISP_BUF_LINES  48
#define DISP_BUF_SIZE   (DISP_HOR_RES * DISP_BUF_LINES)

/* evdev touch-screen device node – override via -DEVDEV_INPUT_DEV="\"/dev/input/event1\"" */
#ifndef EVDEV_INPUT_DEV
#define EVDEV_INPUT_DEV  "/dev/input/event0"
#endif

/* ------------------------------------------------------------------ */
/* LVGL tick source (1 ms resolution using CLOCK_MONOTONIC)            */
/* ------------------------------------------------------------------ */

static uint32_t get_tick_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000ULL +
                      (uint64_t)ts.tv_nsec / 1000000ULL);
}

/* ------------------------------------------------------------------ */
/* Display driver setup                                                 */
/* ------------------------------------------------------------------ */

static lv_disp_draw_buf_t  disp_buf;
static lv_color_t          buf1[DISP_BUF_SIZE];
static lv_color_t          buf2[DISP_BUF_SIZE];
static lv_disp_drv_t       disp_drv;

static void display_init(void)
{
    /* Open /dev/fb0 (or FB_DEV env-var override) */
    fbdev_init();

    /* Read actual resolution from the framebuffer */
    uint32_t hor_res = DISP_HOR_RES;
    uint32_t ver_res = DISP_VER_RES;
    fbdev_get_sizes(&hor_res, &ver_res, NULL);

    /* Double-buffered draw area */
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, DISP_BUF_SIZE);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = (lv_coord_t)hor_res;
    disp_drv.ver_res  = (lv_coord_t)ver_res;
    disp_drv.flush_cb = fbdev_flush;
    disp_drv.draw_buf = &disp_buf;
    lv_disp_drv_register(&disp_drv);
}

/* ------------------------------------------------------------------ */
/* Input device driver setup                                           */
/* ------------------------------------------------------------------ */

static lv_indev_drv_t indev_drv;

static void input_init(void)
{
    /* evdev_set_file() is available in some lv_drivers versions; fall
     * back to the compile-time default if it is not present. */
#ifdef EVDEV_NAME
    evdev_set_file(EVDEV_INPUT_DEV);
#endif
    evdev_init();

    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = evdev_read;
    lv_indev_drv_register(&indev_drv);
}

/* ------------------------------------------------------------------ */
/* Signal handling                                                      */
/* ------------------------------------------------------------------ */

static volatile int g_running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int main(void)
{
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    /* 1 – Initialise LVGL */
    lv_init();

    /* 2 – Register custom tick source */
    lv_tick_set_cb(get_tick_ms);

    /* 3 – Register display and input drivers */
    display_init();
    input_init();

    /* 4 – Initialise WiFi subsystem */
    if (wifi_init() != 0) {
        fprintf(stderr,
                "[wifi] WARNING: wpa_supplicant not available. "
                "WiFi functions will be limited.\n");
    }

    /* 5 – Build the UI */
    wifi_ui_create();

    /* 6 – Main loop */
    while (g_running) {
        /* Advance the LVGL clock and run all pending tasks */
        lv_task_handler();

        /* Sleep 5 ms to avoid 100 % CPU usage */
        usleep(5000);
    }

    /* 7 – Clean up */
    fbdev_exit();

    return EXIT_SUCCESS;
}
