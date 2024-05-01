#include <AccelStepper.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <SPI.h>
#include <i2c_t3.h>
#include <math.h>

/////////////////////////////////////////////////////////////////////////////////
// OPTIONS
/////////////////////////////////////////////////////////////////////////////////

// Firmware Version
#define FIRMWARE_VER        "1.6.0"

// 1. Uncomment to continuously dump button press data over Serial
//#define DEBUG

// 2. Select teensy type
#define TEENSYLC
//#define TEENSY32

// 3. Select a commutator type by uncommenting one of the following
//#define COMMUTATOR_TYPE     "SPI Rev. A"
//#define GEAR_RATIO          1.77777777778

#define COMMUTATOR_TYPE     "Single Channel Coax Rev. A"
#define GEAR_RATIO          2.0

//#define COMMUTATOR_TYPE     "Dual Channel Coax Rev. A"
//#define GEAR_RATIO          3.06666666667

/////////////////////////////////////////////////////////////////////////////////
// CONSTANTS
/////////////////////////////////////////////////////////////////////////////////

// Stepper parameters
#define DETENTS             200
#define USTEPS_PER_STEP     8
#define USTEPS_PER_REV      (DETENTS * USTEPS_PER_STEP)
#define MAX_TURNS           (2147483647 / USTEPS_PER_REV / GEAR_RATIO)

// Turn speed and acceleration
#define SPEED_RPM           100
#define ACCEL_RPMM          150
#define MAX_SPEED_SPS       ((float)USTEPS_PER_REV * GEAR_RATIO * SPEED_RPM / 60.0)
#define MAX_ACCEL_SPSS      ((float)USTEPS_PER_REV * GEAR_RATIO * ACCEL_RPMM / 60.0)

// Motor driver parameters
#define MOTOR_POLL_T_US     20

// Capacitive touch buttons
#define CAP_TURN_CW         0
#define CAP_MODE_SEL        1
#define CAP_TURN_CCW        15
#define CAP_STOP_GO         17

// Stepper driver pins
#define MOT_DIR             14
#define MOT_STEP            16
#define MOT_CFG0_MISO       12
#define MOT_CFG1_MOSI       11
#define MOT_CFG2_SCLK       13
#define MOT_CFG3_CS         10
#define MOT_CFG6_EN         9

// Power options
#define MOT_POW_EN          22
#define VMID_SEL            20
#define CHARGE_CURR         21
#define nPOW_FAIL           23
#define CHARGE_CURR_THRESH  0.06 // Super capacitor charge current, in Amps, that must be reached to transistion to normal operation.
#define RPROG               2000.0 // Charge current programming resistor (Ohms)
#define CODE_TO_AMPS        (3.3 / 1024 * 1000.0 / RPROG)

// I2C
#define SDA                 18
#define SCL                 19

// RGB Driver
#define IS31_ADDR           0x68
#define IS31_SHDN           5
#define IS31_BM             2

// TMC2130 registers
#define WRITE_FLAG          (1<<7)
#define READ_FLAG           (0<<7)
#define REG_GCONF           0x00
#define REG_GSTAT           0x01
#define REG_IHOLD_IRUN      0x10
#define REG_CHOPCONF        0x6C
#define REG_COOLCONF        0x6D
#define REG_DCCTRL          0x6E
#define REG_DRVSTATUS       0x6F
#define REG_PWMCONF         0x70

// Settings address start byte
#define SETTINGS_ADDR_START 0
#define CAP_ADDR_START      0x0F

// Controller state
struct Context {
    int led_on = 1; // Using a bool results in extemely bizarre behavior
    int commutator_en = 1;  // Using a bool results in extemely bizarre behavior
};

// Holds the current state
Context ctx;

// Save settings flag
int save_required = 0;

// Stepper motor
AccelStepper motor(AccelStepper::DRIVER, MOT_STEP, MOT_DIR);
double target_turns = 0;

// Motor update timer
IntervalTimer mot_timer;

// I was touched..., by the power..., on my unit..., in broooooooooooad
// daylight
enum TouchState { untouched, held };

struct TouchSensor {

    // Result
    TouchState result = untouched;

    // Physical constants
    const int pin = 0;

    // Algorithm state
    int last = 0;
    float i = 0;
    const int i_thresh = 400;
    int d_thresh = 0;
    int fresh = 1;
};

// Touch Sensors with pre-measured estimates for capacitance of each button
#ifdef TEENSYLC
TouchSensor touch_cw {.pin = CAP_TURN_CW, .last = 12000, .d_thresh = 8};
TouchSensor touch_ccw {.pin = CAP_TURN_CCW, .last = 12100, .d_thresh = 8};
TouchSensor touch_mode {.pin = CAP_MODE_SEL, .last = 2700, .d_thresh = 2};
TouchSensor touch_stopgo {.pin = CAP_STOP_GO, .last = 10700, .d_thresh = 10};
#endif

#ifdef TEENSY32
TouchSensor touch_cw {.pin = CAP_TURN_CW, .last = 12000, .d_thresh = 15};
TouchSensor touch_ccw {.pin = CAP_TURN_CCW, .last = 12100, .d_thresh = 15};
TouchSensor touch_mode {.pin = CAP_MODE_SEL, .last = 2700, .d_thresh = 3};
TouchSensor touch_stopgo {.pin = CAP_STOP_GO, .last = 10700, .d_thresh = 15};
#endif

void check_touch(TouchSensor *sensor)
{
    int m = touchRead(sensor->pin);
    int d =  m - sensor->last;
    sensor->last = m;
    auto last_state = sensor->result;

    if (abs(d) > sensor->d_thresh)
        sensor->i += (float)d;

    if (sensor->i > sensor->i_thresh)
    {
        sensor->result = held;
    } else {
        sensor->i *=  0.9;
        sensor->result = untouched;
    }

    sensor->fresh = last_state != sensor->result;
}

uint8_t tmc_write(uint8_t cmd, uint32_t data)
{
    uint8_t s;

    digitalWriteFast(MOT_CFG3_CS, LOW);

    s = SPI.transfer(cmd);
    SPI.transfer((data >> 24UL) & 0xFF);
    SPI.transfer((data >> 16UL) & 0xFF);
    SPI.transfer((data >> 8UL) & 0xFF);
    SPI.transfer((data >> 0UL) & 0xFF);

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

    EEPROM.get(addr += sizeof(int), ctx);
}

// Save enable and LED state
void save_settings()
{
    int addr = SETTINGS_ADDR_START;
    EEPROM.put(addr, 0x12); // good settings flag
    EEPROM.put(addr += sizeof(int), ctx);
}

// Motor target update. We integrate turns in the target position and apply to
// motor's motion.
void turn_commutator(double turns)
{
    // Invalid request
    if (abs(turns) > MAX_TURNS)
        return; // Failure, cant turn this far

    // Relative move
    target_turns += turns;

    if (abs(target_turns) < MAX_TURNS)
    {
        motor.moveTo(lround(target_turns * (double)USTEPS_PER_REV * GEAR_RATIO));
    } else {
        // Deal with very unlikely case of overflow
        soft_stop();
        turn_commutator(turns); // Restart this routine now that position has been zeroed
    }
}

// Emergency motor stop/reset
void hard_stop()
{
    motor.setAcceleration(1e6);
    motor.stop();
    motor.runToPosition();
    motor.setCurrentPosition(0);
    target_turns = 0.0;
    motor.setAcceleration(MAX_ACCEL_SPSS);
}

void soft_stop()
{
    motor.setCurrentPosition(0);
    target_turns = 0.0;
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

void setup_rgb()
{
    // I2C for RGB LED
    Wire.begin(I2C_MASTER, 0x00, I2C_PINS_18_19, I2C_PULLUP_EXT, 400000);
    Wire.setDefaultTimeout(200000); // 200ms

    // Enable LED current driver
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

void setup_io()
{
    pinMode(MOT_CFG6_EN, OUTPUT);
    pinMode(MOT_DIR, OUTPUT);
    pinMode(MOT_STEP, OUTPUT);
    pinMode(MOT_CFG3_CS, OUTPUT);
    pinMode(MOT_CFG1_MOSI, OUTPUT);
    pinMode(MOT_CFG0_MISO, INPUT);
    pinMode(MOT_CFG2_SCLK, OUTPUT);

    pinMode(MOT_POW_EN, OUTPUT);
    pinMode(VMID_SEL, OUTPUT);
    pinMode(nPOW_FAIL, INPUT);

    pinMode(IS31_SHDN, OUTPUT);
}

inline void motor_driver_en(int enable)
{
    digitalWriteFast(MOT_CFG6_EN, enable ? LOW : HIGH); // Inactivate driver (LOW active)
}

void setup_motor()
{
    // Default state
    motor_driver_en(0);
    digitalWriteFast(MOT_DIR, LOW); // LOW or HIGH
    digitalWriteFast(MOT_STEP, LOW);

    digitalWriteFast(MOT_CFG3_CS, HIGH);
    digitalWriteFast(MOT_CFG1_MOSI, LOW);
    digitalWriteFast(MOT_CFG0_MISO, HIGH);
    digitalWriteFast(MOT_CFG2_SCLK, LOW);

    // init SPI
    SPI.begin();
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));

    // TMC2130 config
    // voltage on AIN is current reference
    // Stealthchop is on
    tmc_write(WRITE_FLAG | REG_GCONF, 0x00000007UL);

    // Configure steathchip
    // PWM_GRAD = 0x0F
    // PWM_AMPL = 0xFF
    // pwm_autoscale = 0x01
    tmc_write(WRITE_FLAG | REG_PWMCONF, 0x00040FFFUL);

    // IHOLD = 0x0A
    // IRUN = 0x1F (Max)
    // IHOLDDELAY = 0x06
    tmc_write(WRITE_FLAG | REG_IHOLD_IRUN, 0b01100001111100011111UL); // 0x00_04_1F_UL);

    switch (USTEPS_PER_STEP) {
        case 1:
            tmc_write(WRITE_FLAG | REG_CHOPCONF, 0x08008008UL); //  1 microsteps,
            break;
        case 2:
            tmc_write(WRITE_FLAG | REG_CHOPCONF, 0x07008008UL); //  2 microsteps,
            break;
        case 4:
            tmc_write(WRITE_FLAG | REG_CHOPCONF, 0x06008008UL); //  4 microsteps,
            break;
        case 8:
            tmc_write(WRITE_FLAG | REG_CHOPCONF, 0x05008008UL); //  8 microsteps,
            break;
        case 16:
            tmc_write(WRITE_FLAG | REG_CHOPCONF, 0x04008008UL); // 16 microsteps,
            break;
        case 32:
            tmc_write(WRITE_FLAG | REG_CHOPCONF, 0x03008008UL); // 32 microsteps,
            break;
        case 64:
            tmc_write(WRITE_FLAG | REG_CHOPCONF, 0x02008008UL); // 64 microsteps,
            break;
        case 128:
            tmc_write(WRITE_FLAG | REG_CHOPCONF, 0x01008008UL); //128 microsteps,
            break;
        default:
            tmc_write(WRITE_FLAG | REG_CHOPCONF, 0x08008008ul); //  1 microsteps,
            break;
    }

    // Set speed and accel
    motor.setMaxSpeed(MAX_SPEED_SPS);
    motor.setAcceleration(MAX_ACCEL_SPSS);

    // Minimum pulse width
    motor.setMinPulseWidth(3);

    // Setup run() timer
    mot_timer.begin(run_motor_isr, MOTOR_POLL_T_US);

    // Activate motor
    motor_driver_en(1);
}

void run_motor_isr()
{
    if (motor.distanceToGo() != 0) {
        motor.run();
    }
}

void setup_power()
{
    // Turn on breathing mode
    set_rgb_color(255, 0, 0);
    Wire.beginTransmission(IS31_ADDR);
    Wire.write(0x02);
    Wire.write(0x20); // Turn on breathing mode
    Wire.endTransmission();

    // Turn on the motor power
    digitalWriteFast(MOT_POW_EN, HIGH);
    delay(100);

    // NB: Set 2.7V across each super cap
    digitalWriteFast(VMID_SEL, HIGH);

    // Wait for charge current stabilize and breath LED in meantime
    while (digitalRead(nPOW_FAIL) == LOW)
      delay(10);

    Wire.beginTransmission(IS31_ADDR);
    Wire.write(0x02);
    Wire.write(0x00); // Turn off breathing mode
    Wire.endTransmission();
}

inline float charge_current()
{
    return CODE_TO_AMPS * analogRead(CHARGE_CURR);
}

void poll_led()
{
    // Poll the mode button
    check_touch(&touch_mode);

    if (touch_mode.result && touch_mode.fresh) { // If touched and fresh, toggle LED
        save_required = 1;
        ctx.led_on = !ctx.led_on;
    }
}

void poll_turns()
{
    // Poll the buttons to update their state
    check_touch(&touch_cw);
    check_touch(&touch_ccw);
    
    // If the commutator is not enabled then these buttons can't do anything
    if (!ctx.commutator_en)
        return;

    if (touch_cw.result && touch_cw.fresh) {

        // If the motor is turning, stop it
        soft_stop();

        // Set huge target
        motor.move(10e6);

    } else if (touch_cw.fresh) {

        // Button released
        soft_stop();

    } else if (touch_ccw.result == held && touch_ccw.fresh) {

        // If the motor is turning, stop it
        soft_stop();

        // Set huge targetr
        motor.move(-10e6);

    } else if (touch_ccw.fresh) {

        // Button released
        soft_stop();
    }
}

void poll_stop_go()
{
    check_touch(&touch_stopgo);

    if (touch_stopgo.result && touch_stopgo.fresh) {

        if (ctx.commutator_en) {

            ctx.commutator_en = 0;
            save_required = 1;

            // Hard disable/reset on motor
            hard_stop();

            // Allow axel to turn freely
            motor_driver_en(0);

        } else if (!ctx.commutator_en) {

            ctx.commutator_en = 1;
            save_required = 1;

            // Enable driver
            motor_driver_en(1);
        }
    }
}

void setup()
{
    Serial.begin(9600);

    // Setup pin directions
    setup_io();

    // Load parameters from last use
    load_settings();

    // Shutdown the motor driver so that it does not the supercap charge current
    motor_driver_en(0);

    // Setup each block of the board
    setup_rgb(); // Must come first, used by all that follow
    setup_power();
    setup_motor();

    // Set RGB once
    update_rgb();
}

void loop()
{

#ifdef DEBUG

    Serial.print(touch_cw.i_thresh);
    Serial.print(",");
    Serial.print(touch_cw.i);
    Serial.print(",");
    Serial.print(touch_ccw.i);
    Serial.print(",");
    Serial.print(touch_mode.i);
    Serial.print(",");
    Serial.println(touch_stopgo.i);

#endif

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
                {
                    hard_stop();
                    motor_driver_en(0);
                } else {
                    motor_driver_en(1);
                }

                save_required = 1;
            }

            if (root.containsKey("led")) {
                ctx.led_on = root["led"].as<bool>();
                save_required = 1;
            }

            if (root.containsKey("turn") && ctx.commutator_en) {
                double turns = root["turn"].as<double>();
                if (!isnan(turns))
                  turn_commutator(turns);  
            }

            if (root.containsKey("print")) {

                StaticJsonBuffer<256> jbuff;
                JsonObject& doc = jbuff.createObject();

                doc["type"] = COMMUTATOR_TYPE;
                doc["firmware"] = FIRMWARE_VER;
#ifdef TEENSYLC
                doc["teensy"] = "lc";
#endif
#ifdef TEENSY32
                doc["teensy"] = "3.2";
#endif            
                doc["led"] = ctx.led_on;
                doc["enable"] = ctx.commutator_en;
                doc["steps_to_go"] = motor.distanceToGo();
                doc["target_steps"] = motor.targetPosition();
                doc["target_turns"] = target_turns;
                doc["max_turns"] = MAX_TURNS;
                doc["motor_running"] = motor.distanceToGo() != 0;
                doc["charge_curr"] = charge_current();
                doc["power_good"] = digitalReadFast(nPOW_FAIL) == HIGH;
                doc.printTo(Serial);
                Serial.print("\n");
            }
        }
    }

    if (save_required) {

      // Update rgb
       update_rgb();
       save_settings();
       save_required = 0;
    }
}
