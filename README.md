# Commutator for serialized, 6 DOF-tracking headstages and miniscopes
The wide-spread availability of 6 degree of freedom pose tracking using
internal-measurement units (IMUs) allows continuous monitoring of an animal's
rotational state in an environment. This obviates the need for tether torque
measurements to drive an active commutator since the drift-free rotational
state of the animal is known in real-time, and the commutator can simply
follow along. This permits the use of extremely thin [coaxial
tethers](http://www.axon-cable.com/en/02_products/06_coaxial-cables/02/index.aspx)
that cannot function with a standard active commutator because they are too
flexible to translate rotational torque.

This commutator functions with coaxially-serialized head-borne recording
systems that provide appropriate rotational tracking information. Currently
supported devices are next-generation, serialized headstages and miniscopes
such as [headstage-64](), [headstage-256](), and UCLA Miniscope Rev. 4.0.
However, this device operations competely independently from the headstage
itself, so can be used with any device that provides accurate, drift-free
rotational state.

## Usage

### LED
The LED tells you about the commutator state

1. Flashing red: commutator is charging internal super-capacitors.  All
   controls and motor operation are locked Wait until this completes.
1. Solid red: commutator is disabled. Motor is turned off and will not
   turn or respond to button presses or external commands.
1. Pink: commutator is enabled permits manual control only. The device will
   not respond to remote turn commands.
1. Green: commutator is enabled and permits remote turn control. Turn
   buttons are inactive. Stop/Go and LED buttons remain active.
1. Blue: commutator is enabled and permits both remote (RPC) and manual
   (button) turn control. Buttons take precedence over remote commands.

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
    - *Enabled* (LED is blue, green, or pink): When in the *enabled* state, the
      LED will be blue, green, or pink and the motor can be turned via button
      presses or RPCs depending on the operation mode (see Mode Button). In
      this state, pressing the Stop/Go button will instantly disable the
      device.

- __Directional__ (2x): Manually control the motor rotation in the
  direction indicated on each button when the commutator is *Enabled* and in
  *Manual* or *Remote/Manual* mode. These inputs override remote motor control.
  When pressed, all target turns provided via remote control will be cleared,
  such that releasing them will not result in the commutator re-engaging an old
  target position.

- __Mode__: pressing the LED toggles the operation mode.
  - *Manual* (LED is pink): commutator is enabled and responds to manual,
    button-based turn control only
  - *Remote* (LED is green): commutator is enabled and responds to remote,
    RPC-based turn control only
  - *Remote/Manual* (LED is blue): commutator is enabled and responds to both
    remote and manual turn control. Manual control takes presidence over remote
    and will clear all remote targeting state.

### Remote control interface
When in *Remote* or *Remote/Manual* mode, the commutator accepts JSON-encoded
commands over its serial interface. Here are examples of all commands that can
be sent:
```
{enable : true}     // Enable commutator (default = false)
{led : false}       // Turn off RGB LED (default = true)
{mode : 1}          // 0 = manual, 1 = remote, 2 = remote & manaul (default = manual)
{speed : 250}       // Set turn speed to 250 RPM (default = 50 RPM, valid ∈ (0, 500] RPM)
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
All control and speed parameters, whether changed via the remote or manual
interface, are saved in non-volatile memory each time they are changed. The
device will start in the same state it was last used.

## Firmware
Firmware is located [here](../firmware). The controller firmware runs on a
[Teensy 3.2](https://www.pjrc.com/store/teensy32.html). To program the teensy
you will need the following Arduino dependencies:

- [TeensyStep](https://github.com/luni64/TeensyStep)
- [Arduino JSON](https://arduinojson.org/)
- [Teensyduino add-on](https://www.pjrc.com/teensy/td_download.html)
- [Arduino IDE](https://www.arduino.cc/en/Main/Software)

The firmware is located
[here](https://github.com/jonnew/twister3/tree/master/firmware/twister3). The
firmware can be uploaded to the device using the [Arduino
IDE](https://www.arduino.cc/en/Main/Software). _Note that you will need to add
the [Teensyduino add-on](https://www.pjrc.com/teensy/teensyduino.html) to to
the Arduino IDE to program the Teensy_.When installing Teensyduino, you should
opt install all of the bundled libraries as well.

## Construction

### Bill of Materials
The BOM is located [here](https://docs.google.com/spreadsheets/d/1M2R0Q2-OuRHzctt05BxtA3hxNcCHtRZHORzCKElmG1Q/edit?usp=sharing).

### Mechanical
The mechanical components, excluding fasteners, of the commutator are as
follows:

1. RF Rotary Joint
1. 2x reduction gears (3D-printed)
1. NEMA-11 Stepper Motor
1. Housing (3D-printed)

Links to purchase each of these components can be found on the BOM.

### Electronics
The board used to control the commutator consists of the following elements:

1. [Teensy 3.2](https://www.pjrc.com/store/teensy32.html) for receiving
   commands and controlling all circuit elements.
2. [TMC2130 Silent Stepper
   Stick](https://www.watterott.com/en/SilentStepStick-TMC2130) for driving the
   motor.
3. Super-capacitor charge system and step-up regulator for providing
   high-current capacity drive to the motor driver directly from USB.
4. RGB indicator LED.
5. Capacitive touch sensors on the back side of the PCB that serve as buttons
   for manual commutator control

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

### Assembly

TODO: Pictures of assembly

## Hardware License
Copyright Jakob Voigts & Jonathan P. Newman 2019.

This documentation describes Open Hardware and is licensed under the
CERN OHL v.1.2.

You may redistribute and modify this documentation under the terms of the CERN
OHL v.1.2. (http://ohwr.org/cernohl). This documentation is distributed WITHOUT
ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING OF MERCHANTABILITY, SATISFACTORY
QUALITY AND FITNESS FOR A PARTICULAR PURPOSE. Please see the CERN OHL v.1.2 for
applicable conditions
