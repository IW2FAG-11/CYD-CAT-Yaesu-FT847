/*
 * cat_ft847.cpp - Yaesu FT-847 CAT protocol implementation
 */
#include "cat_ft847.h"
#include "config.h"

/* UART2 is used for the CAT link. */
static HardwareSerial CAT(2);

/* ---------------------------------------------------------------------------
 *  Send a raw 5-byte block.  The FT-847 expects the opcode in the LAST byte.
 *  A small delay is added to respect the maximum inter-byte gap of the radio.
 * ------------------------------------------------------------------------- */
void catWrite5(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t op) {
  uint8_t c[5] = { b0, b1, b2, b3, op };
  CAT.write(c, 5);
  CAT.flush();
  delay(2);
}

/* ---------------------------------------------------------------------------
 *  Set frequency in Hz.  vfoOpcode = VFO_MAIN / VFO_SAT_RX / VFO_SAT_TX.
 *  The frequency is sent as big-endian BCD of (Hz / 10).
 * ------------------------------------------------------------------------- */
void catSetFreq(uint32_t hz, uint8_t vfoOpcode) {
  uint32_t f = hz / 10;   // unit = 10 Hz
  uint8_t b0 = (((f / 10000000UL) % 10) << 4) | ((f / 1000000UL) % 10);
  uint8_t b1 = (((f / 100000UL)   % 10) << 4) | ((f / 10000UL)   % 10);
  uint8_t b2 = (((f / 1000UL)     % 10) << 4) | ((f / 100UL)     % 10);
  uint8_t b3 = (((f / 10UL)       % 10) << 4) | ( f              % 10);
  catWrite5(b0, b1, b2, b3, vfoOpcode);
}

/* ---------------------------------------------------------------------------
 *  Set operating mode.  opcode = OP_MODE_MAIN / OP_MODE_RX / OP_MODE_TX.
 * ------------------------------------------------------------------------- */
void catSetMode(uint8_t mode, uint8_t opcode) {
  catWrite5(mode, 0, 0, 0, opcode);
}

/* PTT via CAT. */
void catPTT(bool tx) {
  catWrite5(0, 0, 0, 0, tx ? OP_PTT_ON : OP_PTT_OFF);
}

/* CAT ON (usually not required, the radio answers as soon as it is polled). */
void catOn() {
  catWrite5(0, 0, 0, 0, OP_CAT_ON);
}

/* ---- BCD helpers ------------------------------------------------------- */
static inline uint8_t bcd2bin(uint8_t b) {
  return ((b >> 4) & 0x0F) * 10 + (b & 0x0F);
}

/* ---------------------------------------------------------------------------
 *  Read a single status byte (RX = OP_RX_STATUS, TX = OP_TX_STATUS).
 *  Returns 0xFF on timeout (early FT-847 units do not support reading).
 * ------------------------------------------------------------------------- */
uint8_t catReadStatus(uint8_t opcode) {
  while (CAT.available()) CAT.read();          // flush
  catWrite5(0, 0, 0, 0, opcode);
  unsigned long t = millis();
  while (!CAT.available() && millis() - t < 200) { delay(1); }
  return CAT.available() ? CAT.read() : 0xFF;
}

/* ---------------------------------------------------------------------------
 *  Read frequency + mode (5 bytes returned).  Returns true on success.
 * ------------------------------------------------------------------------- */
bool catReadFreqMode(uint32_t &hz, uint8_t &mode, uint8_t opcode) {
  while (CAT.available()) CAT.read();          // flush
  catWrite5(0, 0, 0, 0, opcode);
  uint8_t d[5];
  int n = 0;
  unsigned long t = millis();
  while (n < 5 && millis() - t < 300) {
    if (CAT.available()) { d[n++] = CAT.read(); t = millis(); }
  }
  if (n < 5) return false;

  uint32_t f = (uint32_t)bcd2bin(d[0]) * 1000000UL
             + (uint32_t)bcd2bin(d[1]) * 10000UL
             + (uint32_t)bcd2bin(d[2]) * 100UL
             + (uint32_t)bcd2bin(d[3]);
  hz   = f * 10;      // back to Hz
  mode = d[4];
  return true;
}

/* ---------------------------------------------------------------------------
 *  Initialise the CAT UART (8N2, no flow control).
 * ------------------------------------------------------------------------- */
void catBegin() {
  CAT.begin(CAT_BAUD, SERIAL_8N2, CAT_RX_PIN, CAT_TX_PIN);
  delay(100);
}
