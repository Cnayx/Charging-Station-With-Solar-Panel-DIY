# Bill of Materials (BOM)

Everything you need to build the Solar Charging Station. Use **matched** 18650 cells (same brand, capacity, and age) when wiring them in parallel.

| # | Component | Qty | Purpose |
|---|---|---|---|
| 1 | Arduino Uno R3 | 1 | Main controller — reads battery voltage, drives LCD and status LEDs |
| 2 | 5 W / 6 V Solar Panel | 1 | Charges the battery pack from sunlight |
| 3 | TP4056 Module (protected, DW01A, with **OUT+** / **OUT−** pins) | 1 | Safely charges the 18650 cells and protects them from over-charge / over-discharge |
| 4 | 18650 Lithium-Ion Battery | 2 | Energy storage — wired **in parallel** for higher capacity |
| 5 | Dual 18650 Battery Holder (parallel wiring) | 1 | Holds both cells and connects them in parallel |
| 6 | MT3608 Step-Up Converter | 1 | Boosts battery voltage to a steady **5.0 V** for the Arduino |
| 7 | 5 V USB-A Boost / Powerbank Module | 1 | Steps battery voltage up to **5 V USB** to charge a phone |
| 8 | SPST Toggle or Rocker Switch | 1 | Master power switch — turns all loads on or off to save battery when idle |
| 9 | 16×2 I2C LCD Display (PCF8574 backpack) | 1 | Shows battery voltage and charge level on screen |
| 10 | 1N4007 Diode | 1 | Prevents the battery from discharging back into the solar panel at night |
| 11 | 10 kΩ Resistor | 2 | Voltage divider — scales battery voltage down so Arduino can read it safely |
| 12 | 220 Ω or 330 Ω Resistor | 3 | Limits current through the status LEDs |
| 13 | Green LED | 1 | Indicates high battery level (≥ 3.60 V) |
| 14 | Yellow LED | 1 | Indicates medium battery level (3.35 V – 3.59 V) |
| 15 | Red LED | 1 | Indicates low battery level (< 3.35 V) |

## Also Recommended (not in firmware, but needed to build)

| Item | Qty | Purpose |
|---|---|---|
| Breadboard | 1 | Prototyping surface for resistors, LEDs, and jumper connections |
| Male-to-male jumper wires | ~20 | Connect modules, Arduino, and breadboard |
| Multimeter | 1 | Verify voltages and polarities before connecting your phone |
| Small screwdriver | 1 | Adjust the MT3608 output to exactly 5.0 V |

## TP4056 — How to Recognize the Correct Module

Buy the version that includes **DW01A protection** and has these pin groups:

- **IN+ / IN−** — solar panel input  
- **B+ / B−** — battery connection  
- **OUT+ / OUT−** — protected output to your switch and loads  

Avoid bare TP4056 boards with only **B+ / B−** and no **OUT+ / OUT−** — they lack the protection circuit this project requires.
