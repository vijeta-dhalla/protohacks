#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#else
#error "This serial-port implementation currently supports macOS and Linux."
#endif

using namespace std;
using namespace std::chrono;

// ============================================================
// CIRCUIT QUEST - C++ CONSOLE GAME WITH ARDUINO SERIAL SUPPORT
//
// Usage on macOS/Linux:
//   ./CircuitQuest /dev/cu.usbmodemXXXX
//   ./CircuitQuest --simulate
//
// With no command-line argument, the program asks whether to use
// a physical Arduino or simulated readings.
//
// Arduino firmware file to upload: CircuitQuestArduino.ino
// Serial protocol: PING -> PONG
//   READ:LED       -> LED,<ADC 0..1023>
//   READ:BUTTON    -> BUTTON,<0 or 1>
//   READ:POT       -> POT,<ADC 0..1023>
//   READ:RESISTORS -> RESISTORS,<ADC 0..1023>
// ============================================================

struct Challenge {
    int level;
    string title;
    string info;
    string parts;
    string schematicFile;
    string schematicDescription;
};

const array<Challenge, 5> CHALLENGES = {{
    {
        1, "LED Polarity",
        "An LED is polarized: current flows through it most easily in one direction.\n"
        "The longer leg is usually the anode (+); the shorter leg / flat edge marks the cathode (-).\n"
        "A series resistor limits current and helps protect the LED.",
        "1 red LED, 1 x 330-ohm resistor, jumper wires",
        "images/level1_led_schematic.png",
        "5 V -> 330-ohm resistor -> A0 test junction -> LED -> GND"
    },
    {
        2, "Pushbutton",
        "A pushbutton temporarily connects two electrical contacts when pressed.\n"
        "On common four-leg tactile buttons, the two legs on each side are internally connected.\n"
        "This challenge uses INPUT_PULLUP: the input reads HIGH when released and LOW when pressed.",
        "1 tactile pushbutton, jumper wires",
        "images/level2_button_schematic.png",
        "Arduino D2 -> pushbutton -> GND; configure D2 as INPUT_PULLUP"
    },
    {
        3, "Potentiometer",
        "A potentiometer is a three-terminal variable resistor. Its middle terminal (the wiper)\n"
        "moves between the two outer terminals, changing the output voltage.\n"
        "Potentiometers are used in volume controls, dimmers, and position adjustment.",
        "1 x 10-kilohm potentiometer, jumper wires",
        "images/level3_potentiometer_schematic.png",
        "Outer terminals -> 5 V and GND; middle wiper -> A1"
    },
    {
        4, "Resistors in Series",
        "Resistors are in series when they form one path, so the same current passes through each.\n"
        "For resistors in series, R_total = R1 + R2. Two 1-kilohm resistors in series\n"
        "have an equivalent resistance of 2 kilohms.",
        "2 x 1-kilohm resistors; fixed 10-kilohm reference resistor in the tester",
        "images/level4_resistors_series_schematic.png",
        "5 V -> fixed 10-kilohm resistor -> A2 -> two 1-kilohm series network -> GND"
    },
    {
        5, "Resistors in Parallel",
        "Resistors are in parallel when both ends of each resistor connect to the same two nodes.\n"
        "For two equal resistors in parallel, the equivalent resistance is half either resistor.\n"
        "Two 1-kilohm resistors in parallel have an equivalent resistance of 500 ohms.",
        "2 x 1-kilohm resistors; fixed 10-kilohm reference resistor in the tester",
        "images/level5_resistors_parallel_schematic.png",
        "5 V -> fixed 10-kilohm resistor -> A2 -> two 1-kilohm parallel network -> GND"
    }
}};

// ------------------------- Console helpers -------------------------
int readIntInRange(const string& prompt, int minValue, int maxValue) {
    int value;
    while (true) {
        cout << prompt;
        if (cin >> value && value >= minValue && value <= maxValue) {
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            return value;
        }
        cout << "Please enter a whole number from " << minValue << " to "
             << maxValue << ".\n";
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
    }
}

string readLine(const string& prompt) {
    cout << prompt;
    string value;
    getline(cin, value);
    return value;
}

void pauseForEnter() {
    cout << "\nPress Enter to continue...";
    string unused;
    getline(cin, unused);
}

void printHeader(const string& title) {
    cout << "\n============================================================\n";
    cout << title << "\n";
    cout << "============================================================\n";
}

// ------------------------- POSIX serial port -------------------------
// This is a small native serial implementation for macOS/Linux, so no
// external serial library is needed. Windows would need a Win32 serial
// implementation or a cross-platform serial library.
class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort() { closePort(); }
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    bool openPort(const string& device, string& error) {
        closePort();
        fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ < 0) {
            error = "Could not open serial port '" + device + "': " + strerror(errno);
            return false;
        }

        termios tty{};
        if (tcgetattr(fd_, &tty) != 0) {
            error = "Could not read serial settings: " + string(strerror(errno));
            closePort();
            return false;
        }

        cfmakeraw(&tty);
        cfsetispeed(&tty, B9600);
        cfsetospeed(&tty, B9600);
        tty.c_cflag |= (CLOCAL | CREAD);
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~PARENB;
        tty.c_cflag &= ~CSIZE;
        tty.c_cflag |= CS8;
        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 0;

        if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
            error = "Could not configure serial port: " + string(strerror(errno));
            closePort();
            return false;
        }

        tcflush(fd_, TCIOFLUSH);
        rxBuffer_.clear();
        device_ = device;
        return true;
    }

    void closePort() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        rxBuffer_.clear();
        device_.clear();
    }

    bool isOpen() const { return fd_ >= 0; }
    const string& device() const { return device_; }

    void flushInput() {
        if (fd_ >= 0) {
            tcflush(fd_, TCIFLUSH);
            rxBuffer_.clear();
        }
    }

    bool writeLine(const string& line, string& error) {
        if (fd_ < 0) {
            error = "Serial port is not open.";
            return false;
        }
        string payload = line + "\n";
        size_t sent = 0;
        auto deadline = steady_clock::now() + milliseconds(1200);
        while (sent < payload.size()) {
            auto now = steady_clock::now();
            if (now >= deadline) {
                error = "Timed out writing to the Arduino.";
                return false;
            }
            auto remaining = duration_cast<milliseconds>(deadline - now);
            timeval tv{};
            tv.tv_sec = static_cast<long>(remaining.count() / 1000);
            tv.tv_usec = static_cast<long>((remaining.count() % 1000) * 1000);
            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(fd_, &writeSet);
            int ready = select(fd_ + 1, nullptr, &writeSet, nullptr, &tv);
            if (ready < 0) {
                if (errno == EINTR) continue;
                error = "Serial write wait failed: " + string(strerror(errno));
                return false;
            }
            if (ready == 0) continue;
            ssize_t n = ::write(fd_, payload.data() + sent, payload.size() - sent);
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;
                error = "Serial write failed: " + string(strerror(errno));
                return false;
            }
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    // Returns one newline-terminated line, or an empty string on timeout.
    string readLine(int timeoutMs) {
        if (fd_ < 0) return {};
        auto deadline = steady_clock::now() + milliseconds(timeoutMs);

        while (steady_clock::now() < deadline) {
            size_t newline = rxBuffer_.find('\n');
            if (newline != string::npos) {
                string line = rxBuffer_.substr(0, newline);
                rxBuffer_.erase(0, newline + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return line;
            }

            auto now = steady_clock::now();
            if (now >= deadline) break;
            auto remaining = duration_cast<milliseconds>(deadline - now);
            timeval tv{};
            tv.tv_sec = static_cast<long>(remaining.count() / 1000);
            tv.tv_usec = static_cast<long>((remaining.count() % 1000) * 1000);

            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(fd_, &readSet);
            int ready = select(fd_ + 1, &readSet, nullptr, nullptr, &tv);
            if (ready < 0) {
                if (errno == EINTR) continue;
                return {};
            }
            if (ready == 0) continue;

            char buffer[128];
            ssize_t n = ::read(fd_, buffer, sizeof(buffer));
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;
                return {};
            }
            if (n == 0) continue;
            rxBuffer_.append(buffer, static_cast<size_t>(n));
            if (rxBuffer_.size() > 2048) rxBuffer_.erase(0, rxBuffer_.size() - 1024);
        }

        size_t newline = rxBuffer_.find('\n');
        if (newline != string::npos) {
            string line = rxBuffer_.substr(0, newline);
            rxBuffer_.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return line;
        }
        return {};
    }

private:
    int fd_ = -1;
    string device_;
    string rxBuffer_;
};

class CircuitTester {
public:
    bool simulationMode = true;

    bool connectToArduino(const string& device, string& error) {
        if (!serial_.openPort(device, error)) return false;
        // Many Arduino Uno boards reset when the serial port opens.
        cout << "Opened " << device << ". Waiting for the Arduino to restart...\n";
        this_thread::sleep_for(milliseconds(2200));
        serial_.flushInput();

        for (int attempt = 0; attempt < 4; ++attempt) {
            string writeError;
            if (!serial_.writeLine("PING", writeError)) {
                error = writeError;
                continue;
            }
            auto deadline = steady_clock::now() + milliseconds(1200);
            while (steady_clock::now() < deadline) {
                auto remaining = duration_cast<milliseconds>(deadline - steady_clock::now());
                string response = serial_.readLine(static_cast<int>(max<int64_t>(1, remaining.count())));
                if (response == "PONG") {
                    simulationMode = false;
                    return true;
                }
                if (response.empty()) break;
            }
            this_thread::sleep_for(milliseconds(150));
        }
        error = "The serial port opened, but the Arduino did not answer PING with PONG.\n"
                "Check that CircuitQuestArduino.ino is uploaded and the baud rate is 9600.";
        serial_.closePort();
        return false;
    }

    void useSimulation() {
        serial_.closePort();
        simulationMode = true;
    }

    optional<int> readAnalog(const string& command, const string& label) {
        if (simulationMode) {
            return readIntInRange("Enter simulated " + label + " ADC reading (0-1023): ", 0, 1023);
        }

        // IMPORTANT: The Arduino returns labels based on the measurement type
        // (for example, "LED,420"), not the display label (for example, "A0").
        // Map each request command to the matching reply tag.
        string expectedTag;
        if (command == "READ:LED") {
            expectedTag = "LED";
        } else if (command == "READ:POT") {
            expectedTag = "POT";
        } else if (command == "READ:RESISTORS") {
            expectedTag = "RESISTORS";
        } else {
            cout << "\nUnsupported analog measurement command: " << command << "\n";
            return nullopt;
        }

        return requestReading(command, expectedTag, 0, 1023);
    }

    optional<int> readDigitalButton() {
        if (simulationMode) {
            return readIntInRange("Enter simulated button state (1=HIGH, 0=LOW): ", 0, 1);
        }
        return requestReading("READ:BUTTON", "BUTTON", 0, 1);
    }

    string modeDescription() const {
        return simulationMode ? "SIMULATION MODE (manual readings)" : "ARDUINO CONNECTED (" + serial_.device() + ")";
    }

private:
    SerialPort serial_;


    optional<int> requestReading(const string& command, const string& expectedTag, int minValue, int maxValue) {
        string writeError;
        if (!serial_.writeLine(command, writeError)) {
            cout << "\nSerial connection error: " << writeError << "\n";
            return nullopt;
        }

        // Ignore unrelated startup/status lines, but only wait a short time.
        auto deadline = steady_clock::now() + milliseconds(1800);
        while (steady_clock::now() < deadline) {
            auto remaining = duration_cast<milliseconds>(deadline - steady_clock::now());
            string line = serial_.readLine(static_cast<int>(max<int64_t>(1, remaining.count())));
            if (line.empty()) break;
            if (line.rfind("ERR,", 0) == 0) {
                cout << "\nArduino reported: " << line << "\n";
                return nullopt;
            }
            string prefix = expectedTag + ",";
            if (line.rfind(prefix, 0) == 0) {
                string number = line.substr(prefix.size());
                try {
                    size_t used = 0;
                    int value = stoi(number, &used);
                    if (used != number.size() || value < minValue || value > maxValue) {
                        cout << "\nInvalid value from Arduino: " << line << "\n";
                        return nullopt;
                    }
                    cout << "Arduino measurement: " << value << "\n";
                    return value;
                } catch (...) {
                    cout << "\nCould not parse Arduino response: " << line << "\n";
                    return nullopt;
                }
            }
        }
        cout << "\nNo valid response from Arduino. Check the USB connection and uploaded firmware.\n";
        return nullopt;
    }
};

enum class CheckResult { Passed, Failed, CommunicationError };

// ------------------------- Intro pages -------------------------
bool showTitleScreen(const CircuitTester& tester) {
    while (true) {
        printHeader("CIRCUIT QUEST");
        cout << "A beginner-friendly breadboard wiring game\n\n";
        cout << "Connection: " << tester.modeDescription() << "\n\n";

        // TODO IMAGE: Put a title illustration/logo here in the future UI.
        // Planned asset: images/circuit_quest_title.png
        cout << "[TITLE IMAGE PLACEHOLDER: images/circuit_quest_title.png]\n\n";
        cout << "1. Start game\n0. Quit\n";
        int choice = readIntInRange("Choose an option: ", 0, 1);
        return choice == 1;
    }
}

void showBreadboardInfo() {
    printHeader("INTRO 1/4 - HOW A BREADBOARD WORKS");
    cout << "A solderless breadboard lets you build circuits without soldering.\n\n";
    cout << "- Many five-hole groups in the centre are connected internally.\n";
    cout << "- The centre gap separates the two sides of the board.\n";
    cout << "- Long side rails are commonly used for power (5 V) and ground (GND).\n";
    cout << "- Components work only when their leads connect to the intended nodes.\n\n";
    // TODO IMAGE: Add a labeled diagram of connected rows, centre gap, and power rails.
    // Planned asset: images/breadboard_connections.png
    cout << "[IMAGE PLACEHOLDER: images/breadboard_connections.png]\n";
    pauseForEnter();
}

void showVoltageAndOhmsLawInfo() {
    printHeader("INTRO 2/4 - VOLTAGE, CURRENT, AND OHM'S LAW");
    cout << "Voltage is electrical potential difference. Current is the flow of charge.\n";
    cout << "Resistance opposes current. Ohm's law is V = I * R.\n\n";
    cout << "You can rearrange it to I = V / R or R = V / I.\n";
    cout << "Example: 2 V across 1 kilohm gives I = 2/1000 A = 2 mA.\n\n";
    cout << "The Arduino measures voltage or digital state; the game compares the\n";
    cout << "measurements with the expected range or behavior for each challenge.\n\n";
    // TODO IMAGE: Add Ohm's law triangle/equation visual and a voltage divider diagram.
    // Planned asset: images/ohms_law_diagram.png
    cout << "[IMAGE PLACEHOLDER: images/ohms_law_diagram.png]\n";
    pauseForEnter();
}

void showHowTheGameWorks(const CircuitTester& tester) {
    printHeader("INTRO 3/4 - HOW CIRCUIT QUEST WORKS");
    cout << "1. Read the component information and inspect the target schematic.\n";
    cout << "2. Build the circuit on the physical breadboard.\n";
    cout << "3. Select Check Circuit. The tester reads the assigned Arduino pin.\n";
    cout << "4. The program compares the result with the expected behavior.\n";
    cout << "5. If the circuit passes, earn points and repeat or advance.\n\n";
    if (tester.simulationMode) {
        cout << "CURRENT MODE: readings are typed manually for testing the game logic.\n";
    } else {
        cout << "CURRENT MODE: measurements are requested from the Arduino over USB serial.\n";
    }
    // TODO IMAGE: Add an illustration showing schematic -> breadboard -> feedback.
    // Planned asset: images/how_the_game_works.png
    cout << "[IMAGE PLACEHOLDER: images/how_the_game_works.png]\n";
    pauseForEnter();
}

void showProvidedParts() {
    printHeader("INTRO 4/4 - YOUR PARTS KIT");
    cout << "- Arduino Uno and USB cable\n";
    cout << "- Solderless breadboard and jumper wires\n";
    cout << "- Red LED and 330-ohm resistor\n";
    cout << "- Tactile pushbutton\n";
    cout << "- 10-kilohm potentiometer\n";
    cout << "- Two 1-kilohm resistors for series/parallel challenges\n";
    cout << "- Fixed 10-kilohm reference resistor in the tester\n\n";
    cout << "Keep the tester's 5 V, GND, and sensing leads fixed. Do not connect\n";
    cout << "external voltages to the Arduino analog inputs.\n\n";
    // TODO IMAGE: Add component photos/icons for the learner's parts kit.
    // Planned asset: images/parts_kit.png
    cout << "[PARTS IMAGE PLACEHOLDER: images/parts_kit.png]\n";
    pauseForEnter();
}

void runIntroduction(const CircuitTester& tester) {
    showBreadboardInfo();
    showVoltageAndOhmsLawInfo();
    showHowTheGameWorks(tester);
    showProvidedParts();
}

void showChallengeInfo(const Challenge& challenge, const CircuitTester& tester) {
    printHeader("LEVEL " + to_string(challenge.level) + " - " + challenge.title);
    cout << "ABOUT THIS COMPONENT / CONCEPT\n" << challenge.info << "\n\n";

    // TODO IMAGE: Render challenge.schematicFile in a future graphical frontend.
    // Put the PNG at the path below, relative to the application/project folder.
    cout << "SCHEMATIC IMAGE PLACEHOLDER\n";
    cout << "  File to add: " << challenge.schematicFile << "\n";
    cout << "  Schematic summary: " << challenge.schematicDescription << "\n\n";

    cout << "COMPONENTS FOR THIS LEVEL\n" << challenge.parts << "\n\n";
    // TODO IMAGE: Add a breadboard layout or pinout image for this level.
    cout << "[OPTIONAL PINOUT / BREADBOARD IMAGE PLACEHOLDER]\n\n";
    cout << "Tester mode: " << tester.modeDescription() << "\n";
    cout << "Build the circuit, then press Enter when ready to check it.\n";
    pauseForEnter();
}

// ------------------------- Verification routines -------------------------
CheckResult checkLED(CircuitTester& tester) {
    printHeader("CHECK CIRCUIT - LED POLARITY");
    cout << "Measuring A0. Example circuit: 5 V -> 330 ohms -> A0 -> LED -> GND.\n";
    auto reading = tester.readAnalog("READ:LED", "A0");
    if (!reading) return CheckResult::CommunicationError;

    // Initial calibration range for a typical red LED, 330-ohm resistor,
    // and nominal 5 V Uno supply. Verify this with your actual components.
    if (*reading >= 320 && *reading <= 500) {
        cout << "PASS: The reading is consistent with a forward-biased red LED.\n";
        return CheckResult::Passed;
    }
    if (*reading >= 800) {
        cout << "NOT YET: The node is near 5 V. Check LED polarity or connections.\n";
    } else {
        cout << "NOT YET: The reading is outside the expected range. Check the LED and resistor.\n";
    }
    return CheckResult::Failed;
}

CheckResult checkButton(CircuitTester& tester) {
    printHeader("CHECK CIRCUIT - PUSHBUTTON");
    cout << "This test uses INPUT_PULLUP: released = HIGH (1), pressed = LOW (0).\n\n";
    cout << "Make sure the button is NOT pressed.\n";
    pauseForEnter();
    auto released = tester.readDigitalButton();
    if (!released) return CheckResult::CommunicationError;

    cout << "Now press and HOLD the button.\n";
    pauseForEnter();
    auto pressed = tester.readDigitalButton();
    if (!pressed) return CheckResult::CommunicationError;

    cout << "Released reading: " << *released << " | Pressed reading: " << *pressed << "\n";
    if (*released == 1 && *pressed == 0) {
        cout << "PASS: The input changes as expected.\n";
        return CheckResult::Passed;
    }
    cout << "NOT YET: Expected HIGH when released and LOW when pressed. Check the wiring.\n";
    return CheckResult::Failed;
}

CheckResult checkPotentiometer(CircuitTester& tester) {
    printHeader("CHECK CIRCUIT - POTENTIOMETER");
    cout << "Measure the middle terminal (wiper) at A1.\n";
    cout << "Turn the knob counter-clockwiseto one end of its range before the first reading.\n";
    pauseForEnter();
    auto first = tester.readAnalog("READ:POT", "A1 first");
    if (!first) return CheckResult::CommunicationError;

    cout << "Now rotate the knob the other way toward the other end of its range.\n";
    pauseForEnter();
    auto second = tester.readAnalog("READ:POT", "A1 second");
    if (!second) return CheckResult::CommunicationError;

    int difference = abs(*second - *first);
    cout << "First reading: " << *first << " | Second reading: " << *second
         << " | Change: " << difference << " counts\n";
    if (difference >= 300) {
        cout << "PASS: The wiper voltage changes substantially as the knob turns.\n";
        return CheckResult::Passed;
    }
    cout << "NOT YET: The reading changed too little. Rotate farther and check the wiper connection.\n";
    return CheckResult::Failed;
}

CheckResult checkResistors(CircuitTester& tester, bool expectSeries) {
    string label = expectSeries ? "SERIES RESISTORS" : "PARALLEL RESISTORS";
    printHeader("CHECK CIRCUIT - " + label);
    cout << "Measure A2. The tester has a fixed 10-kilohm resistor from 5 V to A2.\n";
    cout << "The two student resistors connect between A2 and GND.\n";
    cout << "Two 1-kilohm resistors in series should read around 170 ADC counts;\n";
    cout << "two in parallel should read around 49 counts on a typical 5 V Uno.\n\n";

    auto reading = tester.readAnalog("READ:RESISTORS", "A2");
    if (!reading) return CheckResult::CommunicationError;

    bool looksSeries = (*reading >= 145 && *reading <= 195);
    bool looksParallel = (*reading >= 30 && *reading <= 70);

    if (expectSeries && looksSeries) {
        cout << "PASS: The reading matches the series configuration.\n";
        return CheckResult::Passed;
    }
    if (!expectSeries && looksParallel) {
        cout << "PASS: The reading matches the parallel configuration.\n";
        return CheckResult::Passed;
    }
    if (expectSeries && looksParallel) {
        cout << "NOT YET: The reading looks like the parallel configuration.\n"
             << "Check whether the two resistors are connected end-to-end.\n";
    } else if (!expectSeries && looksSeries) {
        cout << "NOT YET: The reading looks like the series configuration.\n"
             << "For parallel, both resistors must share the same two nodes.\n";
    } else {
        cout << "NOT YET: The reading is outside both expected ranges. Check for loose wires,\n"
             << "an open circuit, a short, or the wrong resistor values.\n";
    }
    return CheckResult::Failed;
}

CheckResult runChallengeCheck(const Challenge& challenge, CircuitTester& tester) {
    switch (challenge.level) {
        case 1: return checkLED(tester);
        case 2: return checkButton(tester);
        case 3: return checkPotentiometer(tester);
        case 4: return checkResistors(tester, true);
        case 5: return checkResistors(tester, false);
        default: return CheckResult::CommunicationError;
    }
}

// Return true if the player wants to retry a failed attempt.
bool offerRetry() {
    cout << "\n1. Try this level again\n0. Return to title screen\n";
    int choice = readIntInRange("Choose an option: ", 0, 1);
    return choice == 1;
}

void showEndScreen(int score) {
    printHeader("THANK YOU FOR PLAYING CIRCUIT QUEST!");
    cout << "You completed the available Circuit Quest session.\n\n";
    cout << "Final score: " << score << " / 500 points\n\n";
    cout << "More levels coming soon!\n\n";
    // TODO IMAGE: Add a completion/trophy illustration.
    // Planned asset: images/end_screen.png
    cout << "[END SCREEN IMAGE PLACEHOLDER: images/end_screen.png]\n";
}

bool chooseConnectionMode(CircuitTester& tester, int argc, char* argv[]) {
    if (argc >= 2 && string(argv[1]) == "--simulate") {
        tester.useSimulation();
        cout << "Starting in simulation mode.\n";
        return true;
    }

    if (argc >= 2) {
        string error;
        if (tester.connectToArduino(argv[1], error)) {
            cout << "Arduino connection successful.\n";
            return true;
        }
        cout << "Could not connect: " << error << "\n";
        int fallback = readIntInRange("Use simulation mode instead? 1=Yes, 0=Quit: ", 0, 1);
        if (fallback == 1) {
            tester.useSimulation();
            return true;
        }
        return false;
    }

    printHeader("CONNECT CIRCUIT QUEST TO YOUR TESTER");
    cout << "1. Connect to a physical Arduino over USB serial\n";
    cout << "2. Run in simulation mode (type sample readings)\n";
    int choice = readIntInRange("Choose a mode: ", 1, 2);
    if (choice == 2) {
        tester.useSimulation();
        return true;
    }

    cout << "\nFind the serial device on macOS with: ls /dev/cu.*\n";
    cout << "Common names include /dev/cu.usbmodemXXXX or /dev/cu.usbserialXXXX.\n";
    cout << "On Linux, common names include /dev/ttyACM0 or /dev/ttyUSB0.\n\n";
    string port = readLine("Enter the full serial port path: ");
    if (port.empty()) {
        cout << "No port supplied. Exiting.\n";
        return false;
    }
    string error;
    if (tester.connectToArduino(port, error)) {
        cout << "Arduino connection successful.\n";
        return true;
    }
    cout << "\nCould not connect: " << error << "\n";
    cout << "Check the selected port, close Arduino Serial Monitor, and upload the matching .ino file.\n";
    int fallback = readIntInRange("Use simulation mode instead? 1=Yes, 0=Quit: ", 0, 1);
    if (fallback == 1) {
        tester.useSimulation();
        return true;
    }
    return false;
}

int main(int argc, char* argv[]) {
    CircuitTester tester;
    if (!chooseConnectionMode(tester, argc, argv)) return 1;

    while (showTitleScreen(tester)) {
        runIntroduction(tester);
        int score = 0;
        array<bool, 5> awarded{};
        bool returnToTitle = false;
        size_t levelIndex = 0;

        while (levelIndex < CHALLENGES.size() && !returnToTitle) {
            const Challenge& challenge = CHALLENGES[levelIndex];
            bool levelPassed = false;

            while (!levelPassed && !returnToTitle) {
                showChallengeInfo(challenge, tester);
                CheckResult result = runChallengeCheck(challenge, tester);

                if (result == CheckResult::Passed) {
                    levelPassed = true;
                    if (!awarded[levelIndex]) {
                        score += 100;
                        awarded[levelIndex] = true;
                        cout << "\n+100 points!\n";
                    } else {
                        cout << "\nYou have already earned points for this level in this session.\n";
                    }
                    cout << "Current score: " << score << " / 500\n";
                } else if (result == CheckResult::CommunicationError) {
                    cout << "\nThe measurement could not be completed.\n";
                    cout << "1. Retry this level\n0. Return to title screen\n";
                    int choice = readIntInRange("Choose an option: ", 0, 1);
                    if (choice == 0) returnToTitle = true;
                } else {
                    if (!offerRetry()) {
                        returnToTitle = true;
                    }
                }
            }

            if (returnToTitle) break;

            printHeader("LEVEL COMPLETE - " + challenge.title);
            if (levelIndex + 1 < CHALLENGES.size()) {
                cout << "1. Repeat this level\n2. Continue to the next level\n0. Return to title screen\n";
                int choice = readIntInRange("Choose an option: ", 0, 2);
                if (choice == 0) {
                    returnToTitle = true;
                } else if (choice == 2) {
                    ++levelIndex;
                }
                // choice 1 keeps levelIndex unchanged to repeat this level.
            } else {
                cout << "All five levels completed!\n";
                showEndScreen(score);
                cout << "1. Replay from the title screen\n0. Quit\n";
                int choice = readIntInRange("Choose an option: ", 0, 1);
                if (choice == 0) return 0;
                returnToTitle = true;
            }
        }
    }

    cout << "\nThanks for playing Circuit Quest!\n";
    return 0;
}

// g++ -std=c++17 -Wall -Wextra -pedantic CircuitQuest.cpp -o CircuitQuest
// ls /dev/cu.usbmodem1101
// ./CircuitQuest /dev/cu.usbmodem1101