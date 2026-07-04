/**
 * lv_conf.h — LVGL v8 configuration
 *
 * Target: ATK-DLT113IS (Allwinner T113-i, ARM Cortex-A7)
 *         800 × 480 display, 16-bit (RGB565) framebuffer
 *         Buildroot 2019 / Linux kernel 5.4.61
 */

#if 1 /* Set to "1" to enable the content below */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/

/** Color depth: 1 (1 byte per pixel), 8, 16, 32 */
#define LV_COLOR_DEPTH 16

/** Swap the 2 bytes of RGB565 color; set to 1 if colors are swapped */
#define LV_COLOR_16_SWAP 0

/** Enable features to mix colors with opacity */
#define LV_COLOR_MIX_ROUND_OFS 0

/*=========================
   MEMORY SETTINGS
 *=========================*/

/** 1: use custom malloc/free; 0: use the built-in `lv_mem_alloc()`/`lv_mem_free()` */
#define LV_MEM_CUSTOM 0

/** Size of the memory available for `lv_mem_alloc()` in bytes (>= 2kB) */
#define LV_MEM_SIZE (512U * 1024U)   /* 512 kB */

/** Set an address for the memory pool instead of allocating it as a global array */
#define LV_MEM_ADR 0  /* 0: unused (LVGL allocates a global array of LV_MEM_SIZE bytes) */
/* LV_MEM_POOL_INCLUDE and LV_MEM_POOL_ALLOC are only needed when
 * LV_MEM_CUSTOM = 1 (custom allocator). They are intentionally omitted here. */

/*====================
   HAL SETTINGS
 *====================*/

/** Default display refresh period (ms) */
#define LV_DISP_DEF_REFR_PERIOD 30

/** Input device read period (ms) */
#define LV_INDEV_DEF_READ_PERIOD 30

/** Use a custom tick source to tell the elapsed time in milliseconds.
 *  It removes the need to manually update the tick with `lv_tick_inc()` */
#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM_INCLUDE <time.h>
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR \
        ({ struct timespec _ts; clock_gettime(CLOCK_MONOTONIC, &_ts); \
           (uint32_t)(_ts.tv_sec * 1000U + _ts.tv_nsec / 1000000U); })
#endif

/** Default Dots Per Inch (used for font rendering) */
#define LV_DPI_DEF 130

/*=======================
   RENDERING BACKEND
 *=======================*/

#define LV_DRAW_COMPLEX 1  /* 1: Enable complex draw engine features (gradients etc.) */

/** Anti-aliasing (1: enable, 0: disable) */
#define LV_ANTIALIAS 1

/*=======================
   DISPLAY SETTINGS
 *=======================*/

#define LV_DISP_SMALL_LIMIT  30
#define LV_DISP_MEDIUM_LIMIT 50
#define LV_DISP_LARGE_LIMIT  70

/*====================
   LOGGING
 *====================*/

#define LV_USE_LOG 1
#if LV_USE_LOG
    /* 0: Trace, 1: Info, 2: Warn, 3: Error, 4: User, 5: None */
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 1
    #define LV_LOG_TRACE_MEM        0
    #define LV_LOG_TRACE_TIMER      0
    #define LV_LOG_TRACE_INDEV      0
    #define LV_LOG_TRACE_DISP_REFR  0
    #define LV_LOG_TRACE_EVENT      0
    #define LV_LOG_TRACE_OBJ_CREATE 0
    #define LV_LOG_TRACE_LAYOUT     0
    #define LV_LOG_TRACE_ANIM       0
#endif /* LV_USE_LOG */

/*=============
  ASSERTIONS
 *=============*/

#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

#define LV_ASSERT_HANDLER_INCLUDE <stdint.h>
#define LV_ASSERT_HANDLER while(1);

/*==================
  BUILT-IN THEMES
 *==================*/

/** A simple, impressive and easy-to-extend theme. */
#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    /** 0: Light mode; 1: Dark mode */
    #define LV_THEME_DEFAULT_DARK 0
    /** 1: Enable grow on press */
    #define LV_THEME_DEFAULT_GROW 1
    /** Default transition time (ms) */
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif

/** A very simple theme that is a good starting point for a custom theme */
#define LV_USE_THEME_BASIC 1

/** A theme designed for monochrome displays */
#define LV_USE_THEME_MONO 0

/*=================
  FONT USAGE
 *=================*/

/* Montserrat fonts with bpp = 4 */
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

/* Demonstrate special features */
#define LV_FONT_MONTSERRAT_12_SUBPX      0
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK            0

/** Always set a default font */
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/** Enable it if you have fonts with a lot of characters.
 *  The limit depends on the font size, font face and bpp.
 *  Compiler error will be triggered if this is not set. */
#define LV_FONT_FMT_TXT_LARGE 0

/** Enables/disables support for compressed fonts. */
#define LV_USE_FONT_COMPRESSED 0

/** Enable subpixel rendering */
#define LV_USE_FONT_SUBPX 0

/** Set the pixel order of the display.
 *  Important only if "subpx fonts" are used.
 *  With "normal" font it doesn't matter.
 *  (0: Red channel first, 1: Blue channel first) */
#if LV_USE_FONT_SUBPX
    #define LV_FONT_SUBPX_BGR 0
#endif

/*=================
  TEXT SETTINGS
 *=================*/

/**
 * Select a character encoding for strings.
 * Your IDE or editor should have the same character encoding.
 * - LV_TXT_ENC_UTF8
 * - LV_TXT_ENC_ASCII
 */
#define LV_TXT_ENC LV_TXT_ENC_UTF8

/** Can break (wrap) texts on these chars */
#define LV_TXT_BREAK_CHARS " ,.;:-_"

/** If a word is at least this long, will break wherever "prettiest" */
#define LV_TXT_LINE_BREAK_LONG_LEN 0

/** Minimum number of characters in a long word to put on a line before a break. */
#define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN 3

/** Minimum number of characters in a long word to put on a line after a break. */
#define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 3

/** The control character to use for signalling text recoloring. */
#define LV_TXT_COLOR_CMD "#"

/** Support bidirectional texts. Allows mixing Left-to-Right and Right-to-Left texts. */
#define LV_USE_BIDI 0

/** Enable Arabic/Persian processing.
 *  In these languages characters should be replaced with another form
 *  based on their position in the text. */
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*==================
  WIDGET USAGE
 *==================*/

#define LV_USE_ARC          1
#define LV_USE_BAR          1
#define LV_USE_BTN          1
#define LV_USE_BTNMATRIX    1
#define LV_USE_CANVAS       0
#define LV_USE_CHECKBOX     1
#define LV_USE_DROPDOWN     1
#define LV_USE_IMG          1
#define LV_USE_LABEL        1
#if LV_USE_LABEL
    #define LV_LABEL_TEXT_SELECTION 1
    #define LV_LABEL_LONG_TXT_HINT  1
#endif

#define LV_USE_LINE         1
#define LV_USE_ROLLER       1
#if LV_USE_ROLLER
    #define LV_ROLLER_INF_PAGES 7
#endif

#define LV_USE_SLIDER       1
#define LV_USE_SWITCH       1
#define LV_USE_TEXTAREA     1
#if LV_USE_TEXTAREA
    #define LV_TEXTAREA_DEF_PWD_SHOW_TIME 1500
#endif

#define LV_USE_TABLE        1

/*==================
  EXTRA COMPONENTS
 *==================*/

/* Layouts */
#define LV_USE_FLEX  1
#define LV_USE_GRID  0

/* Widgets */
#define LV_USE_ANIMIMG     0
#define LV_USE_CALENDAR    0
#define LV_USE_CHART       0
#define LV_USE_COLORWHEEL  0
#define LV_USE_IMGBTN      0
#define LV_USE_KEYBOARD    1
#define LV_USE_LED         0
#define LV_USE_LIST        1
#define LV_USE_MENU        0
#define LV_USE_METER       0
#define LV_USE_MSGBOX      1
#define LV_USE_SPAN        0
#define LV_USE_SPINBOX     0
#define LV_USE_SPINNER     1
#define LV_USE_TABVIEW     0
#define LV_USE_TILEVIEW    0
#define LV_USE_WIN         0

/* Themes */
#define LV_USE_THEME_MATERIAL 0
#define LV_USE_THEME_TEMPLATE 0

/*==================
  EXAMPLES
 *==================*/
#define LV_BUILD_EXAMPLES 0

/*===================
  DEMO USAGE
 *====================*/
#define LV_USE_DEMO_WIDGETS        0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK      0
#define LV_USE_DEMO_STRESS         0
#define LV_USE_DEMO_MUSIC          0

#endif /* LV_CONF_H */

#endif /* End of "Content enable" */
