# Coaxial commutator for serialized headstages and miniscopes
The wide-spread availability of 6 degree of freedom pose tracking using
internal-measurement units (IMUs) allows continuous monitoring of an animal's
rotational state in an environment. This obviates the need for tether torque
measurements to drive an active commutator since the rotational state of the
animal is known in real-time, and the commutator can simply follow along. This
permits the use of extremely thin [coaxial
tethers](http://www.axon-cable.com/en/02_products/06_coaxial-cables/02/index.aspx)
that cannot function with a standard active commutator because they are too
flexible to translate rotational torque.

![Zero-torque coaxial commutator.](./resources/demo.gif)

This commutator functions with coaxially-serialized head-borne recording
systems that provide appropriate rotational tracking information. Currently
supported devices are next-generation, serialized headstages and miniscopes
such as [headstage-64](../headstage-64/README.md),
[headstage-256](../headstage-256/README.md), and UCLA Miniscope Rev. 4.0f.
However, this device operations completely independently from the head stage
itself, so can be used with any device that provides accurate, drift-free
rotational state. It can even be used without an IMU, e.g. using video-based
rotation tracking since its remote control interface is agnostic to how
rotational measurements are taken.

## Features
- High bandwidth link up to 18 GHz
- Optical table & 80/20 rail mountable
- Remote control using JSON-encoded commands
- Manual control 
- Indication LED
    - Can be completely turned off
- Advanced stepper driver (TMC2130)
    - Voltage-controlled for silent operation 
    - Precise motion using step interpolation (256 uSteps/step)
- USB powered and controlled
    - Internal super-capacitor circuitry prevents loading the USB bus during
      motion

## Usage

### LED
The LED tells you about the commutator state:

1. Flashing red (*Charging*): commutator is charging internal super-capacitors.
   All controls and motor operation are locked. Wait until this process
   completes to use the device. It can take up to 30 seconds.
1. Solid red (*Disabled*): commutator is disabled. Motor is turned off and will not
   turn or respond to button presses or external commands.
1. Green (*Enabled*): commutator is enabled and permits both remote and manual
   (button) turn control. Buttons take precedence over remote commands.

The LED can be turned off by pressing and holding it > 500 msec. It can be
turned back on by pressing it.

### Buttons
The front panel has four buttons.

- __Stop/Go__: toggle commutator enable/disable.
    - *Disabled* (LED is red): All motor output will halt instantly, and motor
      driver is powered down. Pressing directional buttons in the stopped state
      will not work. All target turns provided via remote calls will be
      cleared, such that re-enable the motor will not result in the commutator
      re-engaging an old target position. In this state, pressing and holding
      the Stop/Go button for > 0.5 second, or sending the approriate remote
      command will enable the device.
    - *Enabled* (LED  green): When in the *enabled* state, the LED will be
      green and the motor can be turned via button presses or RPCs . In this
      state, pressing the Stop/Go button will instantly disable the device.

- __Directional__ (2x): Manually control the motor rotation in the
  direction indicated on each button when the commutator is *Enabled* and in
  *Manual* or *Remote/Manual* mode. These inputs override remote motor control.
  When pressed, all target turns provided via remote control will be cleared,
  such that releasing them will not result in the commutator re-engaging an old
  target position.

- __LED__: pessing and holding the the LED turns it off. Pressing it again turns it back on.
    - *Manual* (LED is pink): commutator is enabled and responds to manual,
      button-based turn control only
    - *Remote* (LED is green): commutator is enabled and responds to remote,
      RPC-based turn control only
    - *Remote/Manual* (LED is blue): commutator is enabled and responds to both
      remote and manual turn control. Manual control takes presidence over
      remote and will clear all remote targeting state.

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
// 2. Set speed to 25 RPM
// 3. Excecute 1.1 turns CC
// Ordering of commands does not matter
{mode: 1, speed: 25, turn : -1.1}
```

So, to enable remote control mode, set the speed to 100 RPM, and turn the motor
2 times CW we would send the following string to the device's serial port:
```
{mode: 1, speed: 100, turn : 2.0}
```

### Saving settings
All control and speed parameters, whether changed via the remote or manual
interface, are saved in non-volatile memory each time they are changed. The
device will start in the same state it was last used.

## Firmware
The controller firmware is located [here](./firmware/commutator). It runs on a
[Teensy 3.2](https://www.pjrc.com/store/teensy32.html). To compile 
this firmware and program the microcontroller, you need the following 
dependencies:

- [Arduino IDE](https://www.arduino.cc/en/Main/Software)
- [Teensyduino add-on](https://www.pjrc.com/teensy/td_download.html)
- [AccelStepper](https://www.airspayce.com/mikem/arduino/AccelStepper/)
- [Arduino JSON](https://arduinojson.org/)

The firmware can be uploaded to the device using the [Arduino
IDE](https://www.arduino.cc/en/Main/Software). _Note that you will need to add
the [Teensyduino add-on](https://www.pjrc.com/teensy/teensyduino.html) to to
the Arduino IDE to program the Teensy_. When installing Teensyduino, you should
opt to install all of the bundled libraries as well. This takes care of
installing `AccelStepper` library rather than having to install it manually.
ArduinoJSON can be installed through the Arduino IDE's package manager.

## Construction

### Bill of Materials
The BOM is located [here](https://docs.google.com/spreadsheets/d/1F-KWcdvH_63iXjZf0cgCfDiFX6XXW3qw6rlR8DZrFpQ/edit#gid=219603647).

### Mechanical
The mechanical component of the commutator are as follows:

1. RF Rotary Joint
1. 2x reduction gears (3D-printed)
1. NEMA-11 Stepper Motor
1. Housing (3D-printed)
1. Some fasterners

Mechanical designs are located [here](./mechanical/). STL files for 3D printing
are located in the `stl` subdirectory. Links to purchase each of these
components, including 3D-printed parts, can be found on the BOM.

### Electronics
The board used to control the commutator consists of the following elements:

1. [Teensy 3.2](https://www.pjrc.com/store/teensy32.html) for receiving
   commands and controlling all circuit elements.
1. [TMC2130 Silent Stepper
   Stick](https://www.watterott.com/en/SilentStepStick-TMC2130) for driving the
   motor.
1. Super-capacitor charge system and step-up regulator for providing
   high-current capacity drive to the motor driver directly from USB.
1. RGB indicator LED.
1. Capacitive touch sensors on the back side of the PCB that serve as buttons
   for manual commutator control

Board designs are located [here](./control-board/). Board design file types are
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

## Software/Firmware License
Copyright 2019 Jonathan P. Newman 2019

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
