/*
  Water Leak Containment Station - merged control sketch
  - Button 1 (switchPin): in MANUAL mode, toggles pump on/off
  - Button 2 (modePin): toggles between MANUAL and AUTO
  - Potentiometer: sets pump speed in MANUAL mode
  - JSN-SR04T: measures distance to water surface, drives PI control in AUTO mode
  - OLED: shows current mode + pump speed %

  NOTE: relay polarity assumed HIGH = pump enabled, matching your original code.
  If your relay module is active-LOW, swap HIGH/LOW in runManualControl() and runAutoControl().
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- Pins ----------
const int switchPin = 23;      // manual pump on/off toggle button (existing)
const int modePin   = 19;      // NEW: manual/auto mode toggle button -> wire to a spare GPIO + ground, same style as switchPin
const int relayPin  = 13;
const int pumpPin   = 25;      // PWM -> MOSFET gate
const int potPin    = 32;
const int trigPin   = 5;
const int echoPin   = 18;

// ---------- PWM ----------
const int pwmFreq = 5000;
const int pwmChannel = 0;
const int pwmResolution = 8; // 0-255

// ---------- OLED ----------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------- Button/mode state ----------
bool pumpOn = false;      // manual on/off state, toggled by switchPin
bool autoMode = false;    // false = manual, true = automatic

int stableSwitchState = HIGH;
int lastSwitchReading = HIGH;
unsigned long lastSwitchDebounce = 0;

int stableModeState = HIGH;
int lastModeReading = HIGH;
unsigned long lastModeDebounce = 0;

const unsigned long debounceDelay = 50;

// ---------- Ultrasonic ----------
#define SOUND_SPEED 0.034
float currentDistance = 0;
bool distanceValid = false;
unsigned long lastUltrasonicRead = 0;
const unsigned long ultrasonicInterval = 200; // ms between distance updates

// ---------- PI control (tune these on the bench) ----------
const float setpointDistance = 30.0;  // cm -- distance reading you want to trigger pumping at; measure your bucket/sensor geometry and adjust
const float hysteresis = 3.0;         // cm -- stop pumping once distance exceeds setpoint + hysteresis, prevents chatter right at the boundary
float integralError = 0;
const float Kp = 8.0;
const float Ki = 0.5;
const float integralMax = 100.0;      // anti-windup clamp

int currentDutyCycle = 0; // 0-255, shared by both modes, used for OLED %

unsigned long lastControlUpdate = 0;
const unsigned long controlInterval = 100; // ms, control loop rate

unsigned long lastDisplayUpdate = 0;
const unsigned long displayInterval = 250;

void setup() {
  Serial.begin(115200);

  pinMode(switchPin, INPUT_PULLUP);
  pinMode(modePin, INPUT_PULLUP);
  pinMode(relayPin, OUTPUT);
  pinMode(potPin, INPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  digitalWrite(relayPin, LOW); // start OFF for safety -- you'll toggle it on manually or via auto mode

  ledcAttachChannel(pumpPin, pwmFreq, pwmResolution, pwmChannel);
  ledcWrite(pumpPin, 0);

  Wire.begin(); // default ESP32 I2C pins: SDA=21, SCL=22
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed - check wiring/address");
  }
  display.clearDisplay();
  display.display();
}

// ---- read one ultrasonic pulse, returns -1 on timeout ----
float getSingleDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(20);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 26000);
  if (duration == 0) return -1.0;
  return duration * SOUND_SPEED / 2;
}

// ---- median-of-5 filtered read (same idea as your standalone sketch) ----
float readFilteredDistance() {
  float readings[5];
  int count = 0;
  for (int i = 0; i < 5; i++) {
    float d = getSingleDistance();
    if (d > 0) {
      readings[count++] = d;
    }
    delay(15); // let acoustic echoes die out between pulses
  }
  if (count == 0) return -1.0;
  for (int i = 0; i < count - 1; i++) {
    for (int j = i + 1; j < count; j++) {
      if (readings[i] > readings[j]) {
        float tmp = readings[i]; readings[i] = readings[j]; readings[j] = tmp;
      }
    }
  }
  return readings[count / 2]; // median of valid readings
}

void updateButtons() {
  // --- manual pump on/off toggle ---
  int switchReading = digitalRead(switchPin);
  if (switchReading != lastSwitchReading) {
    lastSwitchDebounce = millis();
  }
  if ((millis() - lastSwitchDebounce) > debounceDelay) {
    if (switchReading != stableSwitchState) {
      stableSwitchState = switchReading;
      if (stableSwitchState == LOW) { // pressed (active LOW, INPUT_PULLUP)
        pumpOn = !pumpOn;
      }
    }
  }
  lastSwitchReading = switchReading;

  // --- mode toggle ---
  int modeReading = digitalRead(modePin);
  if (modeReading != lastModeReading) {
    lastModeDebounce = millis();
  }
  if ((millis() - lastModeDebounce) > debounceDelay) {
    if (modeReading != stableModeState) {
      stableModeState = modeReading;
      if (stableModeState == LOW) { // pressed
        autoMode = !autoMode;
        integralError = 0; // reset PI accumulator on mode switch to avoid a stale kick
      }
    }
  }
  lastModeReading = modeReading;
}

void runManualControl() {
  int potVal = analogRead(potPin);
  int dutycycle = map(potVal, 0, 4095, 0, 255);
  currentDutyCycle = dutycycle;

  digitalWrite(relayPin, pumpOn ? HIGH : LOW);
  ledcWrite(pumpPin, pumpOn ? dutycycle : 0);
}

void runAutoControl() {
  if (!distanceValid) {
    // no good reading recently -> fail safe, pump off
    ledcWrite(pumpPin, 0);
    digitalWrite(relayPin, LOW);
    currentDutyCycle = 0;
    return;
  }

  // positive error = distance smaller than setpoint = water level HIGH = need to pump
  float error = setpointDistance - currentDistance;

  if (error > 0) {
    integralError += error * (controlInterval / 1000.0);
    integralError = constrain(integralError, 0, integralMax);

    float output = Kp * error + Ki * integralError;
    int dutycycle = constrain((int)output, 0, 255);

    digitalWrite(relayPin, HIGH);
    ledcWrite(pumpPin, dutycycle);
    currentDutyCycle = dutycycle;
  } else if (currentDistance > setpointDistance + hysteresis) {
    // level dropped comfortably below setpoint -> stop, reset integral
    integralError = 0;
    digitalWrite(relayPin, LOW);
    ledcWrite(pumpPin, 0);
    currentDutyCycle = 0;
  }
  // else: inside hysteresis band -> leave pump state as-is, avoids chatter at the boundary
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print("Mode: ");
  display.println(autoMode ? "AUTO" : "MANUAL");

  display.setCursor(0, 16);
  int percent = map(currentDutyCycle, 0, 255, 0, 100);
  display.print("Pump: ");
  display.print(percent);
  display.println("%");

  display.setCursor(0, 32);
  if (autoMode) {
    display.print("Dist: ");
    if (distanceValid) {
      display.print(currentDistance, 1);
      display.println("cm");
    } else {
      display.println("no reading");
    }
  } else {
    display.println("(manual: pot)");
  }

  display.display();
}

void loop() {
  updateButtons();

  // ultrasonic update, runs on its own interval so it doesn't block buttons/display every cycle
  if (millis() - lastUltrasonicRead >= ultrasonicInterval) {
    lastUltrasonicRead = millis();
    float d = readFilteredDistance();
    if (d > 20 && d < 450) {
      currentDistance = d;
      distanceValid = true;
    } else {
      distanceValid = false;
    }
  }

  // control update
  if (millis() - lastControlUpdate >= controlInterval) {
    lastControlUpdate = millis();
    if (autoMode) {
      runAutoControl();
    } else {
      runManualControl();
    }
  }

  // display update
  if (millis() - lastDisplayUpdate >= displayInterval) {
    lastDisplayUpdate = millis();
    updateDisplay();
  }

  // debug serial, throttled
  static unsigned long lastSerial = 0;
  if (millis() - lastSerial >= 500) {
    lastSerial = millis();
    Serial.print("Mode: "); Serial.print(autoMode ? "AUTO" : "MANUAL");
    Serial.print(" | Dist: "); Serial.print(distanceValid ? currentDistance : -1);
    Serial.print(" | Duty: "); Serial.println(currentDutyCycle);
  }
}