/*
  CIRCUIT QUEST / WIRESMART - ARDUINO UNO FIRMWARE (version 2)

  Upload this sketch to the Arduino Uno. It communicates with the WireSmart
  web game (or the companion CircuitQuest.cpp program) over USB serial at
  9600 baud.

  Serial protocol (newline terminated):
    PC -> PING             Arduino -> PONG
    PC -> VERSION          Arduino -> VERSION,2
    PC -> READ:LED         Arduino -> LED,<average ADC reading 0..1023>
                                      or FLOAT,LED if A0 is not connected
    PC -> READ:BUTTON      Arduino -> BUTTON,<digital reading 0 or 1>
    PC -> READ:POT         Arduino -> POT,<average ADC reading 0..1023>
                                      or FLOAT,POT if A1 is not connected
    PC -> READ:RESISTORS   Arduino -> RESISTORS,<average ADC reading 0..1023>
                                      or FLOAT,RESISTORS if A2 is not connected
    PC -> PROBE            Arduino -> PROBE,<A0 score>,<A1 score>,<A2 score>
                           (calibration aid: scores near 0 = connected,
                            large scores = floating; see FLOAT_THRESHOLD)

  Fixed tester pin assignment:
    A0 = LED test junction
    D2 = pushbutton input, configured INPUT_PULLUP
    A1 = potentiometer wiper
    A2 = series/parallel resistor test junction
    A3, A4 = UNUSED. Leave them unconnected: the floating-input test briefly
             drives them as outputs.

  Why FLOAT exists:
    An analog input with nothing attached "floats". It does not read 0; it
    reads whatever charge is left over from the previous channel or from stray
    pickup, which can land inside a level's expected range and give a false
    PASS. Before every analog reading the sketch therefore checks that the pin
    is actually driven by a low-impedance circuit.

  Safety / setup:
    - Use the Arduino Uno's 5 V and GND rails only for these beginner circuits.
    - Never apply an external voltage to A0/A1/A2 or D2.
    - Do not connect an Arduino output directly to a circuit node that might
      be shorted to another supply or ground. (Only the unused A3/A4 pins are
      ever driven, and only for a fraction of a millisecond.)
    - Close the Arduino IDE Serial Monitor before running the game.
*/

const int LED_SENSE_PIN = A0;
const int BUTTON_PIN = 2;
const int POT_SENSE_PIN = A1;
const int RESISTOR_SENSE_PIN = A2;

// Unused analog pins used only to "pre-charge" the ADC sample-and-hold
// capacitor to a known low / high level for the floating-input test.
const int GHOST_LOW_PIN = A3;
const int GHOST_HIGH_PIN = A4;

const int FIRMWARE_VERSION = 2;
const unsigned long SERIAL_BAUD = 9600;
const int ANALOG_SAMPLES = 16;
const int ANALOG_SAMPLE_DELAY_MS = 2;

// A connected node follows its own voltage whatever the ADC held before, so
// the "pre-charged high" and "pre-charged low" readings agree within a few
// counts. A floating pin follows the pre-charge instead. If the average
// difference exceeds this many counts the pin is reported as floating.
// Typical connected circuits here score 0-10. Run PROBE with nothing
// connected and with a circuit built to check the margin on your board.
const int FLOAT_THRESHOLD = 60;
const int FLOAT_ROUNDS = 4;

String commandBuffer;

int readAveragedAnalog(int pin) {
  // Discard the first conversions after switching channel, then settle.
  analogRead(pin);
  analogRead(pin);
  delay(ANALOG_SAMPLE_DELAY_MS);

  long total = 0;
  for (int i = 0; i < ANALOG_SAMPLES; ++i) {
    total += analogRead(pin);
    delay(ANALOG_SAMPLE_DELAY_MS);
  }
  return static_cast<int>(total / ANALOG_SAMPLES);
}

// Returns the average (high pre-charge reading - low pre-charge reading).
// Near 0 = the pin is driven by a circuit. Large = the pin is floating.
int floatScore(int pin) {
  long diffTotal = 0;
  for (int i = 0; i < FLOAT_ROUNDS; ++i) {
    pinMode(GHOST_LOW_PIN, OUTPUT);
    digitalWrite(GHOST_LOW_PIN, LOW);
    analogRead(GHOST_LOW_PIN);        // sample-and-hold capacitor now ~0 V
    int low = analogRead(pin);        // one conversion, deliberately no settling

    pinMode(GHOST_HIGH_PIN, OUTPUT);
    digitalWrite(GHOST_HIGH_PIN, HIGH);
    analogRead(GHOST_HIGH_PIN);       // sample-and-hold capacitor now ~5 V
    int high = analogRead(pin);

    diffTotal += (high - low);
  }
  pinMode(GHOST_LOW_PIN, INPUT);
  pinMode(GHOST_HIGH_PIN, INPUT);
  return static_cast<int>(diffTotal / FLOAT_ROUNDS);
}

int readDebouncedButton() {
  // A short wait reduces the chance of reading contact bounce immediately
  // after the learner presses or releases the tactile pushbutton.
  delay(25);
  int first = digitalRead(BUTTON_PIN);
  delay(10);
  int second = digitalRead(BUTTON_PIN);
  if (first == second) return second;
  return digitalRead(BUTTON_PIN);
}

void printReading(const char* tag, int value) {
  Serial.print(tag);
  Serial.print(',');
  Serial.println(value);
}

// Reads an analog test pin, or reports FLOAT,<tag> if nothing drives it.
void reportAnalog(const char* tag, int pin) {
  if (floatScore(pin) > FLOAT_THRESHOLD) {
    Serial.print("FLOAT,");
    Serial.println(tag);
    return;
  }
  printReading(tag, readAveragedAnalog(pin));
}

void processCommand(String command) {
  command.trim();

  if (command == "PING") {
    Serial.println("PONG");
    return;
  }

  if (command == "VERSION") {
    printReading("VERSION", FIRMWARE_VERSION);
    return;
  }

  if (command == "READ:LED") {
    reportAnalog("LED", LED_SENSE_PIN);
    return;
  }

  if (command == "READ:BUTTON") {
    printReading("BUTTON", readDebouncedButton());
    return;
  }

  if (command == "READ:POT") {
    reportAnalog("POT", POT_SENSE_PIN);
    return;
  }

  if (command == "READ:RESISTORS") {
    reportAnalog("RESISTORS", RESISTOR_SENSE_PIN);
    return;
  }

  if (command == "PROBE") {
    Serial.print("PROBE,");
    Serial.print(floatScore(LED_SENSE_PIN));
    Serial.print(',');
    Serial.print(floatScore(POT_SENSE_PIN));
    Serial.print(',');
    Serial.println(floatScore(RESISTOR_SENSE_PIN));
    return;
  }

  Serial.print("ERR,UNKNOWN_COMMAND,");
  Serial.println(command);
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.begin(SERIAL_BAUD);
  commandBuffer.reserve(48);
  delay(100);
  Serial.println("READY");
}

void loop() {
  while (Serial.available() > 0) {
    char incoming = static_cast<char>(Serial.read());

    if (incoming == '\n') {
      processCommand(commandBuffer);
      commandBuffer = "";
    } else if (incoming != '\r') {
      // Avoid unbounded growth if malformed serial data is received.
      if (commandBuffer.length() < 80) {
        commandBuffer += incoming;
      } else {
        commandBuffer = "";
        Serial.println("ERR,COMMAND_TOO_LONG");
      }
    }
  }
}
