# Radio-frequency commutator for serialized, 6-DOF-tracking headstages and miniscopes
The idea behind this device is that the wide-spread availability of 6-DOF pose
tracking using internal-measurement units (IMUs) allows continuous monitoring
of an animal's rotational state in an environment. This obviates the need for
tether torque measurements to drive an active commutator since the rotational
state of the animal is known, and the commutator can simply be told to follow
along.

Next-generation, serialized headstages and miniscopes generally provide the
required IMU data over their coaxial link required to drive this commutator.
Examples include the UCLA miniscope rev. 4.0, and open-ephys headstage-64 and
headstage-256.

## Usage
This board is used to control the commutator's stepper motor.

1. [Teensy 3.2](https://www.pjrc.com/store/teensy32.html) for receiving
   commands and controlling all circuit elements.
2. [TMC2130 Silent Stepper
   Stick](https://www.watterott.com/en/SilentStepStick-TMC2130) for driving the
   motor.
3. Super-capacitor charge system and step-up regulator for providing
   high-current capacity drive to the motor driver directly from USB.
4. RGB indicator LED.

### LED
The LED tells you about the commutator state

    1. Flashing red: commutator is charging internal super-capacitors.  All
       controls and motor operation are locked Wait until this completes.
    1. Solid red: commutator is disabled. Motor is turned off and will not
       turn or respond to button  presses or external commands.
    1. Blue: commutator is enabled and permits both remote (RPC) and manual
       (button) turn control. Buttons take preceidence over remote commands.
    1. Green: commutator is enabled and permits remote turn control. Turn
       buttons are inactive. Stop/Go and LED buttons remain active.
    1. Pink: commutator is enabled permits manual control only. The device will
       not respond to remote turn commands.

The LED can be turned off by pressing and holding it > 500 msec. It can be
turned back on by pressing it.

### Buttons
The front panel has four buttons.

- __Stop/Go__: toggle commutator enable/disable.
    - *Disabled* (LED is red): All motor output will halt instantly, and motor
      driver is powered down. Pressing directional buttons in the stopped state
      will not work.  All target turns provided via RPC will be cleared, such
      that re-enable the motor will not result in the commutator re-engaging an
      old target position. In this state, pressing and holding the Stop/Go
      button for > 0.5 second, or sending the approriate RPC will enable the
      device.
    - *Enabled* (LED is green or pink): When in the *enabled* state, the LED
      will be green or pink depending on operation mode and the motor can be
      turned via button presses or RPCs depending on the operation mode (see
      Mode Button). In this state, pressing the Stop/Go button will instantly
      disable the device.

- __Directional buttons (2x)__: Manual control over the motors rotation in the
  direction indicated on each button when the commutator is *enabled*.  These
  inputs override any other form of motor control. When pressed, all target
  turns provided via RPC will be cleared, such that releasing them will not
  result in the commutator re-engaging an old target position.

- __Mode Button__: Pressing the LED toggles the operation mode (remote/manual or
  manual-only).
  - *Manual* (LED is pink): commutator is enabled and responds to manual,
    button-based turn control only
  - *Remote* (LED is green): commutator is enabled and responds to remote,
    RPC-based turn control only
  - *Remote/Manual* (LED is blue): commutator is enabled and responds to both
    remote and manual turn control. Manual control takes presidence over remote
    and will clear all remote targeting state.

### Remote Control Interface
When in *Remote/Manual* mode, the commutator accepts JSON-encoded commands over
its serial interface. Here are examples of all commands that can be sent:
```
{enable : true}     // Enable commutator
{led : false}       // Turn off RGB LED
{mode : 1}          // 0 = manual, 1 = remote, 2 = remote & manaul
{speed : 2.5}       // Scale base turn speed (50 RPM) by 2.5
{turn : 1.1}        // 1.1 turns CW
{turn : -1.1}       // 1.1 turns CCW


// Example multi-command. Any combo can be used.
// In this case:
// 1. Allow remote control
// 2. Scale speed to 25 RPM
// 3. Excecute 1.1 turns CC
// Ordering of commands does not matter
{mode: 1, speed: 0.5, turn : -1.1}
```

So, to enable remote control mode, set the speed to 100 RPM, and turn the motor
2 times CW we would send the following string to the device's serial port:
```
{mode: 1, speed: 2, turn : 2.0}
```

### Saving settings
All control and speed parameters are saved in non-volatile memory each time
they are changed. The device will start in the same state it was last used.

## Firmware
Firmware is located [here](../firmware).

### Construction

### Bill of Materials
The BOM is located [here](https://docs.google.com/spreadsheets/d/1M2R0Q2-OuRHzctt05BxtA3hxNcCHtRZHORzCKElmG1Q/edit?usp=sharing).

### Mechanical
TODO: Mechanical design files can be found...

TODO: Link to shapeways where user can just buy parts

### Electronics
Board designs are located [here](../control-board). Board design file types are
as follows:

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

This is a 4-layer circuit board with very high-tolerance design rules (min
trace space: 6 mil. 0.3 mm min hole size)
