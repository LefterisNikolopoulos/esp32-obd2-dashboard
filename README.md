# ESP32-C6 OBD2 Dashboard

An advanced, interactive, and high-performance **OBD2 Digital Dashboard** for vehicles, designed and tested on a **VW Tiguan 5N 1.4 TSI (Twincharger)**.

The project is built on the **Waveshare ESP32-C6-LCD-1.47** platform (172x320 resolution ST7789 IPS LCD with CST816S capacitive touch sensor), utilizing an **SN65HVD230** CAN transceiver connected directly to the ESP32-C6's built-in TWAI controller.

---

## 🛠️ Technical Features & Implementation

*   **Real-Time OBD2 Telemetry**: Reads and processes data directly from the vehicle's CAN Bus (500kbps) using standard OBD2 PIDs with an optimized polling rate.
*   **Custom Gestural User Interface (UI)**: Designed and developed a swipe gesture navigation system for the CST816S touch sensor. Includes custom slide/Venetian blinds transition animations for switching tabs and items.
*   **4-Tab Architecture**:
    1.  **MAIN**: Displays speed (km/h), engine RPM (with dynamic color coding and safety threshold indicators), throttle position (%), and turbo boost pressure (calculated in bar from the Manifold Absolute Pressure - MAP).
    2.  **TEMPS**: Coolant temperature (°C) (with automatic overheat alert warnings) and intake air temperature (°C) (IAT).
    3.  **TRIP INFO**: Calculates trip distance (km), average/maximum speed (km/h), fuel consumption in liters (L), and average fuel economy (L/100km).
    4.  **SYSTEM**: Battery voltage (V), Diagnostic Trouble Codes (DTC) reader, and performance acceleration timer.
*   **3-Tier Fuel Consumption Estimation Algorithm**: Developed a dynamic fuel consumption calculation system with a triple fallback mechanism for maximum accuracy:
    1.  *Tier 1*: Direct engine fuel rate query (PID 0x5E) from the engine ECU.
    2.  *Tier 2*: Calculation based on Mass Air Flow (MAF) in case PID 0x5E is unsupported.
    3.  *Tier 3 (Speed-Density)*: Mathematical model estimating consumption from manifold absolute pressure (MAP), RPM, and intake air temperature (IAT), custom-tuned for the displacement and efficiency of the VAG 1.4 TSI EA111 engine.
*   **Engine Startup State Machine**:
    *   Intelligent vehicle status tracking (Engine Off / Warmup / Stabilized).
    *   If the engine shuts off, the interface automatically switches to the battery tab after 10 seconds of inactivity to prevent battery drain. It automatically switches back to the speed tab once vehicle motion is detected.
*   **High-Precision Performance Timer**: Custom implementation measuring acceleration runs (0-100 km/h or 60-120 km/h) with live time and speed updates, providing a final summary (elapsed time, distance in meters, exit speed).
*   **DTC Diagnosis & Clearing (Mode 03/04)**: Reads active trouble codes (MIL status and DTCs) from the ECU, and integrates a clear fault codes function directly from the display by holding the screen for 2 seconds.
*   **Flicker-Free Analog Gauges (Custom Easing)**:
    *   Designed custom semicircular analog gauges using physics-based math (spring/critically-damped physics) for smooth pointer transitions.
    *   Developed a custom *delta-rendering* algorithm (redrawing only the changing angular sector of the arc) to maximize CPU efficiency and eliminate screen flickering.
*   **Multithreaded Real-Time Architecture (FreeRTOS)**: Task scheduling in a real-time operating system (RTOS) environment. The CAN bus reading runs in a dedicated background task (`canTask`), while UI rendering and touch detection run in the main thread, utilizing mutexes for thread-safe data access.

---

## 🔌 Pinout & Hardware Connections

### Waveshare ESP32-C6-LCD-1.47 Mappings:
*   **LCD Display (ST7789 SPI)**:
    *   `TFT_SCLK` -> GPIO 1 (LCD_CLK)
    *   `TFT_MOSI` -> GPIO 2 (LCD_DIN)
    *   `TFT_CS` -> GPIO 14 (LCD_CS)
    *   `TFT_DC` -> GPIO 15 (LCD_DC)
    *   `TFT_RST` -> GPIO 22 (LCD_RST)
    *   `TFT_BL` -> GPIO 23 (LCD_BL) (PWM brightness control)
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
