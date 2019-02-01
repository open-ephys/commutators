## Commutator Control Board
This board is used to control the commutator's stepper motor.

1. [Teensy LC](https://www.pjrc.com/teensy/teensyLC.html) for receiving commands and controlling all circuit elements.
2. [TMC2130 Silent Stepper Stick](https://www.watterott.com/en/SilentStepStick-TMC2130) for driving the motor.
3. Super-capacitor charge system and step-up regulator for providing
   high-current capacity drive to the motor driver directly from USB.
4. RGB indicator LED.

### Usage

#### LED
The LED tells you about the commutator state

    1. Flashing red: commutator is charging internal super-capacitors.
       Wait until it stops flashing to use.
    1. Solid red: commutator in *stop* state. Motor will not turn.
    1. Green: external and manual control
    1. Pink: manual control only

#### Buttons
The front panel has four buttons.

- Stop/Go: toggle commutator enable/disable. When in the *stop* state, the LED
  will turn red and all motor output will halt. Pressing directional buttons in
  the stopped state will not work. When in the *go* state, the LED will be
  green or pink depending on operation mode and the motor can be turned.
- Directional buttons (2x): Manual control over the motors rotation in the
  direction indicated on each button when the commutator is the *go* state.
  These inputs override any other form of motor control.
- Mode Button: Pressing the LED toggles the operation mode (external or
  manual).  Holding the LED for 1 second will toggle it on and off.

#### Remote Control Interface
The commutator communicates over its serial interface using JSON. Here are
examples of all commands:
```

{enable : true}     // Enable commutator
{led_on : false}    // Turn off RGB LED
{mode : 1}          // 0 = manual, 1 = automatic (respond to turn command below)
{speed : 2.5}       // Scale base speed (100 RPM) by 2.5
{turn : 1.1}        // 1.1 turns CW
{turn : -1.1}       // 1.1 turns CCW


// Multi command
{mode: 1, speed: 2.5, turn : -1.1} // 1.1 turns CCW
```

### Firmware
Firmware is located [here](../firmware).

### Bill of Materials
The BOM is located [here](https://docs.google.com/spreadsheets/d/1M2R0Q2-OuRHzctt05BxtA3hxNcCHtRZHORzCKElmG1Q/edit?usp=sharing).

### Board Design Files
- `*.sch`: EAGLE schematic file
- `*.brd`: EAGLE board file
- `gerber/*.GKO`: board outline
- `gerber/*.GTS`: top solder mask
- `gerber/*.GBS`: bottom solder mask
- `gerber/*.GTO`: top silk screen
- `gerber/*.GBO`: bottom silk screen
- `gerber/*.GTL`: top copper
- `gerber/*.G2L`: inner layer 2 copper
- `gerber/*.G3L`: inner layer 3 copper
- `gerber/*.GBL`: bottom copper
- `gerber/*.XLN`: drill hits and sizes
- `stencil/*.CST`: solder stencil cutouts
