/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * JPN wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 * ----------------------------------------------------------------------------
 */

#include <EEPROM.h>
#include <AccelStepper.h>
#include <i2c_t3.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <math.h>

// Button read parameters
#define HOLD_MSEC           300
#define TOUCH_MSEC          5

// Stepper parameters
#define DETENTS             200
#define USTEPS_PER_STEP     16
#define USTEPS_PER_REV      (DETENTS * USTEPS_PER_STEP)

// Capacitive touch buttons
#define CAP_TURN_CW         0
#define CAP_MODE_SEL        1
#define CAP_TURN_CCW        15
#define CAP_STOP_GO         17
#define CAP_DELTA           200 // delta capacitance required

// Stepper driver pins
#define MOT_DIR             14
#define MOT_STEP            16
#define MOT_CFG0_MISO       12
#define MOT_CFG1_MOSI       11
#define MOT_CFG2_SCLK       13
#define MOT_CFG3_CS         8 //10 // 8
#define MOT_CFG6_EN         10 // 9 //10

// Power options
#define MOT_POW_EN          22
#define VMID_SEL            20
#define CHARGE_CURR         21
#define nPOW_FAIL           23
#define CHARGE_CURR_THRESH  20 // Charge level required to allow operation. Arbitrary units

// I2C
#define SDA                 18
#define SCL                 19

// RGB Driver
#define IS31_ADDR           0x68
#define IS31_SHDN           5
#define IS31_BM             2

// TMC2130 registers
#define WRITE_FLAG          (1<<7) //write flag
#define READ_FLAG           (0<<7) //read flag
#define REG_GCONF           0x00
#define REG_GSTAT           0x01
#define REG_IHOLD_IRUN      0x10
#define REG_CHOPCONF        0x6C
#define REG_COOLCONF        0x6D
#define REG_DCCTRL          0x6E
#define REG_DRVSTATUS       0x6F

// Settings address start byte
#define SETTINGS_ADDR_START 0

// Controller state
struct Context {
    bool led_on = true;
    bool commutator_en = false;
    float speed_rpm = 100;
    float accel_rpmm = 400;
};

// Holds the current state
Context ctx;

// Save settings flag
bool save_required = false;

// Stepper motor
AccelStepper motor(AccelStepper::DRIVER, MOT_STEP, MOT_DIR);
//StepControl<> motor_ctrl; // Use default settings
double target_turns = 0;

// Global timer
elapsedMillis global_millis;

// Motor update timer
IntervalTimer mot_timer;

// Triggered in case of under current from host
// TODO: use somehow
volatile bool power_failure = false;

// I was touched..., by the power..., on my unit..., in broooooooooooad
// daylight
enum TouchState { untouched, touched, held };

struct TouchSensor {

    // Given last_state and current check, what is the consecutive for the state
    // of this touch sensor?
    TouchState result = untouched;

    int pin = 0;
    int calib_val = 0;
    bool fresh = true;
};

// Touch Sensors
// NB: = {.pin = whatever} -> can't, compiler does not implement
TouchSensor touch_cw;
TouchSensor touch_ccw;
TouchSensor touch_mode;
TouchSensor touch_stopgo;

void calibrate_touch(TouchSensor *sensor, unsigned int msec)
{
    global_millis = 0;

    int k = 0;
    long long val = 0;
    while (global_millis < msec) {

        val += touchRead(sensor->pin);
        k++;
    }

    sensor->calib_val = (int)(val / k);
}

void check_touch(TouchSensor *sensor, unsigned int hold_msec = HOLD_MSEC)
{
    global_millis = 0;
    bool fingy = true;
    int consecutive = 0;
    int k = 0;
    sensor->fresh = false;

    // Check the pad for a min of TOUCH_MSEC and a max of HOLD_MSEC to get
    // consensus on fingy presence
    while ((fingy && global_millis <= hold_msec)
           || global_millis < TOUCH_MSEC) {
        fingy = ((touchRead(sensor->pin) - sensor->calib_val) > CAP_DELTA);
        consecutive += fingy ? 1 : 0;
        k++;
    }

    // Do we deserve an update?
    auto consensus = consecutive > k * 0.9;

    if (global_millis >= hold_msec && consensus) {
        sensor->fresh = sensor->result != held;
        sensor->result = held;
        return;
    }

    // Transitions out of held are forbidden to debounce
    if (consensus && sensor->result != held) {
        sensor->fresh = sensor->result != touched;
        sensor->result = touched;
        return;
    }

    // Do we deserve an update?
    consensus = consecutive < k * 0.1;

    if (consensus) {
        sensor->fresh = sensor->result != untouched;
        sensor->result = untouched;
        return;
    }
}

uint8_t tmc_write(uint8_t cmd, uint32_t data)
{
    uint8_t s;

    digitalWriteFast(MOT_CFG3_CS, LOW);

    s = SPI.transfer(cmd);
    SPI.transfer((data >> 24UL) & 0xFF) & 0xFF;
    SPI.transfer((data >> 16UL) & 0xFF) & 0xFF;
    SPI.transfer((data >> 8UL) & 0xFF) & 0xFF;
    SPI.transfer((data >> 0UL) & 0xFF) & 0xFF;

    digitalWriteFast(MOT_CFG3_CS, HIGH);

    return s;
}

uint8_t tmc_read(uint8_t cmd, uint32_t *data)
{
    uint8_t s;

    tmc_write(cmd, 0UL); // set read address

    digitalWriteFast(MOT_CFG3_CS, LOW);

    s = SPI.transfer(cmd);
    *data = SPI.transfer(0x00) & 0xFF;
    *data <<= 8;
    *data |= SPI.transfer(0x00) & 0xFF;
    *data <<= 8;
    *data |= SPI.transfer(0x00) & 0xFF;
    *data <<= 8;
    *data |= SPI.transfer(0x00) & 0xFF;

    digitalWriteFast(MOT_CFG3_CS, HIGH);

    return s;
}

// Setting state load/save
void load_settings()
{
    int addr = SETTINGS_ADDR_START;

    byte good_settings = 0x0;
    EEPROM.get(addr, good_settings); // check good settings flag
    if (good_settings != 0x12)
        return;

    EEPROM.get(addr += sizeof(Context), ctx);
}

void save_settings()
{
    int addr = SETTINGS_ADDR_START;
    EEPROM.put(addr, 0x12); // good settings flag
    EEPROM.put(addr += sizeof(Context), ctx);
}

// Motor target update. We integrate turns in the target position and apply to
// motor's motion.
void turn_motor(double turns)
{
    // Relative move
    target_turns += turns;
    motor.moveTo(lround(target_turns * (double)USTEPS_PER_REV));
}

// Emergency motor stop/reset
void hard_stop() {
    motor.setAcceleration(1e6);
    motor.stop();
    motor.runToPosition();
    motor.setCurrentPosition(0);
    target_turns = 0.0;
    update_motor_accel();
}

void set_rgb_color(byte r, byte g, byte b)
{
    // Set PWM state
    Wire.beginTransmission(IS31_ADDR);
    Wire.write(0x04);
    Wire.write(r);
    Wire.write(g);
    Wire.write(b);
    Wire.endTransmission();

    // Update PWM
    Wire.beginTransmission(IS31_ADDR);
    Wire.write(0x07);
    Wire.write(0x00);
    Wire.endTransmission();
}

void update_rgb()
{
    if (!ctx.led_on) {
        set_rgb_color(0x00, 0x00, 0x00);
        return;
    }

    if (!ctx.commutator_en) {
        set_rgb_color(255, 0, 0);
        return;
    }

    set_rgb_color(1, 20, 7);
}

void setup_cap_touch()
{
    calibrate_touch(&touch_cw, 10);
    calibrate_touch(&touch_ccw, 10);
    calibrate_touch(&touch_mode, 10);
    calibrate_touch(&touch_stopgo, 10);
}

void setup_rgb()
{
    // I2C for RGB LED
    Wire.begin(I2C_MASTER, 0x00, I2C_PINS_18_19, I2C_PULLUP_EXT, 400000);
    Wire.setDefaultTimeout(200000); // 200ms

    // Enable LED current driver
    pinMode(IS31_SHDN, OUTPUT);
    digitalWriteFast(IS31_SHDN, HIGH);
    delay(1);

    // Set max LED current
    Wire.beginTransmission(IS31_ADDR);
    Wire.write(0x03);
    Wire.write(0x08); // Set max current to 5 mA
    Wire.endTransmission();

    // Default color
    update_rgb();

    // Enable current driver
    Wire.beginTransmission(IS31_ADDR);
    Wire.write(0x00);
    Wire.write(0x20); // Enable current driver
    Wire.endTransmission();
}

void setup_motor()
{
    // Turn on the motor power
    pinMode(MOT_POW_EN, OUTPUT);
    digitalWriteFast(MOT_POW_EN, HIGH);

    // set pins
    pinMode(MOT_CFG6_EN, OUTPUT);
    digitalWriteFast(MOT_CFG6_EN, LOW); // activate driver (LOW active)
    pinMode(MOT_DIR, OUTPUT);
    digitalWriteFast(MOT_DIR, LOW); // LOW or HIGH
    pinMode(MOT_STEP, OUTPUT);
    digitalWriteFast(MOT_STEP, LOW);

    pinMode(MOT_CFG3_CS, OUTPUT);
    digitalWriteFast(MOT_CFG3_CS, HIGH);
    pinMode(MOT_CFG1_MOSI, OUTPUT);
    digitalWriteFast(MOT_CFG1_MOSI, LOW);
    pinMode(MOT_CFG0_MISO, INPUT);
    digitalWriteFast(MOT_CFG0_MISO, HIGH);
    pinMode(MOT_CFG2_SCLK, OUTPUT);
    digitalWriteFast(MOT_CFG2_SCLK, LOW);

    // init SPI
    SPI.begin();
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));

    // TMC2130 config
    // voltage on AIN is current reference
    // Stealthchop is on
    tmc_write(WRITE_FLAG | REG_GCONF, 0x00000003UL);

    // IHOLD=0x10, IRUN=0x10
    tmc_write(WRITE_FLAG | REG_IHOLD_IRUN, 0x00001010UL);

    switch (USTEPS_PER_STEP) {
        case 1:
            tmc_write(WRITE_FLAG|REG_CHOPCONF,   0x08008008ul); //  1 microsteps,
            break;
        case 2:
            tmc_write(WRITE_FLAG|REG_CHOPCONF,   0x07008008UL); //  2 microsteps,
            break;
        case 4:
            tmc_write(WRITE_FLAG|REG_CHOPCONF,   0x06008008UL); //  4 microsteps,
            break;
        case 8:
            tmc_write(WRITE_FLAG|REG_CHOPCONF,   0x05008008UL); //  8 microsteps,
            break;
        case 16:
            tmc_write(WRITE_FLAG|REG_CHOPCONF,   0x04008008UL); // 16 microsteps,
            break;
        case 32:
            tmc_write(WRITE_FLAG|REG_CHOPCONF,   0x03008008UL); // 32 microsteps,
            break;
        case 64:
            tmc_write(WRITE_FLAG | REG_CHOPCONF, 0x02008008UL); // 64 microsteps,
            break;
        case 128:
            tmc_write(WRITE_FLAG|REG_CHOPCONF,   0x01008008UL); //128 microsteps,
            break;
        default:
            tmc_write(WRITE_FLAG|REG_CHOPCONF,   0x08008008ul); //  1 microsteps,
            break;
    }

    // Setup motor driver
    update_motor_speed();
    update_motor_accel();

    // Minimum pulse width
    motor.setMinPulseWidth(3);

    // Setup run() timer
    mot_timer.begin(run_motor_isr, 10);
}

void run_motor_isr()
{
    if (motor.distanceToGo() != 0) {
        motor.run();
    } else {
        target_turns = 0.0; // Take opportunity to reset motor position to 0
        motor.setCurrentPosition(0);
    }
}

void power_fail_isr()
{
    power_failure = true;
}

void setup_power()
{
    pinMode(VMID_SEL, OUTPUT);
    digitalWriteFast(VMID_SEL, HIGH); // 2.7V across each super cap

    pinMode(nPOW_FAIL, INPUT);
    attachInterrupt(nPOW_FAIL, power_fail_isr, FALLING);

    // Turn on breathing mode
    set_rgb_color(255, 0, 0);
    Wire.beginTransmission(IS31_ADDR);
    Wire.write(0x02);
    Wire.write(0x20); // Turn on breathing mode
    Wire.endTransmission();

    // Wait for charge current stabilize and breath LED in meantime
    while (analogRead(CHARGE_CURR) > CHARGE_CURR_THRESH) {
        // Nothing
    }

    Wire.beginTransmission(IS31_ADDR);
    Wire.write(0x02);
    Wire.write(0x00); // Turn on breathing mode
    Wire.endTransmission();
}

void update_motor_speed()
{
    // * 2 is because this is a 2x reduction gear
    auto max_speed = (float)USTEPS_PER_REV * 2 * ctx.speed_rpm / 60.0;
    motor.setMaxSpeed(max_speed);
}

void update_motor_accel()
{
    // * 2 is because this is a 2x reduction gear
    auto a = (float)USTEPS_PER_REV * 2 * ctx.accel_rpmm / 60.0;
    motor.setAcceleration(a);
}

void poll_led()
{
    // Poll the mode button
    check_touch(&touch_mode, 100);

    if (touch_mode.result == held && touch_mode.fresh && ctx.led_on) {
        // If held and fresh, toggle LED
        ctx.led_on = false;
        save_required = true;

    } else if (touch_mode.result == held && touch_mode.fresh && !ctx.led_on) {
        // If LED is off and touched and fresh, toggle LED
        ctx.led_on = true;
        save_required = true;
    }
}

void poll_turns()
{
    // If the commutator is not enabled, or under pure remote control, then
    // these buttons can't do anything
    if (!ctx.commutator_en)
        return;

    // Poll the cw button
    check_touch(&touch_cw, 100);
    if (touch_cw.result == held) {

        // If the motor is turning, stop it
        hard_stop();

        // Set huge target
        motor.move(10e6);

        while (touch_cw.result == held)
            check_touch(&touch_cw);

        // Set all targets to 0 because we are overriding
        // and Disable driver
        hard_stop();

        return;
    }

    // Poll the ccw button
    check_touch(&touch_ccw, 100);
    if (touch_ccw.result == held) {

        // If the motor is turning, stop it
        hard_stop();

        // Set huge targetr
        motor.move(-10e6);

        while (touch_ccw.result == held)
            check_touch(&touch_ccw);

        // Set all targets to 0 because we are overriding
        // and Disable driver
        hard_stop();

        return;
    }
}

void poll_stop_go()
{
    check_touch(&touch_stopgo, 100);

    Serial.print("Button state: ");
    Serial.println(touch_stopgo.result);

    if (touch_stopgo.result && touch_stopgo.fresh
        && ctx.commutator_en) { // Touch or hold can turn off the motor
        ctx.commutator_en = false;
        save_required = true;

        // Hard disable/reset on motor
        hard_stop();

    } else if (touch_stopgo.result == held && touch_stopgo.fresh
        && !ctx.commutator_en) { // Long hold required turn on the motor after disable

        ctx.commutator_en = true;
        save_required = true;
    }
}

void setup()
{
    Serial.begin(9600);

    // NB: see note at declaration
    touch_cw.pin = CAP_TURN_CW;
    touch_ccw.pin = CAP_TURN_CCW;
    touch_mode.pin = CAP_MODE_SEL;
    touch_stopgo.pin = CAP_STOP_GO;

    // Load parameters from last use
    load_settings();

    setup_rgb(); // Must come first, used by all that follow
    setup_power();
    setup_cap_touch();
    setup_motor();
}

void loop()
{
    // Poll the stop/go button and update on change
    poll_stop_go();

    // Poll the mode button and update on change
    poll_led();

    // Poll manual turn buttons
    poll_turns();

    // Memory pool for JSON object tree
    if (Serial.available()) {

        StaticJsonBuffer<200> buff;
        JsonObject &root = buff.parseObject(Serial);

        if (root.success()) {

            if (root.containsKey("enable")) {
                ctx.commutator_en = root["enable"].as<bool>();
                if (!ctx.commutator_en)
                    hard_stop();
                save_required = true;
            }

            if (root.containsKey("speed")) {

                auto rpm = root["speed"].as<float>();

                // Bound speed
                if (rpm > 0 && rpm <= 1000)
                    ctx.speed_rpm = rpm;
                else if (rpm > 1000)
                    ctx.speed_rpm = 1000;

                update_motor_speed();
                save_required = true;
            }

            if (root.containsKey("accel")) {

                auto rpmm = root["accel"].as<float>();

                // Bound speed
                if (rpmm > 0 && rpmm <= 1000)
                    ctx.accel_rpmm = rpmm;
                else if (rpmm > 1000)
                    ctx.accel_rpmm = 1000;

                update_motor_accel();
                save_required = true;
            }

            if (root.containsKey("led")) {
                ctx.led_on = root["led"].as<bool>();
                save_required = true;
            }

            if (root.containsKey("turn") && ctx.commutator_en) {
                turn_motor(2 * root["turn"].as<double>()); // 2 * for reduction gear
            }
        }
    }

    // Update rgb
    update_rgb();

    if (save_required) {
       save_settings();
       save_required = false;
    }
}
