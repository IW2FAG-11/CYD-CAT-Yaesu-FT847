/*
 * cat_ft847.h - Yaesu FT-847 CAT protocol layer
 * ---------------------------------------------------------------------------
 *  The FT-847 CAT protocol uses 5-byte blocks where the OPCODE is the LAST
 *  (5th) byte.  Bytes 1..4 carry data (frequency as big-endian BCD of Hz/10).
 *  Serial format: 8 data bits, no parity, 2 stop bits (8N2).
 *
 *  This differs from the FT-817/857/897 family where the opcode is the FIRST
 *  byte.
 */
#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>

/* ---- VFO / target opcodes (5th byte) ---------------------------------- */
#define VFO_MAIN       0x01   // set MAIN frequency
#define VFO_SAT_RX     0x11   // set SAT RX frequency
#define VFO_SAT_TX     0x21   // set SAT TX frequency

/* ---- Mode opcodes ------------------------------------------------------ */
#define OP_MODE_MAIN   0x07
#define OP_MODE_RX     0x17
#define OP_MODE_TX     0x27

/* ---- Read frequency + mode opcodes ------------------------------------ */
#define OP_READ_FM     0x03   // read MAIN freq + mode
#define OP_READ_FM_RX  0x13   // read SAT RX freq + mode
#define OP_READ_FM_TX  0x23   // read SAT TX freq + mode

/* ---- Other commands ---------------------------------------------------- */
#define OP_PTT_ON      0x08
#define OP_PTT_OFF     0x88
#define OP_CAT_ON      0x00
#define OP_CAT_OFF     0x80
#define OP_RX_STATUS   0xE7   // read RX status (S-meter, squelch, ...)
#define OP_TX_STATUS   0xF7   // read TX status (power, SWR, PTT, ...)
#define OP_SAT_ON      0x4E
#define OP_SAT_OFF     0x8E

/* ---- Mode codes (data byte D1 of the mode command) -------------------- */
#define MD_LSB         0x00
#define MD_USB         0x01
#define MD_CW          0x02
#define MD_CW_R        0x03
#define MD_AM          0x04
#define MD_FM          0x08
#define MD_CW_N        0x82
#define MD_CW_R_N      0x83
#define MD_AM_N        0x84
#define MD_FM_N        0x88

/* ---- API --------------------------------------------------------------- */
void    catBegin();
void    catWrite5(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t op);
void    catSetFreq(uint32_t hz, uint8_t vfoOpcode);
void    catSetMode(uint8_t mode, uint8_t opcode);
void    catPTT(bool tx);
void    catOn();
uint8_t catReadStatus(uint8_t opcode);          // returns 0xFF on timeout
bool    catReadFreqMode(uint32_t &hz, uint8_t &mode, uint8_t opcode = OP_READ_FM);
