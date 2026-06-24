import re

file_path = r"c:\Users\lefte\Desktop\OBD2-Dashboard\OBD2_Dashboard_v8.ino"

with open(file_path, "r", encoding="utf-8") as f:
    content = f.read()

# Let's locate the entire function body for void checkAutoSwitch() { ... }
start_idx = content.find("void checkAutoSwitch()")
if start_idx != -1:
    brace_count = 0
    end_idx = -1
    for i in range(start_idx, len(content)):
        if content[i] == '{':
            brace_count += 1
        elif content[i] == '}':
            brace_count -= 1
            if brace_count == 0:
                end_idx = i + 1
                break
                
    if end_idx != -1:
        check_auto_switch_original = content[start_idx:end_idx]
        print("Found checkAutoSwitch original!")
        
        # New checkAutoSwitch code with RPM <= 10 default to Volts
        new_check_auto_switch = """void checkAutoSwitch() {
    static bool wasCoolant = false;
    static bool wasRPM     = false;
    static bool wasBatt    = false;
  
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
    bool isRPM     = currentRpm > 5500;
    // Battery alarm only triggers when the engine is running to avoid annoying false alerts at key-on (accessory mode is ~11V)
    bool isBatt    = (currentRpm > 400) && (currentBattery < 11.5 || currentBattery > 14.8);
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
      // Default/force volts tab when engine is off (currentRpm <= 10)
      if (currentRpm <= 10) {
        if (currentTab != TAB_SYSTEM || currentItemIndex[TAB_SYSTEM] != 0) {
          // If the user hasn't touched the screen for 10 seconds, go back to Volts
          if (millis() - lastActivityMs > 10000) {
            switchTo(TAB_SYSTEM, 0);
          }
        }
      }

      // If RPM goes above 10, the engine starts spinning/running
      if (currentRpm > 10) {
        if (currentRpm > 765 && currentSpeed == 0) {
          // Cold start: RPM is high, and vehicle is stationary. Go to Battery tab.
          startupState = STATE_WARMUP;
          switchTo(TAB_SYSTEM, 0); // Force switch to Battery tab
        } else {
          // Warm start: engine starts and idle is already <= 765, OR already driving. Go straight to Speed.
          startupState = STATE_STABILIZED;
          switchTo(TAB_MAIN, 0); // Switch to Speed tab
        }
      }
    }
    else if (startupState == STATE_WARMUP) {
      // If engine turns off (RPM <= 10), reset to STATE_ENGINE_OFF and immediately switch to Volts tab
      if (currentRpm <= 10) {
        startupState = STATE_ENGINE_OFF;
        switchTo(TAB_SYSTEM, 0);
      }
      // If RPM drops to 765 or below, OR the car starts moving (speed > 0)
      else if (currentRpm <= 765 || currentSpeed > 0) {
        startupState = STATE_STABILIZED;
        switchTo(TAB_MAIN, 0); // Switch to Speed tab
      }
      else {
        // Keep/force staying on the Battery tab during warmup
        if (currentTab != TAB_SYSTEM || currentItemIndex[TAB_SYSTEM] != 0) {
          switchTo(TAB_SYSTEM, 0);
        }
      }
    }
    else if (startupState == STATE_STABILIZED) {
      // If engine turns off (RPM <= 10), reset to STATE_ENGINE_OFF and immediately switch to Volts tab
      if (currentRpm <= 10) {
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
}"""
        content = content.replace(check_auto_switch_original, new_check_auto_switch)
        
        with open(file_path, "w", encoding="utf-8") as f:
            f.write(content)
        print("Success! Programmatically replaced checkAutoSwitch!")
    else:
        print("Failed to find end brace of checkAutoSwitch!")
else:
    print("Failed to find checkAutoSwitch!")
