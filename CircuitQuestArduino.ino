/*
  CIRCUIT QUEST - ARDUINO UNO FIRMWARE

  Upload this sketch to the Arduino Uno. It communicates with the companion
  CircuitQuest.cpp program over USB serial at 9600 baud.

  Serial protocol (newline terminated):
    PC -> PING             Arduino -> PONG
    PC -> READ:LED         Arduino -> LED,<average ADC reading 0..1023>
    PC -> READ:BUTTON      Arduino -> BUTTON,<digital reading 0 or 1>
    PC -> READ:POT         Arduino -> POT,<average ADC reading 0..1023>
    PC -> READ:RESISTORS   Arduino -> RESISTORS,<average ADC reading 0..1023>

  Fixed tester pin assignment:
    A0 = LED test junction
    D2 = pushbutton input, configured INPUT_PULLUP
    A1 = potentiometer wiper
    A2 = series/parallel resistor test junction

  Safety / setup:
    - Use the Arduino Uno's 5 V and GND rails only for these beginner circuits.
    - Never apply an external voltage to A0/A1/A2 or D2.
    - Do not connect an Arduino output directly to a circuit node that might
      be shorted to another supply or ground.
    - Close the Arduino IDE Serial Monitor before running the C++ game.
*/

const int LED_SENSE_PIN = A0;
const int BUTTON_PIN = 2;
const int POT_SENSE_PIN = A1;
const int RESISTOR_SENSE_PIN = A2;

const unsigned long SERIAL_BAUD = 9600;
const int ANALOG_SAMPLES = 16;
const int ANALOG_SAMPLE_DELAY_MS = 2;

String commandBuffer;

int readAveragedAnalog(int pin) {
  long total = 0;
  for (int i = 0; i < ANALOG_SAMPLES; ++i) {
    total += analogRead(pin);
    delay(ANALOG_SAMPLE_DELAY_MS);
  }
  return static_cast<int>(total / ANALOG_SAMPLES);
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

void processCommand(String command) {
  command.trim();

  if (command == "PING") {
    Serial.println("PONG");
    return;
  }

  if (command == "READ:LED") {
    printReading("LED", readAveragedAnalog(LED_SENSE_PIN));
    return;
  }

  if (command == "READ:BUTTON") {
    printReading("BUTTON", readDebouncedButton());
    return;
  }

  if (command == "READ:POT") {
    printReading("POT", readAveragedAnalog(POT_SENSE_PIN));
    return;
  }

  if (command == "READ:RESISTORS") {
    printReading("RESISTORS", readAveragedAnalog(RESISTOR_SENSE_PIN));
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
