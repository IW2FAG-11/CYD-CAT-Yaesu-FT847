/*
 * ===========================================================================
 *  ft847-cyd-controller
 *  ---------------------------------------------------------------------------
 *  Control a Yaesu FT-847 transceiver over CAT with an ESP32 "Cheap Yellow
 *  Display" (CYD, ESP32-2432S028R) and its touch screen.
 *
 *  HARDWARE
 *    CYD  GPIO22 (UART2 TX, 3.3V) --> MAX3232 T1IN
 *    CYD  GPIO35 (UART2 RX, 3.3V) <-- MAX3232 R1OUT
 *    CYD  GND  ---------------------  MAX3232 GND  ----- FT-847 DB9 pin 5
 *    MAX3232 (RS-232 side)  --------  FT-847 DB9 (NULL-MODEM / crossover cable)
 *
 *  RADIO SETTINGS
 *    Menu #37 [CAT RATE] = 4800 (or 9600): must match CAT_BAUD in config.h.
 *    FT-847 serial format = 8 data bits, no parity, 2 stop bits (8N2).
 *
 *  LIBRARIES
 *    - TFT_eSPI (Bodmer)                 -> display, configured in platformio.ini
 *    - XPT2046_Touchscreen (P. Stoffregen) -> touch, on a separate SPI bus
 *
 *  NOTES
 *    - The FT-847 protocol is 5 bytes: 4 BCD data bytes + 1 opcode byte
 *      (opcode in the LAST position). Frequency = big-endian BCD of Hz/10.
 *    - Early FT-847 units do not support reading status.
 *    - If nothing works, swap TX <-> RX.
 * ===========================================================================
 */

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#include "config.h"
#include "cat_ft847.h"

/* ---------------------------------------------------------------------------
 *  Objects
 * ------------------------------------------------------------------------- */
TFT_eSPI tft = TFT_eSPI();

/* The CYD touch controller lives on its own SPI bus (VSPI). */
SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

/* ---------------------------------------------------------------------------
 *  Application state
 * ------------------------------------------------------------------------- */
uint32_t currentFreq = 145500000UL;   // Hz
uint8_t  currentMode = MD_FM;
uint8_t  sMeter      = 0;             // 0..31
bool     txOn        = false;
bool     rawTouchOut = false;         // serial 'C' command toggle

/* ---------------------------------------------------------------------------
 *  Colours
 * ------------------------------------------------------------------------- */
#define COL_BG      TFT_BLACK
#define COL_TXT     TFT_GREEN
#define COL_BTN     0x0320            // dark green
#define COL_BTN_TX  0x7800            // dark red
#define COL_EDGE    TFT_WHITE

/* ---------------------------------------------------------------------------
 *  Buttons (landscape 320x240)
 * ------------------------------------------------------------------------- */
struct Button { int x, y, w, h; const char *label; };

Button btnBandDn = {   8, 148,  98, 42, "BAND-" };
Button btnBandUp = { 111, 148,  98, 42, "BAND+" };
Button btnMode   = { 214, 148,  98, 42, "MODE"  };
Button btnFreqDn = {   8, 194,  98, 42, "FREQ-" };
Button btnFreqUp = { 111, 194,  98, 42, "FREQ+" };
Button btnPTT    = { 214, 194,  98, 42, "PTT"   };

/* Amateur bands (Hz) - typical FT-847 coverage. */
const uint32_t bands[] = {
  1840000UL, 3573000UL, 7040000UL, 10125000UL, 14074000UL, 18100000UL,
  21074000UL, 24915000UL, 28150000UL, 50150000UL, 144500000UL, 432100000UL
};
const int nBands = sizeof(bands) / sizeof(bands[0]);
int bandIdx = 10;   // 144.500 MHz

/* ===========================================================================
 *  Touch
 * ========================================================================= */
bool readTouch(uint16_t &x, uint16_t &y) {
  if (!(touchscreen.tirqTouched() && touchscreen.touched())) return false;
  TS_Point p = touchscreen.getPoint();

  if (rawTouchOut) {
    Serial.printf("[touch] raw x=%d y=%d z=%d\n", p.x, p.y, p.z);
  }

  long sx = map((long)p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, 320);
  long sy = map((long)p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 240);
  x = (uint16_t)constrain(sx, 0, 319);
  y = (uint16_t)constrain(sy, 0, 239);
  return true;
}

/* ===========================================================================
 *  GUI
 * ========================================================================= */
void drawButton(Button &b, uint16_t color) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 8, color);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 8, COL_EDGE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, color);
  tft.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2, 2);
}

void drawFreq() {
  tft.fillRect(0, 30, 320, 56, COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_TXT, COL_BG);
  char buf[24];
  /* MHz with 4 decimals = 10 Hz resolution */
  sprintf(buf, "%lu.%04lu",
          (unsigned long)(currentFreq / 1000000UL),
          (unsigned long)((currentFreq % 1000000UL) / 100UL));
  tft.drawString(buf, 152, 58, 6);
  tft.setTextColor(TFT_CYAN, COL_BG);
  tft.drawString("MHz", 292, 58, 2);
}

void drawStatus() {
  tft.fillRect(0, 90, 320, 52, COL_BG);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_YELLOW, COL_BG);
  const char *m = "?";
  switch (currentMode) {
    case MD_LSB: m = "LSB";  break;
    case MD_USB: m = "USB";  break;
    case MD_CW:  m = "CW";   break;
    case MD_CW_R:m = "CW-R"; break;
    case MD_AM:  m = "AM";   break;
    case MD_FM:  m = "FM";   break;
    case MD_FM_N:m = "FM-N"; break;
    case MD_AM_N:m = "AM-N"; break;
    case MD_CW_N:m = "CW-N"; break;
  }
  tft.drawString(String("Mode: ") + m, 8, 100, 2);

  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(txOn ? TFT_RED : TFT_GREEN, COL_BG);
  tft.drawString(txOn ? "TX" : "RX", 312, 100, 4);

  /* S-meter bar */
  int bars = map(sMeter, 0, 31, 0, 300);
  tft.drawRect(8, 122, 304, 12, COL_EDGE);
  tft.fillRect(9, 123, bars, 10, TFT_GREEN);
}

void drawAll() {
  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_ORANGE, COL_BG);
  tft.drawString("FT-847 CAT", 160, 15, 2);

  drawFreq();
  drawStatus();

  drawButton(btnBandDn, COL_BTN);
  drawButton(btnBandUp, COL_BTN);
  drawButton(btnMode,   COL_BTN);
  drawButton(btnFreqDn, COL_BTN);
  drawButton(btnFreqUp, COL_BTN);
  drawButton(btnPTT,    txOn ? COL_BTN_TX : COL_BTN);
}

/* ===========================================================================
 *  Actions
 * ========================================================================= */
bool inBtn(Button &b, uint16_t x, uint16_t y) {
  return x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h;
}

void applyFreq() {
  catSetFreq(currentFreq, VFO_MAIN);
  drawFreq();
}

void handleTouch(uint16_t x, uint16_t y) {
  if (inBtn(btnBandDn, x, y)) {
    bandIdx = (bandIdx + nBands - 1) % nBands;
    currentFreq = bands[bandIdx];
    applyFreq();
  } else if (inBtn(btnBandUp, x, y)) {
    bandIdx = (bandIdx + 1) % nBands;
    currentFreq = bands[bandIdx];
    applyFreq();
  } else if (inBtn(btnFreqDn, x, y)) {
    currentFreq -= 1000; applyFreq();          // -1 kHz
  } else if (inBtn(btnFreqUp, x, y)) {
    currentFreq += 1000; applyFreq();          // +1 kHz
  } else if (inBtn(btnMode, x, y)) {
    const uint8_t modes[] = { MD_LSB, MD_USB, MD_CW, MD_AM, MD_FM };
    int i = 0;
    while (i < 5 && modes[i] != currentMode) i++;
    currentMode = modes[(i + 1) % 5];
    catSetMode(currentMode, OP_MODE_MAIN);
    drawStatus();
  } else if (inBtn(btnPTT, x, y)) {
    txOn = !txOn;
    catPTT(txOn);
    drawButton(btnPTT, txOn ? COL_BTN_TX : COL_BTN);
    drawStatus();
  }
}

/* ===========================================================================
 *  Serial command line (USB) - useful for testing without the touch panel
 * ========================================================================= */
void serialMenu() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      line.trim();
      if (line.length()) {
        char cmd = toupper(line[0]);
        long val = line.substring(1).toInt();
        switch (cmd) {
          case 'F': currentFreq = (uint32_t)val; applyFreq();
                    Serial.printf("Set freq %lu Hz\n", (unsigned long)currentFreq); break;
          case 'M': currentMode = (uint8_t)val; catSetMode(currentMode, OP_MODE_MAIN);
                    Serial.printf("Set mode 0x%02X\n", currentMode); break;
          case 'T': catPTT(true);  Serial.println("PTT ON");  break;
          case 'R': catPTT(false); Serial.println("PTT OFF"); break;
          case 'S': { uint8_t s = catReadStatus(OP_RX_STATUS);
                      Serial.printf("RX status = 0x%02X (S=%d)\n", s, s & 0x1F); } break;
          case 'Q': { uint32_t hz; uint8_t md;
                      if (catReadFreqMode(hz, md))
                        Serial.printf("Radio: %lu Hz mode 0x%02X\n", (unsigned long)hz, md);
                      else
                        Serial.println("No answer (early unit without read?)"); } break;
          case 'C': rawTouchOut = !rawTouchOut;
                    Serial.printf("Raw touch output %s\n", rawTouchOut ? "ON" : "OFF"); break;
          default:  Serial.println("Commands: F<hz> M<mode> T R S Q C"); break;
        }
      }
      line = "";
    } else {
      line += c;
    }
  }
}

/* ===========================================================================
 *  Setup / Loop
 * ========================================================================= */
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println("\nFT-847 CYD controller - boot");

  catBegin();

  /* Touch on its own SPI bus */
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(1);   // landscape; try 3 if the touch is mirrored

  /* Display */
  tft.init();
  tft.setRotation(1);           // landscape 320x240
  tft.fillScreen(COL_BG);

  drawAll();

  catOn();
  catSetMode(currentMode, OP_MODE_MAIN);
  catSetFreq(currentFreq, VFO_MAIN);

  Serial.println("Ready. Serial commands: F<hz> M<mode> T R S Q C");
}

unsigned long lastPoll = 0;

void loop() {
  serialMenu();

  uint16_t x, y;
  if (readTouch(x, y)) {
    handleTouch(x, y);
    delay(150);                 // debounce
  }

  /* Poll S-meter every 500 ms; ignore if the radio does not answer. */
  if (millis() - lastPoll > 500) {
    lastPoll = millis();
    uint8_t s = catReadStatus(OP_RX_STATUS);
    if (s != 0xFF) {
      sMeter = s & 0x1F;
      drawStatus();
    }
  }
}
