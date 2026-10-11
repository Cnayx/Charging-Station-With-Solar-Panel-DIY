# Hardware Wiring Guide

Step-by-step instructions for building the Solar Charging Station. Read the full guide once before connecting any wires.

**Before you start:** Disconnect all power. Double-check every **+** and **−** connection. Reversed polarity on the battery or TP4056 can damage components.

See [BOM.md](BOM.md) for the complete parts list.

---

## How the System Works (Big Picture)

```text
Solar Panel  -->  TP4056  -->  Batteries  -->  Main Switch  -->  Two branches:
                                                                    |
                                                    +---------------+---------------+
                                                    |                               |
                                              USB-A Boost                     MT3608
                                              (phone charger)              (Arduino power)
                                                    |                               |
                                                  Phone                    Arduino + LCD + LEDs
```

The Arduino **does not** power the phone. It only **monitors** the battery and shows the status on the LCD and LEDs.

---

## Step 1: Power Input (Solar Panel → TP4056)

Connect the solar panel to the TP4056 charger through a protection diode.

```text
Solar Panel +  -->  1N4007 anode (plain / unmarked end)
1N4007 cathode (stripe end)  -->  TP4056 IN+
Solar Panel −  -->  TP4056 IN−
```

**Tips:**

- The stripe on the 1N4007 points **toward** the TP4056 IN+ pin.
- The diode stops the battery from draining into the panel when it is dark.
- At this stage, do **not** connect the batteries yet.

---

## Step 2: Energy Storage (Batteries & Main Switch)

### 2a. Install the batteries (parallel)

Wire both 18650 cells **in parallel** — both positive terminals together, both negative terminals together.

```text
Cell 1 +  ──┐
Cell 2 +  ──┴──  TP4056 B+
Cell 1 −  ──┐
Cell 2 −  ──┴──  TP4056 B−
```

Use a dual holder designed for parallel wiring, or wire the holder so both **+** terminals connect together and both **−** terminals connect together.

**Important:** Use two identical batteries (same model and capacity). Mixing different cells in parallel is unsafe.

### 2b. Add the main power switch

The switch goes on the **positive** output of the TP4056, **after** the OUT+ pin and **before** any loads.

```text
TP4056 OUT+  -->  Main Switch  -->  "Switched +" rail
TP4056 OUT−  -->  Battery negative bus (always connected)
```

**What the switch does:**

| Switch position | What happens |
|---|---|
| **ON** | Power flows to the USB boost, MT3608, and Arduino |
| **OFF** | All loads are disconnected — battery is preserved when not in use |

Solar charging through the TP4056 continues even when the switch is OFF.

---

## Step 3: Power Distribution

From the **Switched +** rail (the wire that comes **out** of the main switch), split into two branches.

### Branch A — USB-A Boost (Phone Charging)

```text
Switched +  -->  USB-A module B+
Battery −   -->  USB-A module B−
Phone cable -->  USB-A socket
```

**Before plugging in your phone:** Turn the switch ON and measure the USB port with a multimeter. You should see approximately **5 V**.

### Branch B — MT3608 → Arduino

The MT3608 converts the battery voltage (3.0 – 4.2 V) into a stable **5.0 V** for the Arduino.

```text
Switched +  -->  MT3608 IN+  (label may vary — check your module)
Battery −   -->  MT3608 IN−
MT3608 OUT+ -->  Arduino 5V pin
MT3608 OUT− -->  Arduino GND
```

**Adjust the MT3608 before connecting the Arduino:**

1. Turn the switch **ON**.
2. Connect a multimeter to MT3608 OUT+ and OUT−.
3. Turn the small potentiometer on the module until the reading is **5.0 V**.
4. Then connect the Arduino.

**Power rules for the Arduino:**

- Feed power through the **5V pin** from the MT3608.
- Do **not** connect USB, the DC barrel jack, and MT3608 at the same time — use only one power source.
- Connect Arduino **GND** to the battery negative bus.

---

## Step 4: Logic & Sensors (Arduino, LCD, LEDs, Voltage Divider)

### 4a. I2C LCD Display

```text
LCD VCC  -->  Arduino 5V
LCD GND  -->  Arduino GND
LCD SDA  -->  Arduino A4
LCD SCL  -->  Arduino A5
```

The default I2C address is **0x27**. If the backlight turns on but no text appears, try **0x3F** in the firmware or adjust the contrast potentiometer on the LCD backpack.

### 4b. Status LEDs

Each LED needs its own current-limiting resistor (220 Ω or 330 Ω).

```text
Arduino D3  -->  220 Ω  -->  Green LED (+)  -->  Green LED (−)  -->  GND
Arduino D4  -->  220 Ω  -->  Yellow LED (+) -->  Yellow LED (−) -->  GND
Arduino D5  -->  220 Ω  -->  Red LED (+)    -->  Red LED (−)    -->  GND
```

The **long leg** of each LED is **+** (anode). The **short leg** is **−** (cathode).

| LED color | Meaning |
|---|---|
| Green | Battery is high (≥ 3.60 V) |
| Yellow | Battery is medium (3.35 V – 3.59 V) |
| Red | Battery is low (< 3.35 V) |

### 4c. Voltage Divider (Battery Sensor on A0)

The Arduino analog pin **A0** can only accept 0 – 5 V. A fully charged 18650 is ~4.2 V, but we still use a divider for safety and headroom. Two equal **10 kΩ** resistors cut the voltage in half.

**Connect the divider to the Switched + rail** (the same wire that feeds the USB boost and MT3608):

```text
Switched +  -->  10 kΩ  -->  Arduino A0  -->  10 kΩ  -->  GND
```

```text
         Switched +
             |
           [10k]
             |
             +------ Arduino A0
             |
           [10k]
             |
            GND
```

Because the resistors are equal, the voltage at A0 is **half** the battery voltage. The firmware doubles the reading to display the real battery voltage.

**Never connect battery + directly to A0 without the divider.**

---

## Complete Wiring Reference

```text
SOLAR +  --[1N4007]-> TP4056 IN+
SOLAR -  -----------> TP4056 IN-

CELLS +  -----------> TP4056 B+
CELLS -  -----------> TP4056 B-

TP4056 OUT+  -->  [MAIN SWITCH]  -->  Switched +
                                         |-- USB-A B+
                                         |-- MT3608 IN+
                                         `-- 10k -- A0 -- 10k -- GND

TP4056 OUT- / CELLS -  -->  USB-A B-
                         -->  MT3608 IN-
                         -->  Arduino GND

MT3608 OUT+  -->  Arduino 5V
MT3608 OUT-  -->  Arduino GND

LCD:  VCC->5V  GND->GND  SDA->A4  SCL->A5
LEDs: D3/D4/D5 -> resistor -> LED -> GND
```

---

## Testing Checklist

Work through these steps in order:

1. **Batteries only** — Confirm TP4056 B+/B− polarity. No smoke, no heat.
2. **Solar input** — Place the panel in sunlight. The TP4056 charge LED should turn on.
3. **Switch OFF** — Nothing should power up. Battery voltage should hold steady overnight.
4. **MT3608 trim** — Set output to **5.0 V** before connecting the Arduino.
5. **Switch ON** — Arduino boots, LCD shows *"Solar Station"* then battery data.
6. **Voltage check** — Compare the LCD reading to a multimeter on the battery. They should match within ~0.05 V.
7. **LED check** — Green, yellow, and red LEDs respond to battery level.
8. **USB check** — Measure ~5 V at the USB-A port, then connect your phone.

---

## Safety — Do Not

- Connect battery **+** directly to Arduino **A0** (always use the 10 kΩ / 10 kΩ divider).
- Reverse TP4056 or battery polarity.
- Plug the phone into the TP4056 — use the **USB-A boost module** for phone charging.
- Feed more than **5.5 V** into the Arduino **5V** pin.
- Use mismatched 18650 cells in parallel.
- Put the main switch on only the phone branch — it must sit **before** both the USB boost **and** the MT3608.
- Connect the Arduino USB cable while the MT3608 is also powering the **5V** pin.

---

## Troubleshooting

| Problem | Things to check |
|---|---|
| LCD backlight on, no text | I2C address (0x27 vs 0x3F), contrast pot, SDA/SCL wiring |
| Wrong voltage on screen | Divider wired to Switched + (not MT3608 output), GND shared with battery − |
| Arduino does not boot | MT3608 set to 5.0 V, switch is ON, 5V/GND not swapped |
| Phone does not charge | USB boost B+/B− wired correctly, ~5 V at USB port |
| Battery drains overnight | Main switch OFF when not in use |

For firmware setup and upload, see the project root `platformio.ini` and `src/main.cpp`.
