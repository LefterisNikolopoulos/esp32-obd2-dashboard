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


