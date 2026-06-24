/*
 * OBD2 Dashboard v8 — Car Mode
 * Hardware: Waveshare ESP32-C6-LCD-1.47 (172x320, ST7789 IPS)
 * CAN:      SN65HVD230 → GPIO5 (TX), GPIO4 (RX), 500kbps
 * Vehicle:  VW Tiguan 5N 1.4 TSI (Twincharger)
 *
 * Thresholds & Colors:
 *   RPM     : Blue <2500 | Green <5000 | Orange <5500 | Red ≥5500 (alert ≥5500)
 *   Speed   : Green (static)
 *   Throttle: Green <40% | Orange <75% | Red ≥75%
 *   Turbo   : Blue ≤0bar | Green <0.8 | Orange <1.4 | Red ≥1.4
 *   Coolant : Blue <70°C | Green ≤95°C | Red >95°C  (alert >95)
 *   Oil Temp: Blue <80°C | Green ≤110°C | Red >110°C (alert >110)
 *   Intake  : Blue <15°C | Green <35°C | Orange <50°C | Red ≥50°C
 *   Battery : Orange <12.2V | Green 12.2–14.8V | Red outside (alert <11.5 or >14.8)

 */
 #include <SPI.h>
 #include <Wire.h>
 #include <Arduino_GFX_Library.h>  // ✓ Arduino_GFX (works with ESP32-C6!)
 #include <Adafruit_NeoPixel.h>
 
 // U8G2 Fonts Integration
 #include <Adafruit_GFX.h>
 #include <U8g2_for_Adafruit_GFX.h>
 
 // ESP32-C6 CRITICAL: Include hal/twai_ll.h BEFORE driver/twai.h
 // This fixes GPIO matrix routing issues on ESP32-C6
 #include "hal/twai_ll.h"
 #include "driver/twai.h"  // CAN bus driver (ESP32 TWAI)
 
 
 // ===== PINS (CORRECTED FROM PINOUT DIAGRAM!) =====
 // WAVESHARE ESP32-C6-LCD-1.47 CORRECT PINS
 #define TFT_SCLK    1   // GPIO1 = LCD_CLK (SPI SCK) ✓
 #define TFT_MOSI    2   // GPIO2 = LCD_DIN (SPI MOSI) ✓
 #define TFT_CS      14  // GPIO14 = LCD_CS (Chip Select) ✓
 #define TFT_DC      15  // GPIO15 = LCD_DC (Data/Command) ✓
 #define TFT_RST     22  // GPIO22 = LCD_RST (Reset) ✓
 #define TFT_BL      23  // GPIO23 = LCD_BL (Backlight) ✓
 
 // TOUCH & RGB
 // CORRECT PINS FROM PINOUT IMAGE:
 #define TOUCH_SDA   18  // GPIO18 = TP_SDA ✓
 #define TOUCH_SCL   19  // GPIO19 = TP_SCL ✓
 #define TOUCH_RST   20  // GPIO20 = TP_RST ✓
 #define TOUCH_INT   21  // GPIO21 = TP_INT ✓
 #define RGB_PIN     8   // WS2812 RGB LED
 
 // CST816S I2C Address = 0x15 (confirmed from datasheet)
 #define CST816_ADDR  0x15
 // Touch I2C Address (Θα εντοπιστεί αυτόματα στο setup)
 uint8_t touchAddress = 0;
 
 // CAN Bus Pins - ESP32-C6 TWAI Controller
 // Available free GPIOs from pinout: GPIO4, GPIO5, GPIO6, GPIO7
 // Using GPIO5 (TX) + GPIO4 (RX) - both are free and work with GPIO Matrix
 #define CAN_TX      5   // GPIO5 → CAN Module CTX pin
 #define CAN_RX      4   // GPIO4 → CAN Module CRX pin
 
 // ── LISTEN-ONLY DIAGNOSTIC MODE ─────────────────────────────────────────────
 // Uncomment the next line to put TWAI in listen-only mode.
 // In this mode the ESP32 NEVER transmits anything, so it can NEVER go BUS_OFF.
 // If the car is running you WILL see RX CNT climbing on the CAN DIAG screen.
 // Use this to confirm the CAN H/L wiring is correct before sending requests.
 // Comment it out again once the wiring is confirmed.
 //#define CAN_LISTEN_ONLY  // ✓ ΕΝΕΡΓΟΠΟΙΗΜΕΝΟ για διάγνωση!
 
 // ===== DEMO MODE =====
 // Uncomment to show animated fake data (no CAN bus needed)
 // #define DEMO_MODE
 
 #define SCREEN_WIDTH  320
 #define SCREEN_HEIGHT 172
 
 // Display Objects (Arduino_GFX)
 Arduino_DataBus *bus = new Arduino_ESP32SPI(
   TFT_DC,   // DC pin (GPIO 15)
   TFT_CS,   // CS pin (GPIO 14)
   TFT_SCLK, // SCK pin (GPIO 1)
   TFT_MOSI, // MOSI pin (GPIO 2)
   GFX_NOT_DEFINED  // MISO not used
 );
 
 Arduino_ST7789 *tft = new Arduino_ST7789(
   bus,
   TFT_RST,  // RST pin (GPIO 22)
   3,        // rotation (3 = landscape flipped 180°)
   true,     // IPS display
   172,      // width
   320,      // height  
   34,       // col offset 1
   0,        // row offset 1
   34,       // col offset 2
   0         // row offset 2
 );
 
 Adafruit_NeoPixel pixels(1, RGB_PIN, NEO_GRB + NEO_KHZ800);
 
 // === U8g2 Adapter for Arduino_GFX ===
 // CRITICAL FIX: hardcode 320×172 (rotated dims) — NOT t->width()/height() which
 // are called BEFORE tft->begin() and return wrong pre-rotation values (172×320).
 // If Adafruit_GFX gets wrong dims, writePixel() clips all pixels with x > 172
 // causing letters to invisible / appear as empty boxes.
 class Arduino_GFX_Adapter : public Adafruit_GFX {
 private:
     Arduino_GFX* _tft;
 public:
     Arduino_GFX_Adapter(Arduino_GFX* t) : Adafruit_GFX(320, 172), _tft(t) {}
     void drawPixel(int16_t x, int16_t y, uint16_t color) override { _tft->drawPixel(x, y, color); }
     void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override { _tft->drawFastVLine(x, y, h, color); }
     void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override { _tft->drawFastHLine(x, y, w, color); }
     void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override { _tft->fillRect(x, y, w, h, color); }
     void writePixel(int16_t x, int16_t y, uint16_t color) override { _tft->drawPixel(x, y, color); }
 };
 
 Arduino_GFX_Adapter gfx_adapter(tft);
 U8G2_FOR_ADAFRUIT_GFX u8g2;
 
 // U8G2 Font helper — ALL fonts use _tf (full charset: letters + numbers).
 // Using only fonts CONFIRMED present in U8g2_for_Adafruit_GFX library.
 // IMPORTANT: u8g2_SetFont() always resets is_transparent=0 (opaque) when font changes.
 // We MUST call setFontMode(1) here so transparent mode is always restored.
 void setU8g2Font(uint8_t sz) {
   if (sz <= 2) u8g2.setFont(u8g2_font_helvB12_tf);        // ~14px bold labels
   else if (sz == 3) u8g2.setFont(u8g2_font_helvB18_tf);   // ~22px bold medium
   else if (sz == 4) u8g2.setFont(u8g2_font_fub25_tf);     // ~29px FreeUniversal
   else if (sz == 5) u8g2.setFont(u8g2_font_fub30_tf);     // ~35px
   else if (sz == 6) u8g2.setFont(u8g2_font_fub35_tf);     // ~40px fub — σταθερά baseline (αντικαθιστά logisoso32)
   else if (sz == 7) u8g2.setFont(u8g2_font_fub42_tf);     // ~48px fub — σταθερά baseline (αντικαθιστά logisoso42)
   else              u8g2.setFont(u8g2_font_fub49_tn);      // ~55px extra large
   u8g2.setFontMode(1);  // always transparent — setFont() resets this to 0!
 }
 
 
 // Colors (IPS Display με INVON - Χρώματα αντιστρέφονται!)
 #define COLOR_BG        0xFFFF  // Μαύρο background (με INVON: 0xFFFF = μαύρο)
 #define COLOR_HEADER    color565(0, 60, 140)  // Σκούρο μπλε για την πάνω μπάρα
 #define COLOR_TEXT      0x0000  // Άσπρο text (με INVON: 0x0000 = άσπρο)
 #define COLOR_GREEN     0xF81F  // Πράσινο (inverted: 0x07E0 -> 0xF81F)
 #define COLOR_RED       0xFFE0  // Κόκκινο  (φαίνεται ως RED στην οθόνη)
 #define COLOR_ORANGE    0xF9A0  // Πορτοκαλί/Amber (inverted BGR565: R=255,G=200,B=0)
 #define COLOR_BLUE      0x07FF  // Μπλε     (φαίνεται ως BLUE στην οθόνη)
 #define COLOR_CYAN      0x001F  // Cyan     (φαίνεται ως CYAN στην οθόνη)
 #define COLOR_YELLOW    0xF800  // Κίτρινο  (φαίνεται ως YELLOW στην οθόνη)
 #define COLOR_MAGENTA   0x07E0  // Magenta (inverted: 0xF81F -> 0x07E0)
 #define COLOR_GRAY      0x8410  // Gray (inverted)
 #define COLOR_DARK      0xDEFB  // Dark (inverted)
 #define COLOR_PURPLE    0x5AAF  // Purple (inverted)
 
 // ===== OBD2 PIDs =====
 #define PID_RPM      0x0C
 #define PID_SPEED    0x0D
 #define PID_COOLANT  0x05
 #define PID_THROTTLE 0x11
 #define PID_MAP      0x0B  // Manifold Absolute Pressure (Boost)
 #define PID_INTAKE   0x0F
 #define PID_BATTERY  0x42  // Control Module Voltage
 
 #define PID_DTC_COUNT 0x01  // Number of DTCs (Mode 01, PID 01)
 #define PID_CLEAR_DTC 0x04  // Clear Diagnostic Trouble Codes (Mode 04)
 #define PID_ENGINE_LOAD 0x04 // Calculated Engine Load (%)
 #define PID_DIST_SINCE_DTC 0x31 // Distance since DTCs cleared (km)
 #define PID_FUEL_RATE   0x5E  // Engine Fuel Rate - (A*256+B)*0.05 L/h
 #define PID_MAF         0x10  // Mass Air Flow - (256*A+B)/100 g/s
 // VW-specific PIDs (Mode 22) for Tiguan 5N 2009 EA111 engine:
 #define VW_OIL_TEMP_OLD  0x0407 // Oil temp for older VAG (2008-2010) - WORKS!
 #define VW_OIL_TEMP_EA111 0x1156 // Oil temp for EA111/EA888 Gen 1 (VCDS confirmed)
 #define VW_OIL_TEMP_NEW  0x115C // Oil temp for newer VAG (2010+) - fallback
 
 // Data Structures
 struct DashboardItem {
   String label;
   String unit;
   float value;
   int precision;
   uint16_t color;
 };
 
 enum TabID { TAB_MAIN, TAB_TEMPS, TAB_TRIP, TAB_SYSTEM, TAB_COUNT };
 String tabNames[] = { "MAIN", "TEMPS", "TRIP INFO", "SYSTEM" };
 int currentTab = TAB_SYSTEM; // Start at BATTERY (TAB_SYSTEM, index 0)
 int currentItemIndex[TAB_COUNT] = {0}; // Track selected item per tab
 
 // FreeRTOS handles
 SemaphoreHandle_t carMutex    = NULL;  // protects car struct between tasks
 TaskHandle_t      canTaskHandle = NULL;
 struct CarData {
   int rpm;
   int speed;
   int coolant;
   int oil;
   int throttle;
   float boost;
   float battery;
   int intake;
   int turbo;
   int distance;
   int distanceOBD;
   int distanceOffset;
 
   int dtc;
   bool mil;
   int avgSpeed;
   int maxSpeed;
 
   float instantFuelLh; // L/h από PID 0x5E (-1.0=δεν απάντησε ποτέ, ≥0=απάντησε)
   float maf;           // g/s από PID 0x10 (MAF fallback για κατανάλωση)
   float fuelUsed;      // Λίτρα που κάηκαν από reset
 } car;
 

 
 // Alert blink state (continuous border flash until user touches)
 bool     alertBlinking     = false;
 uint8_t  alertDismissedMask = 0;   // bitmask: bit0=coolant, bit1=oil, bit2=rpm, bit3=batt
                                    // set on dismiss, cleared per-condition when it drops below threshold
 uint16_t alertBlinkColor   = 0;
 bool     alertBorderOn     = false;
 unsigned long alertNextBlinkMs    = 0;
 unsigned long alertCooldownUntilMs = 0; // no new alerts until this time (after manual dismiss)
 
 // Cached last gauge draw params (used to restore gauge after alert blink OFF)
 float    lastGaugePct      = 0.0f;
 uint16_t lastGaugeFill     = 0;
 uint16_t lastGaugeTrack    = 0;
 String   lastGaugeValStr   = "";
 String   lastGaugeUnitStr  = "";
 uint8_t  lastGaugeValSize  = 7;
 
 // Touch interrupt flag (set by ISR)
 volatile bool touchEvent = false;
 
 void IRAM_ATTR touchISR() {
   touchEvent = true;
 }
 
 // Touch swipe tracking
 int swipeStartX = -1;
 int swipeStartY = -1;
 unsigned long swipeStartTime = 0;
 unsigned long lastTouchEventMs = 0;  // χρόνος τελευταίας επιτυχούς IRQ event (ανίχνευση finger-up)
 bool fingerDown = false;
 
 // Brightness control
 int currentBrightness = 200;       // 0-255
 bool brightnessMode = false;
 unsigned long brightnessModeTimeout = 0;
 bool longPressArmed = false;
 
 // CAN / OBD2 state
 bool canInitialized = false;
 int  canFailCount     = 0;          // consecutive PID failures
 bool debugPidLogging  = true;       // Enable detailed PID success/fail logging
 // PID success tracking (for on-screen display)
 bool pidWorking[30] = {false};      // Track which PIDs work (expanded to 30)
 String pidNames[30] = {"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""};
 // CAN Diagnostics (shown in TAB_DIAG)
 uint32_t canRxCount     = 0;        // total successful RX responses
 String   canLastPidName = "--";     // last successfully read PID name
 int      canLastVal     = -999;     // last successfully read raw value
 esp_err_t canLastInstallErr = ESP_OK; // last twai_driver_install error code
 #define CAN_FAIL_MAX  30             // after this many fails → uninstall TWAI
 unsigned long canRetryMs = 0;       // when to try reinit (0 = never retry yet)
 #define CAN_RETRY_INTERVAL_MS (10UL * 1000UL) // retry after 10s
 
 // DTC Storage & Clearing
 String activeDTCs[10];
 bool requestClearDTC = false; // Flag to tell CAN task to run Mode 04

 bool isHoldingDtcs = false;
 
 // ===== ACCEL TIMER STATE =====
 enum AccelState { ACCEL_IDLE, ACCEL_ARMED, ACCEL_RUNNING, ACCEL_DONE };
 AccelState     accelState       = ACCEL_IDLE;
 int            accelMode        = 0;        // 0 = 0-100, 1 = 60-120
 unsigned long  accelStartMs     = 0;
 unsigned long  accelResultMs    = 0;
 int            accelExitSpeed   = 0;
 float          accelDistanceM   = 0.0f;     // Distance traveled in meters during accel run
 unsigned long  accelLastCalcMs  = 0;        // For calculating distance delta
 bool           accelNeedsRedraw = true;
 bool           accelHoldFired   = false;  // αποφύγει double-fire στο 1s hold
 unsigned long  accelLastDisplayMs = 0;  // χρόνος τελευταίας ανανέωσης οθόνης (για live timer)
 
 // Screen sleep
 bool screenSleeping       = false;
 bool uiNeedsFullRedraw    = false;  // set after wake/drawUI to force updateUI to repaint values
 unsigned long lastActivityMs = 0;          // reset on every touch
 #define SLEEP_TIMEOUT_MS  (5UL * 60UL * 1000UL)  // 5 λεπτά
 
 // Trip tracking globals
 unsigned long tripStartTime   = 0;   // millis() when trip started
 unsigned long lastOdometerMs  = 0;   // last time we updated distance
 float  tripDistanceF    = 0.0f; // km (float accumulator)
 unsigned long tripElapsedMs   = 0;   // ms driven (speed > 2 km/h)
 float  tripFuelL        = 0.0f; // λίτρα που κάηκαν (accumulator)
 
 // ===== FORWARD DECLARATIONS =====
  void setup();
  void loop();
  void checkAutoSwitch();
  void switchTo(int tab, int item);
  void updateRGB();
  void showTabTransition(bool forward);
  void showItemTransition(bool goLeft);
  void showBootScreen();
  void drawUI();
  void updateUI();
  int getItemsCount(int tab);
  DashboardItem getCurrentItem();
  void resetTrip();
  void updateAccelState();
  void drawAccelScreen();
  void updateAccelHoldBar();
  void updateAccelTimer();
  void handleAccelTap();
  void readOBD2();
  void canShutdown();
  void canTryInit();
  void canTask(void *param);
  void handleTouch();
  void handleSerial();
  void serialEvent();                                  // Arduino auto-calls on Serial RX
  void parseSerialCmd(String &s);
  bool readCST816S(int *x, int *y, uint8_t *gesture, uint8_t *fingerCount = nullptr); // CST816S touch reader
  void initCST816S();                                  // CST816S configuration
  bool getProgressRange(int tab, int idx, float *minV, float *maxV);
  void showAlertFlash(uint16_t color);
  void triggerAlert(uint16_t color);
  void drawAlertArc(uint16_t color);
  void dismissAlert();
  void updateAlertBlink();
  void drawBrightnessOverlay();
  void drawGauge(int cx, int cy, int outerR, int innerR, float pct, uint16_t trackColor, uint16_t fillColor, String valStr, String unitStr, uint8_t valSize, float prevPct = -1.0f, bool skipText = false);
  
  // ===== HELPER FUNCTIONS =====
  // RGB565 color conversion (Arduino_GFX doesn't have color565())
  inline uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }
  
void setup() {
   Serial.begin(115200);
   // USB CDC: wait briefly for host (laptop). In car without USB → times out fast.
   unsigned long _t = millis();
   while (!Serial && (millis() - _t) < 300) delay(10);
   Serial.println("\n\n===========================================");
   Serial.println("    ESP32-C6 OBD2 DASHBOARD v8");
   Serial.println("    WITH ARDUINO_GFX & CORRECTED PINS");
   Serial.println("===========================================");
   
   // Init RGB LED first (for visual feedback)
   Serial.println("Step 1: Initializing RGB LED...");
   pixels.begin();
   pixels.setBrightness(50);
   pixels.setPixelColor(0, pixels.Color(255, 0, 0)); // Red = Starting
   pixels.show();
   
   // Init Backlight (GPIO 23!) — PWM for brightness control
   Serial.println("Step 2: Initializing backlight (GPIO 23)...");
   ledcAttach(TFT_BL, 5000, 8);         // 5 kHz, 8-bit resolution
   ledcWrite(TFT_BL, currentBrightness);
   Serial.println("   Backlight ON (PWM)");
   
   // Init Touch - Reset sequence (ακριβώς όπως στο koendv library)
   Serial.println("Step 4: Initializing Touch CST816S...");
   pinMode(TOUCH_RST, OUTPUT);
   pinMode(TOUCH_INT, INPUT_PULLUP);
 
   digitalWrite(TOUCH_RST, HIGH);
   delay(50);
   digitalWrite(TOUCH_RST, LOW);
   delay(5);
   digitalWrite(TOUCH_RST, HIGH);
   delay(50);
   
   Wire.begin(TOUCH_SDA, TOUCH_SCL);
   Wire.setClock(400000);
   
   // I2C Scanner - find CST816S
   Serial.println("Scanning I2C bus for CST816S (expected 0x15)...");
   byte error, address;
   int nDevices = 0;
   for(address = 1; address < 127; address++ ) {
     Wire.beginTransmission(address);
     error = Wire.endTransmission();
     if (error == 0) {
       Serial.print("   I2C device found at 0x");
       if (address < 16) Serial.print("0");
       Serial.println(address, HEX);
       nDevices++;
       if (touchAddress == 0) touchAddress = address;
     }
   }
   if (nDevices == 0) {
     Serial.println("   No I2C devices found! Using default 0x15.");
     touchAddress = CST816_ADDR;
   } else {
     Serial.print("   Using touch address: 0x");
     Serial.println(touchAddress, HEX);
   }
 
   // Configure CST816S for touch detection
   initCST816S();
 
   // Attach interrupt - FALLING edge (INT goes LOW briefly when touch event)
   attachInterrupt(digitalPinToInterrupt(TOUCH_INT), touchISR, FALLING);
   Serial.println("   Touch interrupt attached (FALLING)");
 
   // Init Display
   Serial.println("Step 3: Initializing ST7789 display...");
   if (!tft->begin()) {
     Serial.println("   ERROR: Display init failed!");
     pixels.setPixelColor(0, pixels.Color(255, 0, 0)); // Red = Error
     pixels.show();
     while (1) delay(1000);
   }
   
   tft->setRotation(3);  // Landscape mode flipped 180° (172x320)
   Serial.println("   Display initialized (172x320, rotation=3)");
   
   // ΑΝ ΒΛΕΠΕΙΣ "NIAM" (Καθρέφτη), βγάλε τα σχόλια από την παρακάτω γραμμή:
   bus->beginWrite();
   bus->writeCommand(0x36); // MADCTL
   bus->write(0xE0);        // 0xE0 = Landscape flipped 180° (MV=1, MX=1, MY=1)
   bus->endWrite();
   
   // FIX: Ενεργοποίηση Color Inversion για IPS
   bus->beginWrite();
   bus->writeCommand(0x21); // INVON - Inversion ON
   bus->endWrite();
   Serial.println("   Color inversion enabled (IPS mode)");
   
   // INIT U8G2 with Adapter
   u8g2.begin(gfx_adapter);
   u8g2.setFontMode(1);           // transparent bg
   u8g2.setFontDirection(0);
   
   Serial.println("\n*** Display working! ***\n");
   
   // RGB LED green = OK
   pixels.setPixelColor(0, pixels.Color(0, 255, 0));
   pixels.show();
   
   // ===== CAN / TWAI INIT =====
   Serial.println("Step 5: Initializing CAN bus (GPIO5=TX, GPIO4=RX, 500kbps)...");
   tripStartTime  = millis();
   lastOdometerMs = millis();
 #ifdef DEMO_MODE
   Serial.println("   DEMO_MODE active — CAN disabled, using fake data");
 #else
   canTryInit();
 #endif
 
   // FreeRTOS: CAN task runs concurrently with UI on the single RISC-V core
   // NOTE: ESP32-C6 is SINGLE-CORE — xTaskCreatePinnedToCore(..., 1) would crash!
   // Priority 2 > loop(1) so CAN gets time-sliced in, but yields on vTaskDelay
   carMutex = xSemaphoreCreateMutex();
   xTaskCreate(
     canTask,        // task function
     "CAN",          // task name
     4096,           // stack size (bytes)
     NULL,           // parameter
     1,              // priority 1 = same as loop (prevents stealing Serial RX)
     &canTaskHandle  // handle
   );
   Serial.println("   CAN task started (single-core, priority 1)");
   
   Serial.println("Showing boot screen...");
   showBootScreen();
   
   // Safe default values so checkAutoSwitch() doesn't fire alerts immediately
   car.battery  = 12.5f;
   car.distanceOffset = -1; // Flag to read initial OBD distance
   car.coolant  = 20;
   car.oil      = 20;
   car.intake   = 20;
   car.instantFuelLh = -1.0f; // -1 = PID 0x5E δεν έχει απαντήσει ακόμα
   car.maf       = 0.0f;
 
   Serial.println("Drawing UI...");
   drawUI();
 
   lastActivityMs = millis();
   Serial.println("=== SETUP COMPLETE ===");
   Serial.println();
   Serial.println("┌─────────────────────────────────────────┐");
   Serial.println("│  SERIAL COMMANDS  (No Line Ending mode) │");
   Serial.println("├─────────────────────────────────────────┤");
   Serial.println("│  sim:0         full car preset          │");
   Serial.println("│  reset:0       zero everything          │");
   Serial.println("│  rpm:3500                               │");
   Serial.println("│  speed:120                              │");
   Serial.println("│  coolant:87    oil:95   intake:28       │");
   Serial.println("│  throttle:45   boost:0.8                │");
   Serial.println("│  battery:14.2                           │");
   Serial.println("└─────────────────────────────────────────┘");
 }
 
 // ===== SLEEP / WAKE =====
 void wakeScreen() {
   if (!screenSleeping) return;
   screenSleeping = false;
   lastActivityMs = millis();
   ledcWrite(TFT_BL, currentBrightness);
   drawUI();  // drawUI sets uiNeedsFullRedraw=true → updateUI repaints values on next loop
   Serial.println("Screen WAKE");
 }
 
 void sleepScreen() {
   if (screenSleeping) return;
   screenSleeping = true;
   brightnessMode = false;          // cancel brightness overlay if open
   ledcWrite(TFT_BL, 0);           // backlight off
   tft->fillScreen(COLOR_BG);      // black screen
   Serial.println("Screen SLEEP");
 }
 
 void loop() {
   // CAN/OBD2 runs on core 1 via canTask() — do NOT call readOBD2() here
   handleSerial();
   handleTouch();
 
// Auto-sleep απενεργοποιημένο
  // if (!screenSleeping && (millis() - lastActivityMs > SLEEP_TIMEOUT_MS)) {
  //   sleepScreen();
  // }
 
   // Exit brightness mode after timeout (5 sec no touch)
   if (brightnessMode && millis() > brightnessModeTimeout) {
     brightnessMode = false;
     drawUI();
   }
   if (!brightnessMode && !screenSleeping) {
     checkAutoSwitch();
     updateAlertBlink();
     updateAccelState();  // Accel timer state machine
     updateUI();
   }
   updateRGB();
  delay(2); // Even smaller delay for more immediate UI/OBD updates
 }
 
 // ===== LOGIC =====
 
 void checkAutoSwitch() {
    static bool wasCoolant = false;
    static bool wasRPM     = false;
    static bool wasBatt    = false;
    static unsigned long engineStartedMs = 0;
  
    // Thread-safe reading of current car variables
    int currentRpm = 0;
    int currentSpeed = 0;
    float currentBattery = 0.0f;
    int currentCoolant = 0;
    if (carMutex && xSemaphoreTake(carMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      currentRpm = car.rpm;
      currentSpeed = car.speed;
      currentBattery = car.battery;
      currentCoolant = car.coolant;
      xSemaphoreGive(carMutex);
    } else {
      currentRpm = car.rpm;
      currentSpeed = car.speed;
      currentBattery = car.battery;
      currentCoolant = car.coolant;
    }

    bool isCoolant = currentCoolant > 95;
    // Track engine runtime to prevent false battery alarms during starting/cranking
     if (currentRpm > 500) {
      if (engineStartedMs == 0) {
        engineStartedMs = millis();
      }
    } else {
      engineStartedMs = 0;
    }
    bool isRPM     = currentRpm > 5500;
    // Battery alarm only triggers when the engine is running to avoid annoying false alerts at key-on (accessory mode is ~11V)
     bool isBatt    = false;
     if (currentRpm > 400 && currentBattery > 0.1f) {
       if (engineStartedMs > 0 && (millis() - engineStartedMs > 8000)) {
         isBatt = (currentBattery < 11.5 || currentBattery > 14.8);
       }
     }
    bool anyAlert  = isCoolant || isRPM || isBatt;
  
    // Όταν λήξει το 10s cooldown → επανενεργοποίηση alerts (αν η συνθήκη παραμένει → re-trigger)
    if (alertCooldownUntilMs > 0 && millis() >= alertCooldownUntilMs) {
      alertCooldownUntilMs = 0;
      alertDismissedMask   = 0;  // re-arm
      // Μηδένισε τα was-flags για ενεργές συνθήκες ώστε να εκληφθούν ως "νέες"
      if (isCoolant) wasCoolant = false;
      if (isRPM)     wasRPM     = false;
      if (isBatt)    wasBatt    = false;
    }
  
    // Κατά το 10s παράθυρο: ελεύθερη πλοήγηση, καμία ενέργεια
    if (alertCooldownUntilMs > 0) return;
  
    // Αν κάποιο alert και η οθόνη κοιμάται → ξύπνα πρώτα
    if (anyAlert && screenSleeping) wakeScreen();
  
    // Auto-dismiss αν όλες οι συνθήκες εξαφανισθούν
    if (!anyAlert && alertBlinking) {
      dismissAlert();
      return;
    }
  
    // Καθάρισε mask bits για συνθήκες που επανήλθαν σε κανονικά
    if (!isCoolant) alertDismissedMask &= ~0x01;
    if (!isRPM)     alertDismissedMask &= ~0x04;
    if (!isBatt)    alertDismissedMask &= ~0x08;
  
    // ── ACCEL LOCK: Αν μετράμε (ARMED/RUNNING/DONE) → ΜΗΝ αλλάζεις tab ──────
    // Ο χρήστης είναι ακόμα στο ACCEL tab, μόνο alert blink επιτρέπεται
    bool accelLocked = (currentTab == TAB_SYSTEM && currentItemIndex[TAB_SYSTEM] == 2 &&
                        (accelState == ACCEL_ARMED ||
                         accelState == ACCEL_RUNNING ||
                         accelState == ACCEL_DONE));
    if (accelLocked) {
      // Μόνο ανάβουμε το blink (χωρίς να αλλάξουμε tab)
      if (anyAlert && !alertBlinking) triggerAlert(alertBlinkColor != 0 ? alertBlinkColor : COLOR_RED);
      return;
    }
  
    // Ενώ αναβοσβήνει (πριν το πρώτο dismiss) → κλείδωσε την καρτέλα στο alert
    if (alertBlinking) {
      bool locked = false;
      if      (isCoolant) { switchTo(TAB_TEMPS, 0); locked = true; }
      else if (isRPM)     { switchTo(TAB_MAIN,  1); locked = true; }
      else if (isBatt)    { switchTo(TAB_SYSTEM, 0); locked = true; }
      if (locked) return;
    }
  
    // ===== RPM LOGIC (ΔΙΟΡΘΩΜΕΝΟ) =====
    if (isRPM && !(alertDismissedMask & 0x04)) {
      if (!wasRPM) {
        // Πρώτη φορά που μπαίνουμε σε επικίνδυνη ζώνη
        switchTo(TAB_MAIN, 1);
        triggerAlert(COLOR_RED);
        wasRPM = true;
        return;  // Μπλοκάρισμα μόνο την πρώτη φορά
      }
      // Ήδη ήμασταν σε επικίνδυνη ζώνη - ΜΗΝ κάνεις return!
      // Απλά ensure ότι είμαστε στο σωστό tab (χωρίς να μπλοκάρεις το UI)
      if (currentTab != TAB_MAIN || currentItemIndex[TAB_MAIN] != 1) {
        switchTo(TAB_MAIN, 1);
      }
    } else {
      wasRPM = isRPM;  // Μηδενίζεται όταν isRPM=false
    }
  
    // ===== COOLANT LOGIC (ίδιο pattern) =====
    if (isCoolant && !(alertDismissedMask & 0x01)) {
      if (!wasCoolant) {
        switchTo(TAB_TEMPS, 0);
        triggerAlert(COLOR_RED);
        wasCoolant = true;
        return;
      }
      if (currentTab != TAB_TEMPS || currentItemIndex[TAB_TEMPS] != 0) {
        switchTo(TAB_TEMPS, 0);
      }
    } else {
      wasCoolant = isCoolant;
    }
  
    // ===== BATTERY LOGIC =====
    if (isBatt && !(alertDismissedMask & 0x08)) {
      if (!wasBatt) {
        switchTo(TAB_SYSTEM, 0);
        triggerAlert(COLOR_RED);
        wasBatt = true;
        return;
      }
      if (currentTab != TAB_SYSTEM || currentItemIndex[TAB_SYSTEM] != 0) {
        switchTo(TAB_SYSTEM, 0);
      }
    } else {
      wasBatt = isBatt;
    }
 
    // ===== ENGINE STARTUP STATE MACHINE =====
    static enum {
      STATE_ENGINE_OFF,
      STATE_WARMUP,
      STATE_STABILIZED
    } startupState = STATE_ENGINE_OFF;

    if (startupState == STATE_ENGINE_OFF) {
      // Default/force volts tab when engine is off or cranking (currentRpm <= 500)
      if (currentRpm <= 500) {
        if (currentTab != TAB_SYSTEM || currentItemIndex[TAB_SYSTEM] != 0) {
          // If the user hasn't touched the screen for 10 seconds, go back to Volts
          if (millis() - lastActivityMs > 10000) {
            switchTo(TAB_SYSTEM, 0);
          }
        }
      }

      // Once the engine successfully fires and RPM goes above 500 (running)
      if (currentRpm > 500) {
        if (currentRpm > 765 && currentSpeed == 0) {
          // Cold start: RPM is high, and vehicle is stationary. Go to RPM tab (TAB_MAIN, 1).
          startupState = STATE_WARMUP;
          switchTo(TAB_MAIN, 1); // Force switch to RPM tab
        } else {
          // Warm start: engine starts and idle is already <= 765, OR already driving. Go straight to Speed.
          startupState = STATE_STABILIZED;
          switchTo(TAB_MAIN, 0); // Switch to Speed tab
        }
      }
    }
    else if (startupState == STATE_WARMUP) {
      // If engine turns off or drops below running threshold (RPM <= 500), reset to STATE_ENGINE_OFF and immediately switch to Volts tab
      if (currentRpm <= 500) {
        startupState = STATE_ENGINE_OFF;
        switchTo(TAB_SYSTEM, 0);
      }
      // If RPM drops to 765 or below, OR the car starts moving (speed > 0)
      else if (currentRpm <= 765 || currentSpeed > 0) {
        startupState = STATE_STABILIZED;
        switchTo(TAB_MAIN, 0); // Switch to Speed tab
      }
      else {
        // Keep/force staying on the RPM tab during warmup
        if (currentTab != TAB_MAIN || currentItemIndex[TAB_MAIN] != 1) {
          switchTo(TAB_MAIN, 1);
        }
      }
    }
    else if (startupState == STATE_STABILIZED) {
      // If engine turns off (RPM <= 500), reset to STATE_ENGINE_OFF and immediately switch to Volts tab
       if (currentRpm <= 500) {
        startupState = STATE_ENGINE_OFF;
        switchTo(TAB_SYSTEM, 0);
      }
    }

    // ===== AUTO-SWITCH TO SPEED (LEGACY BACKUP) =====
    static bool hasAutoSwitchedToSpeed = false;
    if (!hasAutoSwitchedToSpeed && currentSpeed > 0) {
      // Μόλις ξεκινήσουν τα χιλιόμετρα για πρώτη φορά (στο άναμμα του ESP32), πάμε στην καρτέλα Speed 
      switchTo(TAB_MAIN, 0);
      hasAutoSwitchedToSpeed = true; // Το κλειδώνουμε ώστε να μην ξαναγίνει στα φανάρια
    }
}
 
 // ===== DRAWING FUNCTIONS =====
 
 // Staggered-strip tab transition
 // forward=true  → swipe UP   (next tab): top strips clear first,   L→R wipe
 // forward=false → swipe DOWN (prev tab): bottom strips clear first, R→L wipe
 // Each strip clears with smoothstep easing and a BLUE accent line at the
 // leading edge, creating a cascading "venetian blind" effect (~220ms total).
 // Tab transition — "Venetian Blinds" effect
 // 12 οριζόντιες λωρίδες κλείνουν σαν περσίδα, η μία μετά την άλλη.
 // forward=true → cascade top→bottom  |  false → cascade bottom→top
 void showTabTransition(bool forward) {
   const int cY       = 30;
   const int cH       = SCREEN_HEIGHT - cY;  // 142px
   const int W        = SCREEN_WIDTH;         // 320px
   const int nStrips  = 8;
   const int stripH   = (cH + nStrips - 1) / nStrips;  // ~12px each
   const int sweepSteps = 8;    // steps to sweep one strip
   const int stagger    = 1 ;    // frame offset between strips
   const int totalF     = sweepSteps + (nStrips - 1) * stagger;  // ~35 frames

   for (int frame = 0; frame <= totalF; frame++) {
     for (int s = 0; s < nStrips; s++) {
       int order = forward ? s : (nStrips - 1 - s);
       int sf    = order * stagger;
       if (frame < sf) continue;

       float lt = (float)(frame - sf) / sweepSteps;
       lt = constrain(lt, 0.0f, 1.0f);
       lt = lt * lt * (3.0f - 2.0f * lt);  // smoothstep

       int y = cY + s * stripH;
       int h = min(stripH, cH - s * stripH);
       if (h <= 0) continue;

       int filled = (int)(lt * (W + 1));
       if (filled > W) filled = W;
       if (filled <= 0) continue;

       if (forward) {
         // Κάθε λωρίδα σαρώνει L→R
         tft->fillRect(0, y, filled, h, COLOR_BG);
         if (filled < W && lt < 1.0f) {
           tft->drawFastVLine(filled,     y, h, COLOR_BLUE);
           if (filled > 0) tft->drawFastVLine(filled - 1, y, h, COLOR_GRAY);
         }
       } else {
         // Κάθε λωρίδα σαρώνει R→L
         int x0 = W - filled;
         tft->fillRect(x0, y, filled, h, COLOR_BG);
         if (x0 > 0 && lt < 1.0f) {
           tft->drawFastVLine(x0,     y, h, COLOR_BLUE);
           if (x0 + 1 < W) tft->drawFastVLine(x0 + 1, y, h, COLOR_GRAY);
         }
       }
     }
     delay(3);
   }
 }

 // Item transition — κατακόρυφο wipe με glowing horizontal γραμμή
 // swipe LEFT  (next item) → σβήνει από πάνω → κάτω
 // swipe RIGHT (prev item) → σβήνει από κάτω → πάνω
 void showItemTransition(bool goLeft) {
   const int cY    = 30;
   const int cH    = SCREEN_HEIGHT - cY;
   const int W     = SCREEN_WIDTH;
   const int steps = 8;
   uint16_t acc    = getCurrentItem().color;

   for (int i = 1; i <= steps; i++) {
     float t  = (float)i / steps;
     float te = t * t * (3.0f - 2.0f * t);  // smoothstep

     int filled = (int)(te * cH);
     if (filled > cH) filled = cH;

     if (goLeft) {
       // Σβήνει από πάνω → κάτω
       if (filled > 0) tft->fillRect(0, cY, W, filled, COLOR_BG);
       if (filled < cH && i < steps) {
         tft->drawFastHLine(0, cY + filled,     W, acc);
         if (cY + filled + 1 < SCREEN_HEIGHT)
           tft->drawFastHLine(0, cY + filled + 1, W, COLOR_GRAY);
         if (cY + filled + 2 < SCREEN_HEIGHT)
           tft->drawFastHLine(0, cY + filled + 2, W, COLOR_DARK);
       }
     } else {
       // Σβήνει από κάτω → πάνω
       int y0 = cY + cH - filled;
       if (filled > 0) tft->fillRect(0, y0, W, filled, COLOR_BG);
       if (y0 > cY && i < steps) {
         tft->drawFastHLine(0, y0 - 1, W, acc);
         if (y0 - 2 >= cY)
           tft->drawFastHLine(0, y0 - 2, W, COLOR_GRAY);
         if (y0 - 3 >= cY)
           tft->drawFastHLine(0, y0 - 3, W, COLOR_DARK);
       }
     }
     delay(3);
   }
 }
 
 // ── VW Logo Bitmap (112x112, generated from logo.png) ───────────────────────
 // This data was exported from the high-quality VW logo image (logo.png)
 // using a 1-bit monochrome converter so it renders clean and sharp.
 const unsigned char vw_logo_bmp[] PROGMEM = {
 0x00,0x00,0x00,0x00,0x00,0x00,0x3F,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,
 0x00,0x00,0x00,0x00,0x00,0x0F,0xFF,0xFF,0xF8,0x00,0x00,0x00,0x00,0x00,
 0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x80,0x00,0x00,0x00,0x00,
 0x00,0x00,0x00,0x00,0x07,0xFF,0xFF,0xFF,0xFF,0xF0,0x00,0x00,0x00,0x00,
 0x00,0x00,0x00,0x00,0x1F,0xFF,0xFF,0xFF,0xFF,0xFC,0x00,0x00,0x00,0x00,
 0x00,0x00,0x00,0x00,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0x80,0x00,0x00,0x00,
 0x00,0x00,0x00,0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xE0,0x00,0x00,0x00,
 0x00,0x00,0x00,0x07,0xFF,0xFF,0xC0,0x01,0xFF,0xFF,0xF0,0x00,0x00,0x00,
 0x00,0x00,0x00,0x1F,0xFF,0xF8,0x00,0x00,0x0F,0xFF,0xFC,0x00,0x00,0x00,
 0x00,0x00,0x00,0x3F,0xFF,0xC0,0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,0x00,
 0x00,0x00,0x00,0xFF,0xFF,0x80,0x00,0x00,0x00,0xFF,0xFF,0x80,0x00,0x00,
 0x00,0x00,0x01,0xFF,0xFF,0xC0,0x00,0x00,0x00,0xFF,0xFF,0xC0,0x00,0x00,
 0x00,0x00,0x07,0xFF,0xFF,0xC0,0x00,0x00,0x01,0xFF,0xFF,0xF0,0x00,0x00,
 0x00,0x00,0x0F,0xFF,0xFF,0xC0,0x00,0x00,0x01,0xFF,0xFF,0xF8,0x00,0x00,
 0x00,0x00,0x1F,0xFE,0x7F,0xE0,0x00,0x00,0x03,0xFF,0x3F,0xFC,0x00,0x00,
 0x00,0x00,0x3F,0xF8,0x3F,0xE0,0x00,0x00,0x03,0xFE,0x0F,0xFE,0x00,0x00,
 0x00,0x00,0x7F,0xF0,0x3F,0xF0,0x00,0x00,0x07,0xFE,0x07,0xFF,0x00,0x00,
 0x00,0x00,0xFF,0xC0,0x1F,0xF0,0x00,0x00,0x07,0xFC,0x01,0xFF,0x80,0x00,
 0x00,0x01,0xFF,0x80,0x1F,0xF8,0x00,0x00,0x0F,0xFC,0x00,0xFF,0xC0,0x00,
 0x00,0x03,0xFF,0x00,0x0F,0xF8,0x00,0x00,0x0F,0xF8,0x00,0x7F,0xE0,0x00,
 0x00,0x07,0xFE,0x00,0x0F,0xFC,0x00,0x00,0x0F,0xF8,0x00,0x3F,0xF0,0x00,
 0x00,0x0F,0xFC,0x00,0x0F,0xFC,0x00,0x00,0x1F,0xF8,0x00,0x1F,0xF8,0x00,
 0x00,0x0F,0xF8,0x00,0x07,0xFE,0x00,0x00,0x1F,0xF0,0x00,0x0F,0xF8,0x00,
 0x00,0x1F,0xF0,0x00,0x07,0xFE,0x00,0x00,0x3F,0xF0,0x00,0x07,0xFC,0x00,
 0x00,0x3F,0xE0,0x00,0x03,0xFF,0x00,0x00,0x3F,0xE0,0x00,0x03,0xFE,0x00,
 0x00,0x3F,0xC0,0x00,0x03,0xFF,0x00,0x00,0x7F,0xE0,0x00,0x01,0xFF,0x00,
 0x00,0x7F,0x80,0x00,0x01,0xFF,0x00,0x00,0x7F,0xC0,0x00,0x00,0xFF,0x00,
 0x00,0xFF,0x80,0x00,0x01,0xFF,0x80,0x00,0xFF,0xC0,0x00,0x00,0xFF,0x80,
 0x00,0xFF,0x00,0x00,0x00,0xFF,0x80,0x00,0xFF,0x80,0x00,0x00,0x7F,0x80,
 0x01,0xFE,0x00,0x00,0x00,0xFF,0xC0,0x01,0xFF,0x80,0x00,0x00,0x3F,0xC0,
 0x01,0xFE,0x00,0x00,0x00,0x7F,0xC0,0x01,0xFF,0x00,0x00,0x00,0x3F,0xE0,
 0x03,0xFE,0x00,0x00,0x00,0x7F,0xE0,0x03,0xFF,0x00,0x00,0x00,0x3F,0xE0,
 0x03,0xFF,0x00,0x00,0x00,0x3F,0xE0,0x03,0xFE,0x00,0x00,0x00,0x7F,0xF0,
 0x07,0xFF,0x00,0x00,0x00,0x3F,0xF0,0x07,0xFE,0x00,0x00,0x00,0x7F,0xF0,
 0x07,0xFF,0x80,0x00,0x00,0x1F,0xF0,0x07,0xFE,0x00,0x00,0x00,0xFF,0xF0,
 0x0F,0xFF,0x80,0x00,0x00,0x1F,0xF8,0x07,0xFC,0x00,0x00,0x00,0xFF,0xF8,
 0x0F,0xFF,0xC0,0x00,0x00,0x1F,0xF8,0x0F,0xFC,0x00,0x00,0x01,0xFF,0xF8,
 0x0F,0xFF,0xC0,0x00,0x00,0x0F,0xFC,0x0F,0xF8,0x00,0x00,0x01,0xFF,0xFC,
 0x1F,0xFF,0xE0,0x00,0x00,0x0F,0xFC,0x1F,0xF8,0x00,0x00,0x03,0xFF,0xFC,
 0x1F,0xFF,0xE0,0x00,0x00,0x07,0xFE,0x1F,0xF0,0x00,0x00,0x03,0xFF,0xFC,
 0x1F,0xFF,0xF0,0x00,0x00,0x07,0xFE,0x3F,0xF0,0x00,0x00,0x07,0xFF,0xFE,
 0x3F,0xDF,0xF0,0x00,0x00,0x03,0xFE,0x3F,0xE0,0x00,0x00,0x07,0xFE,0xFE,
 0x3F,0x9F,0xF8,0x00,0x00,0x03,0xFF,0x7F,0xE0,0x00,0x00,0x0F,0xFC,0xFE,
 0x3F,0x8F,0xF8,0x00,0x00,0x01,0xFF,0xFF,0xC0,0x00,0x00,0x0F,0xFC,0xFE,
 0x3F,0x8F,0xFC,0x00,0x00,0x01,0xFF,0xFF,0xC0,0x00,0x00,0x1F,0xF8,0xFF,
 0x7F,0x87,0xFC,0x00,0x00,0x00,0xFF,0xFF,0xC0,0x00,0x00,0x1F,0xF8,0x7F,
 0x7F,0x07,0xFE,0x00,0x00,0x00,0xFF,0xFF,0x80,0x00,0x00,0x3F,0xF0,0x7F,
 0x7F,0x03,0xFE,0x00,0x00,0x00,0x7F,0xFF,0x80,0x00,0x00,0x3F,0xF0,0x7F,
 0x7F,0x03,0xFF,0x00,0x00,0x00,0x7F,0xFF,0x00,0x00,0x00,0x7F,0xE0,0x7F,
 0x7F,0x01,0xFF,0x00,0x00,0x00,0x7F,0xFF,0x00,0x00,0x00,0x7F,0xE0,0x7F,
 0x7F,0x01,0xFF,0x80,0x00,0x00,0x3F,0xFE,0x00,0x00,0x00,0xFF,0xC0,0x3F,
 0x7F,0x00,0xFF,0xC0,0x00,0x00,0x3F,0xFE,0x00,0x00,0x00,0xFF,0xC0,0x3F,
 0xFF,0x00,0xFF,0xC0,0x00,0x00,0x1F,0xFC,0x00,0x00,0x01,0xFF,0x80,0x3F,
 0xFF,0x00,0x7F,0xE0,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0xFF,0x80,0x3F,
 0xFE,0x00,0x7F,0xE0,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0xFF,0x00,0x3F,
 0xFE,0x00,0x3F,0xF0,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xFF,0x00,0x3F,
 0xFE,0x00,0x3F,0xF0,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xFE,0x00,0x3F,
 0xFE,0x00,0x1F,0xF8,0x00,0x00,0x1F,0xFE,0x00,0x00,0x0F,0xFE,0x00,0x3F,
 0xFE,0x00,0x1F,0xF8,0x00,0x00,0x3F,0xFE,0x00,0x00,0x0F,0xFC,0x00,0x3F,
 0xFF,0x00,0x0F,0xFC,0x00,0x00,0x3F,0xFE,0x00,0x00,0x1F,0xF8,0x00,0x3F,
 0xFF,0x00,0x0F,0xFC,0x00,0x00,0x7F,0xFF,0x00,0x00,0x1F,0xF8,0x00,0x3F,
 0x7F,0x00,0x07,0xFE,0x00,0x00,0x7F,0xFF,0x00,0x00,0x3F,0xF0,0x00,0x3F,
 0x7F,0x00,0x07,0xFE,0x00,0x00,0xFF,0xFF,0x80,0x00,0x3F,0xF0,0x00,0x3F,
 0x7F,0x00,0x03,0xFF,0x00,0x00,0xFF,0xFF,0x80,0x00,0x7F,0xE0,0x00,0x7F,
 0x7F,0x00,0x03,0xFF,0x00,0x01,0xFF,0xFF,0xC0,0x00,0x7F,0xE0,0x00,0x7F,
 0x7F,0x00,0x01,0xFF,0x80,0x01,0xFF,0xFF,0xC0,0x00,0xFF,0xC0,0x00,0x7F,
 0x7F,0x00,0x00,0xFF,0x80,0x01,0xFF,0x7F,0xE0,0x00,0xFF,0xC0,0x00,0x7F,
 0x7F,0x80,0x00,0xFF,0xC0,0x03,0xFF,0x3F,0xE0,0x01,0xFF,0x80,0x00,0x7F,
 0x3F,0x80,0x00,0x7F,0xC0,0x03,0xFE,0x3F,0xF0,0x01,0xFF,0x80,0x00,0xFF,
 0x3F,0x80,0x00,0x7F,0xE0,0x07,0xFE,0x3F,0xF0,0x03,0xFF,0x00,0x00,0xFE,
 0x3F,0x80,0x00,0x3F,0xE0,0x07,0xFC,0x1F,0xF0,0x03,0xFF,0x00,0x00,0xFE,
 0x3F,0xC0,0x00,0x3F,0xF0,0x0F,0xFC,0x1F,0xF8,0x07,0xFE,0x00,0x01,0xFE,
 0x1F,0xC0,0x00,0x1F,0xF0,0x0F,0xF8,0x0F,0xF8,0x07,0xFE,0x00,0x01,0xFE,
 0x1F,0xC0,0x00,0x1F,0xF8,0x1F,0xF8,0x0F,0xFC,0x0F,0xFC,0x00,0x01,0xFC,
 0x1F,0xE0,0x00,0x0F,0xFC,0x1F,0xF8,0x07,0xFC,0x0F,0xFC,0x00,0x03,0xFC,
 0x0F,0xE0,0x00,0x0F,0xFC,0x3F,0xF0,0x07,0xFE,0x1F,0xF8,0x00,0x03,0xFC,
 0x0F,0xF0,0x00,0x07,0xFE,0x3F,0xF0,0x03,0xFE,0x1F,0xF8,0x00,0x03,0xF8,
 0x0F,0xF0,0x00,0x07,0xFE,0x3F,0xE0,0x03,0xFF,0x3F,0xF0,0x00,0x07,0xF8,
 0x07,0xF8,0x00,0x03,0xFF,0x7F,0xE0,0x01,0xFF,0x3F,0xF0,0x00,0x07,0xF0,
 0x07,0xF8,0x00,0x03,0xFF,0xFF,0xC0,0x01,0xFF,0xFF,0xE0,0x00,0x0F,0xF0,
 0x03,0xFC,0x00,0x01,0xFF,0xFF,0xC0,0x01,0xFF,0xFF,0xE0,0x00,0x0F,0xE0,
 0x03,0xFC,0x00,0x01,0xFF,0xFF,0x80,0x00,0xFF,0xFF,0xC0,0x00,0x1F,0xE0,
 0x01,0xFE,0x00,0x00,0xFF,0xFF,0x80,0x00,0xFF,0xFF,0xC0,0x00,0x3F,0xC0,
 0x01,0xFF,0x00,0x00,0xFF,0xFF,0x00,0x00,0x7F,0xFF,0x80,0x00,0x3F,0xC0,
 0x00,0xFF,0x00,0x00,0x7F,0xFF,0x00,0x00,0x7F,0xFF,0x80,0x00,0x7F,0x80,
 0x00,0xFF,0x80,0x00,0x7F,0xFF,0x00,0x00,0x3F,0xFF,0x00,0x00,0xFF,0x80,
 0x00,0x7F,0xC0,0x00,0x3F,0xFE,0x00,0x00,0x3F,0xFF,0x00,0x00,0xFF,0x00,
 0x00,0x3F,0xE0,0x00,0x3F,0xFE,0x00,0x00,0x1F,0xFE,0x00,0x01,0xFE,0x00,
 0x00,0x3F,0xE0,0x00,0x1F,0xFC,0x00,0x00,0x1F,0xFE,0x00,0x03,0xFE,0x00,
 0x00,0x1F,0xF0,0x00,0x1F,0xFC,0x00,0x00,0x0F,0xFC,0x00,0x07,0xFC,0x00,
 0x00,0x0F,0xF8,0x00,0x0F,0xF8,0x00,0x00,0x0F,0xFC,0x00,0x0F,0xF8,0x00,
 0x00,0x07,0xFC,0x00,0x0F,0xF8,0x00,0x00,0x07,0xF8,0x00,0x1F,0xF8,0x00,
 0x00,0x07,0xFE,0x00,0x07,0xF0,0x00,0x00,0x07,0xF8,0x00,0x3F,0xF0,0x00,
 0x00,0x03,0xFF,0x00,0x07,0xF0,0x00,0x00,0x07,0xF0,0x00,0x7F,0xE0,0x00,
 0x00,0x01,0xFF,0x80,0x03,0xF0,0x00,0x00,0x03,0xE0,0x00,0xFF,0xC0,0x00,
 0x00,0x00,0xFF,0xE0,0x03,0xE0,0x00,0x00,0x03,0xE0,0x03,0xFF,0x80,0x00,
 0x00,0x00,0x7F,0xF0,0x01,0xE0,0x00,0x00,0x01,0xC0,0x07,0xFF,0x00,0x00,
 0x00,0x00,0x3F,0xF8,0x00,0xC0,0x00,0x00,0x00,0xC0,0x0F,0xFE,0x00,0x00,
 0x00,0x00,0x1F,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x3F,0xFC,0x00,0x00,
 0x00,0x00,0x0F,0xFF,0x80,0x00,0x00,0x00,0x00,0x00,0xFF,0xF8,0x00,0x00,
 0x00,0x00,0x03,0xFF,0xE0,0x00,0x00,0x00,0x00,0x01,0xFF,0xF0,0x00,0x00,
 0x00,0x00,0x01,0xFF,0xF8,0x00,0x00,0x00,0x00,0x07,0xFF,0xC0,0x00,0x00,
 0x00,0x00,0x00,0xFF,0xFE,0x00,0x00,0x00,0x00,0x3F,0xFF,0x80,0x00,0x00,
 0x00,0x00,0x00,0x3F,0xFF,0xC0,0x00,0x00,0x01,0xFF,0xFE,0x00,0x00,0x00,
 0x00,0x00,0x00,0x1F,0xFF,0xF8,0x00,0x00,0x0F,0xFF,0xFC,0x00,0x00,0x00,
 0x00,0x00,0x00,0x07,0xFF,0xFF,0xE0,0x03,0xFF,0xFF,0xF0,0x00,0x00,0x00,
 0x00,0x00,0x00,0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xC0,0x00,0x00,0x00,
 0x00,0x00,0x00,0x00,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,
 0x00,0x00,0x00,0x00,0x1F,0xFF,0xFF,0xFF,0xFF,0xFC,0x00,0x00,0x00,0x00,
 0x00,0x00,0x00,0x00,0x03,0xFF,0xFF,0xFF,0xFF,0xE0,0x00,0x00,0x00,0x00,
 0x00,0x00,0x00,0x00,0x00,0x7F,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,
 0x00,0x00,0x00,0x00,0x00,0x07,0xFF,0xFF,0xF0,0x00,0x00,0x00,0x00,0x00,
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
 };
 
 void showBootScreen() {
   tft->fillScreen(COLOR_BLUE);  // Μπλε background για το boot
 
   int cx = SCREEN_WIDTH / 2;
   int cy = SCREEN_HEIGHT / 2 - 12;
 
   // ── VW Logo Reveal Animation ────────────────
   // Λευκό εξωτερικό περίγραμμα, μπλε εσωτερικό
   for (int R = 6; R <= 54; R += 3) {
     tft->fillCircle(cx, cy, R, COLOR_TEXT);           // λευκό περίγραμμα
     tft->fillCircle(cx, cy, R * 94 / 100, COLOR_BLUE); // εσωτερικό μπλε
     delay(20);
   }
 
   // Draw the exact 112x112 VW logo bitmap — λευκό logo πάνω σε μπλε φόντο
   // Arduino_GFX drawBitmap requires 7 arguments: x, y, bitmap, w, h, color, background
   int bmpW = 112, bmpH = 112;
   int finalR = 54;
   tft->drawBitmap(cx - bmpW/2, cy - bmpH/2, vw_logo_bmp, bmpW, bmpH, COLOR_TEXT, COLOR_BLUE);
 
   // Μικρή παύση ώστε να φανεί καθαρά πρώτα μόνο το logo
   delay(500);
 
   // ── "Welcome Lefo" — γράμμα-γράμμα εμφάνιση κάτω από το logo ────────────
   setU8g2Font(3);  // helvB18
   u8g2.setFontMode(1);
   u8g2.setForegroundColor(COLOR_TEXT);
   const char* msg = "Welcome Lefo";
   int msgLen = strlen(msg);
   int mw = u8g2.getUTF8Width(msg);
   int logoBottom = cy + finalR;
   int textZoneY  = logoBottom + 4;
   int textZoneH  = SCREEN_HEIGHT - textZoneY;
   int textY      = textZoneY + (textZoneH + u8g2.getFontAscent()) / 2;
   int textX      = (SCREEN_WIDTH - mw) / 2;

   // Τυπώνουμε γράμμα-γράμμα με καθυστέρηση 80ms — χωρίς καθόλου σβήσιμο
   char buf[32];
   for (int i = 1; i <= msgLen; i++) {
     strncpy(buf, msg, i);
     buf[i] = '\0';
     u8g2.setCursor(textX, textY);
     u8g2.print(buf);
     delay(80);
   }

   // Μένει 1.5 δευτερόλεπτα και μετά πάει στο main
   delay(1500);
 }
 
 // Brightness overlay — drawn over current content
 void drawBrightnessOverlay() {
   // Panel background
   tft->fillRect(25, 55, 270, 80, COLOR_DARK);
   tft->drawRect(25, 55, 270, 80, COLOR_GRAY);
 
   // Title
   setU8g2Font(2);
   u8g2.setForegroundColor(COLOR_TEXT);
   String title = "< BRIGHTNESS >";
   int tw = u8g2.getUTF8Width(title.c_str());
   u8g2.setCursor((SCREEN_WIDTH - tw) / 2, 62 + u8g2.getFontAscent());
   u8g2.print(title.c_str());
 
   // Bar track
   tft->fillRect(35, 84, 250, 14, COLOR_BG);
   // Bar fill
   int barW = (int)(250.0f * currentBrightness / 255.0f);
   if (barW > 0) tft->fillRect(35, 84, barW, 14, COLOR_YELLOW);
 
   // Percentage
   int pct = (int)(currentBrightness * 100 / 255);
   String pctStr = String(pct) + "%";
   setU8g2Font(2);
   tw = u8g2.getUTF8Width(pctStr.c_str());
   u8g2.setCursor((SCREEN_WIDTH - tw) / 2, 104 + u8g2.getFontAscent());
   u8g2.print(pctStr.c_str());
 }
 
 // Legacy one-shot flash (kept for compatibility, now unused by checkAutoSwitch)
 void showAlertFlash(uint16_t color) { (void)color; }
 
 // Start continuous border blink — called once when threshold is newly crossed
 void triggerAlert(uint16_t color) {
   alertBlinking     = true;
   alertBlinkColor   = color;
   alertBorderOn     = false;
   alertNextBlinkMs  = millis() + 120;  // first blink very soon
   Serial.printf("ALERT triggered color=0x%04X\n", color);
 }
 
 // Flash only the FILLED portion of the gauge arc — uses drawFastHLine for instant render
 void drawAlertArc(uint16_t color) {
   const int cx     = SCREEN_WIDTH / 2;
   const int cy     = 165;
   const int outerR = 130;
   const int innerR = 112;
   float fillAngleDeg = 180.0f * lastGaugePct;
 
   if (fillAngleDeg <= 0.0f) return;
 
   // Helper to avoid atan2f — maps angle directly to X bound
   auto getXCut = [](float angle, int dy, int outerX) -> int {
     if (angle <= 0.0f) return -outerX - 1;
     if (angle >= 179.9f) return outerX + 1;
     if (angle == 90.0f) return 0;
     float rad = angle * (float)M_PI / 180.0f;
     int xc = (int)((float)dy / tanf(rad));
     if (xc < -outerX - 1) xc = -outerX - 1;
     if (xc > outerX + 1)  xc = outerX + 1;
     return xc;
   };

   for (int y = cy - outerR; y <= cy; y++) {
     if (y < 0 || y >= SCREEN_HEIGHT) continue;
     int dy  = y - cy;   // always <= 0
     float dy2 = (float)(dy * dy);
 
     int outerX = (int)sqrtf((float)(outerR * outerR) - dy2);
     int innerX = 0;
     if (dy2 < (float)(innerR * innerR)) {
         innerX = (int)ceilf(sqrtf((float)(innerR * innerR) - dy2));
     }
 
     int xCutAbs = cx + getXCut(fillAngleDeg, dy, outerX);

     int spans[2][2];
     int numSpans = 0;
     if (innerX > 0) {
         spans[0][0] = cx - outerX; spans[0][1] = cx - innerX;
         spans[1][0] = cx + innerX; spans[1][1] = cx + outerX;
         numSpans = 2;
     } else {
         spans[0][0] = cx - outerX; spans[0][1] = cx + outerX;
         numSpans = 1;
     }

     for (int s = 0; s < numSpans; s++) {
         int S_start = spans[s][0];
         int S_end   = spans[s][1];
         if (S_start > S_end) continue;

         int f_end = min(S_end, xCutAbs);
         if (S_start <= f_end) {
             tft->drawFastHLine(S_start, y, f_end - S_start + 1, color);
         }
     }
   }
 }
 
 // Stop blinking and restore gauge — called when user touches OR condition clears
 void dismissAlert() {
    alertBlinking  = false;
    alertBorderOn  = false;
    alertDismissedMask =
      (car.coolant > 95                            ? 0x01 : 0) |
      (car.rpm > 5500                              ? 0x04 : 0) |
      (((car.rpm > 400) && (car.battery < 11.5 || car.battery > 14.8)) ? 0x08 : 0);
    // Restore gauge to real colors immediately (full redraw — color changed)
    if (lastGaugePct > 0.0f && lastGaugeFill != 0) {
      drawGauge(SCREEN_WIDTH / 2, 165, 130, 112,
                lastGaugePct, lastGaugeTrack, lastGaugeFill,
                lastGaugeValStr, lastGaugeUnitStr, lastGaugeValSize,
                -1.0f);  // force full redraw
    } else {
      uiNeedsFullRedraw = true;
    }
    // Erase border
    for (int t = 0; t < 4; t++)
      tft->drawRect(t, 30 + t, SCREEN_WIDTH - 2*t, SCREEN_HEIGHT - 30 - 2*t, COLOR_BG);
    // LED off
    pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    pixels.show();
    Serial.printf("Alert DISMISSED mask=0x%02X\n", alertDismissedMask);
}
 
 // Called every loop iteration — toggles semicircle arc blink every 400ms
 void updateAlertBlink() {
   if (!alertBlinking) return;
   if (millis() < alertNextBlinkMs) return;
   alertNextBlinkMs = millis() + 100;  // 5Hz = 100ms per half-cycle
   alertBorderOn = !alertBorderOn;
 
   if (alertBorderOn) {
     // Flash filled arc with alert color
     drawAlertArc(alertBlinkColor);
     // Draw border around content area
     for (int t = 0; t < 4; t++)
       tft->drawRect(t, 30 + t, SCREEN_WIDTH - 2*t, SCREEN_HEIGHT - 30 - 2*t, alertBlinkColor);
   } else {
     // OFF: paint filled arc with track color so it visually disappears
     drawAlertArc(lastGaugeTrack);
     // Erase border
     for (int t = 0; t < 4; t++)
       tft->drawRect(t, 30 + t, SCREEN_WIDTH - 2*t, SCREEN_HEIGHT - 30 - 2*t, COLOR_BG);
   }
 
   // Update RGB LED in sync with semicircle
   if (alertBorderOn) {
     uint8_t lr = 0, lg = 0, lb = 0;
     if      (alertBlinkColor == COLOR_RED)    lr = 255;
     else if (alertBlinkColor == COLOR_ORANGE) { lr = 255; lg = 80; }
     else                                      { lr = 255; lg = 255; lb = 255; }
     pixels.setPixelColor(0, pixels.Color(lr, lg, lb));
   } else {
     pixels.setPixelColor(0, pixels.Color(0, 0, 0));
   }
   pixels.show();
 }
 
 
 void drawUI() {
   uiNeedsFullRedraw = true;  // tell updateUI() to repaint values after skeleton is drawn
   tft->fillScreen(COLOR_BG);
 
   // ── Header bar ──────────────────────────────────────────
   tft->fillRect(0, 0, SCREEN_WIDTH, 30, COLOR_HEADER);
 
   // Tab name centered
   setU8g2Font(3);  // helvB18 — μεγαλύτερη γραμματοσειρά για την πάνω μπάρα
   u8g2.setForegroundColor(COLOR_TEXT);
   String title = tabNames[currentTab];
   int tW = u8g2.getUTF8Width(title.c_str());
   u8g2.setCursor((SCREEN_WIDTH - tW) / 2, 4 + u8g2.getFontAscent());
   u8g2.print(title.c_str());
 
   // Tab indicators: capsule for active, outline circle for others
   int dotStartX = 253;  // 5 tabs × 14px = 70px → 253+56=309 < 320
   for (int i = 0; i < TAB_COUNT; i++) {
     int cx = dotStartX + (i * 14);
     if (i == currentTab) {
       tft->fillRoundRect(cx - 8, 11, 16, 8, 4, COLOR_RED);
     } else {
       tft->drawCircle(cx, 15, 3, COLOR_DARK);
     }
   }
 
   // ── Left-side item position dots ────────────────────────
   uint16_t accent = getCurrentItem().color;
   int totalItems = getItemsCount(currentTab);
   const int lineH = 10;
   const int gap   = 7;
   int totalH = (totalItems * lineH) + ((totalItems - 1) * gap);
   int startY = 31 + (141 - totalH) / 2;
 
   for (int i = 0; i < totalItems; i++) {
     int iy = startY + i * (lineH + gap);
     if (i == currentItemIndex[currentTab]) {
       tft->fillRoundRect(4, iy - 2, 5, lineH + 4, 2, accent);
     } else {
       tft->fillRoundRect(5, iy + 1, 3, lineH - 2, 1, COLOR_DARK);
     }
   }
 }
 
 // Semicircle gauge — pixel-scan renderer ─────────────────
 // Perfect fill, no radial-line gaps. cy = flat base (can be below screen).
 // Arc: from left (180°) clockwise through top to right (360°).
 void drawGauge(int cx, int cy, int outerR, int innerR, float pct,
                uint16_t trackColor, uint16_t fillColor,
                String valStr, String unitStr, uint8_t valSize,
                float prevPct, bool skipText) {
   pct = constrain(pct, 0.0f, 1.0f);
   float fillAngleDeg = 180.0f * pct;
 
   bool fullRedraw = (prevPct < 0.0f);
   float prevAngleDeg = fullRedraw ? 0.0f : (180.0f * constrain(prevPct, 0.0f, 1.0f));

   // Fast math equivalent to inverse atan2f: maps an angle boundary straight to an X-coordinate
   auto getXCut = [](float angle, int dy, int outerX) -> int {
     if (angle <= 0.0f) return -outerX - 1;
     if (angle >= 179.9f) return outerX + 1;
     if (angle == 90.0f) return 0;
     float rad = angle * (float)M_PI / 180.0f;
     int xc = (int)((float)dy / tanf(rad));
     if (xc < -outerX - 1) xc = -outerX - 1;
     if (xc > outerX + 1)  xc = outerX + 1;
     return xc;
   };
 
   // Horizontal scanline vector rendering (Eliminates all slow atan2f/sqrtf calls per pixel)
   for (int y = cy - outerR; y <= cy; y++) {
     if (y < 0 || y >= SCREEN_HEIGHT) continue;
     int dy = y - cy; // dy <= 0
     float dy2 = (float)(dy * dy);
 
     int outerX = (int)sqrtf((float)(outerR * outerR) - dy2);
     int innerX = 0;
     if (dy2 < (float)(innerR * innerR)) {
         innerX = (int)ceilf(sqrtf((float)(innerR * innerR) - dy2));
     }

     int currXCut = cx + getXCut(fillAngleDeg, dy, outerX);
     int prevXCut = cx + getXCut(prevAngleDeg, dy, outerX);

     int spans[2][2];
     int numSpans = 0;
     if (innerX > 0) {
         spans[0][0] = cx - outerX; spans[0][1] = cx - innerX;
         spans[1][0] = cx + innerX; spans[1][1] = cx + outerX;
         numSpans = 2;
     } else {
         spans[0][0] = cx - outerX; spans[0][1] = cx + outerX;
         numSpans = 1;
     }

     for (int s = 0; s < numSpans; s++) {
         int S_start = spans[s][0];
         int S_end   = spans[s][1];
         if (S_start > S_end) continue;

         if (fullRedraw) {
             int f_end = min(S_end, currXCut);
             if (S_start <= f_end) {
                 tft->drawFastHLine(S_start, y, f_end - S_start + 1, fillColor);
             }
             int t_start = max(S_start, currXCut + 1);
             if (t_start <= S_end) {
                 tft->drawFastHLine(t_start, y, S_end - t_start + 1, trackColor);
             }
         } else if (fillAngleDeg > prevAngleDeg) {
             // Arc grew: paint only the new slice
             int g_start = max(S_start, prevXCut + 1);
             int g_end   = min(S_end, currXCut);
             if (g_start <= g_end) {
                 tft->drawFastHLine(g_start, y, g_end - g_start + 1, fillColor);
             }
         } else if (fillAngleDeg < prevAngleDeg) {
             // Arc shrank: erase only the removed slice
             int g_start = max(S_start, currXCut + 1);
             int g_end   = min(S_end, prevXCut);
             if (g_start <= g_end) {
                 tft->drawFastHLine(g_start, y, g_end - g_start + 1, trackColor);
             }
         }
     }
   }
 
   // Value + Unit text — skip if arc-only delta update and number hasn't changed
   if (!skipText) {
     setU8g2Font(valSize);
     int tw = u8g2.getUTF8Width(valStr.c_str());
     int th = u8g2.getFontAscent() - u8g2.getFontDescent(); // Approx real height
 
     int uw = 0, uh = 0;
     if (unitStr.length() > 0) {
       setU8g2Font(2);
       uw = u8g2.getUTF8Width(unitStr.c_str());
       uh = u8g2.getFontAscent() - u8g2.getFontDescent();
     }
 
     int unitGap = (uw > 0) ? 4 : 0;
     int totalW  = tw + unitGap + uw;
     int startX  = cx - totalW / 2;
     
     int textTopY = 95;  // upper bound for text rect
     if (textTopY < 32) textTopY = 32;
 
     // Clear a generous, fixed-height area to guarantee no leftover pixels 
     // when font size changes or glyphs exceed nominal ascent.
     int clearY = textTopY - 6; 
     int clearH = 151 - clearY; // Clear down to Y=151 (Label clear takes over at 151)
     
     int   _dy        = clearY - cy;
     float _chordF    = sqrtf((float)(innerR * innerR) - (float)(_dy * _dy));
     int   _safeHalf  = (int)_chordF - 6;
     if (_safeHalf < 4) _safeHalf = 4;
     
     // Σβήνουμε τον παλιό αριθμό με σταθερό ύψος (χωρίς να αφήνει ίχνη)
     tft->fillRect(cx - _safeHalf, clearY, _safeHalf * 2, clearH, COLOR_BG);
 
     setU8g2Font(valSize);
     u8g2.setForegroundColor(fillColor);
     u8g2.setCursor(startX, textTopY + u8g2.getFontAscent());
     u8g2.print(valStr.c_str());
 
     if (uw > 0) {
       setU8g2Font(2);
       u8g2.setForegroundColor(fillColor);
       // Align baseline
       u8g2.setCursor(startX + tw + unitGap, textTopY + th - uh + u8g2.getFontAscent());
       u8g2.print(unitStr.c_str());
     }
   } // end !skipText
 }
 
 // ── Βοηθητική: εκτύπωση κεντραρισμένου κειμένου ──────────────────────────
 static void accelCenterText(int y, const char* text, uint8_t sz, uint16_t col) {
   setU8g2Font(sz);
   u8g2.setForegroundColor(col);
   int w = u8g2.getUTF8Width(text);
   u8g2.setCursor((SCREEN_WIDTH - w) / 2, y + u8g2.getFontAscent());
   u8g2.print(text);
 }
 
 // ── Accel screen renderer ─────────────────────────────────────────────────
 void drawAccelScreen() {
   // Καθαρισμός περιοχής περιεχομένου (πάνω από header δεν αγγίζουμε)
   tft->fillRect(13, 31, SCREEN_WIDTH - 13, SCREEN_HEIGHT - 31, COLOR_BG);
 
   int mode    = accelMode; // 0=0-100, 1=60-120
   int startKmh = (mode == 0) ? 0  : 60;
   int endKmh   = (mode == 0) ? 100 : 120;
   const char* modeName = (mode == 0) ? "0-100" : "60-120";
 
   // Ανάγνωση τρέχουσας ταχύτητας υπό mutex
   int curSpeed = 0;
   if (carMutex && xSemaphoreTake(carMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
     curSpeed = car.speed;
     xSemaphoreGive(carMutex);
   }
 
   if (accelState == ACCEL_IDLE) {
     // ── IDLE: mode selector + START ──────────────────────────────────
     if (mode == 0) {
       accelCenterText(75, "0-100",  4, COLOR_YELLOW);  // επιλεγμένο
       accelCenterText(112, "60-120", 2, COLOR_YELLOW); // ανενεργό
     } else {
       accelCenterText(75, "0-100",  2, COLOR_YELLOW);  // ανενεργό
       accelCenterText(108, "60-120", 4, COLOR_YELLOW); // επιλεγμένο
     }
     // ── Κράτα 1s οπουδήποτε για εκκίνηση ────────────────────────────
     accelCenterText(153, "HOLD  1s  to  START", 2, COLOR_TEXT);

   } else if (accelState == ACCEL_ARMED) {
     // ── ARMED: Παγωμένο στο 0.00s ────────────────────────────────────
     accelCenterText(55, modeName, 2, COLOR_YELLOW);
     // Χρόνος παγωμένος στο 0.00s (σταθερό, χωρίς αναβόσβησμα)
     accelCenterText(100, "0.00s", 5, COLOR_GREEN);
     // Κουμπί CANCEL
     const int btnW = 110, btnH = 26, btnR = 8;
     int btnX = (SCREEN_WIDTH - btnW) / 2;
     int btnY = 137;
     tft->fillRoundRect(btnX, btnY, btnW, btnH, btnR, COLOR_DARK);
     tft->drawRoundRect(btnX - 1, btnY - 1, btnW + 2, btnH + 2, btnR + 1, COLOR_GRAY);
     setU8g2Font(2);
     u8g2.setForegroundColor(COLOR_TEXT);
     int bw = u8g2.getUTF8Width("CANCEL");
     int bh = u8g2.getFontAscent() - u8g2.getFontDescent();
     u8g2.setCursor(btnX + (btnW - bw) / 2, btnY + (btnH - bh) / 2 + u8g2.getFontAscent());
     u8g2.print("CANCEL");

   } else if (accelState == ACCEL_RUNNING) {
     // ── RUNNING: Χρόνος και Ταχύτητα ──────────────────────────────────
     accelCenterText(55, modeName, 2, COLOR_YELLOW);
     float elapsed = (millis() - accelStartMs) / 1000.0f;
     char timeBuf[16];
     snprintf(timeBuf, sizeof(timeBuf), "%.2fs", elapsed);
     char spdBuf[16];
     snprintf(spdBuf, sizeof(spdBuf), "%d km/h", curSpeed);
     
     tft->fillRect(13, 70, SCREEN_WIDTH - 13, 90, COLOR_BG);
     accelCenterText(90, timeBuf, 5, COLOR_GREEN);
     accelCenterText(135, spdBuf, 3, COLOR_CYAN);

   } else if (accelState == ACCEL_DONE) {
     // ── DONE: modeName μικρό + αποτέλεσμα + exit + RESET ─────────────
     accelCenterText(40, modeName, 2, COLOR_YELLOW);
     // Αποτέλεσμα χρόνου (μεγάλο)
     float result = accelResultMs / 1000.0f;
     char resBuf[16];
     snprintf(resBuf, sizeof(resBuf), "%.2fs", result);
     accelCenterText(80, resBuf, 4, COLOR_GREEN);
     // Ταχύτητα εξόδου και απόσταση
     char exitBuf[40];
     snprintf(exitBuf, sizeof(exitBuf), "Exit: %d km/h | Dist: %.0fm", accelExitSpeed, accelDistanceM);
     accelCenterText(112, exitBuf, 2, COLOR_CYAN);
     // Κουμπί RESET
     const int btnW = 160, btnH = 34, btnR = 10;
     int btnX = (SCREEN_WIDTH - btnW) / 2;
     int btnY = 130;
     tft->fillRoundRect(btnX, btnY, btnW, btnH, btnR, COLOR_ORANGE);
     tft->drawRoundRect(btnX - 1, btnY - 1, btnW + 2, btnH + 2, btnR + 1, COLOR_TEXT);
     setU8g2Font(2);
     u8g2.setForegroundColor(COLOR_BG);
     int bw = u8g2.getUTF8Width("RESET");
     int bh = u8g2.getFontAscent() - u8g2.getFontDescent();
     u8g2.setCursor(btnX + (btnW - bw) / 2, btnY + (btnH - bh) / 2 + u8g2.getFontAscent());
     u8g2.print("RESET");
   }
 
   accelNeedsRedraw = false;
   accelLastDisplayMs = millis();
 }
 
 // ── Hold progress bar (για ACCEL IDLE όταν κρατάει το δάχτυλο) ────────────────
 void updateAccelHoldBar() {
   unsigned long holdElapsed = millis() - swipeStartTime;
   float prog = constrain(holdElapsed / 1000.0f, 0.0f, 1.0f);
 
   // 1δευτερόλεπτο κράτημα ανιχνεύεται εδώ (όχι στο IRQ handler) ώστε να περνάει η 1s ακώλυτα
   if (holdElapsed >= 1000 && !accelHoldFired) {
     accelHoldFired = true;
     handleAccelTap();  // IDLE → ARMED
     return;
   }
 
   // Εμφάνιση ανόδου bar
   const int barW = 150, barH = 7;
   int barX = (SCREEN_WIDTH - barW) / 2;
   int barY = 130;
   tft->fillRect(barX, barY, barW, barH, COLOR_DARK);  // track
   if (prog > 0.01f) tft->fillRect(barX, barY, (int)(barW * prog), barH, COLOR_GREEN);
   accelLastDisplayMs = millis();
 }
 
 // ── Μερική ανανέωση timer (χωρίς full screen clear → χωρίς flicker) ─────────
 void updateAccelTimer() {
   float elapsed = (millis() - accelStartMs) / 1000.0f;
   char timeBuf[16];
   snprintf(timeBuf, sizeof(timeBuf), "%.2fs", elapsed);

   int curSpeed = 0;
   if (carMutex && xSemaphoreTake(carMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
     curSpeed = car.speed;
     xSemaphoreGive(carMutex);
   } else {
     curSpeed = car.speed;
   }
   char spdBuf[16];
   snprintf(spdBuf, sizeof(spdBuf), "%d km/h", curSpeed);

   tft->fillRect(13, 70, SCREEN_WIDTH - 13, 90, COLOR_BG);  // μόνο η περιοχή του αριθμού (διευρυμένη)
   accelCenterText(90, timeBuf, 5, COLOR_GREEN);
   accelCenterText(135, spdBuf, 3, COLOR_CYAN);
   accelLastDisplayMs = millis();
 }
 
 // ── Accel state machine (καλείται στο loop) ──────────────────────────────
 void updateAccelState() {
   if (accelState == ACCEL_ARMED) {
     int spd = 0;
     if (carMutex && xSemaphoreTake(carMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
       spd = car.speed;
       xSemaphoreGive(carMutex);
     }
     int mode = accelMode;
     if (mode == 0) {
       // 0-100: Ξεκινά όταν το αμάξι αρχίζει να κινείται (speed > 1 km/h)
       if (spd > 1) {
         accelState      = ACCEL_RUNNING;
         accelStartMs    = millis();
         accelLastCalcMs = accelStartMs;
         accelDistanceM  = 0.0f;
         accelNeedsRedraw = true;
       }
     } else {
       // 60-120: Ξεκινά όταν η ταχύτητα φτάσει 60 km/h
       if (spd >= 60) {
         accelState      = ACCEL_RUNNING;
         accelStartMs    = millis();
         accelLastCalcMs = accelStartMs;
         accelDistanceM  = 0.0f;
         accelNeedsRedraw = true;
       }
     }
   } else if (accelState == ACCEL_RUNNING) {
     int spd = 0;
     if (carMutex && xSemaphoreTake(carMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
       spd = car.speed;
       xSemaphoreGive(carMutex);
     }
     
     // Calculate distance traveled since last loop
     unsigned long nowMs = millis();
     unsigned long dtMs = nowMs - accelLastCalcMs;
     if (dtMs > 0) {
       // speed (km/h) / 3.6 = speed (m/s)
       // distance = speed * dt (seconds)
       accelDistanceM += (spd / 3.6f) * (dtMs / 1000.0f);
       accelLastCalcMs = nowMs;
     }

     int endKmh = (accelMode == 0) ? 100 : 120;
     if (spd >= endKmh) {
       accelResultMs  = millis() - accelStartMs;
       accelExitSpeed = spd;
       accelState     = ACCEL_DONE;
       accelNeedsRedraw = true;
     }
   }
 }
 
 // ── Tap handler για το ACCEL tab ─────────────────────────────────────────
 void handleAccelTap() {
   if (accelState == ACCEL_IDLE) {
     // IDLE → tap → ARMED (εκκίνηση αναμονής)
     accelState       = ACCEL_ARMED;
     accelNeedsRedraw = true;
   } else if (accelState == ACCEL_DONE) {
     // DONE → tap (RESET) → IDLE — αφήνει τον χρήστη να δει mode και να πατήσει START
     accelState       = ACCEL_IDLE;
     accelNeedsRedraw = true;
   } else if (accelState == ACCEL_ARMED) {
     // Ακύρωση αναμονής → επιστροφή στο IDLE
     accelState       = ACCEL_IDLE;
     accelNeedsRedraw = true;
   }
   // RUNNING: tap δεν διακόπτει τη μέτρηση
 }
 
 void updateUI() {
   static float   prevValue       = -9999;
   static String  prevLabel       = "";
   static int     prevTab         = -1;
   static int     prevItem        = -1;
   static uint16_t prevColor      = 0;
   // Smooth animation state (Grafana-style)
   static float   gaugeSmoothPct  = 0.0f;   // current animated position
   static float   gaugeVelocity   = 0.0f;   // spring physics velocity
   static float   gaugeDrawnPct   = -1.0f;  // what was actually last painted
   static String  gaugeDrawnValStr = "";    // text drawn last time (for delta text)
   static unsigned long lastAnimMs = 0;      // time-based animation clock
   static int     prevTxtX = 0, prevTxtY = 0, prevTxtW = 0, prevTxtH = 0; // Text-only footprint tracking
 
   // Force full repaint after screen wake or drawUI() call
   if (uiNeedsFullRedraw) {
     prevTab        = -1;
     prevItem       = -1;
     prevValue      = -9999;
     prevLabel      = "";
     prevColor      = 0;
     gaugeDrawnPct  = -1.0f;   // force full arc redraw
     // gaugeSmoothPct kept — snap at fullRedraw draws actual value instantly
     gaugeVelocity  = 0.0f;
     gaugeDrawnValStr = "";
     lastAnimMs = millis();
     uiNeedsFullRedraw = false;
     if (currentTab == TAB_SYSTEM && currentItemIndex[TAB_SYSTEM] == 2) accelNeedsRedraw = true;  // force accel redraw on tab switch
   }
 
   // ── SYSTEM ACCEL item: custom renderer ──────────────────────────────────────
   if (currentTab == TAB_SYSTEM && currentItemIndex[TAB_SYSTEM] == 2) {
     if (accelNeedsRedraw) {
       drawAccelScreen();
     } else if (accelState == ACCEL_RUNNING && millis() - accelLastDisplayMs > 80) {
       updateAccelTimer();
     } else if (accelState == ACCEL_IDLE && fingerDown && millis() - accelLastDisplayMs > 50) {
       updateAccelHoldBar();
     }
     return;
   }

   DashboardItem item = getCurrentItem();
   bool fullRedraw = (prevTab != currentTab || prevItem != currentItemIndex[currentTab]);
   if (fullRedraw) {
     prevTab = currentTab; prevItem = currentItemIndex[currentTab];
     prevValue = -9999; prevLabel = ""; prevColor = 0;
     gaugeDrawnPct = -1.0f; gaugeVelocity = 0.0f; gaugeDrawnValStr = "";
     lastAnimMs = millis();
   }
   String valStr;
   if (item.precision == 0) valStr = String((int)item.value);
   else                     valStr = String(item.value, item.precision);
   float minV = 0, maxV = 1;
   bool hasGauge = getProgressRange(currentTab, currentItemIndex[currentTab], &minV, &maxV);
   bool valueChanged = (item.value != prevValue);

  // ── DTC item: custom list renderer ──────────────────────────────────────
  if (currentTab == TAB_SYSTEM && currentItemIndex[TAB_SYSTEM] == 1) {
    if (!fullRedraw && !valueChanged) return;
    tft->fillRect(13, 31, SCREEN_WIDTH - 13, SCREEN_HEIGHT - 31, COLOR_BG);
    CarData snap;
    String codes[10];
    if (carMutex && xSemaphoreTake(carMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      snap = car;
      for (int i = 0; i < 10; i++) codes[i] = activeDTCs[i];
      xSemaphoreGive(carMutex);
    } else {
      snap = car;
      for (int i = 0; i < 10; i++) codes[i] = activeDTCs[i];
    }
    if (snap.dtc == 0) {
      setU8g2Font(3); u8g2.setForegroundColor(COLOR_GREEN);
      const char* ok = "NO FAULTS";
      int w = u8g2.getUTF8Width(ok);
      u8g2.setCursor((SCREEN_WIDTH - w) / 2, 75 + u8g2.getFontAscent());
      u8g2.print(ok);
    } else {
      setU8g2Font(2);
      u8g2.setForegroundColor(snap.mil ? COLOR_RED : COLOR_ORANGE);
      char hdr[32];
      snprintf(hdr, sizeof(hdr), "%s  %d CODE%s",
        snap.mil ? "MIL ON" : "MIL OFF", snap.dtc, snap.dtc == 1 ? "" : "S");
      int hw = u8g2.getUTF8Width(hdr);
      u8g2.setCursor((SCREEN_WIDTH - hw) / 2, 35 + u8g2.getFontAscent());
      u8g2.print(hdr);
      setU8g2Font(3);
      int lineH = u8g2.getFontAscent() - u8g2.getFontDescent() + 4;
      int shown = 0;
      for (int i = 0; i < 10 && shown < 5; i++) {
        if (codes[i].length() == 0) continue;
        u8g2.setForegroundColor(COLOR_RED);
        int cw = u8g2.getUTF8Width(codes[i].c_str());
        u8g2.setCursor((SCREEN_WIDTH - cw) / 2, 52 + shown * lineH + u8g2.getFontAscent());
        u8g2.print(codes[i].c_str());
        shown++;
      }
      if (shown == 0) {
        setU8g2Font(2); u8g2.setForegroundColor(COLOR_ORANGE);
        const char* wait = "Reading codes...";
        int ww = u8g2.getUTF8Width(wait);
        u8g2.setCursor((SCREEN_WIDTH - ww) / 2, 75 + u8g2.getFontAscent());
        u8g2.print(wait);
      }
      setU8g2Font(2); u8g2.setForegroundColor(COLOR_DARK);
      const char* hint = "Hold 2s to CLEAR";
      int hw2 = u8g2.getUTF8Width(hint);
      u8g2.setCursor((SCREEN_WIDTH - hw2) / 2, 153 + u8g2.getFontAscent());
      u8g2.print(hint);
    }
    prevValue = item.value; prevColor = item.color; prevLabel = item.label;
    return;
  }
  // — ignore tiny fluctuations to reduce unnecessary redraws
   if (currentTab == TAB_MAIN && currentItemIndex[currentTab] == 1 && valueChanged) {
      if (abs(item.value - prevValue) < 60) valueChanged = false;
   }
   if (currentTab == TAB_SYSTEM && currentItemIndex[currentTab] == 0 && valueChanged) {
      if (abs(item.value - prevValue) < 0.1) valueChanged = false;
   }
   // Fix flickering for Liters (Fuel Used & L/100km)
   if (currentTab == TAB_TRIP && (currentItemIndex[currentTab] == 3 || currentItemIndex[currentTab] == 4) && valueChanged) {
      if (abs(item.value - prevValue) < 0.05) valueChanged = false;
   }

   // For gauge items: compute animation target EVERY frame so the easing loop
   // keeps running even after prevValue is already == item.value.
   float targetPct = 0.0f;
   bool  gaugeNeedsFrame = false;
   if (hasGauge) {
     targetPct = constrain((item.value - minV) / (maxV - minV), 0.0f, 1.0f);
     gaugeNeedsFrame = (fabsf(targetPct - gaugeSmoothPct) > 0.003f);
   }
 
   if (valueChanged || item.color != prevColor || fullRedraw || gaugeNeedsFrame) {
     if (hasGauge) {
       // ── SMOOTH GAUGE (constant-speed linear animation) ──────────────────────
       // Κινεί το arc με σταθερή ταχύτητα (full range σε 350ms) βάσει millis().
       // Έτσι κάθε frame κινεί το ίδιο ορατό ποσό → χωρίς κομμάτια/chunks.
       unsigned long nowMs = millis();
       float dt = (float)(nowMs - lastAnimMs);
       lastAnimMs = nowMs;

       // Γρήγορo critically-damped spring με sub-stepping για σταθερότητα.
       // k=400, d=40 → φτάνει ~95% σε 30ms ανεξάρτητα από frame-rate.
       const float springK = 400.0f;
       const float dampC   = 40.0f;
       const int   steps   = 4;
       float subDt = min(dt, 50.0f) / 1000.0f / steps;
       for (int s = 0; s < steps; s++) {
         float acc = (targetPct - gaugeSmoothPct) * springK - gaugeVelocity * dampC;
         gaugeVelocity  += acc * subDt;
         gaugeSmoothPct += gaugeVelocity * subDt;
       }
       gaugeSmoothPct = constrain(gaugeSmoothPct, 0.0f, 1.0f);

       float diff = targetPct - gaugeSmoothPct;
       bool animating = fabsf(diff) > 0.002f || fabsf(gaugeVelocity) > 0.001f;
       if (!animating) {
         gaugeSmoothPct = targetPct;
         gaugeVelocity  = 0.0f;
       }
 
       bool colorChanged = (item.color != prevColor);
 
       // Υπολογισμός μεγέθους κειμένου (χρειάζεται και στο fullRedraw παρακάτω)
       uint16_t unitPxW = item.unit.length() > 0 ? (item.unit.length() * 12 + 4) : 0;
       const int safeTextW = 150;
       // UNIFORM SIZE: Start at 6 instead of 7 to match text-only and avoid "small->big" jumps
       uint8_t vsz = 6;
       while (vsz > 1 && ((int)valStr.length() * vsz * 6 + (int)unitPxW) > safeTextW) vsz--;
 
       // Όταν αλλάζει tab/item: snap άμεσα στην τιμή, χωρίς animation
       if (fullRedraw) {
         float snapPct = constrain((item.value - minV) / (maxV - minV), 0.0f, 1.0f);
         drawGauge(SCREEN_WIDTH / 2, 165, 130, 112,
                   snapPct, COLOR_DARK, item.color,
                   valStr, item.unit, vsz,
                   -1.0f, false);  // full redraw at actual value
         gaugeDrawnPct    = snapPct;
         gaugeDrawnValStr = valStr;
         gaugeSmoothPct   = snapPct;  // ξεκινά ήδη στη σωστή θέση
         gaugeVelocity    = 0.0f;
         prevValue = item.value;
         prevColor = item.color;
         prevLabel = "";
         return;
       }
 
       // Delta vs full: use delta unless tab changed, color changed, or first draw.
       // Delta drawing is ~100x faster (only the changed arc slice) → zero flicker.
       float prevP = (colorChanged || gaugeDrawnPct < 0.0f)
                     ? -1.0f   // full redraw
                     : gaugeDrawnPct; // delta
 
       // vsz υπολογίστηκε ήδη παραπάνω
 
       // Cache gauge params so alert blink can restore them
       lastGaugePct     = gaugeSmoothPct;
       lastGaugeFill    = item.color;
       lastGaugeTrack   = COLOR_DARK;
       lastGaugeValStr  = valStr;
       lastGaugeUnitStr = item.unit;
       lastGaugeValSize = vsz;
 
       bool textUnchanged = (prevP >= 0.0f && valStr == gaugeDrawnValStr);
 
       drawGauge(SCREEN_WIDTH / 2, 165, 130, 112,
                 gaugeSmoothPct, COLOR_DARK, item.color,
                 valStr, item.unit, vsz,
                 prevP,          // delta: only changed arc slice
                 textUnchanged); // skipText: avoid clearing number mid-animation
 
       gaugeDrawnPct    = gaugeSmoothPct;
       gaugeDrawnValStr = valStr;
 
       // Re-apply alert arc on top if blinking
       if (alertBlinking && alertBorderOn)
         drawAlertArc(alertBlinkColor);
       else if (alertBlinking && !alertBorderOn)
         drawAlertArc(lastGaugeTrack);
 
       // Force label redraw after full arc redraw
       if (prevP < 0.0f) prevLabel = "";
 
       // Update cached values only when animation has settled
       if (!animating || colorChanged || fullRedraw) {
         prevValue = item.value;
         prevColor = item.color;
       }
     } else {
       // ── Text-only mode (items without a range) ──────────
       lastAnimMs = millis();  // reset animation clock so gauge tab doesn't jump
       uint8_t sz = 7;
       int vw = 0, vh = 0, uw = 0, uh = 0, unitSpace = 0;
       
       while (sz > 2) {
         setU8g2Font(sz);
         vw = u8g2.getUTF8Width(valStr.c_str());
         vh = u8g2.getFontAscent() - u8g2.getFontDescent();
         
         if (item.unit.length() > 0) {
           setU8g2Font(2);
           uw = u8g2.getUTF8Width(item.unit.c_str());
           uh = u8g2.getFontAscent() - u8g2.getFontDescent();
           unitSpace = 5 + uw;
         } else {
           uw = 0; uh = 0; unitSpace = 0;
         }
         
         if (vw + unitSpace <= (SCREEN_WIDTH - 20)) break; // fits!
         sz--;
       }
 
       // Fixed content zone above label: y=33..139 (107px)
       const int zoneTop = 33, zoneH = 107;
       int charH  = vh;
       int valueTopY = zoneTop + (zoneH - charH) / 2;
       if (valueTopY < zoneTop) valueTopY = zoneTop;
 
       int totalW = vw + unitSpace;
       int valueX = (SCREEN_WIDTH - totalW) / 2;
 
       if (fullRedraw) {
         // Clear the full content zone initially
         tft->fillRect(13, zoneTop, SCREEN_WIDTH - 13, zoneH, COLOR_BG);
       } else {
         // Clear ONLY the exact footprint of the previous text to prevent massive flickering
         int pad = 6; // generous padding to catch ascender/descender artifacts
         tft->fillRect(prevTxtX - pad, prevTxtY - pad, prevTxtW + pad * 2, prevTxtH + pad * 2, COLOR_BG);
       }
 
       setU8g2Font(sz);
       u8g2.setForegroundColor(item.color);
       u8g2.setCursor(valueX, valueTopY + u8g2.getFontAscent());
       u8g2.print(valStr.c_str());
 
       if (uw > 0) {
         setU8g2Font(2);
         u8g2.setForegroundColor(item.color);
         // Baseline-align unit with value text (bottom edges match)
         u8g2.setCursor(valueX + vw + 5, valueTopY + charH - uh + u8g2.getFontAscent());
         u8g2.print(item.unit.c_str());
       }

       // Save text footprint for next frame clearing
       prevTxtX = valueX;
       prevTxtY = valueTopY;
       prevTxtW = totalW;
       prevTxtH = charH;

       prevValue = item.value;
       prevColor = item.color;
     }
   }
 
   // ── Label ───────────────────────────────────────────────
   String labelText = item.label;
   if (labelText != prevLabel || fullRedraw) {
     int labelY = hasGauge ? 153 : 144;

     // FIX: Το κλασικό fillRect(0, labelY-2, SCREEN_WIDTH, 28) σβήνει τις κάτω
     // σειρές της ημικυκλικής γκάουτζ (cy=165, outerR=130 → arc φτάνει ως y=165).
     // Αντί για full-width erase, καθαρίζουμε ΜΟΝΟ το εσωτερικό chord (innerR=112)
     // στις σειρές που επικαλύπτονται με το arc — εκεί δεν υπάρχουν arc pixels.
     if (hasGauge) {
       const int gCx  = SCREEN_WIDTH / 2;
       const int gCy  = 165;
       const int gIn  = 112;
       int yEnd = labelY - 2 + 28;
       for (int iy = labelY - 2; iy < yEnd; iy++) {
         if (iy < 0 || iy >= SCREEN_HEIGHT) continue;
         int diy  = iy - gCy;
         int diy2 = diy * diy;
         if (diy <= 0 && diy2 < gIn * gIn) {
           // Σειρά που ανήκει στο arc: καθάρισε μόνο το εσωτερικό chord
           int ix = (int)sqrtf((float)(gIn * gIn - diy2));
           if (ix > 0)
             tft->drawFastHLine(gCx - ix + 1, iy, 2 * ix - 1, COLOR_BG);
         } else {
           // Εκτός ζώνης arc: καθάρισε πλήρες πλάτος (αφήνουμε τα 13px αριστερά dots)
           tft->drawFastHLine(13, iy, SCREEN_WIDTH - 13, COLOR_BG);
         }
       }
     } else {
       tft->fillRect(0, labelY - 2, SCREEN_WIDTH, 28, COLOR_BG);
     }

     setU8g2Font(2);  // helvB12 — κανονικό μέγεθος για την κάτω μπάρα
     u8g2.setForegroundColor(COLOR_TEXT);
     int w = u8g2.getUTF8Width(labelText.c_str());
     int lx = (SCREEN_WIDTH - w) / 2;
     int fontH = u8g2.getFontAscent() - u8g2.getFontDescent();

     // Clear a solid rect behind the label text so arc pixels don't bleed through
     tft->fillRect(lx - 2, labelY - 1, w + 4, fontH + 3, COLOR_BG);

     u8g2.setFontMode(1);  // transparent (background already cleared above)
     u8g2.setCursor(lx, labelY + u8g2.getFontAscent());
     u8g2.print(labelText.c_str());
     prevLabel = labelText;
   }
 }
 
 int getItemsCount(int tab) {
   switch(tab) {
     case TAB_MAIN: return 4;
     case TAB_TEMPS: return 2; // Removed Oil Temp
     case TAB_TRIP: return 5;
     case TAB_SYSTEM: return 3;
     default: return 1;
   }
 }
 
 DashboardItem getCurrentItem() {
   // Take a consistent snapshot of car data under mutex so core-1 writes can't corrupt reads
   CarData snap;
   if (carMutex && xSemaphoreTake(carMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
     snap = car;
     xSemaphoreGive(carMutex);
   } else {
     snap = car; // fallback: stale read is fine for display
   }
 
   DashboardItem item;
   int idx = currentItemIndex[currentTab];
   
   if (currentTab == TAB_MAIN) {
     if (idx == 0) {
       item = {"SPEED", "km/h", (float)snap.speed, 0, COLOR_GREEN};
     }
     else if (idx == 1) {
        uint16_t col = COLOR_GREEN;
        if      (snap.rpm < 2500) col = COLOR_BLUE;     // ψυχρό κινητήρα
        else if (snap.rpm < 5000) col = COLOR_GREEN;    // κανονικό
        else if (snap.rpm < 5500) col = COLOR_ORANGE;   // προσοχή (κοντά στο όριο)
        else                      col = COLOR_RED;       // πάνω από 5500 = alert
        item = {"RPM", "", (float)snap.rpm, 0, col};
     }
     else if (idx == 2) {
       uint16_t col = COLOR_GREEN;
       if      (snap.throttle < 40) col = COLOR_GREEN;
       else if (snap.throttle < 75) col = COLOR_ORANGE;
       else                         col = COLOR_RED;
       item = {"THROTTLE", "%", (float)snap.throttle, 0, col};
     }
     else if (idx == 3) {
       uint16_t col = COLOR_GREEN;
       if      (snap.boost <= 0.0f) col = COLOR_BLUE;    // vacuum / idle
       else if (snap.boost <  0.8f) col = COLOR_GREEN;   // κανονικό boost
       else if (snap.boost <  1.4f) col = COLOR_ORANGE;  // υψηλό
       else                         col = COLOR_RED;      // πολύ υψηλό
       item = {"TURBO", "bar", snap.boost, 1, col};
     }
   }
   else if (currentTab == TAB_TEMPS) {
     if (idx == 0) {
        uint16_t col = COLOR_GREEN;
        if (snap.coolant < 70) col = COLOR_BLUE;
        else if (snap.coolant <= 95) col = COLOR_GREEN;
        else col = COLOR_RED;
        item = {"COOLANT", "C", (float)snap.coolant, 0, col};
     }
     else if (idx == 1) {
       uint16_t col = COLOR_GREEN;
       if      (snap.intake < 15)  col = COLOR_BLUE;    // κρύος αέρας
       else if (snap.intake < 35)  col = COLOR_GREEN;   // ιδανικό
       else if (snap.intake < 50)  col = COLOR_ORANGE;  // ζεστός
       else                        col = COLOR_RED;      // πολύ ζεστός
       item = {"INTAKE", "C", (float)snap.intake, 0, col};
     }
   }
   else if (currentTab == TAB_TRIP) {
     if      (idx == 0) item = {"DISTANCE", "km",    (float)snap.distance,  0, COLOR_MAGENTA};
     else if (idx == 1) item = {"AVG SPD",  "km/h", (float)snap.avgSpeed, 0, COLOR_TEXT};
     else if (idx == 2) item = {"MAX SPD",  "km/h", (float)snap.maxSpeed, 0, COLOR_RED};
     else if (idx == 3) item = {"FUEL USED","L",    snap.fuelUsed,         2, COLOR_ORANGE};
     else if (idx == 4) {
       float lp100 = (snap.distance > 1) ? (snap.fuelUsed / (float)snap.distance * 100.0f) : 0.0f;
       item = {"L/100KM", "L/100", lp100, 1, COLOR_YELLOW};
     }
   }
   else if (currentTab == TAB_SYSTEM) {
     if (idx == 0) {
        uint16_t col = COLOR_GREEN;
        if (snap.battery < 11.5) col = COLOR_RED;
        else if (snap.battery < 12.2) col = COLOR_ORANGE;
        else if (snap.battery <= 14.8) col = COLOR_GREEN;
        else col = COLOR_RED;
        item = {"BATTERY", "V", snap.battery, 1, col};
     }
     else if (idx == 1) {
        uint16_t col = COLOR_GREEN;
        if (snap.mil) col = COLOR_RED;
        else if (snap.dtc > 0) col = COLOR_ORANGE;
        item = {"DTC", "Codes", (float)snap.dtc, 0, col};
     }
     else if (idx == 2) {
        item = {"ACCEL", "km/h", 0, 0, COLOR_YELLOW};
     }
   }
   else { item = {"NO DATA", "", 0, 0, COLOR_GRAY}; }
   return item;
 }
 
 // Returns the min/max range for a progress bar; false = no bar for this item
 bool getProgressRange(int tab, int idx, float *minV, float *maxV) {
   if (tab == TAB_MAIN) {
     if (idx == 0) { *minV =    0; *maxV =  250; return true; }  // SPEED km/h
     if (idx == 1) { *minV =    0; *maxV = 8000; return true; }  // RPM
     if (idx == 2) { *minV =    0; *maxV =  100; return true; }  // THROTTLE %
     if (idx == 3) { *minV =    0; *maxV =  2.0; return true; }  // TURBO bar
   }
   if (tab == TAB_TEMPS) {
     if (idx == 0) { *minV =    0; *maxV =  130; return true; }  // COOLANT
     if (idx == 1) { *minV =  -20; *maxV =   60; return true; }  // INTAKE
   }
   if (tab == TAB_SYSTEM) {
     if (idx == 0) { *minV =   10; *maxV =   16; return true; }  // BATTERY V
   }
   return false;
 }
 
 // ===== OBD2 READING LOGIC =====
 
 // Mode 01 Standard PID Reader (ISO 15765-4, 500kbps, 11-bit IDs)
 // Returns raw integer ready for unit conversion; -999 = no response / error.
 int readStandardPID(byte pid) {
   if (!canInitialized) return -999;
 
   // Check bus state — Bus-Off happens when no transceiver is present
   // (TEC reaches 256 after ~32 unacknowledged frames)
   twai_status_info_t canStatus;
   if (twai_get_status_info(&canStatus) != ESP_OK) return -999;
   if (canStatus.state == TWAI_STATE_BUS_OFF) {
     twai_initiate_recovery();  // start 128-bit recovery sequence (non-blocking)
     return -999;
   }
   if (canStatus.state != TWAI_STATE_RUNNING) return -999;
 
   twai_message_t tx_msg;
   tx_msg.identifier        = 0x7DF;  // OBD2 functional broadcast address
   tx_msg.extd              = 0;
   tx_msg.rtr               = 0;
   tx_msg.data_length_code  = 8;
   tx_msg.data[0] = 0x02; tx_msg.data[1] = 0x01; tx_msg.data[2] = pid;
   tx_msg.data[3] = 0; tx_msg.data[4] = 0; tx_msg.data[5] = 0;
   tx_msg.data[6] = 0; tx_msg.data[7] = 0;
 
   // Ξέπλυμα παλιών frames από RX queue πριν στείλουμε το request
   { twai_message_t _flush; while (twai_receive(&_flush, 0) == ESP_OK) {} }
 
   // timeout=0 → non-blocking: return immediately if TX queue is full
   if (twai_transmit(&tx_msg, 0) != ESP_OK) {
     canFailCount++;
     return -999;
   }
 
   unsigned long start = millis();
   // 100ms max — Tiguan J533 gateway χρειάζεται 20-100ms για να απαντήσει
   // pdMS_TO_TICKS(10) → αποδίδει CPU σε UI task ενώ περιμένουμε (single-core)
   while (millis() - start < 100) {
     twai_message_t rx_msg;
     if (twai_receive(&rx_msg, pdMS_TO_TICKS(10)) == ESP_OK) {
       // Engine ECU response 0x7E8; service 0x41 = Mode 01 response
       if (rx_msg.identifier == 0x7E8 &&
           rx_msg.data[1] == 0x41 &&
           rx_msg.data[2] == pid) {
         canFailCount = 0;  // reset on success
         canRxCount++;
         int A = rx_msg.data[3];
         int B = rx_msg.data[4];
         int result;
         switch (pid) {
           case PID_RPM:       result = ((A * 256) + B) / 4;  canLastPidName = "RPM";       break;
           case PID_SPEED:     result = A;                     canLastPidName = "SPEED";     break;
           case PID_COOLANT:   result = A - 40;                canLastPidName = "COOLANT";   break;
           case PID_INTAKE:    result = A - 40;                canLastPidName = "INTAKE";    break;
           case PID_THROTTLE:  result = (A * 100) / 255;      canLastPidName = "THROTTLE";  break;
 
           case PID_MAP:       result = A;                     canLastPidName = "MAP/BOOST"; break; // kPa absolute
           case PID_BATTERY:   result = (A * 256) + B;        canLastPidName = "BATTERY";   break; // mV
           case PID_ENGINE_LOAD: result = (A * 100) / 255;    canLastPidName = "ENG LOAD";  break; // %
           case PID_DIST_SINCE_DTC: result = (A * 256) + B;   canLastPidName = "DIST DTC";  break; // km
           case PID_MAF:       result = (A * 256) + B;        canLastPidName = "MAF";        break; // raw g/s×100; caller ÷100
           case PID_FUEL_RATE: result = (A * 256) + B;        canLastPidName = "FUEL RATE";  break; // raw; caller ×0.05 = L/h
           default:            result = A;                     canLastPidName = "0x" + String(pid, HEX); break;
         }
         canLastVal = result;
         if (debugPidLogging) {
           Serial.printf("✓ PID 0x%02X (%s) = %d\n", pid, canLastPidName.c_str(), result);
         }
         return result;
       }
     }
   }
   canFailCount++;
   if (debugPidLogging) {
     Serial.printf("✗ PID 0x%02X TIMEOUT\n", pid);
   }
   return -999;
 }
 
 // ===== DTC FUNCTIONS =====
 
 // Read DTC count and MIL status from PID 0x01
 bool readDTCCount(int *dtcCount, bool *milStatus) {
   if (!canInitialized) return false;
 
   twai_status_info_t canStatus;
   if (twai_get_status_info(&canStatus) != ESP_OK) return false;
   if (canStatus.state == TWAI_STATE_BUS_OFF) {
     twai_initiate_recovery();
     return false;
   }
   if (canStatus.state != TWAI_STATE_RUNNING) return false;
 
   twai_message_t tx_msg;
   tx_msg.identifier = 0x7DF;
   tx_msg.extd = 0;
   tx_msg.rtr = 0;
   tx_msg.data_length_code = 8;
   tx_msg.data[0] = 0x01;  // Mode 01
   tx_msg.data[1] = 0x01;  // PID 01
   tx_msg.data[2] = 0; tx_msg.data[3] = 0; tx_msg.data[4] = 0;
   tx_msg.data[5] = 0; tx_msg.data[6] = 0; tx_msg.data[7] = 0;
 
   { twai_message_t _flush; while (twai_receive(&_flush, 0) == ESP_OK) {} }
 
   if (twai_transmit(&tx_msg, 0) != ESP_OK) return false;
 
   unsigned long start = millis();
   while (millis() - start < 100) {
     twai_message_t rx_msg;
     if (twai_receive(&rx_msg, pdMS_TO_TICKS(10)) == ESP_OK) {
       if (rx_msg.identifier == 0x7E8 &&
           rx_msg.data[1] == 0x41 &&
           rx_msg.data[2] == 0x01) {
         
         uint8_t A = rx_msg.data[3];
         *milStatus = (A & 0x80) != 0;
         *dtcCount = A & 0x7F;  // Lower 7 bits = DTC count
         
         canLastPidName = "DTC CNT";
         canLastVal = *dtcCount;
         if (debugPidLogging) {
           Serial.printf("✓ DTC COUNT = %d, MIL = %s\n", *dtcCount, *milStatus ? "ON" : "OFF");
         }
         return true;
       }
     }
   }
   return false;
 }
 
 // Read actual DTC codes using Mode 03
 bool readDTCCodes(String dtcList[], int maxDTCs, int *found) {
   *found = 0;
   if (!canInitialized) return false;
   twai_status_info_t s;
   if (twai_get_status_info(&s) != ESP_OK || s.state != TWAI_STATE_RUNNING) return false;

   twai_message_t tx_msg = {};
   tx_msg.identifier = 0x7DF;
   tx_msg.data_length_code = 8;
   tx_msg.data[0] = 0x01; tx_msg.data[1] = 0x03;
   { twai_message_t f; while (twai_receive(&f, 0) == ESP_OK) {} }
   if (twai_transmit(&tx_msg, 0) != ESP_OK) return false;

   unsigned long start = millis();
   while (millis() - start < 300 && *found < maxDTCs) {
     twai_message_t rx;
     if (twai_receive(&rx, pdMS_TO_TICKS(50)) != ESP_OK) continue;
     if (rx.identifier != 0x7E8 || rx.data[1] != 0x43) continue;
     int n = rx.data[2];
     for (int i = 0; i < n && *found < maxDTCs; i++) {
       uint8_t A = rx.data[2 + i*2];
       uint8_t B = rx.data[3 + i*2];
       if (A == 0 && B == 0) continue;
       char cat;
       switch ((A & 0xC0) >> 6) {
         case 0: cat = 'P'; break; case 1: cat = 'C'; break;
         case 2: cat = 'B'; break; default: cat = 'U'; break;
       }
       char buf[8];
       snprintf(buf, sizeof(buf), "%c%X%X%X%X", cat,
         (A&0x30)>>4, A&0x0F, (B&0xF0)>>4, B&0x0F);
       dtcList[(*found)++] = String(buf);
     }
   }
   return true;
 }

 // Clear DTCs using Mode 04
 bool clearDTCs() {
     if (!canInitialized) return false;
 
     twai_message_t tx_msg;
     tx_msg.identifier = 0x7DF;
     tx_msg.extd = 0;
     tx_msg.rtr = 0;
     tx_msg.data_length_code = 8;
     tx_msg.data[0] = 0x01;  // 1 byte data
     tx_msg.data[1] = 0x04;  // Mode 04 - Clear DTCs
     tx_msg.data[2] = 0; tx_msg.data[3] = 0; tx_msg.data[4] = 0;
     tx_msg.data[5] = 0; tx_msg.data[6] = 0; tx_msg.data[7] = 0;
 
     if (twai_transmit(&tx_msg, 0) != ESP_OK) return false;
 
     // Wait for confirmation
     unsigned long start = millis();
     while (millis() - start < 100) {
         twai_message_t rx_msg;
         if (twai_receive(&rx_msg, pdMS_TO_TICKS(10)) == ESP_OK) {
             if (rx_msg.identifier == 0x7E8 && rx_msg.data[1] == 0x44) {
                 return true;  // Success
             }
         }
     }
     return false;
 }
 
 // Fully uninstall TWAI to stop error-ISR storms when no bus is present
 void canShutdown() {
   twai_stop();
   twai_driver_uninstall();
   canInitialized = false;
   canFailCount   = 0;
   canRetryMs     = millis() + CAN_RETRY_INTERVAL_MS;
   Serial.println("CAN: too many failures — driver uninstalled. Retry in 30s.");
 }
 
 // Try to (re)start TWAI — called from readOBD2() when canRetryMs is due
 void canTryInit() {
   Serial.printf("CAN: canTryInit() TX=GPIO%d RX=GPIO%d\n", CAN_TX, CAN_RX);
 
   // ESP32-C6 CRITICAL FIX #1: Set GPIO pins as INPUT/OUTPUT BEFORE twai_driver_install()
   // This is a known ESP32-C6 TWAI bug workaround from ESP-IDF forums
   pinMode(CAN_TX, OUTPUT);
   pinMode(CAN_RX, INPUT);
   Serial.println("CAN: GPIO pins configured (TX=OUTPUT, RX=INPUT)");
 
   // ESP32-C6 CRITICAL FIX #2: Add small delay after pinMode() for GPIO matrix to settle
   delay(10);
 
 #ifdef CAN_LISTEN_ONLY
   twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
       (gpio_num_t)CAN_TX, (gpio_num_t)CAN_RX, TWAI_MODE_LISTEN_ONLY);
   // ESP32-C6 TWAI known issue: tx_queue_len=0 can cause install to fail.
   // Use 1 as workaround — LISTEN_ONLY mode still never transmits.
   g_config.tx_queue_len = 1;
   g_config.rx_queue_len = 32; // large RX buffer so we don't miss frames
   
   // ESP32-C6 CRITICAL FIX #3: Disable alerts to prevent ISR conflicts
   g_config.alerts_enabled = 0;
   
   Serial.println("CAN: mode = LISTEN_ONLY");
 #else
   twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
       (gpio_num_t)CAN_TX, (gpio_num_t)CAN_RX, TWAI_MODE_NORMAL);
   g_config.tx_queue_len = 1;
   g_config.rx_queue_len = 10;
   
   // ESP32-C6 CRITICAL FIX #3: Disable alerts to prevent ISR conflicts
   g_config.alerts_enabled = 0;
   
   Serial.println("CAN: mode = NORMAL");
 #endif
 
   twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
   twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
 
   esp_err_t installErr = twai_driver_install(&g_config, &t_config, &f_config);
   canLastInstallErr = installErr;
   Serial.printf("CAN: twai_driver_install → 0x%x (%s)\n",
                 installErr, installErr == ESP_OK ? "OK" : "FAIL");
 
   if (installErr == ESP_OK) {
     // ESP32-C6 CRITICAL FIX #4: Add delay before twai_start()
     delay(50);
     
     esp_err_t startErr = twai_start();
     Serial.printf("CAN: twai_start → 0x%x (%s)\n",
                   startErr, startErr == ESP_OK ? "OK" : "FAIL");
     if (startErr == ESP_OK) {
       canInitialized = true;
       canFailCount   = 0;
       canRetryMs     = 0;
       Serial.println("CAN: *** INITIALIZED OK — listening for frames ***");
       return;
     }
     twai_driver_uninstall();
   }
   // Failed — wait and retry
   canRetryMs = millis() + CAN_RETRY_INTERVAL_MS;
   Serial.println("CAN: init FAILED, retry in 10s");
 }
 
 // ===== DEMO DATA (animated fake values — no CAN bus needed) =====
 #ifdef DEMO_MODE
 void updateDemoData() {
   static float angle = 0.0f;
   angle += 0.05f;
   if (angle > TWO_PI) angle -= TWO_PI;
 
   // RPM: 800 idle → ~6200 redline, slow sine
   int rpm  = (int)(800 + 2700 * (1.0f + sinf(angle * 0.7f)));
   // Speed: 0-170 km/h, slightly out of phase
   int spd  = constrain((int)(85 + 82 * sinf(angle * 0.5f)), 0, 250);
   // Boost: -0.1…1.8 bar
   float boost = 0.8f + 0.9f * sinf(angle * 1.2f);
   // Throttle follows boost
   int thr  = constrain((int)(50 + 45 * sinf(angle * 1.2f)), 0, 100);
   // Temps warm up slowly
   static float coolantF = 20.0f, oilF = 20.0f;
   if (coolantF < 90.0f) coolantF += 0.1f;
   if (oilF     < 99.0f) oilF     += 0.06f;
   float battery   = 13.8f + 0.3f * sinf(angle * 0.3f);
 
   static unsigned long lastDemoMs = 0;
   unsigned long nowMs = millis();
   if (lastDemoMs == 0) lastDemoMs = nowMs;
   float dt_h = (float)(nowMs - lastDemoMs) / 3600000.0f;
   unsigned long dt_ms = nowMs - lastDemoMs;  // save BEFORE update
   lastDemoMs = nowMs;
 
   if (xSemaphoreTake(carMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
     car.rpm         = rpm;
     car.speed       = spd;
     car.boost       = boost;
     car.throttle    = thr;
     car.coolant     = (int)coolantF;
     car.oil         = (int)oilF;
     car.intake      = 28;
     car.battery     = battery;
 
     if (spd > car.maxSpeed) car.maxSpeed = spd;
     tripDistanceF  += spd * dt_h;
     car.distance    = (int)tripDistanceF;
     
     if (spd >= 2) tripElapsedMs += dt_ms;  // use saved dt_ms (before lastDemoMs update)
     
     float tripElapsedHours = (float)tripElapsedMs / 3600000.0f;
     if (tripElapsedHours > 0.001f)
       car.avgSpeed = (int)(tripDistanceF / tripElapsedHours);
     xSemaphoreGive(carMutex);
   }
 }
 #endif
 
 // ===== CAN TASK (runs concurrently with UI) =====
 // car struct is protected by carMutex.
 void canTask(void *param) {
   for (;;) {
 #ifdef DEMO_MODE
     updateDemoData();
     vTaskDelay(pdMS_TO_TICKS(50));
 #else
     readOBD2();
     // Μικρή παύση μεταξύ PIDs — η 100ms αναμονή στο readStandardPID παρέχει ήδη pacing
    vTaskDelay(pdMS_TO_TICKS(5));   // λίγο γρηγορότερη ανανέωση OBD2 χωρίς να "πνίγει" την CPU
 #endif
   }
 }
 
 void readOBD2() {
  // Handle pending DTC clear request from UI
  if (requestClearDTC) {
    clearDTCs();
    requestClearDTC = false;
  }

 #ifdef CAN_LISTEN_ONLY
   // ── LISTEN-ONLY MODE ──────────────────────────────────────────────────────
   // Drain RX queue FIRST (while canInitialized is true), then handle retry.
   if (canInitialized) {
     // STOPPED recovery (same fix as NORMAL mode)
     twai_status_info_t healthChk;
     if (twai_get_status_info(&healthChk) == ESP_OK) {
       if (healthChk.state == TWAI_STATE_STOPPED) {
         if (twai_start() != ESP_OK) {
           twai_driver_uninstall();
           canInitialized = false;
           canRetryMs = millis() + CAN_RETRY_INTERVAL_MS;
         }
       }
     }
     // Drain up to 20 frames per call (non-blocking)
     twai_message_t rx_msg;
     for (int i = 0; i < 20; i++) {
       if (twai_receive(&rx_msg, 0) == ESP_OK) {
         canRxCount++;
         canLastPidName = "ID:0x" + String(rx_msg.identifier, HEX);
         canLastVal     = rx_msg.data[0];
         Serial.printf("CAN RX: ID=0x%03X DATA[0]=0x%02X\n",
                       rx_msg.identifier, rx_msg.data[0]);
       } else {
         break;
       }
     }
   }
   // If not initialized, retry on schedule
   if (!canInitialized) {
     if (canRetryMs > 0 && millis() >= canRetryMs) canTryInit();
   }
   return;  // nothing else to do in listen mode
 
 #else
   // ── NORMAL MODE ───────────────────────────────────────────────────────────
   // STOPPED recovery
   if (canInitialized) {
     twai_status_info_t healthChk;
     if (twai_get_status_info(&healthChk) == ESP_OK) {
       if (healthChk.state == TWAI_STATE_STOPPED) {
         if (twai_start() != ESP_OK) {
           twai_driver_uninstall();
           canInitialized = false;
           canRetryMs = millis() + CAN_RETRY_INTERVAL_MS;
         }
       }
     }
   }
 
   // No CAN — check if it's time to retry init
   if (!canInitialized) {
     static bool firstCall = true;
     if (firstCall) {
       car.rpm = 0; car.speed = 0; car.coolant = 20; car.oil = 20;
       car.throttle = 0; car.boost = 0.0f; car.battery = 12.5f;
       car.intake = 20;
       firstCall = false;
     }
     if (canRetryMs > 0 && millis() >= canRetryMs) canTryInit();
     return;
   }
 
   // Auto-shutdown if too many consecutive failures (no bus / no transceiver)
   if (canFailCount >= CAN_FAIL_MAX) {
     canShutdown();
     return;
   }
 
 #endif  // CAN_LISTEN_ONLY
 
   // ---- Round-robin: 1 PID per loop call → max ~8ms blocking per loop ----
   // Cycle 0-9: high-freq (RPM/Speed/MAP/Throttle repeat 2.5x)
   // Cycle 10-15: slow PIDs (Coolant, IAT, OilTemp, Battery, Fuel, FuelRate)
   static uint8_t pidSlot = 0;
   int val;
 
   switch (pidSlot) {
     case 0: case 3: case 6:  // RPM — 3× per full cycle
       pidNames[0] = "RPM (0x0C)";
       val = readStandardPID(PID_RPM);
       pidWorking[0] = (val != -999);
       if (val != -999) { if(xSemaphoreTake(carMutex, pdMS_TO_TICKS(5))) { car.rpm = val; xSemaphoreGive(carMutex); } }
       break;
     case 1: case 4: case 7:  // Speed — 3× per full cycle
       pidNames[1] = "SPEED (0x0D)";
       val = readStandardPID(PID_SPEED);
       pidWorking[1] = (val != -999);
       if (val != -999) { if(xSemaphoreTake(carMutex, pdMS_TO_TICKS(5))) { car.speed = val; xSemaphoreGive(carMutex); } }
       break;
     case 2: case 5:          // MAP/Boost — 2× per full cycle
       pidNames[2] = "MAP (0x0B)";
       val = readStandardPID(PID_MAP);
       pidWorking[2] = (val != -999);
       if (val != -999) { if(xSemaphoreTake(carMutex, pdMS_TO_TICKS(5))) { car.boost = (float)(val - 101) / 100.0f; xSemaphoreGive(carMutex); } }
       break;
     case 8:                  // Throttle
       pidNames[8] = "THROTTLE (0x11)";
       val = readStandardPID(PID_THROTTLE);
       pidWorking[8] = (val != -999);
       if (val != -999) { if(xSemaphoreTake(carMutex, pdMS_TO_TICKS(5))) { car.throttle = val; xSemaphoreGive(carMutex); } }
       break;
     case 9:                  // Coolant
       pidNames[9] = "COOLANT (0x05)";
       val = readStandardPID(PID_COOLANT);
       pidWorking[9] = (val != -999);
       if (val != -999) { if(xSemaphoreTake(carMutex, pdMS_TO_TICKS(5))) { car.coolant = val; xSemaphoreGive(carMutex); } }
       break;
     case 10:                 // Intake AIR temp - standard PID 0x0F works!
       pidNames[10] = "INTAKE (0x0F)";
       val = readStandardPID(PID_INTAKE);
       pidWorking[10] = (val != -999);
       if (val != -999) { if(xSemaphoreTake(carMutex, pdMS_TO_TICKS(5))) { car.intake = val; xSemaphoreGive(carMutex); } }
       break;
     case 11: {                // Oil temp - 0x0407 works, test EA111 too
       // Oil Temp removed per user request
       break;
     }
     case 12:                 // Battery voltage
       pidNames[12] = "BATTERY (0x42)";
       val = readStandardPID(PID_BATTERY);
       pidWorking[12] = (val != -999);
       if (val != -999) { if(xSemaphoreTake(carMutex, pdMS_TO_TICKS(5))) { car.battery = val / 1000.0f; xSemaphoreGive(carMutex); } }
       break;
     case 13: {              // DTC count + codes
       pidNames[13] = "DTC (0x01)";
       int dtcCount = 0;
       bool milOn = false;
       if (readDTCCount(&dtcCount, &milOn)) {
         pidWorking[15] = true;
         if (xSemaphoreTake(carMutex, pdMS_TO_TICKS(5))) {
           car.dtc = dtcCount;
           car.mil = milOn;
           xSemaphoreGive(carMutex);
         }
         // Read actual codes if any exist
         if (dtcCount > 0) {
           String codes[10];
           int found = 0;
           readDTCCodes(codes, 10, &found);
           if (xSemaphoreTake(carMutex, pdMS_TO_TICKS(5))) {
             for (int i = 0; i < 10; i++)
               activeDTCs[i] = (i < found) ? codes[i] : "";
             xSemaphoreGive(carMutex);
           }
         } else {
           if (xSemaphoreTake(carMutex, pdMS_TO_TICKS(5))) {
             for (int i = 0; i < 10; i++) activeDTCs[i] = "";
             xSemaphoreGive(carMutex);
           }
         }
       } else {
         pidWorking[15] = false;
       }
       break;
     }
     case 14: {               // Fuel Rate (0x5E) — L/h instantaneous (πρωτεύον)
       int raw = readStandardPID(PID_FUEL_RATE);
       if (raw != -999) {
         // 0x5E απάντησε: αποθηκεύουμε (0 ή >0). Πλέον instantFuelLh ≥ 0 για πάντα
         float lh = (float)raw * 0.05f;  // (A*256+B)*0.05 = L/h
         if(xSemaphoreTake(carMutex, pdMS_TO_TICKS(5))) {
           car.instantFuelLh = lh;
           xSemaphoreGive(carMutex);
         }
       }
       // αν -999: αφήνουμε instantFuelLh = -1 → MAF fallback θα το καλύψει
       break;
     }
     case 15: {               // MAF (0x10) — g/s, fallback υπολογισμός κατανάλωσης
       int rawMaf = readStandardPID(PID_MAF);
       if (rawMaf != -999) {
         float gps = (float)rawMaf / 100.0f;  // (256*A+B)/100 = g/s
         if(xSemaphoreTake(carMutex, pdMS_TO_TICKS(5))) {
           car.maf = gps;
           xSemaphoreGive(carMutex);
         }
       }
       break;
     }
   }
 
   pidSlot++;
   if (pidSlot > 15) pidSlot = 0;  // 16 slots (0-15)
 
   // ---- Max speed tracking ----
   if (xSemaphoreTake(carMutex, 0)) {
     if (car.speed > car.maxSpeed) car.maxSpeed = car.speed;
     xSemaphoreGive(carMutex);
   }
 
   // ---- Trip data — updated every 100ms for better accuracy ----
   unsigned long now = millis();
   if (now - lastOdometerMs >= 100) {
     unsigned long dt_ms = now - lastOdometerMs;
     float dt_h = (float)dt_ms / 3600000.0f;
     lastOdometerMs = now;
     if (xSemaphoreTake(carMutex, pdMS_TO_TICKS(10))) {
       float dDist = (float)car.speed * dt_h;
       tripDistanceF += dDist;
       car.distance = (int)tripDistanceF;
 
       // Συσσώρευση λίτρων καυσίμου — 3-tier fallback:
       //   Tier 1: PID 0x5E > 0  → άμεσo L/h από το ECU
       //   Tier 2: PID 0x10 MAF > 0  → L/h = MAF(g/s) / AFR / ρ × 3600
       //           1.4 TSI: AFR=14.7, ρ=735 g/L → factor 3600/(14.7×735) ≈ 0.333
       //   Tier 3: MAP + RPM + IAT (Speed-Density / ΕΩΣ 0x0B+0x0C+0x0F)
       //           Για VW 1.4 TSI EA111 (1395cc, AFR=14.7, ρ=740g/L, VE≈0.85):
       //           L/h = MAP_kPa × RPM × 0.011393 / IAT_K
       //           Constant = disp(L)×3600×1000×VE / (120×R_air×AFR×ρ_fuel)
       //                    = 1.395×3600×1000×0.85 / (120×287×14.7×0.740) ≈ 0.011393
       float effectiveLh = 0.0f;
       if (car.instantFuelLh > 0.0f) {
         // Tier 1: PID 0x5E fuel rate — guard >0 (όχι >=0) αλλιώς ECU που
         // επιστρέφει 0 μπλοκάρει τα Tier 2/3 fallbacks
         effectiveLh = car.instantFuelLh;
       } else if (car.maf > 0.0f && car.rpm > 400) {
         // Tier 2: MAF sensor (PID 0x10)
         effectiveLh = car.maf * 0.333f;
       } else if (car.rpm > 400) {
         // Tier 3: Speed-Density — MAP(0x0B) + RPM(0x0C) + IAT(0x0F)
         // Δουλεύει σε κάθε αυτοκίνητο που απαντά σε αυτά τα 3 PIDs
         float map_kPa = car.boost * 100.0f + 101.0f;  // bar gauge → kPa abs
         float iat_K   = (float)car.intake + 273.15f;
         if (map_kPa > 20.0f && iat_K > 233.0f) {
           effectiveLh = map_kPa * (float)car.rpm * 0.011393f / iat_K;
         }
       }
       if (effectiveLh > 0.0f) {
         tripFuelL += effectiveLh * dt_h;
         car.fuelUsed = tripFuelL;
       }
       
       if (car.speed >= 2) tripElapsedMs += dt_ms;
       
       float tripElapsedHours = (float)tripElapsedMs / 3600000.0f;
       if (tripElapsedHours > 0.001f)
         car.avgSpeed = (int)(tripDistanceF / tripElapsedHours);
       else
         car.avgSpeed = 0;
         
       xSemaphoreGive(carMutex);
     }
   }
 }
 
 // ===== SERIAL MONITOR INPUT =====
 // Use "No line ending" OR "Newline" in Arduino Serial Monitor.
 // Commands: sim:0  reset:0  rpm:3500  speed:120  coolant:87  etc.
 String _serialBuf = "";
 unsigned long _serialLastCharMs = 0;
 
 void serialEvent() {
   while (Serial.available()) {
     char c = (char)Serial.read();
     _serialLastCharMs = millis();
     if (c == '\n' || c == '\r') {
       _serialBuf.trim();
       if (_serialBuf.length() > 0) { parseSerialCmd(_serialBuf); }
       _serialBuf = ""; _serialLastCharMs = 0;
       return;
     }
     if (c >= 32 && c < 127) {
       _serialBuf += c;
       if (_serialBuf.length() > 40) _serialBuf = "";
     }
   }
 }
 
 void handleSerial() {
   serialEvent(); // drain bytes
   // "No Line Ending" fallback: fire after 200ms silence
   if (_serialBuf.length() > 0 && _serialLastCharMs > 0 &&
       (millis() - _serialLastCharMs) >= 200) {
     _serialBuf.trim();
     if (_serialBuf.length() > 0) parseSerialCmd(_serialBuf);
     _serialBuf = ""; _serialLastCharMs = 0;
   }
 }
 
 // Parse and execute one "key:value" command (called from handleSerial)
 void parseSerialCmd(String &s) {
   s.trim();
   // Αν η εντολή περιέχει κενά, σπάσε την σε μικρότερες εντολές
   int spaceIdx = s.indexOf(' ');
   if (spaceIdx > 0) {
     int start = 0;
     while (start < s.length()) {
       int end = s.indexOf(' ', start);
       if (end == -1) end = s.length();
       String token = s.substring(start, end);
       token.trim();
       if (token.length() > 0) parseSerialCmd(token); // Αναδρομική κλήση
       start = end + 1;
     }
     return;
   }
 
   Serial.print("CMD: ["); Serial.print(s); Serial.println("]");
 
   int sep = s.indexOf(':');
   if (sep < 0) {
     Serial.println("ERR: format is  key:value  e.g.  sim:0  rpm:3500");
     return;
   }
   String key = s.substring(0, sep);
   key.trim(); key.toLowerCase();
   float val = s.substring(sep + 1).toFloat();
 
   // Write directly — no mutex needed (canTask does nothing when CAN is off)
   if      (key == "rpm")      car.rpm         = (int)val;
   else if (key == "speed")    car.speed        = (int)val;
   else if (key == "coolant")  car.coolant      = (int)val;
   else if (key == "oil")      car.oil          = (int)val;
   else if (key == "intake")   car.intake       = (int)val;
   else if (key == "throttle") car.throttle     = (int)val;
   else if (key == "boost")    car.boost        = val;
   else if (key == "battery")  car.battery      = val;
 
   else if (key == "dtc")      car.dtc          = (int)val;
   else if (key == "sim") {
     car.rpm = 2200; car.speed = 100; car.coolant = 87; car.oil = 92;
     car.intake = 28; car.throttle = 22; car.boost = 0.3f;
     car.battery = 14.2f;
     car.maxSpeed = 100; car.dtc = 0;
     tripDistanceF = 15.0f; car.distance = 15;
     tripElapsedMs = 540000UL;  car.avgSpeed = 100;
     Serial.println("OK: sim — speed=100 rpm=2200 coolant=87 oil=92 battery=14.2");
     return;
   }
   else if (key == "reset") {
     resetTrip();
     Serial.println("OK: reset done");
     return;
   }
   else {
     Serial.print("ERR: unknown key '"); Serial.print(key); Serial.println("'");
     Serial.println("     sim  reset  rpm  speed  coolant  oil  intake  throttle  boost  battery  instant  dtc");
     return;
   }
   Serial.print("OK: "); Serial.print(key); Serial.print(" = "); Serial.println(val);
 }
 
 void resetTrip() {
   if (xSemaphoreTake(carMutex, pdMS_TO_TICKS(100))) {
     tripDistanceF = 0.0f;
     tripElapsedMs = 0;
     tripFuelL     = 0.0f;
     car.distance  = 0;
     car.avgSpeed  = 0;
     car.maxSpeed  = 0;
     car.fuelUsed  = 0.0f;
     // instantFuelLh: -1 = δεν ξέρουμε ακόμα αν 0x5E δουλεύει (θα ενημερωθεί σύντομα)
     car.instantFuelLh = -1.0f;
     car.distanceOffset = -1; // Force re-read of OBD distance offset
     xSemaphoreGive(carMutex);
   }
   tripStartTime = millis();
   lastOdometerMs = millis();
 }
 
 // ===== TOUCH HANDLING =====
 void handleTouch() {
   // ---- Long-press detection (brightness) — disabled on DTC tab ----
   if (fingerDown && !brightnessMode && longPressArmed &&
       !(currentTab == TAB_SYSTEM && currentItemIndex[TAB_SYSTEM] == 1)) {
     if ((millis() - swipeStartTime) >= 2000) {
       brightnessMode = true;
       brightnessModeTimeout = millis() + 5000;
       fingerDown = false;
       swipeStartX = -1;
       swipeStartY = -1;
       longPressArmed = false;
       drawBrightnessOverlay();
       return;
     }
   }
 
   // ---- ACCEL 1s hold ---- ίδιος τρόπος με brightness, χωρίς να ριχνει fingerDown
   if (fingerDown && currentTab == TAB_SYSTEM && currentItemIndex[TAB_SYSTEM] == 2 && accelState == ACCEL_IDLE
       && longPressArmed && !accelHoldFired) {
     if ((millis() - swipeStartTime) >= 1000) {
       accelHoldFired = true;
       longPressArmed = false;
       handleAccelTap();  // IDLE → ARMED
       return;
     }
   }
   
   // ---- DTC 2s hold ----
   if (fingerDown && currentTab == TAB_SYSTEM && currentItemIndex[TAB_SYSTEM] == 1 && !requestClearDTC && !alertBlinking) {
       isHoldingDtcs = true;
       unsigned long holdElap = millis() - swipeStartTime;
       if (holdElap >= 2000) {
           requestClearDTC = true;
           isHoldingDtcs = false;
           longPressArmed = false;
           fingerDown = false;
           swipeStartX = -1;
           swipeStartY = -1;
           Serial.println("DTC CLEAR REQUESTED BY TOUCH");
           // Show feedback
           tft->fillRect(13, 31, SCREEN_WIDTH - 13, SCREEN_HEIGHT - 31, COLOR_BG);
           setU8g2Font(4);
           u8g2.setForegroundColor(COLOR_GREEN);
           int cw = u8g2.getUTF8Width("CLEARED");
           u8g2.setCursor((SCREEN_WIDTH - cw) / 2, 90 + u8g2.getFontAscent());
           u8g2.print("CLEARED");
           delay(500);
           drawUI();
           return;
       } else if (holdElap > 200) {
           // Draw progress bar
           float prog = holdElap / 2000.0f;
           const int barW = 150, barH = 10;
           int barX = (SCREEN_WIDTH - barW) / 2;
           int barY = 120;
           tft->fillRect(barX, barY, barW, barH, COLOR_DARK);  // track
           if (prog > 0.01f) tft->fillRect(barX, barY, (int)(barW * prog), barH, COLOR_ORANGE);
       }
   }

   // Interrupt-driven: μόνο όταν το ISR έχει θέσει το flag
   if (!touchEvent) {
     // Αν είχαμε finger down και πέρασαν > 300ms από το τελευταίο event = finger lifted
     // Χρησιμοποιούμε lastTouchEventMs (όχι swipeStartTime) ώστε το hold > 1s να μην κοπεί
     if (fingerDown && (millis() - lastTouchEventMs > 300)) {
       fingerDown = false;
       swipeStartX = -1;
       swipeStartY = -1;
       longPressArmed = false;
       // Αν διακόπτεται η κράτηση στο IDLE: καθάρισμα bar σταδιακής εξέλιξης
       if (currentTab == TAB_SYSTEM && currentItemIndex[TAB_SYSTEM] == 2 && accelState == ACCEL_IDLE) accelNeedsRedraw = true;
       // καθάρισμα dtc bar
       if (isHoldingDtcs) { isHoldingDtcs = false; drawUI(); }
     }
     return;
   }
   touchEvent = false; // Καθαρισμός flag
 
   // Αν η οθόνη κοιμάται → ξύπνα και αγνόησε το touch (μην κάνεις swipe)
   if (screenSleeping) {
     wakeScreen();
     return;
   }
 
   // Ανανέωσε τον χρόνο τελευταίας δραστηριότητας
   lastActivityMs = millis();
 
   // Alert blink → tap = dismiss + 10s ελεύθερη πλοήγηση
   // Μετά τα 10s: αν ακόμα στην επικίνδυνη ζώνη → νέο alert αυτόματα
   if (alertBlinking && !fingerDown) {
     dismissAlert();
     alertDismissedMask   = 0;
     alertCooldownUntilMs = millis() + 10000;  // 10s ελεύθερη πλοήγηση
     fingerDown = false;
     return;
   }
 
   int touchX = 0, touchY = 0;
   uint8_t gesture = 0;
   uint8_t fingerCount = 1;
 
   if (!readCST816S(&touchX, &touchY, &gesture, &fingerCount)) {
     fingerDown = false;
     swipeStartX = -1;
     swipeStartY = -1;
     longPressArmed = false;
     return;
   }
   lastTouchEventMs = millis();  // ανανέωση χρόνου τελευταίας IRQ event
 
   // ---- Brightness mode: swipe left/right adjusts brightness ----
   if (brightnessMode) {
     brightnessModeTimeout = millis() + 5000; // extend on each touch
     if (!fingerDown) {
       fingerDown = true;
       swipeStartX = touchX;
       swipeStartY = touchY;
       swipeStartTime = millis();
       return;
     }
     int dy = touchY - swipeStartY;
     if (abs(dy) >= 30) {
       int step = (dy < 0) ? -25 : 25;  // swipe up = dimmer, swipe down = brighter
       currentBrightness = constrain(currentBrightness + step, 10, 255);
       ledcWrite(TFT_BL, currentBrightness);
       drawBrightnessOverlay();
       swipeStartX = touchX;
       swipeStartY = touchY;
       swipeStartTime = millis();
     }
     return;
   }
 
   // ---- Πρώτη επαφή: αποθήκευση start position ----
   if (!fingerDown) {
     fingerDown = true;
     swipeStartX = touchX;
     swipeStartY = touchY;
     swipeStartTime = millis();
     longPressArmed = true;  // start long-press timer
     accelHoldFired = false;  // επαναφορά hold flag σε κάθε νέα επαφή
     Serial.printf("Touch START: X=%d Y=%d\n", touchX, touchY);
     return;
   }
 
   // ---- Συνεχής κίνηση: υπολόγισε delta ----
   int dx = touchX - swipeStartX;
   int dy = touchY - swipeStartY;
   int absDx = abs(dx);
   int absDy = abs(dy);
   unsigned long elapsed = millis() - swipeStartTime;
 
   // If finger moved significantly, disarm long press
   if (absDx > 15 || absDy > 15) longPressArmed = false;
 
   if (absDx < 35 && absDy < 35) {
     // Μικρή κίνηση — για ACCEL IDLE αφήνουμε τα swipes να περνάνε κανονικά
     if (currentTab == TAB_SYSTEM && currentItemIndex[TAB_SYSTEM] == 2 && accelState == ACCEL_IDLE) {
       // Μην μπλοκάρεις εδώ — αφηνίστε το swipe detection να τρέξει κανονικά
     } else if (currentTab == TAB_SYSTEM && currentItemIndex[TAB_SYSTEM] == 2 && accelState != ACCEL_IDLE && !accelHoldFired && elapsed > 80) {
       // Σύντομο τάπ: ARMED → ακύρωση, DONE → επαναφορά
       handleAccelTap();
       fingerDown     = false;
       swipeStartX    = -1;
       swipeStartY    = -1;
       longPressArmed = false;
       return;
     } else {
       return;  // άλλα tabs: περιμένε finger-up timeout
     }
   }
 
   // Cooldown μετά από επιτυχή gesture → αποτρέπει διπλό trigger
   static unsigned long gestureCooldownMs = 0;
   if (millis() < gestureCooldownMs) return;
 
   Serial.printf("Swipe: dx=%d dy=%d elapsed=%lu ms\n", dx, dy, elapsed);
 
   // Χαλαρότερη αναλογία κατεύθυνσης (1.3) για καλύτερη αντίληψη
   if (absDx > absDy * 1.3) {
     // === ΟΡΙΖΟΝΤΙΟ SWIPE -> Αλλαγή Item ===
     // On SYSTEM ACCEL item: horizontal swipe toggles accel mode
     if (currentTab == TAB_SYSTEM && currentItemIndex[TAB_SYSTEM] == 2) {
       accelMode        = 1 - accelMode; // toggle 0↔1
       accelState       = ACCEL_IDLE;
       accelNeedsRedraw = true;
     } else {
       if (dx < 0) {
         Serial.println("   -> NEXT ITEM (Swipe Left)");
         currentItemIndex[currentTab]++;
         if (currentItemIndex[currentTab] >= getItemsCount(currentTab))
           currentItemIndex[currentTab] = 0;
       } else {
         Serial.println("   -> PREV ITEM (Swipe Right)");
         currentItemIndex[currentTab]--;
         if (currentItemIndex[currentTab] < 0)
           currentItemIndex[currentTab] = getItemsCount(currentTab) - 1;
       }
       // Arriving at SYSTEM ACCEL item: reset accel state
       if (currentTab == TAB_SYSTEM && currentItemIndex[TAB_SYSTEM] == 2) {
         accelState       = ACCEL_IDLE;
         accelNeedsRedraw = true;
       }
     }
     showItemTransition(dx < 0);
     drawUI();
   } else if (absDy > absDx * 1.3) {
     // === ΚΑΘΕΤΟ SWIPE → Αλλαγή Tab (πάντα, από όλες τις καταστάσεις) ===
     // Αλλαγή mode με horizontal swipe (L/R)
     if (dy > 0) {
       Serial.println("   -> PREV TAB (Swipe Down)");
       currentTab--;
       if (currentTab < 0) currentTab = TAB_COUNT - 1;
     } else {
       Serial.println("   -> NEXT TAB (Swipe Up)");
       currentTab++;
       if (currentTab >= TAB_COUNT) currentTab = 0;
     }
     currentItemIndex[currentTab] = 0;
     showTabTransition(dy < 0);
     drawUI();
   } else {
     // Διαγώνιο - αγνόησε, μην επαναφέρεις
     return;
   }
 
   // Cooldown 350ms μετά από επιτυχή gesture + reset
   gestureCooldownMs = millis() + 350;
   fingerDown = false;
   swipeStartX = -1;
   swipeStartY = -1;
 }
 
 // ===== CST816S INIT =====
 // mode_touch: το chip στέλνει IRQ κάθε 10ms ενώ το δάχτυλο αγγίζει
 // Εμείς μετράμε το delta X/Y για να βρούμε κατεύθυνση swipe
 void initCST816S() {
   // Wire already initialized in setup() - no need to call Wire.begin() again
   delay(10);
 
   // Disable auto sleep
   Wire.beginTransmission(touchAddress);
   Wire.write(0xFE); // REG_DIS_AUTOSLEEP
   Wire.write(0xFF); // Disable
   if (Wire.endTransmission(true) != 0) {
     Serial.println("   WARNING: CST816S write failed (check wiring!)");
   }
   delay(5);
 
   // IRQ mode: mode_touch = 0x40 (interrupt κάθε 10ms ενώ αγγίζεις)
   // Αυτό είναι το ΠΙΣΤΟΠΟΙΗΜΕΝΟ ότι δουλεύει από το koendv library
   Wire.beginTransmission(touchAddress);
   Wire.write(0xFA); // REG_IRQ_CTL
   Wire.write(0x40); // IRQ_EN_TOUCH - fire every 10ms while touching
   Wire.endTransmission(true);
   delay(5);
 
   // Verify: read chip ID
   Wire.beginTransmission(touchAddress);
   Wire.write(0xA7); // REG_CHIP_ID
   Wire.endTransmission(true);
   delayMicroseconds(200);
   Wire.requestFrom((uint8_t)touchAddress, (uint8_t)1);
   if (Wire.available()) {
     uint8_t chipId = Wire.read();
     Serial.printf("   CST816S ChipID=0x%02X (", chipId);
     if (chipId == 0xB4) Serial.print("CST816S");
     else if (chipId == 0xB5) Serial.print("CST816T");
     else if (chipId == 0xB6) Serial.print("CST816D");
     else if (chipId == 0x20) Serial.print("CST716");
     else Serial.print("UNKNOWN");
     Serial.println(") - mode_touch (IRQ every 10ms)");
   } else {
     Serial.println("   ERROR: Cannot read CST816S ChipID!");
   }
 }
 
 // ===== CST816S READ =====
 // Reads touch data directly (called from handleTouch after ISR flag)
 // No INT pin check here - already handled by interrupt
 bool readCST816S(int *x, int *y, uint8_t *gesture, uint8_t *fingerCount) {
   // Write register address (with STOP)
   Wire.beginTransmission(touchAddress);
   Wire.write(0x01); // REG_GESTURE_ID
   if (Wire.endTransmission(true) != 0) return false;
 
   delayMicroseconds(200);
 
   // Read 6 bytes
   uint8_t received = Wire.requestFrom((uint8_t)touchAddress, (uint8_t)6);
   if (received < 6) return false;
 
   uint8_t buf[6];
   for (int i = 0; i < 6; i++) buf[i] = Wire.read();
 
   // buf[0]=GestureID, buf[1]=FingerNum
   // buf[2]=XposH[3:0], buf[3]=XposL
   // buf[4]=YposH[3:0], buf[5]=YposL
   uint8_t fingerNum = buf[1];
   if (fingerNum == 0) return false;
 
   *gesture = buf[0];
   *x = (((uint16_t)(buf[2] & 0x0F)) << 8) | buf[3];
   *y = (((uint16_t)(buf[4] & 0x0F)) << 8) | buf[5];
   if (fingerCount) *fingerCount = fingerNum;
 
   // Rotation 3 (180° flip): invert both axes to match display orientation
   *x = 319 - *x;
   *y = 171 - *y;
 
   return true;
 }
 

// ===== MISSING IMPLEMENTATIONS =====

void switchTo(int tab, int item) {
  if (tab < 0 || tab >= TAB_COUNT) return;
  int maxItems = getItemsCount(tab);
  if (item < 0 || item >= maxItems) item = 0;
  if (currentTab == tab && currentItemIndex[tab] == item) return;
  currentTab = tab;
  currentItemIndex[tab] = item;
  drawUI();
}

void updateRGB() {
  // During alert: updateAlertBlink() controls the LED — don't override it
  if (alertBlinking) return;

  uint32_t color = 0;
  if (car.rpm > 6000) {
    if ((millis() / 100) % 2 == 0) color = pixels.Color(255, 0, 0);
  } else if (car.coolant > 100) {
    color = pixels.Color(255, 0, 0);
  } else if (car.coolant < 60) {
    color = pixels.Color(0, 0, 50);
  }
  pixels.setPixelColor(0, color);
  pixels.show();
}
