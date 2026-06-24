# ESP32-C6 OBD2 Dashboard

Ένα προηγμένο, διαδραστικό και γρήγορο **ψηφιακό όργανο OBD2 (Dashboard)** για αυτοκίνητα, σχεδιασμένο και δοκιμασμένο σε **VW Tiguan 5N 1.4 TSI (Twincharger)**. 

Το project βασίζεται στην πλακέτα **Waveshare ESP32-C6-LCD-1.47** (οθόνη IPS ST7789 με ανάλυση 172x320 και αισθητήρα αφής CST816S), χρησιμοποιώντας έναν πομποδέκτη CAN **SN65HVD230** συνδεδεμένο απευθείας στον ελεγκτή TWAI του ESP32-C6.

---

## 🛠️ Χαρακτηριστικά (Features)

*   **Real-time OBD2 Telemetry**: Λήψη δεδομένων μέσω του διαύλου CAN (500kbps) χρησιμοποιώντας standard OBD2 PIDs.
*   **Σύστημα Καρτελών (Tabs) & Πλοήγηση με Χειρονομίες (Swipe Gestures)**:
    *   **Swipe Up / Down**: Εναλλαγή μεταξύ των 4 βασικών κατηγοριών (Tabs) με εφέ "Venetian Blinds".
    *   **Swipe Left / Right**: Εναλλαγή μεταξύ των επιμέρους παραμέτρων σε κάθε Tab με φωτεινή γραμμή μετάβασης.
    *   **Tap / Hold**: Έλεγχος φωτεινότητας, έναρξη μετρήσεων accel, ή διαγραφή βλαβών (DTC).
*   **4 Θεματικές Ενότητες (Tabs)**:
    1.  **MAIN**: Speed (km/h), RPM (έγχρωμη κλίμακα & ειδοποίηση στις 5500+), Throttle Position (%), Turbo Boost (bar, υπολογισμένο από το MAP).
    2.  **TEMPS**: Coolant Temperature (°C, ειδοποίηση άνω των 95°C), Intake Air Temperature (°C).
    3.  **TRIP INFO**: Απόσταση διαδρομής (km), Μέση Ταχύτητα (km/h), Μέγιστη Ταχύτητα (km/h), Κατανάλωση Καυσίμου (L) & Μέση Κατανάλωση (L/100km).
    4.  **SYSTEM**: Τάση Μπαταρίας (V), Ανάγνωση & Διαγραφή DTC σφαλμάτων (Mode 03/04), Acceleration Timer.
*   **Υπολογισμός Κατανάλωσης 3 Επιπέδων (3-Tier Fuel Consumption Fallback)**:
    1.  *Tier 1*: Άμεση ανάγνωση Fuel Rate (PID 0x5E) από τον εγκέφαλο.
    2.  *Tier 2 (Fallback)*: Υπολογισμός μέσω MAF (PID 0x10) αν το Fuel Rate δεν υποστηρίζεται.
    3.  *Tier 3 (Speed-Density)*: Υπολογισμός μέσω MAP, RPM και INTAKE (IAT) με μαθηματικό μοντέλο βελτιστοποιημένο για τον κινητήρα EA111 1.4 TSI.
*   **Engine Startup State Machine**: 
    *   Ανίχνευση εκκίνησης/λειτουργίας του κινητήρα. Αν ο κινητήρας σβήσει (RPM <= 500), η οθόνη μεταβαίνει αυτόματα στην ένδειξη της Μπαταρίας (Battery Volts) μετά από 10 δευτερόλεπτα για αποφυγή αποφόρτισης.
    *   Αυτόματη εναλλαγή στην καρτέλα Speed όταν το όχημα ξεκινήσει να κινείται.
*   **Acceleration Timer**: Ψηφιακό χρονόμετρο επιτάχυνσης **0-100 km/h** ή **60-120 km/h** με ζωντανή ροή χρόνου/ταχύτητας και τελική αναφορά (χρόνος, απόσταση σε μέτρα, ταχύτητα εξόδου).
*   **Ανάγνωση & Διαγραφή Βλαβών (DTC Reader/Clearer)**: Εμφάνιση των κωδικών βλάβης (π.χ. P0301) στην οθόνη και διαγραφή τους κρατώντας πατημένη την οθόνη για 2 δευτερόλεπτα.
*   **Flicker-Free Easing Gauge**: Ημικυκλικές μπάρες (Gauges) με Grafana-style ομαλή κίνηση (spring physics) και τεχνική delta rendering (επανασχεδίαση μόνο της διαφοράς γωνίας) για εξαιρετικά γρήγορο ρυθμό ανανέωσης χωρίς τρεμόπαιγμα.
*   **FreeRTOS Multitasking**: Διαχωρισμός εργασιών σε single-core RISC-V. Η λήψη OBD2 τρέχει σε background task (`canTask`), ενώ η σχεδίαση UI και η αφή στο main `loop()`, με προστασία δεδομένων μέσω Mutexes.

---

## 🔌 Συνδεσμολογία & Pins (Hardware Pinout)

### Waveshare ESP32-C6-LCD-1.47 Mappings:
*   **LCD Display (ST7789 SPI)**:
    *   `TFT_SCLK` -> GPIO 1 (LCD_CLK)
    *   `TFT_MOSI` -> GPIO 2 (LCD_DIN)
    *   `TFT_CS` -> GPIO 14 (LCD_CS)
    *   `TFT_DC` -> GPIO 15 (LCD_DC)
    *   `TFT_RST` -> GPIO 22 (LCD_RST)
    *   `TFT_BL` -> GPIO 23 (LCD_BL) (PWM φωτεινότητας)
*   **Touch Screen (CST816S I2C)**:
    *   `TOUCH_SDA` -> GPIO 18
    *   `TOUCH_SCL` -> GPIO 19
    *   `TOUCH_RST` -> GPIO 20
    *   `TOUCH_INT` -> GPIO 21 (Interrupt-driven)
*   **RGB LED (WS2812)**:
    *   `RGB_PIN` -> GPIO 8
*   **CAN Transceiver (SN65HVD230)**:
    *   `CAN_TX` -> GPIO 5
    *   `CAN_RX` -> GPIO 4

> [!IMPORTANT]
> **ESP32-C6 Critical Fix**: Λόγω ιδιαιτερότητας του GPIO Matrix στο ESP32-C6, τα pins `CAN_TX` και `CAN_RX` πρέπει να δηλωθούν ως `OUTPUT` και `INPUT` αντίστοιχα με την `pinMode()` **πριν** από την αρχικοποίηση του TWAI driver, ώστε να αποφευχθούν σφάλματα δρομολόγησης.

---

## 💻 Εντολές Serial Debugging (Simulation Mode)

Αν δεν είστε συνδεδεμένοι στο αυτοκίνητο, μπορείτε να προσομοιώσετε τιμές στέλνοντας εντολές μέσω του Serial Monitor (baud rate: `115200`, ρύθμιση `No Line Ending` ή `Newline`):

*   `sim:0` - Ενεργοποιεί προκαθορισμένες τιμές προσομοίωσης για δοκιμή του UI.
*   `reset:0` - Μηδενίζει τα στατιστικά ταξιδιού (trip values).
*   `rpm:3500` - Ορίζει τις στροφές κινητήρα στις 3500 RPM.
*   `speed:120` - Ορίζει την ταχύτητα στα 120 km/h.
*   `coolant:90` - Ορίζει τη θερμοκρασία ψυκτικού στους 90°C.
*   `boost:0.8` - Ορίζει την πίεση Turbo στα 0.8 bar.
*   `battery:14.2` - Ορίζει την τάση μπαταρίας στα 14.2V.
*   `throttle:45` - Ορίζει το άνοιγμα της πεταλούδας στο 45%.
*   `dtc:2` - Προσομοιώνει την ύπαρξη 2 κωδικών βλάβης (DTCs).

---

## 📚 Απαραίτητες Βιβλιοθήκες (Required Libraries)

Για τη μεταγλώττιση (compilation) στο Arduino IDE, απαιτούνται οι εξής βιβλιοθήκες:
1.  **Arduino_GFX_Library** (για την οθόνη ST7789)
2.  **Adafruit_NeoPixel** (για τον έλεγχο του ενσωματωμένου WS2812 LED)
3.  **Adafruit_GFX Library** (core βιβλιοθήκη γραφικών)
4.  **U8g2_for_Adafruit_GFX** (για την υποστήριξη ελληνικών/διεθνών TrueType fonts)

---

## ⚙️ Ρυθμίσεις Μεταγλώττισης (Compilation Notes)
*   **Πλακέτα στο Arduino IDE**: Επιλέξτε `ESP32C6 Dev Module`.
*   **USB CDC On Boot**: Ανεβάστε τον κώδικα με το `USB CDC On Boot` ρυθμισμένο σε `Enabled` αν θέλετε serial logging, ή `Disabled` αν θέλετε άμεση εκκίνηση στο αυτοκίνητο χωρίς καθυστέρηση αναμονής USB.
*   **CAN Diagnostic (Listen-Only)**: Στον κώδικα υπάρχει η επιλογή `#define CAN_LISTEN_ONLY`. Αν την ενεργοποιήσετε, το ESP32 θα λαμβάνει μόνο δεδομένα χωρίς να εκπέμπει (έτσι δεν μπορεί ποτέ να προκαλέσει σφάλμα διαύλου `BUS_OFF`). Χρήσιμο για τη δοκιμή της σωστής καλωδίωσης CAN-H/CAN-L.

---

## 📜 Άδεια Χρήσης (License)
Αυτό το project διατίθεται ελεύθερα για προσωπική χρήση.
