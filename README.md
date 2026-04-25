# DS1284 / DS1286 NVRAM Tester

This Arduino sketch provides a comprehensive hardware validation for the **Dallas DS1284 and DS1286 NVRAM/RTC chips**. Using an Arduino Uno as a parallel bus master, it verifies memory integrity, oscillator stability, and timekeeping precision, outputting all results via the Serial Monitor.

---

### **Overview**
The tester acts as a diagnostic suite to ensure your vintage or replacement RTC chips are fully functional before installation. It validates:
* **Memory Integrity:** Full read/write tests of the User RAM.
* **Data Retention:** Checks if the internal battery holds data after power-off.
* **Oscillator Stability:** Verifies the 32.768kHz crystal and internal clock.
* **Squarewave Accuracy:** Measures the 1024Hz output for jitter or frequency drift.

---

### **Testing Procedure**
To perform a complete validation, you must run the script twice:

1.  **First Run:** The script fills the User RAM with pseudo-random data, tests the RAM cells, and starts the clock.
2.  **Data Retention:** Unplug the Arduino/NVRAM and let it sit for a short while. This tests if the internal battery can maintain the RAM without Vcc.
3.  **Second Run:** Plug it back in and restart. The script compares the stored data against the expected pseudo-random pattern. Any mismatch indicates a failing battery or bad memory cells.

**Note:** The script runs automatically on boot. Simply press the **Reset** button on the Arduino to initiate a test run.

---

### **Theory of Operation**
The diagnostic sequence follows a strict **"Verify -> Stress -> Clean"** methodology:

*   **[BUS INIT]** – Configures GPIOs and sets Control Lines (CE/OE/WE) to Inactive (HIGH) immediately to prevent spurious writes during boot.
*   **[RETENTION]** – Reads NVRAM to detect a specific hash pattern.
    *   *Match:* Battery and NVRAM are healthy.
    *   *Mismatch:* Indicates power loss or a first testing of a fresh chip.
*   **[STRESS TEST]** – Performs destructive write/read cycles with 4 adversarial patterns:
    *   `0xFF` (All High) / `0x00` (All Low) – Checks for stuck-at faults.
    *   `0x55` (01010101) / `0xAA` (10101010) – Checks for adjacent-bit coupling/shorts.
*   **[CLEANUP]** – If retention passed, it zeros out the RAM. If it failed, it primes the RAM with the hash pattern for the *next* test.
*   **[OSCILLATOR]** – Kickstarts the crystal and performs atomic Freeze/Read/Unfreeze cycles to verify timekeeping is operating correctly over a 10-second test.
*   **[FREQUENCY]** – Measures the SQW pin using jitter-corrected averaging (32 samples) to validate the 1024Hz output (+/- 5%).

---

### **Hardware Configuration**

#### **Data Bus (Bi-Directional)**


| Arduino Pin | DS Pin | Function |
| :--- | :--- | :--- |
| Pin 2 | Pin 11 | DQ0 |
| Pin 3 | Pin 12 | DQ1 |
| Pin 4 | Pin 13 | DQ2 |
| Pin 5 | Pin 15 | DQ3 |
| Pin 6 | Pin 16 | DQ4 |
| Pin 7 | Pin 17 | DQ5 |
| Pin 8 | Pin 18 | DQ6 |
| Pin 9 | Pin 19 | DQ7 |

#### **Address Bus (Output)**


| Arduino Pin | DS Pin | Function |
| :--- | :--- | :--- |
| Pin A0 | Pin 10 | A0 |
| Pin A1 | Pin 9 | A1 |
| Pin A2 | Pin 8 | A2 |
| Pin A3 | Pin 7 | A3 |
| Pin A4 | Pin 6 | A4 |
| Pin A5 | Pin 5 | A5 |

#### **Control Bus (Active Low)**


| Arduino Pin | DS Pin | Function |
| :--- | :--- | :--- |
| Pin 10 | Pin 20 | /CE (Chip Enable) |
| Pin 11 | Pin 22 | /OE (Output Enable) |
| Pin 12 | Pin 27 | /WE (Write Enable) |

#### **Frequency Input**


| Arduino Pin | DS Pin | Function |
| :--- | :--- | :--- |
| Pin 13 | Pin 23 | SQW (1024Hz Input) |

#### **Power & Special Pins**
*   **GND (Pin 14, 21):** Common Ground.
*   **RCLR (Pin 24):** To erase User RAM, remove Vcc and pull this pin Low. This does *not* affect time/alarm registers.
*   **DS1284 Specific:** X1 (Pin 2) and X2 (Pin 3) require a 32.768kHz crystal; VBAT (Pin 25) requires +3V.

---

### **Sample Serial Monitor Output**

### **First Run**
```
=== DS1286/DS1284 GENERAL DIAGNOSTIC (v1.0) ===
>> Hardware Initialized.

[1] Verifying Data Retention (Checking for data from previous run)...
    RAM is Empty (All 0x00).
    If this is the first run, this is expected.
    If this is the second run, then the battery may be dead.
>> RETENTION CHECK: [CLEAN]

[2] Testing RAM Integrity...
  >> Pattern 0xFF (11111111): Writing... Verifying... [PASS]
  >> Pattern 0x00 (00000000): Writing... Verifying... [PASS]
  >> Pattern 0x55 (01010101): Writing... Verifying... [PASS]
  >> Pattern 0xAA (10101010): Writing... Verifying... [PASS]
>> OVERALL RAM STATUS: [PASS]

[3] Filling RAM with Retention Pattern...
>> RAM Pattern Written.

[4] Oscillator & Time Validation...
--- Pre-Sync State ---
Reg 0x09 (Ctrl): 0x01
Reg 0x0B (Cmd):  0x03
    Syncing Time...
    Monitoring Tick (10s)...
    RTC: 20:36:55.70 [TICK]
    RTC: 20:36:56.71 [TICK]
    RTC: 20:36:57.71 [TICK]
    RTC: 20:36:58.72 [TICK]
    RTC: 20:36:59.72 [WAIT]
    RTC: 20:37:00.72 [TICK]
    RTC: 20:37:01.72 [TICK]
    RTC: 20:37:02.72 [TICK]
    RTC: 20:37:03.72 [TICK]
    RTC: 20:37:04.72 [TICK]
>> TIME TEST: [PASS]

[5] Testing 1024Hz SQW Output...
    Integrating over 32 cycles...
    Avg Period:      968 us
    Calc Frequency:  1033.06 Hz
>> SQW TEST: [PASS] (Signal within +/- 5% spec)

>> RAM Primed. (Power cycle Dallas chip now to test retention).
>> Pull chip from socket for at least 10 seconds.
```


### **Second Run**
```
=== DS1286/DS1284 GENERAL DIAGNOSTIC (v1.0) ===
>> Hardware Initialized.

[1] Verifying Data Retention (Checking for data from previous run)...
    Found Valid Pattern (50 bytes match).
>> RETENTION CHECK: [PASS]

[2] Testing RAM Integrity...
  >> Pattern 0xFF (11111111): Writing... Verifying... [PASS]
  >> Pattern 0x00 (00000000): Writing... Verifying... [PASS]
  >> Pattern 0x55 (01010101): Writing... Verifying... [PASS]
  >> Pattern 0xAA (10101010): Writing... Verifying... [PASS]
>> OVERALL RAM STATUS: [PASS]

[3] Clearing User RAM (0x00)...
>> RAM Cleared.

[4] Oscillator & Time Validation...
--- Pre-Sync State ---
Reg 0x09 (Ctrl): 0x01
Reg 0x0B (Cmd):  0x03
    Syncing Time...
    Monitoring Tick (10s)...
    RTC: 20:36:55.40 [TICK]
    RTC: 20:36:56.41 [TICK]
    RTC: 20:36:57.41 [TICK]
    RTC: 20:36:58.42 [TICK]
    RTC: 20:36:59.42 [TICK]
    RTC: 20:37:00.42 [TICK]
    RTC: 20:37:01.42 [TICK]
    RTC: 20:37:02.42 [TICK]
    RTC: 20:37:03.42 [TICK]
    RTC: 20:37:04.42 [TICK]
>> TIME TEST: [PASS]

[5] Testing 1024Hz SQW Output...
    Integrating over 32 cycles...
    Avg Period:      969 us
    Calc Frequency:  1031.99 Hz
>> SQW TEST: [PASS] (Signal within +/- 5% spec)

>> ALL SYSTEMS GO. Chip is clean and verified.
```
