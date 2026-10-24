CIRCUIT QUEST: C++ + Arduino integration
========================================

FILES
-----
CircuitQuest.cpp          C++ console game and macOS/Linux USB serial client
CircuitQuestArduino.ino   Arduino Uno firmware using the matching serial protocol

WHAT THIS VERSION DOES
----------------------
- The C++ game can connect to an Arduino over USB serial at 9600 baud.
- It sends PING / READ:* commands and parses the Arduino's response.
- It retains a simulation mode so game logic can be tested without hardware.
- The original introduction, five levels, image placeholders, retries, score,
  repeat/continue flow and end screen are included.

UPLOAD THE ARDUINO SKETCH
-------------------------
1. Open CircuitQuestArduino.ino in the Arduino IDE.
2. Select the correct board (Arduino Uno) and its port.
3. Upload the sketch.
4. Close the Arduino IDE Serial Monitor before starting the C++ game.

BUILD THE C++ PROGRAM (macOS or Linux)
--------------------------------------
From the folder containing CircuitQuest.cpp:

  g++ -std=c++17 -Wall -Wextra -pedantic CircuitQuest.cpp -o CircuitQuest

RUN IN SIMULATION MODE
----------------------
  ./CircuitQuest --simulate

RUN WITH A PHYSICAL ARDUINO (macOS EXAMPLE)
-------------------------------------------
First inspect serial device names:

  ls /dev/cu.*

Typical device names include /dev/cu.usbmodemXXXX or /dev/cu.usbserialXXXX.
Then run (substitute your actual device path):

  ./CircuitQuest /dev/cu.usbmodemXXXX

On Linux, a common port is /dev/ttyACM0 or /dev/ttyUSB0. If permissions prevent
access, resolve the serial-device permissions for your system; do not run an
unknown downloaded executable with elevated privileges.

FIXED TESTER WIRING
-------------------
Shared rails:
- Arduino 5V -> breadboard positive rail
- Arduino GND -> breadboard ground rail
- If the breadboard rails are split, bridge the matching rail sections.

Level 1, LED:
- 5V -> 330-ohm resistor -> measurement junction -> A0
- LED anode (long leg) -> same measurement junction
- LED cathode (short leg / flat edge) -> GND

Level 2, button:
- D2 -> one side of the pushbutton
- opposite side of the pushbutton -> GND
- Firmware uses INPUT_PULLUP (released HIGH, pressed LOW).
- Place a typical four-leg tactile button across the breadboard centre gap.

Level 3, potentiometer:
- One outer terminal -> 5V
- Other outer terminal -> GND
- Centre terminal / wiper -> A1

Level 4 and 5, resistor network:
- Fixed tester resistor: 10 kOhm from 5V to A2
- Student's two-resistor network: A2 to GND
- Series level: two 1 kOhm resistors end-to-end (equivalent 2 kOhm)
- Parallel level: both resistors share the same A2 node and GND node

IMPORTANT CIRCUIT NOTES
-----------------------
- This is a beginner, low-voltage demonstrator and should use only the specified
  5 V supply and passive components. Do not connect batteries or external supplies
  to the analog inputs.
- Keep the fixed tester's reference resistor and A0/A1/A2/D2 sensing wires in place.
  The student changes the circuit within the relevant challenge area.
- The resistor thresholds are initial estimates for a nominal 5 V Uno and the
  specified resistor values. Check your actual hardware and calibrate thresholds.
- The method checks electrical behavior at the assigned points; it cannot prove
  every physical wire placement for arbitrary circuits.
- The C++ serial implementation supports macOS and Linux. Windows needs a different
  serial-port implementation or a cross-platform serial library.
