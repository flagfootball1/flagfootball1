// Pattern Master
// 16-switch pattern puzzle: 8 switches on this board + 8 on the I2C slave.
// Press RESET to generate a random target pattern; match all 16 switches
// to reveal the key word.

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------- Hardware ----------
const uint8_t LCD_ADDRESS   = 0x27;
const uint8_t LCD_COLUMNS   = 16;
const uint8_t SLAVE_ADDRESS = 8;
const uint8_t RESET_PIN     = 13;
const uint8_t SWITCH_PINS[] = { 2, 3, 4, 5, 6, 7, 8, 9 };
const uint8_t NUM_LOCAL     = sizeof(SWITCH_PINS);
const uint8_t TOTAL_SWITCHES = 16;   // 8 local + 8 on the slave

// ---------- Game settings ----------
const char KEY_WORD[] = "TRIANGLE";

const unsigned long DEBOUNCE_MS     = 50;
const unsigned long NEW_PATTERN_MS  = 1500;  // "NEW PATTERN" splash
const unsigned long CONGRATS_MS     = 3000;  // "CONGRATS!" splash
const unsigned long POLL_MS         = 100;   // how often switches are read

const int SCORE_NONE  = -1;  // nothing shown yet (forces a redraw)
const int SCORE_ERROR = -2;  // slave did not answer

LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, 2);

enum GameState
{
  WAITING,      // power-up, waiting for first RESET
  NEW_PATTERN,  // showing "NEW PATTERN" splash
  PLAYING,      // showing score, waiting for 16/16
  CONGRATS,     // showing "CONGRATS!" splash
  SOLVED        // showing the key word until RESET is pressed
};

GameState state = WAITING;
unsigned long stateStart = 0;
uint16_t targetPattern = 0;      // bit i = required state of switch i
int lastShownScore = SCORE_NONE;

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

void showScreen(const char *top, const char *bottom)
{
  showLine(0, top);
  showLine(1, bottom);
}

// ---------- Input ----------

// Non-blocking debounce. Returns true once per press (HIGH -> LOW).
bool resetPressed()
{
  static bool stableState = HIGH;
  static bool lastReading = HIGH;
  static unsigned long lastChange = 0;

  bool reading = digitalRead(RESET_PIN);

  if (reading != lastReading)
  {
    lastReading = reading;
    lastChange = millis();
  }

  if (millis() - lastChange >= DEBOUNCE_MS && reading != stableState)
  {
    stableState = reading;
    return stableState == LOW;
  }

  return false;
}

// Reads all 16 switches into a bitmask (bit i = switch i is ON).
// Bits 0-7 are local, bits 8-15 come from the slave.
// Returns false if the slave did not answer.
bool readSwitches(uint16_t &states)
{
  states = 0;

  for (uint8_t i = 0; i < NUM_LOCAL; i++)
  {
    if (digitalRead(SWITCH_PINS[i]) == LOW)
    {
      states |= (1u << i);
    }
  }

  if (Wire.requestFrom((int)SLAVE_ADDRESS, 1) != 1)
  {
    return false;
  }

  states |= (uint16_t)Wire.read() << 8;
  return true;
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

// True at most once every POLL_MS, so the I2C bus isn't flooded.
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

// Returns 0-16, or SCORE_ERROR if the slave is not responding.
int readScore()
{
  uint16_t states;

  if (!readSwitches(states))
  {
    return SCORE_ERROR;
  }

  return countCorrect(states);
}

void showScore(int score)
{
  if (score == lastShownScore)
  {
    return;  // nothing changed, don't rewrite the LCD
  }
  lastShownScore = score;

  if (score == SCORE_ERROR)
  {
    showScreen("SLAVE NOT FOUND", "CHECK WIRING");
    return;
  }

  char line[LCD_COLUMNS + 1];
  snprintf(line, sizeof(line), "%d/%d CORRECT", score, TOTAL_SWITCHES);
  showScreen("MISSION ACTIVE", line);
}

void enterState(GameState newState)
{
  state = newState;
  stateStart = millis();

  switch (state)
  {
    case WAITING:
      showScreen("MISSION READY", "PRESS RESET");
      break;

    case NEW_PATTERN:
      showScreen("NEW PATTERN", "GENERATED");
      break;

    case PLAYING:
      lastShownScore = SCORE_NONE;  // force the score to be drawn
      break;

    case CONGRATS:
      Serial.println(F("SOLVED"));
      showScreen(" CONGRATS!", "16/16 CORRECT");
      break;

    case SOLVED:
      showScreen("FIRST KEY =", KEY_WORD);
      break;
  }
}

// ---------- Arduino ----------

void setup()
{
  Serial.begin(9600);

  Wire.begin();
#if defined(WIRE_HAS_TIMEOUT)
  // Don't hang forever if the I2C bus locks up (e.g. a loose wire).
  Wire.setWireTimeout(3000, true);
#endif

  randomSeed(analogRead(A0));  // A0 must be left unconnected

  for (uint8_t i = 0; i < NUM_LOCAL; i++)
  {
    pinMode(SWITCH_PINS[i], INPUT_PULLUP);
  }
  pinMode(RESET_PIN, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  enterState(WAITING);
}

void loop()
{
  // RESET works at any time, even during a splash screen.
  if (resetPressed())
  {
    Serial.println(F("RESET BUTTON PRESSED"));
    generatePattern();
    enterState(NEW_PATTERN);
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
    {
      if (!pollDue())
      {
        break;
      }

      int score = readScore();
      if (score == TOTAL_SWITCHES)
      {
        enterState(CONGRATS);
      }
      else
      {
        showScore(score);
      }
      break;
    }

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
  }
}
