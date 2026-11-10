# Sedentary Reminder

An STC89C52RC-based timer for sitting and rest breaks. P1.1 simulates sitting or standing, P1.0 sets the required rest time, a four-digit common-anode display shows the timer, and a buzzer provides reminders. The Keil project uses the compatible AT89C52 target for compilation; the physical chip is an STC89C52RC.

**Author:** Andy (Yong) Liu  
**School:** Sir Winston Churchill Secondary School

[中文说明 / Chinese README](README.zh-CN.md)

> This document describes the current Timer.c. Sitting time is displayed as an **elapsed count up**; rest time is displayed as a **countdown**. The sitting limit defaults to 30 minutes and can be switched to a one-minute test mode. The rest time defaults to 10 minutes.

## Hardware

The physical circuit uses an STC89C52RC with an 11.0592 MHz crystal. Timer0 is configured for **12T mode** and interrupts approximately every 1 ms. Confirm that the chip is set to 12T when programming it: STC89C52RC supports both 6T and 12T, and the current timing will run too fast in 6T mode. See the [STC89C series datasheet](https://www.stcmicro.com/datasheet/STC89C51RC-en.pdf).

| Function | MCU pin | Active level |
| --- | --- | --- |
| Adjust rest time | P1.0 | Low means pressed |
| Simulate sitting | P1.1 | Low means sitting; high means standing |
| Buzzer | P2.0 | High means on |
| Digit selects, left to right | P2.4, P2.3, P2.2, P2.1 | High selects a digit |
| Display segments | P0.0–P0.7 | Low lights a segment |

### MCU and display wiring diagram

The MCU pin numbers below are for the **STC89C52RC PDIP-40 package**, viewed from above with the notch at the top. The display pin numbers follow this project's 12-pin, four-digit common-anode wiring; verify them against the actual display's pinout before wiring it.

    Set switch → GND  P1.0   1 ┌───────∩───────┐ 40  VCC    → +5V
    Sit switch → GND  P1.1   2 │               │ 39  P0.0   → A  (display pin 11)
                      P1.2   3 │               │ 38  P0.1   → F  (display pin 10)
                      P1.3   4 │               │ 37  P0.2   → B  (display pin  7)
                      P1.4   5 │               │ 36  P0.3   → E  (display pin  1)
                      P1.5   6 │               │ 35  P0.4   → D  (display pin  2)
                      P1.6   7 │ STC89C52RC    │ 34  P0.5   → DP (display pin  3)
                      P1.7   8 │   PDIP-40     │ 33  P0.6   → C  (display pin  4)
    Reset circuit     RST   9 │               │ 32  P0.7   → G  (display pin  5)
                      P3.0  10 │               │ 31  EA     → +5V
                      P3.1  11 │               │ 30  ALE
                      P3.2  12 │               │ 29  PSEN
                      P3.3  13 │               │ 28  P2.7
                      P3.4  14 │               │ 27  P2.6
                      P3.5  15 │               │ 26  P2.5
                      P3.6  16 │               │ 25  P2.4   → NPN Q1 → COM1 (pin 12)
                      P3.7  17 │               │ 24  P2.3   → NPN Q2 → COM2 (pin  9)
    Crystal terminal  XTAL2 18 │               │ 23  P2.2   → NPN Q3 → COM3 (pin  8)
    Crystal terminal  XTAL1 19 │               │ 22  P2.1   → NPN Q4 → COM4 (pin  6)
    Ground            GND   20 └───────────────┘ 21  P2.0   → buzzer driver input

The 11.0592 MHz crystal connects between pins 18 and 19. RST (pin 9) is low during normal operation. The two momentary switches connect their P1 pins **directly to GND**, with **no external 10 kΩ resistors**: P1's internal weak pull-ups make a released switch read high and a pressed switch read low. Software debouncing takes about 20 ms.

Each display LED segment path already has a **470 Ω** current-limiting resistor. There is **no resistor in the display's common-anode VCC path**. Any separate LEDs use **330 Ω** resistors, but this program does not assign pins to separate LEDs.

The code requires P2.4–P2.1 to select a digit when high and P0 segment lines to light a segment when low. The diagram's NPN connection assumes collector to VCC, emitter to the digit's COM pin, and base driven from P2. This is an inferred emitter-follower arrangement consistent with the code's select polarity; verify the actual transistor pinout, base resistor, and available drive current against the physical circuit. The four COM pins cannot all remain permanently connected to VCC, or digit scanning will not work.

### How the digits appear

    Seven-segment positions:

             ── A ──
            │       │
            F       B
            │       │
             ── G ──
            │       │
            E       C
            │       │
             ── D ──   · DP

To display **1**, the selected digit lights only B and C, so P0.2 and P0.6 output low. To display **0**, it lights A, B, C, D, E, and F while G stays off. For **00:01**, the program disables every digit, outputs the segment pattern for one position, enables that position, and repeats for the other three positions. It advances approximately every 1 ms, making all four digits appear continuously lit. P0.5 controls the clock points while scanning the second and third digits.

## Operation

### Standby and rest-time setting

On power-up, standby shows the default rest setting **10:00** with a steady colon. Each P1.0 press adds one minute. The range is 1–10 minutes; pressing again at 10 minutes wraps to 1 minute. The displayed setting updates immediately and remains visible until sitting begins. Settings are locked after sitting begins and throughout the rest period.

### Sitting-limit test mode

While in standby, complete **three P1.1 press-and-release cycles within one second** to toggle the sitting limit between **30 minutes** and **one minute**. Each tap must be shorter than about 250 ms. These taps do not begin sitting. After a successful toggle, the display shows **30:00** or **01:00** for about two seconds and then returns to the rest setting. Another triple tap toggles back.

### Sitting and standing

Holding P1.1 for about 250 ms starts sitting. The hold-detection interval is included in the sitting time. The display counts **up from 00:00** while the colon flashes about every 500 ms. Releasing P1.1 means standing; the display switches to the remaining rest-time countdown, with a flashing colon.

**Standing before the sitting limit:** Sitting time pauses and a full rest period begins. If P1.1 is pressed again before rest finishes, that rest attempt is discarded and sitting resumes from the accumulated time. The next time the user stands, a new full rest countdown starts. If the rest countdown reaches zero while standing, the accumulated sitting time is cleared and standby begins.

**Reaching the sitting limit:** Mandatory rest begins. The buzzer sounds continuously while P1.1 still indicates sitting. Standing stops the continuous alarm and starts the rest countdown. Sitting again pauses the countdown and resumes the continuous alarm; standing again resumes the countdown from where it stopped. A new sitting cycle and rest-time adjustments are available only after the entire required rest time is completed.

When any rest countdown finishes, the buzzer sounds for about **one second** to signal that the next cycle can begin.

## Buzzer pattern

Short beeps start during the last 30 seconds of the sitting limit. Each lasts about 50 ms.

| Sitting time remaining | Short beeps per second |
| --- | --- |
| 30–11 seconds | 1 |
| 10 seconds | 1 |
| 9 seconds | 2 |
| 8 seconds | 3 |
| Each following second | One more than the previous second |
| 1 second | 10 |

The ten beeps in the final second are triggered at 0, 100, …, and 900 ms. On reaching the sitting limit, the output changes to the continuous alarm while the user remains seated.

## Display states

| State | Display | Colon |
| --- | --- | --- |
| Standby / rest-time adjustment | Configured rest time, initially 10:00 | Steady |
| Two seconds after a sitting-limit toggle | 30:00 or 01:00 | Steady |
| Sitting | Elapsed sitting time, counting up from 00:00 | Flashing |
| Rest | Time remaining; mandatory rest pauses when sitting resumes | Flashing |

## Building with Keil µVision 3

1. Open [Timer.Uv2](Timer.Uv2). It includes [Timer.c](Timer.c) and [STARTUP.A51](STARTUP.A51). The Keil target is AT89C52 for 8051 compatibility; the physical chip is STC89C52RC.
2. Confirm **11.0592 MHz** in **Options for Target → Target**. Timer.Uv2 is configured for this frequency. The Timer0 reload value is 0xFC66, giving approximately 1 ms at 11.0592 MHz in 12T mode.
3. Build the project. HEX output is enabled. Program the generated Timer.hex with a tool suitable for STC89C52RC, such as STC-ISP, and confirm 12T mode.

Timing is based on an approximately 1 ms Timer0 interrupt and is intended for reminders, not precision timekeeping. This repository does not include an automated build or hardware test setup.

## Repository files

| File | Purpose |
| --- | --- |
| [Timer.c](Timer.c) | Application logic |
| [STARTUP.A51](STARTUP.A51) | Keil C51 startup assembly |
| [Timer.Uv2](Timer.Uv2) | µVision 3 project |
| [README.zh-CN.md](README.zh-CN.md) | Original Chinese documentation |
| [.gitignore](.gitignore) | Excludes build outputs, local options, and backups |
| [LICENSE](LICENSE) | MIT license for the original project content |

OBJ, LST, M51, HEX, PLG, and LNP files, as well as the extensionless Timer executable, are generated by the project and are not source files.

## License

The original project content is licensed under the [MIT License](LICENSE). Copyright (c) 2026 Andy (Yong) Liu <liuyong@kaka888.com>. The bundled `STARTUP.A51` is a Keil C51 compiler file with its own copyright notice and is not covered by this MIT grant.
