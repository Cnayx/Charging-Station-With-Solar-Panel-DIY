/**
 * Solar Charging Station — production firmware (Arduino Uno / ATmega328P)
 *
 * Architectural contract
 * ----------------------
 * loop() is a cooperative scheduler. It never calls delay(). Every periodic
 * task is gated by unsigned millis() deltas so the CPU stays available for
 * I2C, GPIO, and future ISRs. Splash, ADC, LED, LCD, and Serial are independent
 * cadences, not a single blocking "read → print → sleep 1 s" pipeline.
 *
 * Why integer millivolts
 * ----------------------
 * avr-gcc has no hardware FPU. Every float is a multi-hundred-cycle library
 * call and occupies 4 B of SRAM. This firmware keeps the entire measurement
 * path in uint16_t / uint32_t millivolts. Display formatting is integer too,
 * so we never pull in soft-float or printf-float.
 *
 * Why F() on every literal
 * ------------------------
 * File-scope and automatic string literals land in .data and are copied into
 * the 2048 B SRAM at reset. F() / __FlashStringHelper keeps them in flash and
 * streams one byte at a time. On a 16x2 + Serial UI that is tens of bytes
 * recovered for stack and the I2C library.
 *
 * Hardware (see docs/HARDWARE_WIRING.md)
 * --------------------------------------
 *   5 W / 6 V panel -> TP4056 (DW01A) -> dual 18650 (parallel)
 *   TP4056 OUT+ -> MAIN SPST switch -> Branch A: USB-A boost (phone)
 *                                   -> Branch B: MT3608 -> Arduino 5V pin
 *   Divider AFTER switch: Switched+ -- 10k -- A0 -- 10k -- GND
 *   Pack - ------------------------- Arduino GND  (common ref mandatory)
 *   LED green D3, LED yellow D4, LED red D5
 *   LCD I2C   SDA=A4, SCL=A5, address 0x27
 *
 * MT3608 feeds a regulated 5.0 V to the 5V pin so AVCC tracks the ADC scale.
 * When the main switch is OFF, the MCU is unpowered and A0 is high-Z — by design.
 *
 * Field calibration (do not "correct" to a textbook Li-ion curve)
 * ---------------------------------------------------------------
 *   3.72 V → 100 %     3.20 V → 0 %
 *   ≥ 3.60 V green     3.35–3.59 V yellow     < 3.35 V red
 */

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <stdint.h>

// =============================================================================
// Pin map — constexpr uint8_t so the compiler can emit immediate operands
// (ldi / cbi / sbi) instead of loading 16-bit const int from SRAM.
// =============================================================================

constexpr uint8_t kPinBattery = A0;
constexpr uint8_t kLedGreen   = 3;
constexpr uint8_t kLedYellow  = 4;
constexpr uint8_t kLedRed     = 5;

// PCF8574 backpacks ship as 0x27 or 0x3F. Change only this constant if the
// splash screen never appears and the backlight is already on.
constexpr uint8_t kLcdAddress = 0x27;
constexpr uint8_t kLcdColumns = 16;
constexpr uint8_t kLcdRows    = 2;

// =============================================================================
// Scheduler periods. ADC is fast and cheap (~104 µs conversion). UI is slow
// because HD44780-over-I2C is a multi-millisecond transaction; refreshing it
// every sample would starve the bus and flicker the glyphs.
// =============================================================================

constexpr uint32_t kAdcPeriodMs    = 10;    // 100 Hz sample clock
constexpr uint32_t kLedPeriodMs    = 50;    // 20 Hz state-machine tick
constexpr uint32_t kLcdPeriodMs    = 500;   // human-readable, low I2C load
constexpr uint32_t kSerialPeriodMs = 1000;  // 1 Hz debug log
constexpr uint32_t kSplashDurationMs = 1500;

// =============================================================================
// Electrical model — ATmega328P datasheet § ADC Conversion Result:
//   ADC = (Vin * 1024) / Vref
// therefore
//   Vin = (ADC * Vref) / 1024
//
// Use 1024 bins, not 1023. Code 0x3FF means Vref − 1 LSB, not Vref itself.
// Because 1024 is a power of two, the compiler can optimize the divide.
//
// Divider: equal 10k / 10k ⇒ V(A0) = Vbatt / 2 (ideal resistors).
//   Vbatt_mV = Vin_mV * 2
//            = (adc * 5000 * 2) / 1024
//            = (adc * 10000) / 1024
//
// Overflow audit (must stay inside uint32_t):
//   adc_max = 1023
//   1023 * 10000 = 10 230 000  <  2^32 − 1
// At a healthy 4.20 V cell, A0 sees 2.10 V → code ≈ 430, huge headroom.
// =============================================================================

constexpr uint32_t kVrefMv         = 5000;   // DEFAULT = AVCC. Assumes a true 5.0 V rail.
constexpr uint32_t kAdcFullScale   = 1024;   // datasheet bins, NOT 1023
constexpr uint32_t kDividerNum     = 2;      // (R1 + R2) / R2  with R1 = R2 = 10k
constexpr uint32_t kAdcToBatteryMv = kVrefMv * kDividerNum; // 10000

static_assert(kAdcToBatteryMv == 10000UL, "Divider scale drifted from 10k/10k @ 5.0 V");
static_assert((1023UL * kAdcToBatteryMv) < 0xFFFFFFFFUL, "ADC*scale must fit uint32_t");
static_assert((kAdcFullScale & (kAdcFullScale - 1U)) == 0U,
              "Scale must stay a power of two so the compiler can shift");

// Field calibration, stored as millivolts to keep comparisons integer.
constexpr uint16_t kVoltageMinMv      = 3200;  // 0 %
constexpr uint16_t kVoltageMaxMv      = 3720;  // 100 %
constexpr uint16_t kThresholdGreenMv  = 3600;
constexpr uint16_t kThresholdYellowMv = 3350;

// 30 mV hysteresis (~6 LSB after the divider) kills LED chatter when the cell
// sits on a threshold and the USB-A boost load PWM-pulses the rail.
constexpr uint16_t kHysteresisMv = 30;

static_assert(kVoltageMaxMv > kVoltageMinMv, "Calibration window inverted");
static_assert(kThresholdGreenMv > kThresholdYellowMv, "LED bands inverted");

// EMA coefficient α = 1/8 = 0.125. One uint16_t of state replaces a blocking
// multi-sample average. The shift (>> 3) avoids floating-point entirely.
constexpr uint8_t kEmaShift = 3;  // divide by 8

// =============================================================================
// Types
// =============================================================================

enum class AppPhase : uint8_t { kSplash, kRunning };

enum class LedBand : uint8_t { kRed, kYellow, kGreen };

// =============================================================================
// Persistent state — all file-scope, no heap, no Arduino String.
// String / malloc on AVR is the classic SRAM-fragmentation time bomb.
// =============================================================================

LiquidCrystal_I2C lcd(kLcdAddress, kLcdColumns, kLcdRows);

static uint16_t  gFilteredVoltageMv = 0;
static bool      gFilterPrimed      = false;
static LedBand   gLedBand           = LedBand::kRed;
static AppPhase  gPhase             = AppPhase::kSplash;

static uint32_t  gT0Adc    = 0;
static uint32_t  gT0Led    = 0;
static uint32_t  gT0Lcd    = 0;
static uint32_t  gT0Serial = 0;
static uint32_t  gT0Splash = 0;

// Last values pushed to the glass. Skipping identical frames avoids needless
// I2C traffic (each glyph is a START + address + data + STOP).
static uint16_t  gLcdLastMv  = 0xFFFF;
static uint8_t   gLcdLastPct = 0xFF;

// =============================================================================
// Time helper — unsigned subtraction is wrap-safe at the 49.7-day millis()
// overflow. Signed "now > last + period" is the textbook wraparound bug.
// last = now (not last += period) so a late I2C frame cannot schedule a
// catch-up burst of ADC/LCD work.
// =============================================================================

static bool periodElapsed(const uint32_t now,
                          uint32_t& last,
                          const uint32_t periodMs) {
  if (static_cast<uint32_t>(now - last) >= periodMs) {
    last = now;
    return true;
  }
  return false;
}

// =============================================================================
// ADC → battery millivolts (single conversion, no delay)
// =============================================================================

static uint16_t adcToBatteryMillivolts(const uint16_t adc) {
  // (adc * 10000) / 1024 — see electrical model above.
  const uint32_t mv = (static_cast<uint32_t>(adc) * kAdcToBatteryMv) / kAdcFullScale;
  if (mv > 0xFFFFUL) {
    return 0xFFFF;  // defensive: electrically unreachable with 10k/10k
  }
  return static_cast<uint16_t>(mv);
}

static void sampleAdc() {
  // analogRead() blocks only for the hardware conversion (~104 µs). That is
  // not a scheduler delay(); replacing it with a free-running ISR ADC would
  // buy nothing at a 10 ms period and would complicate I2C reentrancy.
  const uint16_t raw = static_cast<uint16_t>(analogRead(kPinBattery));
  const uint16_t sampleMv = adcToBatteryMillivolts(raw);

  if (!gFilterPrimed) {
    // Seed with the first real sample so the splash→run transition does not
    // ramp from 0 V and flash the red LED / 0 % for a second.
    gFilteredVoltageMv = sampleMv;
    gFilterPrimed = true;
    return;
  }

  // Integer EMA: y += (x − y) / 8
  // Written as (y*7 + x)/8 so the rounding stays non-negative and we never
  // need a signed intermediate. Equivalent to α = 0.125.
  const uint32_t acc =
      (static_cast<uint32_t>(gFilteredVoltageMv) * 7UL) +
      static_cast<uint32_t>(sampleMv);
  gFilteredVoltageMv = static_cast<uint16_t>(acc >> kEmaShift);
}

// =============================================================================
// Derived UI quantities
// =============================================================================

static uint8_t calculatePercentage(const uint16_t mv) {
  if (mv >= kVoltageMaxMv) {
    return 100;
  }
  if (mv <= kVoltageMinMv) {
    return 0;
  }

  // Rounded integer map: (delta * 100 + span/2) / span
  // Max numerator: 519 * 100 + 260 = 52 160 — fits uint32_t with huge margin.
  const uint32_t span = static_cast<uint32_t>(kVoltageMaxMv - kVoltageMinMv);
  const uint32_t delta = static_cast<uint32_t>(mv - kVoltageMinMv);
  return static_cast<uint8_t>((delta * 100UL + (span / 2UL)) / span);
}

static void applyLedOutputs(const LedBand band) {
  // Drive all three explicitly so a missed transition can never leave two
  // LEDs lit. Called on band change and once when leaving the splash phase
  // (the initial band may already be kRed — a "write only on change"
  // policy would skip the first frame and leave the LEDs dark).
  digitalWrite(kLedGreen,  band == LedBand::kGreen  ? HIGH : LOW);
  digitalWrite(kLedYellow, band == LedBand::kYellow ? HIGH : LOW);
  digitalWrite(kLedRed,    band == LedBand::kRed    ? HIGH : LOW);
}

static void classifyBandAbsolute(const uint16_t mv) {
  if (mv >= kThresholdGreenMv) {
    gLedBand = LedBand::kGreen;
  } else if (mv >= kThresholdYellowMv) {
    gLedBand = LedBand::kYellow;
  } else {
    gLedBand = LedBand::kRed;
  }
  applyLedOutputs(gLedBand);
}

static void tickLedStateMachine(const uint16_t mv) {
  const LedBand previous = gLedBand;

  switch (gLedBand) {
    case LedBand::kGreen:
      if (mv < static_cast<uint16_t>(kThresholdGreenMv - kHysteresisMv)) {
        gLedBand = (mv >= kThresholdYellowMv) ? LedBand::kYellow : LedBand::kRed;
      }
      break;

    case LedBand::kYellow:
      if (mv >= kThresholdGreenMv) {
        gLedBand = LedBand::kGreen;
      } else if (mv < static_cast<uint16_t>(kThresholdYellowMv - kHysteresisMv)) {
        gLedBand = LedBand::kRed;
      }
      break;

    case LedBand::kRed:
    default:
      if (mv >= kThresholdGreenMv) {
        gLedBand = LedBand::kGreen;
      } else if (mv >= kThresholdYellowMv) {
        gLedBand = LedBand::kYellow;
      }
      break;
  }

  if (gLedBand != previous) {
    applyLedOutputs(gLedBand);
  }
}

// Integer "x.xx" without pulling in Print::print(double) (soft-float).
static void printVolts(Print& dest, const uint16_t mv) {
  const uint16_t wholeUnits = mv / 1000U;
  const uint16_t hundredths = (mv % 1000U) / 10U;
  dest.print(wholeUnits);
  dest.print('.');
  if (hundredths < 10U) {
    dest.print('0');
  }
  dest.print(hundredths);
}

static void displaySplash() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Solar Station"));
  lcd.setCursor(0, 1);
  lcd.print(F("Starting..."));
}

static void displayMeasurement(const uint16_t mv, const uint8_t percentage) {
  if (mv == gLcdLastMv && percentage == gLcdLastPct) {
    return;
  }
  gLcdLastMv = mv;
  gLcdLastPct = percentage;

  // Fixed-width fields + trailing spaces overwrite leftover glyphs.
  // "Battery:" is 8 chars; "x.xxV" is 5; three spaces pad to 16.
  lcd.setCursor(0, 0);
  lcd.print(F("Battery:"));
  printVolts(lcd, mv);
  lcd.print(F("V   "));

  lcd.setCursor(0, 1);
  lcd.print(F("Level:"));
  lcd.print(percentage);
  lcd.print(F("%   "));
}

static void logSerial(const uint16_t mv, const uint8_t percentage) {
  Serial.print(F("Battery: "));
  printVolts(Serial, mv);
  Serial.print(F(" V | Level: "));
  Serial.print(percentage);
  Serial.println(F(" %"));
}

// =============================================================================
// Arduino entry points
// =============================================================================

void setup() {
  pinMode(kLedGreen, OUTPUT);
  pinMode(kLedYellow, OUTPUT);
  pinMode(kLedRed, OUTPUT);
  digitalWrite(kLedGreen, LOW);
  digitalWrite(kLedYellow, LOW);
  digitalWrite(kLedRed, LOW);

  // DEFAULT = AVCC. MT3608 must be trimmed to 5.0 V on the Arduino 5V pin so
  // the (adc * 5000 * 2 / 1024) scale matches physical millivolts. If AVCC
  // sags under load, readings scale proportionally — fix the rail, not the code.
  analogReference(DEFAULT);

  Serial.begin(9600);

  lcd.init();
  lcd.backlight();
  displaySplash();

  const uint32_t t0 = millis();
  gT0Adc    = t0;
  gT0Led    = t0;
  gT0Lcd    = t0;
  gT0Serial = t0;
  gT0Splash = t0;

  sampleAdc();  // prime EMA before the first scheduled tick
}

void loop() {
  const uint32_t now = millis();

  if (periodElapsed(now, gT0Adc, kAdcPeriodMs)) {
    sampleAdc();
  }

  if (gPhase == AppPhase::kSplash) {
    if (static_cast<uint32_t>(now - gT0Splash) >= kSplashDurationMs) {
      lcd.clear();
      gLcdLastMv = 0xFFFF;  // force first run-time frame
      gLcdLastPct = 0xFF;
      classifyBandAbsolute(gFilteredVoltageMv);
      gPhase = AppPhase::kRunning;
    }
    return;  // ADC keeps running above; UI is held on the splash glyphs
  }

  if (periodElapsed(now, gT0Led, kLedPeriodMs)) {
    tickLedStateMachine(gFilteredVoltageMv);
  }

  if (periodElapsed(now, gT0Lcd, kLcdPeriodMs)) {
    displayMeasurement(gFilteredVoltageMv, calculatePercentage(gFilteredVoltageMv));
  }

  if (periodElapsed(now, gT0Serial, kSerialPeriodMs)) {
    logSerial(gFilteredVoltageMv, calculatePercentage(gFilteredVoltageMv));
  }
}
