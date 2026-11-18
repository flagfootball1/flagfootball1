// Pattern Puzzle (Arduino Mega 2560)
// 16-switch pattern puzzle on a single Mega.
// Press RESET to generate a random target pattern; match all 16 switches
// to reveal the key word. Hold RESET for 2 seconds for the wire test screen.
//
// Wire checks (no extra hardware needed):
//   - Loose wire: if a switch flickers ON/OFF fast (a wire making bad contact),
//     the LCD shows the switch and pin for 3 seconds, then goes back to the
//     game. It is also printed on the Serial Monitor.
//   - Wire test: hold RESET 2 s, flip every switch ON once, press RESET.
//     Any switch that never turned ON is listed as NOT CONNECTED.
//   Note: a wire that comes cleanly undone reads the same as a switch that
//   is OFF, so it can only be found with the wire test.
//
// Wiring (Mega 2560):
//   Switches 1-8   -> pins 2-9    (other side of each switch -> GND)
//   Switches 9-16  -> pins 22-29  (other side of each switch -> GND)
//   RESET button   -> pin 13      (other side -> GND)
//   LCD (I2C)      -> SDA = pin 20, SCL = pin 21, VCC = 5V, GND = GND
//                     (PCF8574 I2C backpack at address 0x27)
// No resistors needed: the Mega's built-in pull-ups are used.

#include <Wire.h>
#include <LiquidCrystal_I2C.h>  // Library Manager: "LiquidCrystal I2C" by Frank de Brabander

// ---------- Hardware ----------
const uint8_t LCD_ADDRESS = 0x27;  // found with the I2C scanner
const uint8_t LCD_COLUMNS = 16;
const uint8_t RESET_PIN   = 13;

// Switch number (1-16) -> Mega pin. Change a pin here if you rewire.
const uint8_t SWITCH_PINS[] =
{
   2,  3,  4,  5,  6,  7,  8,  9,   // switches 1-8
  22, 23, 24, 25, 26, 27, 28, 29    // switches 9-16
};
const uint8_t TOTAL_SWITCHES = sizeof(SWITCH_PINS);  // 16

// ---------- Game settings ----------
const char KEY_WORD[] = "TRIANGLE";

const unsigned long DEBOUNCE_MS    = 50;
const unsigned long NEW_PATTERN_MS = 1500;  // "NEW PATTERN" splash
const unsigned long CONGRATS_MS    = 3000;  // "CONGRATS!" splash
const unsigned long LONG_PRESS_MS  = 2000;  // hold RESET this long for the wire test
const unsigned long TEST_INTRO_MS  = 2000;  // "WIRE TEST" title screen
const unsigned long POLL_MS        = 100;   // how often the game/LCD checks switches
const unsigned long MONITOR_MS     = 20;    // how often the Serial wiring monitor checks

// Loose-wire detection: a switch that changes this many times within
// LOOSE_WINDOW_MS is treated as a loose wire (a person flipping a switch
// changes it once).
const uint8_t       LOOSE_CHANGES     = 6;
const unsigned long LOOSE_WINDOW_MS   = 1000;
const unsigned long LOOSE_COOLDOWN_MS = 5000;  // don't repeat the same alert sooner
const unsigned long ALERT_MS          = 3000;  // how long a wire alert stays on the LCD

LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, 2);

// Types are defined here, before any functions, because the Arduino IDE
// adds function declarations above the first function automatically.
enum ButtonEvent
{
  NO_PRESS,
  SHORT_PRESS,  // pressed and let go quickly
  LONG_PRESS    // held down for LONG_PRESS_MS
};

enum GameState
{
  WAITING,      // power-up, waiting for first RESET
  NEW_PATTERN,  // showing "NEW PATTERN" splash
  PLAYING,      // showing score, waiting for 16/16
  CONGRATS,     // showing "CONGRATS!" splash
  SOLVED,       // showing the key word until RESET is pressed
  WIRE_TEST     // showing the live state of all 16 switches
};

GameState state = WAITING;
unsigned long stateStart = 0;
uint16_t targetPattern = 0;  // bit i = required state of switch i+1
int lastShownScore = -1;     // -1 = nothing shown yet (forces a redraw)

bool alertActive = false;            // a wire alert is on the LCD
unsigned long alertStart = 0;
uint16_t wireTestSeenOn = 0;         // switches that turned ON during the wire test

// ---------- LCD helpers ----------

// Prints text on a row and pads with spaces so leftovers from the
// previous message are erased (no lcd.clear() flicker).
void showLine(uint8_t row, const char *text)
{
  lcd.setCursor(0, row);

  uint8_t n = 0;
  for (; text[n] != '\0' && n < LCD_COLUMNS; n++)
  {
    lcd.print(text[n]);
  }
  for (; n < LCD_COLUMNS; n++)
  {
    lcd.print(' ');
  }
}

// Normal screens. While a wire alert is showing they are skipped; the
// current screen is redrawn when the alert ends.
void showScreen(const char *top, const char *bottom)
{
  if (alertActive)
  {
    return;
  }

  showLine(0, top);
  showLine(1, bottom);
}

// ---------- Input ----------

// Non-blocking debounce for the RESET button.
// A short press is reported when the button is let go; a long press is
// reported as soon as it has been held long enough (only once per hold).
ButtonEvent readResetButton()
{
  static bool stableState = HIGH;
  static bool lastReading = HIGH;
  static unsigned long lastChange = 0;
  static unsigned long pressStart = 0;
  static bool longReported = false;

  bool reading = digitalRead(RESET_PIN);

  if (reading != lastReading)
  {
    lastReading = reading;
    lastChange = millis();
  }

  if (millis() - lastChange >= DEBOUNCE_MS && reading != stableState)
  {
    stableState = reading;

    if (stableState == LOW)
    {
      pressStart = millis();
      longReported = false;
    }
    else if (!longReported)
    {
      return SHORT_PRESS;
    }
  }

  if (stableState == LOW && !longReported && millis() - pressStart >= LONG_PRESS_MS)
  {
    longReported = true;
    return LONG_PRESS;
  }

  return NO_PRESS;
}

// Reads all 16 switches into a bitmask (bit i = switch i+1 is ON).
uint16_t readSwitches()
{
  uint16_t states = 0;

  for (uint8_t i = 0; i < TOTAL_SWITCHES; i++)
  {
    if (digitalRead(SWITCH_PINS[i]) == LOW)
    {
      states |= (1u << i);
    }
  }

  return states;
}

// Shows a message for ALERT_MS, then the game screen comes back.
void showAlert(const char *top, const char *bottom)
{
  alertActive = true;
  alertStart = millis();
  showLine(0, top);
  showLine(1, bottom);
}

// ---------- Wiring monitor (Serial Monitor, 9600 baud) ----------

// Prints e.g. "Switch 11 -> pin 24  ON"
void printSwitch(uint8_t i, bool on)
{
  Serial.print(F("Switch "));
  Serial.print(i + 1);
  Serial.print(i + 1 < 10 ? F("  -> pin ") : F(" -> pin "));
  Serial.print(SWITCH_PINS[i]);
  Serial.println(on ? F("  ON") : F("  OFF"));
}

// Called every time switch i changes. If it changes too often in a short
// time, the wire is making bad contact: show it on the LCD and Serial.
void checkLooseWire(uint8_t i)
{
  static unsigned long windowStart[16];
  static uint8_t changes[16];
  static unsigned long lastAlert[16];
  static bool alerted[16];

  unsigned long now = millis();

  if (now - windowStart[i] > LOOSE_WINDOW_MS)
  {
    windowStart[i] = now;
    changes[i] = 0;
  }
  changes[i]++;

  if (changes[i] < LOOSE_CHANGES)
  {
    return;
  }
  changes[i] = 0;

  if (alerted[i] && now - lastAlert[i] < LOOSE_COOLDOWN_MS)
  {
    return;
  }
  alerted[i] = true;
  lastAlert[i] = now;

  // LCD first: Serial printing at 9600 baud is slow.
  char bottom[LCD_COLUMNS + 1];
  snprintf(bottom, sizeof(bottom), "SW %d = PIN %d", i + 1, SWITCH_PINS[i]);
  showAlert("LOOSE WIRE!", bottom);

  Serial.print(F("*** LOOSE WIRE: Switch "));
  Serial.print(i + 1);
  Serial.print(F(" -> pin "));
  Serial.print(SWITCH_PINS[i]);
  Serial.println(F(" is flickering ON/OFF - check this wire and its GND wire ***"));
}

// Runs all the time (in every game state) and prints whenever a switch
// changes. Use it to find a bad wire:
//   - flip a switch and nothing prints     -> that wire is broken/unplugged
//   - one flip prints ON/OFF many times    -> that wire is loose
void monitorSwitches()
{
  static unsigned long lastCheck = 0;
  static bool firstRun = true;
  static uint16_t lastStates = 0;

  if (millis() - lastCheck < MONITOR_MS)
  {
    return;
  }
  lastCheck = millis();

  uint16_t states = readSwitches();

  if (firstRun)
  {
    Serial.println(F("Switches ON at start-up:"));
    if (states == 0)
    {
      Serial.println(F("  (none)"));
    }
  }

  uint16_t changed = firstRun ? 0 : (states ^ lastStates);
  uint16_t toPrint = firstRun ? states : changed;
  for (uint8_t i = 0; i < TOTAL_SWITCHES; i++)
  {
    if ((toPrint >> i) & 1)
    {
      printSwitch(i, (states >> i) & 1);
    }
    if ((changed >> i) & 1)
    {
      checkLooseWire(i);
    }
  }

  if (state == WIRE_TEST)
  {
    wireTestSeenOn |= states;
  }

  lastStates = states;
  firstRun = false;
}

// ---------- Wire test screen (LCD) ----------

// Shows every switch as 1 (ON) or 0 (OFF), in order from left to right:
//   SW 1-8  10010000
//   SW 9-16 00000001
// Flip each switch: if its digit never changes, that wire is broken.
void showWireTest()
{
  uint16_t states = readSwitches();

  char top[LCD_COLUMNS + 1]    = "SW 1-8  ";
  char bottom[LCD_COLUMNS + 1] = "SW 9-16 ";

  for (uint8_t i = 0; i < 8; i++)
  {
    top[8 + i]    = bitRead(states, i)     ? '1' : '0';
    bottom[8 + i] = bitRead(states, i + 8) ? '1' : '0';
  }
  top[16] = '\0';
  bottom[16] = '\0';

  showScreen(top, bottom);
}

// Leaving the wire test: report every switch that never turned ON.
void finishWireTest()
{
  uint16_t missing = ~wireTestSeenOn;  // one bit per switch (16 switches)

  enterState(WAITING);

  if (missing == 0)
  {
    showAlert("WIRE TEST OK", "ALL 16 WORKING");
    Serial.println(F("WIRE TEST RESULT: all 16 switches turned ON - all connected"));
    return;
  }

  // Build the LCD line, e.g. "SW 7 12". If it doesn't fit, it ends with "+".
  char bottom[LCD_COLUMNS + 1] = "SW";
  uint8_t len = 2;

  for (uint8_t i = 0; i < TOTAL_SWITCHES; i++)
  {
    if ((missing >> i) & 1)
    {
      char num[4];
      uint8_t n = snprintf(num, sizeof(num), " %d", i + 1);
      if (len + n <= LCD_COLUMNS - 1)
      {
        strcpy(bottom + len, num);
        len += n;
      }
      else
      {
        strcpy(bottom + len, "+");
        break;
      }
    }
  }

  // LCD first: Serial printing at 9600 baud is slow.
  showAlert("NOT CONNECTED:", bottom);

  // Full list on the Serial Monitor.
  Serial.println(F("WIRE TEST RESULT: these switches never turned ON (not connected?):"));
  for (uint8_t i = 0; i < TOTAL_SWITCHES; i++)
  {
    if ((missing >> i) & 1)
    {
      Serial.print(F("  Switch "));
      Serial.print(i + 1);
      Serial.print(F(" -> pin "));
      Serial.print(SWITCH_PINS[i]);
      Serial.println(F("  check this wire and its GND wire"));
    }
  }
}

// ---------- Game logic ----------

void generatePattern()
{
  // 1..0xFFFE: never all-off or all-on.
  targetPattern = (uint16_t)random(1, 0xFFFF);

  Serial.print(F("NEW PATTERN: "));
  for (uint8_t i = 0; i < TOTAL_SWITCHES; i++)
  {
    Serial.print(bitRead(targetPattern, i));
  }
  Serial.println();
}

uint8_t countCorrect(uint16_t states)
{
  uint16_t wrong = states ^ targetPattern;
  uint8_t correct = TOTAL_SWITCHES;

  while (wrong)
  {
    correct -= wrong & 1;
    wrong >>= 1;
  }

  return correct;
}

// True at most once every POLL_MS.
bool pollDue()
{
  static unsigned long lastPoll = 0;

  if (millis() - lastPoll < POLL_MS)
  {
    return false;
  }

  lastPoll = millis();
  return true;
}

void showScore(int score)
{
  if (score == lastShownScore)
  {
    return;  // nothing changed, don't rewrite the LCD
  }
  lastShownScore = score;

  char line[LCD_COLUMNS + 1];
  snprintf(line, sizeof(line), "%d/%d CORRECT", score, TOTAL_SWITCHES);
  showScreen("MISSION ACTIVE", line);
}

// Draws the screen for the current state (used when entering a state and
// when a wire alert ends).
void drawStateScreen()
{
  switch (state)
  {
    case WAITING:
      showScreen("MISSION READY", "PRESS RESET");
      break;

    case NEW_PATTERN:
      showScreen("NEW PATTERN", "GENERATED");
      break;

    case PLAYING:
      lastShownScore = -1;  // force the score to be drawn on the next check
      break;

    case CONGRATS:
      showScreen(" CONGRATS!", "16/16 CORRECT");
      break;

    case SOLVED:
      showScreen("FIRST KEY =", KEY_WORD);
      break;

    case WIRE_TEST:
      if (millis() - stateStart < TEST_INTRO_MS)
      {
        showScreen("WIRE TEST", "FLIP EACH ON");
      }
      else
      {
        showWireTest();
      }
      break;
  }
}

void enterState(GameState newState)
{
  state = newState;
  stateStart = millis();

  if (state == CONGRATS)
  {
    Serial.println(F("SOLVED"));
  }
  if (state == WIRE_TEST)
  {
    wireTestSeenOn = 0;
    Serial.println(F("WIRE TEST: flip every switch ON once, then press RESET."));
    Serial.println(F("Any switch that never turns ON will be listed."));
  }

  drawStateScreen();
}

// Ends a wire alert after ALERT_MS and puts the game screen back.
void updateAlert()
{
  if (alertActive && millis() - alertStart >= ALERT_MS)
  {
    alertActive = false;
    drawStateScreen();
  }
}

// ---------- Arduino ----------

void setup()
{
  Serial.begin(9600);

  randomSeed(analogRead(A0));  // A0 must be left unconnected

  for (uint8_t i = 0; i < TOTAL_SWITCHES; i++)
  {
    pinMode(SWITCH_PINS[i], INPUT_PULLUP);
  }
  pinMode(RESET_PIN, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();
  lcd.clear();

#if defined(WIRE_HAS_TIMEOUT)
  // Don't hang forever if the LCD's I2C wires come loose.
  Wire.setWireTimeout(3000, true);
#endif

  Serial.println(F("Hold RESET for 2 seconds for the wire test screen."));
  Serial.println(F("Loose wires (switches flickering ON/OFF) are reported automatically."));
  enterState(WAITING);
}

void loop()
{
  monitorSwitches();
  updateAlert();

  // RESET works at any time, even during a splash screen.
  //   short press -> new game (or leave the wire test)
  //   hold 2 sec  -> wire test screen
  ButtonEvent button = readResetButton();

  if (button == LONG_PRESS)
  {
    if (state == WIRE_TEST)
    {
      finishWireTest();
    }
    else
    {
      enterState(WIRE_TEST);
    }
  }
  else if (button == SHORT_PRESS)
  {
    if (state == WIRE_TEST)
    {
      finishWireTest();
    }
    else
    {
      Serial.println(F("RESET BUTTON PRESSED"));
      generatePattern();
      enterState(NEW_PATTERN);
    }
  }

  unsigned long elapsed = millis() - stateStart;

  switch (state)
  {
    case WAITING:
      break;

    case NEW_PATTERN:
      if (elapsed >= NEW_PATTERN_MS)
      {
        enterState(PLAYING);
      }
      break;

    case PLAYING:
      if (pollDue())
      {
        uint8_t score = countCorrect(readSwitches());
        if (score == TOTAL_SWITCHES)
        {
          enterState(CONGRATS);
        }
        else
        {
          showScore(score);
        }
      }
      break;

    case CONGRATS:
      if (elapsed >= CONGRATS_MS)
      {
        enterState(SOLVED);
      }
      break;

    case SOLVED:
      // Puzzle stays solved: the key remains on screen until RESET,
      // even if the switches are moved afterwards.
      break;

    case WIRE_TEST:
      if (elapsed >= TEST_INTRO_MS && pollDue())
      {
        showWireTest();
      }
      break;
  }
}
