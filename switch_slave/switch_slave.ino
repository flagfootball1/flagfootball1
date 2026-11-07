// Switch Slave
// Reads 8 toggle switches and reports them to the master over I2C as one byte.
// Bit i of the byte is 1 when switch i is ON (pulled to GND).

#include <Wire.h>

const uint8_t I2C_ADDRESS = 8;
const uint8_t SWITCH_PINS[] = { 2, 3, 4, 5, 6, 7, 8, 9 };
const uint8_t NUM_SWITCHES = sizeof(SWITCH_PINS);

// Updated continuously in loop(); read by the I2C interrupt handler.
// A single byte is read/written atomically on AVR, so no locking is needed.
volatile uint8_t switchState = 0;

void sendSwitches()
{
  // Runs inside an interrupt: keep it as short as possible.
  Wire.write(switchState);
}

void setup()
{
  for (uint8_t i = 0; i < NUM_SWITCHES; i++)
  {
    pinMode(SWITCH_PINS[i], INPUT_PULLUP);
  }

  Serial.begin(9600);
  Serial.println(F("SLAVE READY"));

  Wire.begin(I2C_ADDRESS);
  Wire.onRequest(sendSwitches);
}

void loop()
{
  uint8_t state = 0;

  for (uint8_t i = 0; i < NUM_SWITCHES; i++)
  {
    if (digitalRead(SWITCH_PINS[i]) == LOW)
    {
      state |= (1 << i);
    }
  }

  // Wiring check: if this board is plugged into the computer, print every
  // switch change. (The master also prints these as switches 9-16.)
  uint8_t changed = state ^ switchState;
  for (uint8_t i = 0; i < NUM_SWITCHES; i++)
  {
    if ((changed >> i) & 1)
    {
      Serial.print(F("Slave switch "));
      Serial.print(i + 1);
      Serial.print(F(" (overall switch "));
      Serial.print(i + 9);
      Serial.print(F(") -> SLAVE pin "));
      Serial.print(SWITCH_PINS[i]);
      Serial.println(((state >> i) & 1) ? F("  ON") : F("  OFF"));
    }
  }

  switchState = state;
}
