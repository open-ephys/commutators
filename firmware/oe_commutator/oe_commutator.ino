// This script is used to control the open ephys commutator. It is very shitty. It was
// written by JPN while mildly drunk.

#include <EEPROM.h>
#include <StepControl.h>
#include <i2c_t3.h>
#include <SPI.h>
#include <ArduinoJson.h>

// Button read parameters
#define HOLD_MSEC           300
#define TOUCH_MSEC          5

// Stepper parameters
#define DETENTS             200
#define USTEPS_PER_REV      (DETENTS * 64)

// Capacitive touch buttons
#define CAP_TURN_CW         0
#define CAP_MODE_SEL        1
#define CAP_TURN_CCW        15
#define CAP_STOP_GO         17
#define CAP_DELTA           300 // delta capacitance required

// Stepper driver pins
#define MOT_DIR             14
#define MOT_STEP            16
#define MOT_CFG0_MISO       12
#define MOT_CFG1_MOSI       11
#define MOT_CFG2_SCLK       13
#define MOT_CFG3_CS         8
#define MOT_CFG6_EN         10
#define MOT_CFG6_EN         10
#define MOT_BASE_RPM        100.0

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

// Operation modes
enum Mode { manual, automatic };

// Controller state
struct Context {
    Mode mode = manual;
    bool led_on = true;
    bool commutator_en = false;
    float speed_scale = 1.0;
};

// Holds the current state
Context ctx;

// Save settings flag
bool save_required = false;

// Stepper motor
Stepper motor(MOT_STEP, MOT_DIR);
StepControl<> motor_ctrl; // Use default settings
float target_turns = 0;

// Global timer
elapsedMillis global_millis;

// Triggered in case of under current from host
// TODO: use somehow
volatile bool power_failure = false;

// I was touched..., by the power..., in broooooooooooad daylight
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

void calibrate_touch(TouchSensor *sensor, int msec)
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

void check_touch(TouchSensor *sensor)
{
    // TODO: update and use sensor->last_state to prevent glitching

    global_millis = 0;
    bool fingy = true;
    int consecutive = 0;
    int k = 0;
    sensor->fresh = false;

    // Check the pad for a min of TOUCH_MSEC and a max of HOLD_MSEC to get
    // consensus on fingy presence
    while ((fingy && global_millis <= HOLD_MSEC)
           || global_millis < TOUCH_MSEC) {
        fingy = ((touchRead(sensor->pin) - sensor->calib_val) > CAP_DELTA);
        consecutive += fingy ? 1 : 0;
        k++;
    }

    // Do we deserve an update?
    auto consensus = consecutive > k * 0.9;

    if (global_millis >= HOLD_MSEC && consensus) {
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

int turn_motor(float turns)
{
    // Make sure driver is enabled
    digitalWriteFast(MOT_CFG6_EN, LOW);

    target_turns += turns;
    motor_ctrl.stopAsync();

    motor.setTargetAbs((long)(target_turns * (float)USTEPS_PER_REV));
    motor_ctrl.moveAsync(motor);
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

    switch (ctx.mode) {
        case manual:
            set_rgb_color(80, 0, 40);
            break;
        case automatic:
            set_rgb_color(1, 30, 10);
            break;
    }
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
    digitalWriteFast(MOT_CFG6_EN, HIGH); // deactivate driver (LOW active)
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

    // tmc_write(WRITE_FLAG | REG_CHOPCONF,
    // 0x00008008UL); // native 256 microsteps, MRES=0, TBL=1=24, TOFF=8
    // tmc_write(WRITE_FLAG|REG_CHOPCONF,   0x01008008UL); //128 microsteps,
    // MRES=0, TBL=1=24, TOFF=8
    tmc_write(WRITE_FLAG | REG_CHOPCONF, 0x02008008UL); // 64 microsteps,
    // MRES=0, TBL=1=24, TOFF=8
    // tmc_write(WRITE_FLAG|REG_CHOPCONF,   0x03008008UL); // 32 microsteps,
    // MRES=0, TBL=1=24, TOFF=8
    // tmc_write(WRITE_FLAG|REG_CHOPCONF,   0x04008008UL); // 16 microsteps,
    // MRES=0, TBL=1=24, TOFF=8
    // tmc_write(WRITE_FLAG|REG_CHOPCONF,   0x05008008UL); //  8 microsteps,
    // MRES=0, TBL=1=24, TOFF=8
    // tmc_write(WRITE_FLAG|REG_CHOPCONF,   0x06008008UL); //  4 microsteps,
    // MRES=0, TBL=1=24, TOFF=8
    // tmc_write(WRITE_FLAG|REG_CHOPCONF,   0x07008008UL); //  2 microsteps,
    // MRES=0, TBL=1=24, TOFF=8
    // tmc_write(WRITE_FLAG|REG_CHOPCONF,   0x08008008UL); //  1 microsteps,
    // MRES=0, TBL=1=24, TOFF=8

    // Setup motor driver
    update_motor_speed();
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
    Wire.beginTransmission(IS31_ADDR);
    Wire.write(0x02);
    Wire.write(0x20); // Turn on breathing mode
    Wire.endTransmission();

    // Wait for charge current stabilize and breath LED in meantime
    while (analogRead(CHARGE_CURR) > CHARGE_CURR_THRESH) {
        // Nothing
    }

    // We wanna see it blink at least a couple times, even if supercaps don't
    // leak :)
    delay(1000);

    Wire.beginTransmission(IS31_ADDR);
    Wire.write(0x02);
    Wire.write(0x00); // Turn on breathing mode
    Wire.endTransmission();
}

void update_motor_speed()
{
    auto max_speed = (float)USTEPS_PER_REV * MOT_BASE_RPM * ctx.speed_scale / 60.0;
    motor.setMaxSpeed(max_speed);
    motor.setAcceleration(max_speed * 5);
}

void poll_mode()
{
    // Poll the mode button
    check_touch(&touch_mode);

    if (touch_mode.result == held && touch_mode.fresh) {
        // If held and fresh, toggle LED
        ctx.led_on = !ctx.led_on;
        save_required = true;

    } else if (touch_mode.result == touched && touch_mode.fresh) {
        // If touched and fresh, toggle operation mode
        ctx.mode = ctx.mode == manual ? automatic : manual;
        save_required = true;
    }
}

void poll_turns()
{
    // If the commutator is not enabled, then these buttons can't do anything
    if (!ctx.commutator_en)
        return;

    // Poll the cw button
    check_touch(&touch_cw);
    if (touch_cw.result == held) {

        // Make sure driver is enabled
        digitalWriteFast(MOT_CFG6_EN, LOW);

        motor_ctrl.emergencyStop();
        motor.setTargetRel(1e6);
        motor_ctrl.moveAsync(motor);

        while (touch_cw.result == held)
            check_touch(&touch_cw);

        // Set all targets to 0 because we are overriding
        motor_ctrl.emergencyStop();
        motor.setPosition(0);
        target_turns = 0;

        // Disable driver
        digitalWriteFast(MOT_CFG6_EN, HIGH);

        return;
    }

    // Poll the ccw button
    check_touch(&touch_ccw);
    if (touch_ccw.result == held) {

        // Make sure driver is enabled
        digitalWriteFast(MOT_CFG6_EN, LOW);

        motor_ctrl.emergencyStop();
        motor.setTargetRel(-1e6);
        motor_ctrl.moveAsync(motor);

        while (touch_ccw.result == held)
            check_touch(&touch_ccw);

        // Set all targets to 0 because we are overriding
        motor_ctrl.emergencyStop();
        motor.setPosition(0);
        target_turns = 0;

        // Disable driver
        digitalWriteFast(MOT_CFG6_EN, HIGH);

        return;
    }
}

void poll_stop_go()
{
    check_touch(&touch_stopgo);
    if (touch_stopgo.result && touch_stopgo.fresh) {

        ctx.commutator_en = !ctx.commutator_en;
        save_required = true;

        // Disable the motor
        if (!ctx.commutator_en)
            digitalWriteFast(MOT_CFG6_EN, HIGH);
    }
    // Renable will be taken care of by turn_motor()
}

void setup()
{
    Serial.begin(9600);

    // NB: see note at declaration
    touch_cw.pin = CAP_TURN_CW;
    touch_ccw.pin = CAP_TURN_CCW;
    touch_mode.pin = CAP_MODE_SEL;
    touch_stopgo.pin = CAP_STOP_GO;

    setup_rgb(); // Must come first, used by all that follow
    setup_power();
    setup_cap_touch();
    setup_motor();

    // Load parameters from last use
    load_settings();
}

void loop()
{
    // Poll the stop/go button and update on change
    poll_stop_go();

    // Poll the mode button and update on change
    poll_mode();

    // Poll manual turn buttons
    poll_turns();

    // Memory pool for JSON object tree
    if (Serial.available()) {

        StaticJsonBuffer<200> buff;
        JsonObject &root = buff.parseObject(Serial);

        if (root.success()) {

            if (root.containsKey("enable")) {
                ctx.commutator_en = root["enable"].as<bool>();
                save_required = true;
            }

            if (root.containsKey("speed")) {
                ctx.speed_scale = root["speed"].as<float>();
                update_motor_speed();
                save_required = true;
            }

            if (root.containsKey("led_on")) {
                ctx.led_on = root["led_on"].as<bool>();
                save_required = true;
            }

            if (root.containsKey("mode")) {
                ctx.mode = root["mode"].as<int>() == 0 ? manual : automatic;
                save_required = true;
            }

            if (root.containsKey("turn") && ctx.mode == automatic && ctx.commutator_en)
                turn_motor(root["turn"].as<float>());
        }
    }

    // Update rgb
    update_rgb();

    // If we are not using the motor, shut it down
    if (!motor_ctrl.isRunning())
        digitalWriteFast(MOT_CFG6_EN, HIGH);

    if (save_required) {
       save_settings();
       save_required = false;
    }
}
