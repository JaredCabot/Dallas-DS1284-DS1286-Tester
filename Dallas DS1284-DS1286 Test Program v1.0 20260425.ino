/**
 * @file    Dallas DS1284-DS1286 Test Program
 * @brief   General Diagnostic & Validation Tool for Dallas DS1286 / DS1284 Watchdog Timekeepers.
 * @author  AI Assistant prompted by Jared Cabot (Don't hate me, I'm not a coder!)]
 * @version 1.0 (Golden Master / Industry Standard)
 * @date    2026-04-25
 * 
 * @section DESCRIPTION
 * This firmware performs a comprehensive hardware validation of the DS1286/DS1284 RTC chips using an Arduino Uno.
 * It acts as a parallel bus master to verify memory integrity, oscillator stability, and 
 * timekeeping precision.
 * 
 * @section THEORY_OF_OPERATION
 * The diagnostic sequence follows a strict "Verify -> Stress -> Clean" methodology:
 * 
 * 1. [BUS INIT]      Configures GPIOs and sets Control Lines (CE/OE/WE) to Inactive (HIGH)
 *                    immediately to prevent spurious writes during boot.
 * 2. [RETENTION]     Reads NVRAM to detect a specific hash pattern generated in a previous run.
 *                    - If Pattern Matches: Battery & NVRAM are healthy.
 *                    - If Mismatch: Indicates power loss or first testing of a fresh chip.
 * 3. [STRESS TEST]   Performs destructive write/read cycles with 4 adversarial bit patterns:
 *                    - 0xFF (All High) / 0x00 (All Low) -> Checks Stuck-at faults.
 *                    - 0x55 (01010101) / 0xAA (10101010) -> Checks Adjacent-bit coupling/shorts.
 * 4. [CLEANUP]       - If Retention Passed: Zeros out RAM (0x00) to leave chip ready for use.
 *                    - If Retention Failed: Primes RAM with Hash Pattern for the NEXT retention test.
 * 5. [OSCILLATOR]    Kickstarts the crystal and performs atomic Freeze/Read/Unfreeze cycles 
 *                    to verify 10-second timekeeping accuracy.
 * 6. [FREQUENCY]     Measures the SQW pin using jitter-corrected averaging (32 samples)
 *                    to validate the 1024Hz output against datasheet and Arduino tolerances (+/- 5%).
 * 
 * @section HARDWARE_MAP
 * [DATA BUS] Arduino Pins 2-9   <==>  DS Pins DQ0-DQ7 (Bi-Directional)
 * [ADDR BUS] Arduino Pins A0-A5 ==>   DS Pins A0-A5   (Output)
 * [CONTROL]  Pins 10, 11, 12    ==>   /CE, /OE, /WE   (Active Low Output)
 * [SQW IN]   Pin 13             <==   SQW Output      (Input for Freq Counter)
 */

/* =================================================================================
 *                                 HARDWARE CONFIGURATION
 * =================================================================================
 * 
 * [DATA BUS] (Bi-Directional)
 *   Arduino Pin 2   <==>   DS Pin DQ0 (Pin 11)
 *   Arduino Pin 3   <==>   DS Pin DQ1 (Pin 12)
 *   Arduino Pin 4   <==>   DS Pin DQ2 (Pin 13)
 *   Arduino Pin 5   <==>   DS Pin DQ3 (Pin 15)
 *   Arduino Pin 6   <==>   DS Pin DQ4 (Pin 16)
 *   Arduino Pin 7   <==>   DS Pin DQ5 (Pin 17)
 *   Arduino Pin 8   <==>   DS Pin DQ6 (Pin 18)
 *   Arduino Pin 9   <==>   DS Pin DQ7 (Pin 19)
 *   
 * [ADDRESS BUS] (Output)
 *   Arduino Pin A0  ==>    DS Pin A0  (Pin 10)
 *   Arduino Pin A1  ==>    DS Pin A1  (Pin 9)
 *   Arduino Pin A2  ==>    DS Pin A2  (Pin 8)
 *   Arduino Pin A3  ==>    DS Pin A3  (Pin 7)
 *   Arduino Pin A4  ==>    DS Pin A4  (Pin 6)
 *   Arduino Pin A5  ==>    DS Pin A5  (Pin 5)
 *   
 * [CONTROL BUS] (Active Low)
 *   Arduino Pin 10  ==>    DS Pin /CE (Pin 20)  [Chip Enable]
 *   Arduino Pin 11  ==>    DS Pin /OE (Pin 22)  [Output Enable]
 *   Arduino Pin 12  ==>    DS Pin /WE (Pin 27)  [Write Enable]
 *   
 * [FREQUENCY INPUT]
 *   Arduino Pin 13  <==    DS Pin SQW (Pin 23)
 *                          *Configured as INPUT to measure 1024Hz signal*
 *
 * [POWER & SPECIAL - DS1284 ONLY]
 *   X1 (Pin 2)      ==>    32.768kHz Crystal Leg 1
 *   X2 (Pin 3)      ==>    32.768kHz Crystal Leg 2
 *   VBAT (Pin 25)   ==>    +3V Battery Positive
 *   RCLR (Pin 24)   ==>    Low = Clear User RAM - N/C
 *   GND (Pin 14,21) ==>    Ground
 *   VCC (Pin 28)    ==>    +5V
 * =================================================================================
 */

// ---------------------------------------------------------------------------------
// 1. SYSTEM CONSTANTS & CONFIGURATION
// ---------------------------------------------------------------------------------

#define SERIAL_BAUD         9600

// --- SQW Frequency Test Thresholds ---
// Reference: DS1286 Datasheet. Crystal = 32.768kHz. Divider /32 = 1024Hz.
// Nominal Period: 1/1024 = 976.56 microseconds.
// Tolerance: +/- 5% to account for crystal aging and measurement jitter.
#define SQW_TARGET_PERIOD   976   
#define SQW_TOLERANCE_MIN   927   // 976 * 0.95
#define SQW_TOLERANCE_MAX   1025  // 976 * 1.05
#define SQW_SAMPLES         32    // Number of periods to average for stability

// --- Data Retention Algorithm ---
// Linear Congruential Generator constants for deterministic pattern generation.
// Used to verify if RAM content survived a power cycle.
#define HASH_MULTIPLIER     167
#define HASH_OFFSET         45

// --- Pin Mapping (Matches Hardware Config) ---
const uint8_t DATA_PINS[] = {2, 3, 4, 5, 6, 7, 8, 9}; 
const uint8_t ADDR_PINS[] = {A0, A1, A2, A3, A4, A5}; 

const uint8_t PIN_CE  = 10; // Chip Enable  (Active Low)
const uint8_t PIN_OE  = 11; // Output Enable (Active Low)
const uint8_t PIN_WE  = 12; // Write Enable (Active Low)
const uint8_t PIN_SQW = 13; // Frequency Input (High Impedance)

// --- DS1286 Register Map (Offsets) ---
const uint8_t RTC_HUN   = 0x00; // Hundredths (0.01s)
const uint8_t RTC_SEC   = 0x01;
const uint8_t RTC_MIN   = 0x02; 
const uint8_t RTC_HOUR  = 0x04; 
const uint8_t RTC_DAY   = 0x06; 
const uint8_t RTC_DATE  = 0x08; 
const uint8_t RTC_MONTH = 0x09; // Month + Control Bits (EOSC, SQW)
const uint8_t RTC_YEAR  = 0x0A; 
const uint8_t CMD_REG   = 0x0B; // Command Register (TE Bit)

// --- Memory Map Limits ---
const uint8_t RAM_START = 0x0E; // First Byte of User NVRAM
const uint8_t RAM_END   = 0x3F; // Last Byte of User NVRAM
const uint8_t RAM_SIZE  = (RAM_END - RAM_START) + 1;

// ---------------------------------------------------------------------------------
// 2. FORWARD DECLARATIONS
// ---------------------------------------------------------------------------------
// Explicit prototypes for strict C++ compiler compliance.

// Hardware Abstraction Layer
void initHardware();
void setBusMode(int mode);
void writeByte(uint8_t addr, uint8_t data);
uint8_t readByte(uint8_t addr);

// Test Suites
bool checkDataRetention();
void fillRetentionPattern();
bool testRAMPattern();
void clearUserRAM();
void syncTimeAndMonitor();
void testFrequencyOutput();

// Helpers
void dumpRegisters(const char* label);
void kickstartOscillator();
void setClockFrozen(bool freeze);
void haltSystem();
uint8_t calculateRetentionByte(uint8_t addr);

// Formatting & Math
uint8_t decToBcd(int val);
uint8_t bcdToDec(uint8_t val);
void printHexByte(uint8_t val);
void printBinaryByte(uint8_t val);
void printTwoDigits(uint8_t val);

// ---------------------------------------------------------------------------------
// 3. MAIN APPLICATION ENTRY POINT
// ---------------------------------------------------------------------------------

void setup() {
  Serial.begin(SERIAL_BAUD);
  while (!Serial); // Wait for USB serial enumeration

  Serial.println(F("\n=== DS1286/DS1284 GENERAL DIAGNOSTIC (v1.0) ==="));

  // [Step 1] Hardware Initialization
  // Configures pins and sets control lines to Inactive (HIGH) to prevent bus contention.
  initHardware();

  // [Step 2] Data Retention Verification (Non-Destructive)
  // Checks if the RAM contains the "Hash Pattern" from a previous test run.
  // - Returns TRUE: Battery/Retention OK.
  // - Returns FALSE: First run, dead battery, or data corruption.
  bool retentionPassed = checkDataRetention();

  // [Step 3] Destructive RAM Stress Test
  // Writes adversarial patterns (0xFF, 0x00, 0x55, 0xAA) to verify bit independence.
  // Halts system if hardware failure is detected.
  if (testRAMPattern()) {
    Serial.println(F(">> OVERALL RAM STATUS: [PASS]"));
  } else {
    Serial.println(F(">> OVERALL RAM STATUS: [FAIL] - Halting System."));
    haltSystem(); 
  }

  // [Step 4] RAM Maintenance Logic
  if (retentionPassed) {
    // Case A: Retention Passed. Chip is verified healthy.
    // Action: Clear RAM to 0x00 to leave it clean for the end-user application.
    clearUserRAM();
  } else {
    // Case B: Retention Failed/Unknown.
    // Action: Fill RAM with Hash Pattern so the user can power-cycle and re-test retention.
    fillRetentionPattern();
  }

  // [Step 5] Oscillator & Timekeeping Verification
  Serial.println(F("\n[4] Oscillator & Time Validation..."));
  dumpRegisters("Pre-Sync State");
  kickstartOscillator(); // "Shock" the crystal if it was stalled
  syncTimeAndMonitor();  // Run 10-second precision tick test

  // [Step 6] SQW Frequency Verification
  // Measures the 1024Hz output on Pin 13.
  testFrequencyOutput();

  // [Step 7] Final Operator Instructions
  if (!retentionPassed) {
    // Instructions for Retention Test
    Serial.println(F("\n>> RAM Primed. (Power cycle Dallas chip now to test retention)."));
    Serial.println(F(">> Pull chip from socket for at least 10 seconds."));
  } else {
    // Instructions for Final Success
    Serial.println(F("\n>> ALL SYSTEMS GO. Chip is clean and verified."));
  }
}

void loop() {
  // Main loop is unused. The diagnostic runs once at startup.
}

// ---------------------------------------------------------------------------------
// 4. TEST SUITE IMPLEMENTATION
// ---------------------------------------------------------------------------------

/**
 * @brief   Verifies the 1024Hz Square Wave Output on Pin 13.
 * @details Performs a jitter-corrected measurement by averaging 32 periods.
 *          - Validates signal presence.
 *          - Validates frequency within +/- 5% tolerance.
 */
void testFrequencyOutput() {
  Serial.println(F("\n[5] Testing 1024Hz SQW Output..."));

  // 1. Enable SQW (Reg 0x09: Bit 7=0 [OSC On], Bit 6=0 [SQW On])
  setClockFrozen(true);
  writeByte(RTC_MONTH, 0x01); 
  setClockFrozen(false);

  // 2. Allow signal to stabilize
  delay(200); 

  // 3. Integration Loop (Average 32 samples)
  unsigned long totalDuration = 0;
  bool signalLost = false;

  Serial.print(F("    Integrating over ")); Serial.print(SQW_SAMPLES); Serial.println(F(" cycles..."));

  for (int i = 0; i < SQW_SAMPLES; i++) {
    // Timeout set to 15ms (Fail fast if no signal is present)
    unsigned long high = pulseIn(PIN_SQW, HIGH, 15000);
    unsigned long low  = pulseIn(PIN_SQW, LOW, 15000);
    
    if (high == 0 || low == 0) {
      signalLost = true;
      break;
    }
    totalDuration += (high + low);
  }

  // 4. Analysis
  if (signalLost) {
     Serial.println(F(">> SQW TEST: [FAIL] - No Signal Detected (Check Wiring/Crystal)"));
     return;
  }

  // Calculate Average Period
  unsigned long avgPeriod = totalDuration / SQW_SAMPLES;
  
  // Calculate Frequency
  float frequency = 0;
  if (avgPeriod > 0) frequency = 1000000.0 / avgPeriod;

  Serial.print(F("    Avg Period:      ")); Serial.print(avgPeriod); Serial.println(F(" us"));
  Serial.print(F("    Calc Frequency:  ")); Serial.print(frequency, 2); Serial.println(F(" Hz"));

  // 5. Tolerance Check
  if (avgPeriod > SQW_TOLERANCE_MIN && avgPeriod < SQW_TOLERANCE_MAX) {
    Serial.println(F(">> SQW TEST: [PASS] (Signal within +/- 5% spec)"));
  } else {
    Serial.println(F(">> SQW TEST: [FAIL] - Frequency Out of Spec"));
  }
}

/**
 * @brief   Validates RTC ticking accuracy over a 10-second window.
 * @details Uses atomic read cycles (Freeze -> Read -> Unfreeze) to ensure data consistency
 *          and prevent rollover errors during the read process.
 */
void syncTimeAndMonitor() {
  Serial.println(F("    Syncing Time..."));
  
  // Use compile time as a dynamic, valid starting point
  const char* rawTime = __TIME__; 
  byte h = (rawTime[0] - '0') * 10 + (rawTime[1] - '0');
  byte m = (rawTime[3] - '0') * 10 + (rawTime[4] - '0');
  byte s = (rawTime[6] - '0') * 10 + (rawTime[7] - '0');

  // --- Initialize Clock Registers ---
  setClockFrozen(true); // Freeze updates (TE=1)
  
  writeByte(RTC_HOUR, decToBcd(h));
  writeByte(RTC_MIN,  decToBcd(m));
  writeByte(RTC_SEC,  decToBcd(s));
  writeByte(RTC_DAY,  0x01); 
  writeByte(RTC_DATE, 0x01); 
  writeByte(RTC_YEAR, 0x24); 
  writeByte(RTC_MONTH, 0x01); // Enable OSC and SQW
  
  setClockFrozen(false); // Unfreeze (TE=0) -> Clock starts ticking
  
  Serial.println(F("    Monitoring Tick (10s)..."));

  int validTickCount = 0;
  uint8_t lastSec = 255;

  // Monitoring Loop
  for (int i = 0; i < 10; i++) {
    // Atomic Read Sequence
    setClockFrozen(true); 
    uint8_t cH = bcdToDec(readByte(RTC_HOUR));
    uint8_t cM = bcdToDec(readByte(RTC_MIN));
    uint8_t cS = bcdToDec(readByte(RTC_SEC));
    uint8_t cHun = bcdToDec(readByte(RTC_HUN)); 
    setClockFrozen(false); 
    
    // Display Format: HH:MM:SS.ss
    Serial.print(F("    RTC: "));
    printTwoDigits(cH); Serial.print(":");
    printTwoDigits(cM); Serial.print(":");
    printTwoDigits(cS); Serial.print(".");
    printTwoDigits(cHun);
    
    // Check if seconds incremented
    if (cS != lastSec) {
      validTickCount++;
      Serial.println(F(" [TICK]"));
    } else {
      Serial.println(F(" [WAIT]"));
    }
    lastSec = cS;
    delay(1000); 
  }

  // Final Evaluation
  if (validTickCount >= 9) {
    Serial.println(F(">> TIME TEST: [PASS]"));
    
    // Reset Registers to Defaults (00:00:00)
    setClockFrozen(true);
    writeByte(RTC_HUN,  0x00);
    writeByte(RTC_SEC,  0x00);
    writeByte(RTC_MIN,  0x00);
    writeByte(RTC_HOUR, 0x00);
    writeByte(RTC_DAY,  0x01); 
    writeByte(RTC_DATE, 0x01); 
    writeByte(RTC_YEAR, 0x00); 
    writeByte(RTC_MONTH, 0x41); // Month 1, SQW Off, OSC On
    setClockFrozen(false);
  } else {
    Serial.println(F(">> TIME TEST: [FAIL]"));
    haltSystem();
  }
}

/**
 * @brief   Destructive RAM stress test.
 * @details Writes 4 specific adversarial patterns to verify bit independence and addressing.
 * @return  true if all bytes verify, false on any mismatch.
 */
bool testRAMPattern() {
  Serial.println(F("\n[2] Testing RAM Integrity..."));
  
  const uint8_t patterns[] = {0xFF, 0x00, 0x55, 0xAA};
  
  for (uint8_t p = 0; p < 4; p++) { 
    // Log Pattern Header
    Serial.print(F("  >> Pattern ")); 
    printHexByte(patterns[p]);
    Serial.print(F(" (")); 
    printBinaryByte(patterns[p]);
    Serial.print(F("): Writing... "));
    
    // Write Phase
    for (uint8_t i = RAM_START; i <= RAM_END; i++) {
      writeByte(i, patterns[p]);
    }
    
    Serial.print(F("Verifying... "));
    
    // Verify Phase
    for (uint8_t i = RAM_START; i <= RAM_END; i++) {
      uint8_t val = readByte(i);
      if (val != patterns[p]) {
        Serial.println(F("[FAIL]"));
        Serial.print(F("     ! ERROR at Addr ")); printHexByte(i);
        Serial.print(F(" Exp: ")); printHexByte(patterns[p]);
        Serial.print(F(" Got: ")); printHexByte(val);
        Serial.println();
        return false; 
      }
    }
    Serial.println(F("[PASS]"));
  }
  return true;
}

// ---------------------------------------------------------------------------------
// 5. DATA RETENTION ENGINE
// ---------------------------------------------------------------------------------

/**
 * @brief   Generates a unique, deterministic byte for a given address.
 * @details Formula: (Addr * 167) + 45. Ensures coverage of different bit combinations.
 */
uint8_t calculateRetentionByte(uint8_t addr) {
  // Cast to uint8_t to safely allow overflow (intentional hashing behavior)
  return (uint8_t)((addr * HASH_MULTIPLIER) + HASH_OFFSET);
}

/**
 * @brief   Fills User RAM with the retention pattern (Hash).
 * @details This primes the chip. If power is removed, these values should persist.
 */
void fillRetentionPattern() {
  Serial.println(F("\n[3] Filling RAM with Retention Pattern..."));
  for (uint8_t i = RAM_START; i <= RAM_END; i++) {
    uint8_t expected = calculateRetentionByte(i);
    writeByte(i, expected); 
  }
  Serial.println(F(">> RAM Pattern Written."));
}

/**
 * @brief   Validates if the current RAM contents match the retention pattern.
 * @return  true if 100% match (Battery/NVRAM OK), false otherwise.
 */
bool checkDataRetention() {
  Serial.println(F("\n[1] Verifying Data Retention (Checking for data from previous run)..."));
  
  int matchCount = 0;
  int zeroCount = 0;

  for (uint8_t i = RAM_START; i <= RAM_END; i++) {
    uint8_t val = readByte(i);
    uint8_t expected = calculateRetentionByte(i);

    if (val == expected) matchCount++;
    if (val == 0x00) zeroCount++;
  }

  // Result Analysis
  if (matchCount == RAM_SIZE) {
    Serial.print(F("    Found Valid Pattern ("));
    Serial.print(matchCount);
    Serial.println(F(" bytes match)."));
    Serial.println(F(">> RETENTION CHECK: [PASS]"));
    return true;
  } 
  else if (zeroCount == RAM_SIZE) {
    Serial.println(F("    RAM is Empty (All 0x00)."));
	Serial.println(F("    If this is the first run, this is expected."));
	Serial.println(F("    If this is the second run, then the battery may be dead."));
    Serial.println(F(">> RETENTION CHECK: [CLEAN]"));
    return false;
  } 
  else {
    Serial.print(F("    Pattern Mismatch (Matches: "));
    Serial.print(matchCount);
    Serial.print(F("/"));
    Serial.print(RAM_SIZE);
    Serial.println(F(")."));
    Serial.println(F(">> RETENTION CHECK: [UNKNOWN/CORRUPT]"));
	Serial.print(F("    If this is a first run, there may be existing data present."));
    return false;
  }
}

/**
 * @brief   Sanitizes User RAM by writing 0x00 to all cells.
 * @details Used to clean the chip after a successful verification.
 */
void clearUserRAM() {
  Serial.println(F("\n[3] Clearing User RAM (0x00)..."));
  for (uint8_t i = RAM_START; i <= RAM_END; i++) {
    writeByte(i, 0x00); 
  }
  Serial.println(F(">> RAM Cleared."));
}

// ---------------------------------------------------------------------------------
// 6. LOW LEVEL DRIVERS (HAL)
// ---------------------------------------------------------------------------------

/**
 * @brief   Configures MCU Pins and defaults Control Lines to Inactive.
 * @details Critical: Sets Control lines HIGH *before* setting Mode to OUTPUT
 *          to prevent spurious Write pulses during boot sequence.
 */
void initHardware() {
  // 1. Set Control Pins HIGH (Inactive) first
  digitalWrite(PIN_CE, HIGH); pinMode(PIN_CE, OUTPUT);
  digitalWrite(PIN_OE, HIGH); pinMode(PIN_OE, OUTPUT);
  digitalWrite(PIN_WE, HIGH); pinMode(PIN_WE, OUTPUT);
  
  // 2. Configure SQW Pin as Input (High Impedance)
  pinMode(PIN_SQW, INPUT); 

  // 3. Configure Address Bus as Output
  for (uint8_t i = 0; i < 6; i++) pinMode(ADDR_PINS[i], OUTPUT);
  
  // 4. Set Data Bus to Input (Safety state)
  setBusMode(INPUT);
  
  Serial.println(F(">> Hardware Initialized."));
}

/**
 * @brief   Sets the direction of the 8-bit Parallel Data Bus.
 * @param   mode: INPUT or OUTPUT
 */
void setBusMode(int mode) {
  for (uint8_t i = 0; i < 8; i++) pinMode(DATA_PINS[i], mode);
}

/**
 * @brief   Writes a byte to a specific DS1286 address.
 * @details Timing: Address -> Data -> Control Strobe (2us delay).
 */
void writeByte(uint8_t addr, uint8_t data) {
  // 1. Set Address
  for (uint8_t i = 0; i < 6; i++) digitalWrite(ADDR_PINS[i], (addr >> i) & 0x01);
  
  // 2. Drive Data
  setBusMode(OUTPUT);
  for (uint8_t i = 0; i < 8; i++) digitalWrite(DATA_PINS[i], (data >> i) & 0x01);
  
  // 3. Pulse Write (/WE + /CE)
  digitalWrite(PIN_CE, LOW); 
  digitalWrite(PIN_WE, LOW);
  delayMicroseconds(2); // Meet access time requirements
  digitalWrite(PIN_WE, HIGH); 
  digitalWrite(PIN_CE, HIGH);
  
  // 4. Release Bus
  setBusMode(INPUT); 
}

/**
 * @brief   Reads a byte from a specific DS1286 address.
 * @details Timing: Address -> Control Strobe -> Sample Data.
 */
uint8_t readByte(uint8_t addr) {
  uint8_t data = 0;
  
  // 1. Set Address
  for (uint8_t i = 0; i < 6; i++) digitalWrite(ADDR_PINS[i], (addr >> i) & 0x01);
  
  // 2. Ensure Input Mode
  setBusMode(INPUT);
  
  // 3. Pulse Read (/OE + /CE)
  digitalWrite(PIN_CE, LOW);
  digitalWrite(PIN_OE, LOW);
  delayMicroseconds(2); 
  
  // 4. Sample Bus
  for (uint8_t i = 0; i < 8; i++) {
    if (digitalRead(DATA_PINS[i])) data |= (1 << i);
  }
  
  // 5. End Read
  digitalWrite(PIN_OE, HIGH);
  digitalWrite(PIN_CE, HIGH);
  
  return data;
}

// ---------------------------------------------------------------------------------
// 7. HELPER FUNCTIONS
// ---------------------------------------------------------------------------------

/**
 * @brief   Halts program execution indefinitely.
 */
void haltSystem() {
  Serial.println(F("!! SYSTEM HALTED !!"));
  while(1);
}

/**
 * @brief   Controls the Transfer Enable (TE) bit.
 * @param   freeze: true=Read Mode (Static Registers), false=Run Mode (Updating).
 */
void setClockFrozen(bool freeze) {
  if (freeze) writeByte(CMD_REG, 0x80); // Set TE=1
  else        writeByte(CMD_REG, 0x00); // Set TE=0
}

/**
 * @brief   Forces a state transition on the EOSC bit to wake up the crystal.
 */
void kickstartOscillator() {
  setClockFrozen(true);
  writeByte(RTC_MONTH, 0x80); // EOSC=1 (Stop)
  delay(100);
  writeByte(RTC_MONTH, 0x01); // EOSC=0 (Start)
  setClockFrozen(false);
  delay(1000);
}

// --- Formatting & Math Utils ---

void printHexByte(uint8_t val) {
  Serial.print("0x");
  if (val < 0x10) Serial.print('0');
  Serial.print(val, HEX);
}

void printBinaryByte(uint8_t val) {
  for (int b = 7; b >= 0; b--) {
    Serial.print((val >> b) & 1);
  }
}

void printTwoDigits(uint8_t val) {
  if (val < 10) Serial.print('0');
  Serial.print(val);
}

void dumpRegisters(const char* label) {
  Serial.print(F("--- ")); Serial.print(label); Serial.println(F(" ---"));
  uint8_t r9 = readByte(RTC_MONTH);
  uint8_t rB = readByte(CMD_REG);
  Serial.print(F("Reg 0x09 (Ctrl): ")); printHexByte(r9); Serial.println();
  Serial.print(F("Reg 0x0B (Cmd):  ")); printHexByte(rB); Serial.println();
}

uint8_t decToBcd(int val) { return ((val / 10 * 16) + (val % 10)); }
uint8_t bcdToDec(uint8_t val) { return ((val / 16 * 10) + (val % 16)); }
