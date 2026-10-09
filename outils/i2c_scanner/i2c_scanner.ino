/*
  Scanner I2C pour ESP8266 (deux bus) - outil de diagnostic du projet Capteur d'air.
  Moniteur serie a 115200 bauds. Un scan toutes les 5 s.

  Attendu :
    bus capteur (SDA D2 / SCL D1) : 0x12 (PMSA003I)
    bus ecran   (SDA D6 / SCL D7) : 0x3C (OLED)
*/
#include <Wire.h>

#define PIN_SET 14   // D5 : maintient le capteur actif pendant le scan

struct Bus { const char* nom; int sda, scl; };
const Bus BUS[] = {
  {"capteur (D2/D1)", 4, 5},
  {"ecran (D6/D7)",   12, 13},
};

void setup() {
  Serial.begin(115200);
  pinMode(PIN_SET, OUTPUT);
  digitalWrite(PIN_SET, HIGH);
  delay(3000);   // laisse le capteur demarrer
  Serial.println(F("\nScanner I2C"));
}

void loop() {
  for (const Bus& b : BUS) {
    Wire.begin(b.sda, b.scl);
    Serial.printf("Bus %s :\n", b.nom);
    int n = 0;
    for (uint8_t a = 1; a < 127; a++) {
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) {
        Serial.printf("  0x%02X%s\n", a, a == 0x12 ? "  PMSA003I" : a == 0x3C || a == 0x3D ? "  OLED" : "");
        n++;
      }
      yield();
    }
    Serial.printf("  %d peripherique(s)\n", n);
  }
  Serial.println();
  delay(5000);
}
