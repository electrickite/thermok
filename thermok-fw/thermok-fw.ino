#include <EEPROM.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MAX31855.h>
#include <Adafruit_SleepyDog.h>

#define LED_PIN        13
#define MAXCS_PIN      8
#define BTN_PIN        7
#define BTNSINK_PIN    5

#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   32
#define TEXT_SIZE       3
#define OLED_RESET      -1
#define SCREEN_ADDRESS  0x3C

#define UNIT_ADDR      0
#define INTERVAL_ADDR  1
#define CAL_ADDR       2
#define CHECK_ADDR     126

#define MODE_SAMPLE    0
#define MODE_INTERVAL  1
#define MODE_CAL       2

#define CELSIUS_UNIT      0
#define FAHRENHEIT_UNIT   1
#define BLINK_DURATION    500
#define DEBOUNCE_DELAY    5
#define BTN_NONE          0
#define BTN_SHORT         1
#define BTN_LONG          2
#define BTN_LONG_DELAY    1100
#define CHECK_VAL         22577
#define WDT_TIMEOUT       4000
#define WDT_RESET_DELAY   1000

Adafruit_MAX31855 thermocouple(MAXCS_PIN);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

uint8_t unit = 0;
uint16_t interval = 1000;
int8_t offset = 0;

uint8_t mode = MODE_SAMPLE;
double temp = 0;
__FlashStringHelper *tempError;

unsigned long previousSampleTime = 0;
unsigned long wdtResetTime = 0;
unsigned long btnLowTime = 0;
unsigned long btnHighTime = 0;
unsigned long btnPressTime = 0;
unsigned long btnReleaseTime = 0;
bool btnPressed = false;

void clearDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);
}

void displayText(double msg) {
  clearDisplay();
  display.print(msg);
  display.display();
}

void displayText(char *msg) {
  clearDisplay();
  display.print(msg);
  display.display();
}

void displayText(const __FlashStringHelper *msg) {
  clearDisplay();
  display.print(msg);
  display.display();
}

void displayTemp() {
  double ftemp;
  clearDisplay();

  if (isnan(temp)) {
    display.print(F("ERR "));
    display.print(tempError);
  } else if (unit == FAHRENHEIT_UNIT) {
    ftemp = ctof(temp);
    display.print(ftemp, ftemp < 100 ? 1 : 0);
    display.print(F(" F"));
  } else {
    display.print(temp, temp < 100 ? 1 : 0);
    display.print(F(" C"));
  }
  display.display();
}

void displayMode() {
  clearDisplay();
  switch (mode) {
  case MODE_INTERVAL:
    display.print(F("INT  "));
    display.print((int)(interval / 1000));
    break;
  case MODE_CAL:
    display.print(F("CAL"));
    if (offset < 0) {
      display.print(F(" "));
    } else {
      display.print(F("  "));
    }
    display.print(offset);
    break;
  }
  display.display();
}

void showSampleIndicator(bool show) {
  digitalWrite(LED_PIN, show ? HIGH : LOW);
  display.fillCircle(124, 28, 2, (show ? SSD1306_WHITE : SSD1306_BLACK));
  display.display();
}

void readTemp() {
  // Initialize variables.
  int i = 0;
  double thermocoupleVoltage= 0;
  double internalVoltage = 0;
  double correctedTemp = 0;
  double totalVoltage = 0;

  // Read the internal temperature of the MAX31855.
  double internalTemp = thermocouple.readInternal();

  // Read the temperature of the thermocouple.
  // This temp is compensated for cold junction temperature.
  double rawTemp = thermocouple.readCelsius();

  // Check for errors before proceeding.
  if (isnan(rawTemp)) {
    temp = NAN;
    Serial.print(F("FAULT: "));
    uint8_t e = thermocouple.readError();
    if (e & MAX31855_FAULT_OPEN) {
      tempError = F("NC");
      Serial.println(F("Thermocouple is open - no connections"));
    } else if (e & MAX31855_FAULT_SHORT_GND) {
      tempError = F("GND");
      Serial.println(F("Thermocouple is short-circuited to GND"));
    } else if (e & MAX31855_FAULT_SHORT_VCC) {
      tempError = F("VCC");
      Serial.println(F("Thermocouple is short-circuited to VCC"));
    } else {
      tempError = F("");
      Serial.println(F("Unknown thermocouple fault"));
    }
    return;
  }

  // Steps 1 & 2. Subtract cold junction temperature from the raw thermocouple temperature.
  thermocoupleVoltage = (rawTemp - internalTemp)*0.041276;  // C * mv/C = mV

  // Step 3. Calculate the cold junction equivalent thermocouple voltage.
  if (internalTemp >= 0) { // For positive temperatures use appropriate NIST coefficients
    // Coefficients and equations available from http://srdata.nist.gov/its90/download/type_k.tab
    double c[] = {-0.176004136860E-01,  0.389212049750E-01,  0.185587700320E-04, -0.994575928740E-07,  0.318409457190E-09, -0.560728448890E-12,  0.560750590590E-15, -0.320207200030E-18,  0.971511471520E-22, -0.121047212750E-25};

    // Count the the number of coefficients. There are 10 coefficients for positive temperatures (plus three exponential coefficients),
    // but there are 11 coefficients for negative temperatures.
    int cLength = sizeof(c) / sizeof(c[0]);

    // Exponential coefficients. Only used for positive temperatures.
    double a0 =  0.118597600000E+00;
    double a1 = -0.118343200000E-03;
    double a2 =  0.126968600000E+03;

    // From NIST: E = sum(i=0 to n) c_i t^i + a0 exp(a1 (t - a2)^2), where E is the thermocouple voltage in mV and t is the temperature in degrees C.
    // In this case, E is the cold junction equivalent thermocouple voltage.
    // Alternative form: C0 + C1*internalTemp + C2*internalTemp^2 + C3*internalTemp^3 + ... + C10*internaltemp^10 + A0*e^(A1*(internalTemp - A2)^2)
    // This loop sums up the c_i t^i components.
    for (i = 0; i < cLength; i++) {
      internalVoltage += c[i] * pow(internalTemp, i);
    }
    // This section adds the a0 exp(a1 (t - a2)^2) components.
    internalVoltage += a0 * exp(a1 * pow((internalTemp - a2), 2));
  }
  else if (internalTemp < 0) { // for negative temperatures
    double c[] = {0.000000000000E+00,  0.394501280250E-01,  0.236223735980E-04, -0.328589067840E-06, -0.499048287770E-08, -0.675090591730E-10, -0.574103274280E-12, -0.310888728940E-14, -0.104516093650E-16, -0.198892668780E-19, -0.163226974860E-22};
    // Count the number of coefficients.
    int cLength = sizeof(c) / sizeof(c[0]);

    // Below 0 degrees Celsius, the NIST formula is simpler and has no exponential components: E = sum(i=0 to n) c_i t^i
    for (i = 0; i < cLength; i++) {
      internalVoltage += c[i] * pow(internalTemp, i) ;
    }
  }

  // Step 4. Add the cold junction equivalent thermocouple voltage calculated in step 3 to the thermocouple voltage calculated in step 2.
  totalVoltage = thermocoupleVoltage + internalVoltage;

  // Step 5. Use the result of step 4 and the NIST voltage-to-temperature (inverse) coefficients to calculate the cold junction compensated, linearized temperature value.
  // The equation is in the form correctedTemp = d_0 + d_1*E + d_2*E^2 + ... + d_n*E^n, where E is the totalVoltage in mV and correctedTemp is in degrees C.
  // NIST uses different coefficients for different temperature subranges: (-200 to 0C), (0 to 500C) and (500 to 1372C).
  if (totalVoltage < 0) { // Temperature is between -200 and 0C.
    double d[] = {0.0000000E+00, 2.5173462E+01, -1.1662878E+00, -1.0833638E+00, -8.9773540E-01, -3.7342377E-01, -8.6632643E-02, -1.0450598E-02, -5.1920577E-04, 0.0000000E+00};

    int dLength = sizeof(d) / sizeof(d[0]);
    for (i = 0; i < dLength; i++) {
      correctedTemp += d[i] * pow(totalVoltage, i);
    }
  }
  else if (totalVoltage < 20.644) { // Temperature is between 0C and 500C.
    double d[] = {0.000000E+00, 2.508355E+01, 7.860106E-02, -2.503131E-01, 8.315270E-02, -1.228034E-02, 9.804036E-04, -4.413030E-05, 1.057734E-06, -1.052755E-08};
    int dLength = sizeof(d) / sizeof(d[0]);
    for (i = 0; i < dLength; i++) {
      correctedTemp += d[i] * pow(totalVoltage, i);
    }
  }
  else if (totalVoltage < 54.886 ) { // Temperature is between 500C and 1372C.
    double d[] = {-1.318058E+02, 4.830222E+01, -1.646031E+00, 5.464731E-02, -9.650715E-04, 8.802193E-06, -3.110810E-08, 0.000000E+00, 0.000000E+00, 0.000000E+00};
    int dLength = sizeof(d) / sizeof(d[0]);
    for (i = 0; i < dLength; i++) {
      correctedTemp += d[i] * pow(totalVoltage, i);
    }
  } else { // NIST only has data for K-type thermocouples from -200C to +1372C. If the temperature is not in that range, set temp to impossible value.
    temp = NAN;
    tempError = F("RNG");
    Serial.print(F("FAULT: Temperature out of range"));
    return;
  }

  temp = correctedTemp + (double) offset;
}

double ctof(double c) {
  return (c * 1.8) + 32.0;
}

void toggleUnit() {
  unit = (unit == CELSIUS_UNIT) ? FAHRENHEIT_UNIT : CELSIUS_UNIT;
  EEPROM.put(UNIT_ADDR, (uint8_t) unit);
}

void modeSample() {
  mode = MODE_SAMPLE;
}

void modeSetInterval(bool change) {
  mode = MODE_INTERVAL;
  if (change) {
    switch (interval) {
    case 1000:
      interval = 2000;
      break;
    case 2000:
      interval = 5000;
      break;
    case 5000:
      interval = 10000;
      break;
    case 10000:
      interval = 15000;
      break;
    case 15000:
      interval = 30000;
      break;
    case 30000:
      interval = 60000;
      break;
    default:
      interval = 1000;
      break;
    }
    EEPROM.put(INTERVAL_ADDR, (uint8_t)(interval / 1000));
  }
  displayMode();
}

void modeSetCal(bool change) {
  mode = MODE_CAL;
  if (change) {
    if (offset >= 30) {
      offset = -30;
    } else {
      offset = offset + 1;
    }
    EEPROM.put(CAL_ADDR, (uint8_t) offset);
  }
  displayMode();
}

void eepromInit() {
  EEPROM.put(UNIT_ADDR, (uint8_t) CELSIUS_UNIT);
  EEPROM.put(INTERVAL_ADDR, (uint8_t) 1);
  EEPROM.put(CAL_ADDR, (int8_t) 0);
  EEPROM.put(CHECK_ADDR, (uint16_t) CHECK_VAL);
}

void eepromLoad() {
  uint8_t interval_sec;

  EEPROM.get(UNIT_ADDR, unit);
  EEPROM.get(INTERVAL_ADDR, interval_sec);
  interval = interval_sec * 1000;
  EEPROM.get(CAL_ADDR, offset);
}

uint8_t checkButton(unsigned long currentTime) {
  uint8_t pressType = BTN_NONE;

  if (btnPressed && btnReleaseTime != btnHighTime && (unsigned long)(currentTime - btnHighTime) > DEBOUNCE_DELAY) {
    btnPressed = false;
    btnReleaseTime = btnHighTime;
    if ((btnReleaseTime - btnPressTime) < BTN_LONG_DELAY) {
      pressType = BTN_SHORT;
    } else {
      pressType = BTN_LONG;
    }
    btnPressTime = btnLowTime;
  } else if (!btnPressed && btnPressTime != btnLowTime && (unsigned long)(currentTime - btnLowTime) > DEBOUNCE_DELAY) {
    btnPressTime = btnLowTime;
    btnPressed = true;
  }

  return pressType;
}

void btnISR() {
  if (digitalRead(BTN_PIN) == LOW) {
    btnLowTime = millis();
  } else {
    btnHighTime = millis();
  }
}

void setup() {
  uint16_t serialCheck = 2000;
  uint16_t eepromCheck;
  tempError = F("");

  // Configure pins
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(BTNSINK_PIN, OUTPUT);
  digitalWrite(BTNSINK_PIN, LOW);
  pinMode(BTN_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), btnISR, CHANGE);

  EEPROM.get(CHECK_ADDR, eepromCheck);
  if (eepromCheck != CHECK_VAL) {
    eepromInit();
  }
  eepromLoad();

  Serial.begin(9600);
  delay(1000);
  while (!Serial && serialCheck > 0) {
    delay(1000);
    serialCheck = serialCheck - 1000;
  }

  Serial.print(F("Initializing display... "));
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("ERROR."));
    while (1) delay(1000);
  }
  clearDisplay();
  display.setTextSize(TEXT_SIZE);
  display.setTextColor(SSD1306_WHITE);
  display.cp437(true);
  Serial.println(F("DONE."));

  Serial.print(F("Initializing sensor... "));
  if (!thermocouple.begin()) {
    Serial.println(F("ERROR."));
    displayText(F("ERR"));
    while (1) delay(1000);
  }
  Serial.println(F("DONE."));

  Serial.print("Initializing watchdog timer... ");
  int wdtMS = Watchdog.enable(WDT_TIMEOUT);
  Serial.print(wdtMS, DEC);
  Serial.println("ms");
}

void loop() {
  unsigned long currentTime = millis();
  uint8_t btn = checkButton(currentTime);

  switch (mode) {
  case MODE_INTERVAL:
    if (btn == BTN_LONG) {
      modeSetCal(false);
    } else if (btn == BTN_SHORT) {
      modeSetInterval(true);
    }
    break;
  case MODE_CAL:
    if (btn == BTN_LONG) {
      modeSample();
    } else if (btn == BTN_SHORT) {
      modeSetCal(true);
    }
    break;
  default:
    if (btn == BTN_LONG) {
      showSampleIndicator(false);
      modeSetInterval(false);
    } else if (btn == BTN_SHORT) {
      toggleUnit();
      displayTemp();
    } else if ((unsigned long)(currentTime - previousSampleTime) >= interval) {
      readTemp();
      if (! isnan(temp)) {
        Serial.print(F("C "));
        Serial.println(temp);
      }
      displayTemp();
      showSampleIndicator(true);
      previousSampleTime = currentTime;
    }
    break;
  }

  if ((unsigned long)(currentTime - previousSampleTime) >= BLINK_DURATION) {
    showSampleIndicator(false);
  }

  if ((unsigned long)(currentTime - wdtResetTime) >= WDT_RESET_DELAY) {
    wdtResetTime = currentTime;
    Watchdog.reset();
  }
}
