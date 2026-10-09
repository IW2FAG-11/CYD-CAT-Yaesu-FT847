/*
 * config.h - Hardware configuration for the FT-847 CYD controller
 * ---------------------------------------------------------------------------
 *  All the wiring / pin / baud definitions live here so they are easy to
 *  change without touching the rest of the code.
 *
 *  NOTE: the TFT_eSPI display configuration (driver, pins, SPI bus) is NOT
 *  here: it is defined in platformio.ini via "build_flags" so that the
 *  TFT_eSPI library does not need to be edited.
 */
#pragma once

#include <Arduino.h>

/* ===========================================================================
 *  XPT2046 resistive touch panel  (SEPARATE SPI bus, VSPI)
 *  On the CYD the touch controller is NOT on the display SPI bus, therefore
 *  it must be driven by its own library (XPT2046_Touchscreen) and its own
 *  SPIClass instance.
 * =========================================================================== */
#define XPT2046_IRQ   36   // T_IRQ
#define XPT2046_MOSI  32   // T_DIN
#define XPT2046_MISO  39   // T_OUT
#define XPT2046_CLK   25   // T_CLK
#define XPT2046_CS    33   // T_CS

/* ===========================================================================
 *  CAT serial link to the FT-847  (UART2 -> MAX3232 -> DB9)
 *  The FT-847 uses RS-232C levels, so a MAX3232 level shifter is required.
 *  Use a NULL-MODEM (crossover) cable between the MAX3232 and the radio.
 * =========================================================================== */
#define CAT_RX_PIN    35   // ESP32 RX2  <- MAX3232 R1OUT   (GPIO35 is input-only: fine for RX)
#define CAT_TX_PIN    22   // ESP32 TX2  -> MAX3232 T1IN
#define CAT_BAUD      4800 // MUST match Menu #37 [CAT RATE] of the radio (4800/9600/57600)

/* ===========================================================================
 *  USB serial monitor (debug + simple command line)
 * =========================================================================== */
#define SERIAL_BAUD   115200

/* ===========================================================================
 *  Touch raw calibration window
 *  These are the raw XPT2046 readings that correspond to the screen edges in
 *  LANDSCAPE orientation (rotation 1).  If the touch is offset or inverted,
 *  use the serial command 'C' to print the raw values while touching the
 *  corners and adjust the four numbers below.
 * =========================================================================== */
#define TOUCH_X_MIN   200
#define TOUCH_X_MAX   3700
#define TOUCH_Y_MIN   240
#define TOUCH_Y_MAX   3800
