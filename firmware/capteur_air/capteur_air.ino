/*
  =============================================================================
   Capteur de qualite de l'air connecte - v2.0
   ESP8266 + Adafruit PMSA003I + OLED SSD1306 128x64 -> ThingSpeak + page web
  =============================================================================

  Bibliotheques a installer (Gestionnaire de bibliotheques Arduino) :
    - Adafruit PM25 AQI
    - Adafruit SSD1306
    - Adafruit GFX Library
    - ThingSpeak (by MathWorks)
    - WiFiManager (by tzapu, version 2.x)
  (ESP8266WebServer, ESP8266mDNS, ArduinoOTA et LittleFS sont fournis avec la carte ESP8266)

  Carte : NodeMCU 1.0 (ESP-12E) ou LOLIN(WEMOS) D1 mini
  Outils > Flash Size : une option avec "FS" (ex. 4MB (FS:2MB OTA:~1019KB))

  Cablage (DEUX_BUS_I2C = 1, reglage par defaut) :
    PMSA003I VIN -> 3V3   GND -> GND   SDA -> D2   SCL -> D1   SET -> D5
    OLED     VCC -> 3V3   GND -> GND   SDA -> D6   SCL -> D7
  Avec DEUX_BUS_I2C = 0 : ecran et capteur ensemble sur D2 (SDA) / D1 (SCL).

  Premier demarrage :
    1. Le capteur cree le reseau Wi-Fi "CapteurAir-XXXX".
    2. Connecte ton telephone a ce reseau : la page de configuration s'ouvre
       (sinon va sur http://192.168.4.1).
    3. Choisis ton Wi-Fi, saisis le mot de passe et, si tu veux, ThingSpeak.
  Ensuite : page web sur http://capteur-air.local (ou l'adresse IP affichee).

  Bouton FLASH (D3) : appui court = page suivante / reveil de l'ecran,
                      appui 3 s = rouvrir le portail de configuration Wi-Fi.

  Moniteur serie 115200 bauds, commandes (lettre + Entree) :
    h aide | s etat | m mesure | p page | e ecran | i scan I2C
    w portail Wi-Fi | x oublier le Wi-Fi | r redemarrage
  =============================================================================
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include "Adafruit_PM25AQI.h"
#include "ThingSpeak.h"
#include "web.h"

#define VERSION "2.2"

// ------------------------------- Reglages -----------------------------------
#define DEBUG                 1         // 0 = aucun message serie
#define DEBUG_CHAQUE_LECTURE  1         // 1 = detail de chaque lecture capteur

#define CYCLE_MIN_DEFAUT      5         // intervalle par defaut (modifiable sur la page web)
#define WARMUP_MS             30000UL   // chauffe apres reveil (fiche Plantower : 30 s)
#define MEASURE_MS            10000UL   // duree de la phase de mesure
#define READ_PERIOD_MS        2000UL    // une lecture toutes les 2 s pendant la mesure
#define UTILISER_SET          1         // 0 = broche SET non cablee, capteur toujours actif

#define TS_MIN_INTERVAL_MS    16000UL   // limite ThingSpeak gratuite : 15 s
#define TS_RETRY_MS           30000UL   // delai avant nouvel essai d'envoi
#define TS_MAX_RETRIES        3         // essais supplementaires avant abandon
#define TS_ENVOYER_STATUT     1         // 1 = envoie aussi un statut texte (uptime, RSSI...)

#define CAPTEUR_INIT_TIMEOUT  30000UL   // recherche du capteur au demarrage
#define CAPTEUR_MAX_ECHECS    3         // cycles sans lecture valide avant reinitialisation

#define HOSTNAME              "capteur-air"   // -> http://capteur-air.local
#define PORTAIL_PREFIXE       "CapteurAir-"   // nom du reseau de configuration
#define PORTAIL_MDP           ""              // mot de passe du portail ("" = ouvert, sinon 8 car. min)
#define PORTAIL_TIMEOUT_S     300             // fermeture du portail sans action (s)
#define WIFI_PORTAIL_APRES_MS 900000UL        // rouvre le portail apres 15 min sans Wi-Fi (0 = jamais)
#define OTA_MDP               ""              // mot de passe des mises a jour par Wi-Fi ("" = aucun)
#define FUSEAU                "CET-1CEST,M3.5.0,M10.5.0/3"   // heure de Paris

#define HISTO_N               288       // points d'historique en memoire (24 h a 5 min)
#define PAGE_DUREE_MS         5000UL    // duree d'affichage de chaque page de l'ecran
#define ECRAN_REVEIL_MS       30000UL   // ecran rallume par le bouton pendant ce temps

#define OLED_ADDR             0x3C      // 0x78 sur l'etiquette = 0x3C en 7 bits
#define DEUX_BUS_I2C          1         // 1 = ecran sur D6/D7, capteur sur D2/D1 (0 = tout sur D2/D1)

// ------------------------------- Broches ------------------------------------
#define PIN_SDA     4    // D2  (capteur, et ecran si un seul bus)
#define PIN_SCL     5    // D1
#define PIN_SET     14   // D5
#define PIN_BOUTON  0    // D3  (bouton FLASH)
#if DEUX_BUS_I2C
  #define ECRAN_SDA 12   // D6
  #define ECRAN_SCL 13   // D7
#else
  #define ECRAN_SDA PIN_SDA
  #define ECRAN_SCL PIN_SCL
#endif

// ------------------------------- Journal ------------------------------------
#if DEBUG
  #define LOG(...) do { logPrefixe(); Serial.printf(__VA_ARGS__); Serial.println(); } while (0)
#else
  #define LOG(...) do {} while (0)
#endif

// Prototypes explicites
void logPrefixe();
void demarrerPortail();
void afficher();

// ------------------------------- Objets -------------------------------------
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Adafruit_PM25AQI aqi;
WiFiClient client;
ESP8266WebServer server(80);
WiFiManager wm;
WiFiEventHandler hGotIp, hDisconnected;

WiFiManagerParameter pInfo("<p>ThingSpeak (facultatif) : laisse vide pour ne pas envoyer les mesures.</p>");
WiFiManagerParameter pChannel("ts_channel", "ThingSpeak - Channel ID", "", 11);
WiFiManagerParameter pKey("ts_key", "ThingSpeak - Write API Key", "", 20);

// ------------------------------- Configuration ------------------------------
struct Config {
  char    tsChannel[12];
  char    tsKey[24];
  uint8_t cycleMin;
  bool    nuitActive;
  uint8_t nuitDebut, nuitFin;
};
Config cfg = {"", "", CYCLE_MIN_DEFAUT, false, 23, 7};

// ------------------------------- Etat ---------------------------------------
enum Etat { CHAUFFE, MESURE, VEILLE };
Etat etat = CHAUFFE;
const char* nomEtat(Etat e) {
  switch (e) {
    case CHAUFFE: return "CHAUFFE";
    case MESURE:  return "MESURE";
    case VEILLE:  return "VEILLE";
  }
  return "?";
}

bool oledOk = false, ecranAllume = true, ecranForceEteint = false;
bool capteurOk = false, otaEnCours = false;
bool portailActif = false, serveurActif = false, servicesOk = false;
char nomAP[24];

unsigned long cycleStartMs = 0, lastReadMs = 0, lastDrawMs = 0;
unsigned long lastWifiOkMs = 0, lastTsTryMs = 0, portailDebutMs = 0;
unsigned long reveilEcranJusqua = 0, pageManuelleJusqua = 0;
unsigned long actionDiffereeMs = 0;
uint8_t actionDifferee = 0;          // 1 portail, 2 oublier Wi-Fi, 3 redemarrer
uint8_t pageManuelle = 0;

// Statistiques d'une phase de mesure
struct Stat {
  uint32_t somme; uint16_t mini, maxi;
  void reset()           { somme = 0; mini = 0xFFFF; maxi = 0; }
  void ajoute(uint16_t v){ somme += v; if (v < mini) mini = v; if (v > maxi) maxi = v; }
};
Stat sPm1, sPm25, sPm10, sP03;
uint16_t nbValides = 0, nbRejets = 0;

struct Resultat { float pm1, pm25, pm10, p03; bool valide; };
Resultat dernier = {0, 0, 0, 0, false};

// Historique circulaire
struct Point { uint32_t ms; uint16_t pm25x10, pm10x10; };
Point histo[HISTO_N];
uint16_t histoNb = 0, histoTete = 0;

// Envoi ThingSpeak
bool envoiEnAttente = false;
uint8_t essaisEnvoi = 0;
int lastHttpCode = 0;

// Compteurs de diagnostic
uint32_t nbCycles = 0, nbEnvoisOk = 0, nbEnvoisKo = 0;
uint32_t nbLecturesKo = 0, nbConnexionsWifi = 0, nbReinitCapteur = 0;
uint8_t  cyclesSansMesure = 0;

volatile bool evtGotIp = false, evtDeconnecte = false;
volatile int  raisonDeconnexion = 0;

// ============================================================================
//                                 Outils
// ============================================================================
void logPrefixe() {
  unsigned long ms = millis();
  Serial.printf("[%7lu.%03lu] ", ms / 1000, ms % 1000);
}

unsigned long cycleMs() { return (unsigned long)cfg.cycleMin * 60000UL; }
unsigned long chauffeMs() { return (UTILISER_SET || nbCycles <= 1) ? WARMUP_MS : 0; }
bool tsActif() { return strtoul(cfg.tsChannel, nullptr, 10) > 0 && strlen(cfg.tsKey) > 0; }

void capteurActif(bool on) {
#if UTILISER_SET
  digitalWrite(PIN_SET, on ? HIGH : LOW);
#else
  (void)on;
#endif
}

const char* fmtDuree(unsigned long ms) {
  static char buf[24];
  unsigned long s = ms / 1000;
  snprintf(buf, sizeof(buf), "%luj %02lu:%02lu:%02lu",
           s / 86400, (s / 3600) % 24, (s / 60) % 60, s % 60);
  return buf;
}

const char* fmtDureeCourte(unsigned long ms) {
  static char buf[16];
  unsigned long m = ms / 60000;
  if (m < 60)        snprintf(buf, sizeof(buf), "%lu min", m);
  else if (m < 1440) snprintf(buf, sizeof(buf), "%luh%02lu", m / 60, m % 60);
  else               snprintf(buf, sizeof(buf), "%luj %02luh", m / 1440, (m / 60) % 24);
  return buf;
}

void fmtVal(float v, char* out, size_t n) {
  if (v < 10)        snprintf(out, n, "%.1f", v);
  else if (v < 1000) snprintf(out, n, "%.0f", v);
  else               snprintf(out, n, "999+");
}

const char* texteCodeTS(int code) {
  switch (code) {
    case 200:  return "OK";
    case 400:  return "cle API incorrecte";
    case 404:  return "URL incorrecte";
    case -101: return "valeur hors plage";
    case -201: return "numero de champ invalide";
    case -210: return "aucun champ renseigne";
    case -301: return "connexion au serveur impossible";
    case -302: return "erreur inattendue";
    case -303: return "reponse serveur illisible";
    case -304: return "delai depasse";
    case -401: return "refuse (envoi trop rapproche ?)";
    default:   return "code inconnu";
  }
}

const char* qualite(float pm25) {
  if (pm25 <= 9)   return "Bon";
  if (pm25 <= 35)  return "Moyen";
  if (pm25 <= 55)  return "Mauvais";
  if (pm25 <= 125) return "Tres mauvais";
  return "Dangereux";
}

const char* qualiteCourte(float pm25) {
  if (pm25 <= 9)   return "BON";
  if (pm25 <= 35)  return "MOYEN";
  if (pm25 <= 55)  return "MAUVAIS";
  if (pm25 <= 125) return "T.MAUVAIS";
  return "DANGER";
}

bool heureLocale(struct tm& t) {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;   // heure pas encore synchronisee
  localtime_r(&now, &t);
  return true;
}

bool estLaNuit() {
  struct tm t;
  if (!cfg.nuitActive || !heureLocale(t)) return false;
  int h = t.tm_hour;
  if (cfg.nuitDebut == cfg.nuitFin) return false;
  if (cfg.nuitDebut < cfg.nuitFin) return h >= cfg.nuitDebut && h < cfg.nuitFin;
  return h >= cfg.nuitDebut || h < cfg.nuitFin;
}

void jsonEsc(const String& s, char* out, size_t n) {
  size_t j = 0;
  for (size_t i = 0; i < s.length() && j + 2 < n; i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { out[j++] = '\\'; out[j++] = c; }
    else if ((uint8_t)c >= 0x20) out[j++] = c;
  }
  out[j] = 0;
}

String chiffres(const String& s) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) if (isdigit(s[i])) o += s[i];
  return o;
}

String htmlEsc(const char* s) {
  String o;
  for (; *s; s++) {
    if (*s == '&') o += F("&amp;"); else if (*s == '<') o += F("&lt;");
    else if (*s == '>') o += F("&gt;"); else if (*s == '"') o += F("&quot;");
    else o += *s;
  }
  return o;
}

// ============================================================================
//                          Configuration (LittleFS)
// ============================================================================
void chargerConfig() {
  if (!LittleFS.begin()) { LOG("LittleFS : montage impossible, reglages par defaut"); return; }
  File f = LittleFS.open("/config.txt", "r");
  if (!f) { LOG("Config : aucun fichier, reglages par defaut"); return; }
  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    int eq = l.indexOf('=');
    if (eq < 0) continue;
    String k = l.substring(0, eq), v = l.substring(eq + 1);
    if      (k == "ts_channel") strlcpy(cfg.tsChannel, v.c_str(), sizeof(cfg.tsChannel));
    else if (k == "ts_key")     strlcpy(cfg.tsKey, v.c_str(), sizeof(cfg.tsKey));
    else if (k == "cycle_min")  cfg.cycleMin = constrain(v.toInt(), 1, 60);
    else if (k == "nuit")       cfg.nuitActive = v.toInt() != 0;
    else if (k == "nuit_debut") cfg.nuitDebut = constrain(v.toInt(), 0, 23);
    else if (k == "nuit_fin")   cfg.nuitFin = constrain(v.toInt(), 0, 23);
  }
  f.close();
  LOG("Config : ThingSpeak %s (canal %s), cycle %u min, nuit %s %u h-%u h",
      tsActif() ? "actif" : "inactif", cfg.tsChannel[0] ? cfg.tsChannel : "-",
      cfg.cycleMin, cfg.nuitActive ? "oui" : "non", cfg.nuitDebut, cfg.nuitFin);
}

void sauverConfig() {
  File f = LittleFS.open("/config.txt", "w");
  if (!f) { LOG("Config : ERREUR d'ecriture"); return; }
  f.printf("ts_channel=%s\nts_key=%s\ncycle_min=%u\nnuit=%d\nnuit_debut=%u\nnuit_fin=%u\n",
           cfg.tsChannel, cfg.tsKey, cfg.cycleMin, cfg.nuitActive ? 1 : 0, cfg.nuitDebut, cfg.nuitFin);
  f.close();
  LOG("Config enregistree");
}

// ============================================================================
//                                   I2C
// ============================================================================
uint8_t busActif = 0;   // 1 = capteur, 2 = ecran

void busSelect(uint8_t b) {
#if !DEUX_BUS_I2C
  b = 1;                // un seul bus : rien a commuter
#endif
  if (busActif == b) return;
  if (b == 1) Wire.begin(PIN_SDA, PIN_SCL);
  else        Wire.begin(ECRAN_SDA, ECRAN_SCL);
  busActif = b;
}

// Libere un bus si un peripherique est reste bloque en tenant SDA a l'etat bas
void i2cDebloque(int sda, int scl) {
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, INPUT_PULLUP);
  delayMicroseconds(10);
  if (digitalRead(sda) == HIGH) return;
  LOG("I2C (SDA=%d) : ligne bloquee, deblocage...", sda);
  pinMode(scl, OUTPUT);
  for (int i = 0; i < 9 && digitalRead(sda) == LOW; i++) {
    digitalWrite(scl, LOW);  delayMicroseconds(10);
    digitalWrite(scl, HIGH); delayMicroseconds(10);
  }
  pinMode(scl, INPUT_PULLUP);
  LOG("I2C (SDA=%d) : %s", sda, digitalRead(sda) == HIGH ? "liberee" : "TOUJOURS BLOQUEE (cablage ?)");
}

void i2cDemarre() {
  i2cDebloque(PIN_SDA, PIN_SCL);
#if DEUX_BUS_I2C
  i2cDebloque(ECRAN_SDA, ECRAN_SCL);
#endif
  busActif = 0;
  busSelect(1);
}

void i2cScanBus(uint8_t bus) {
  busSelect(bus);
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      const char* nom = (a == 0x12) ? "PMSA003I" : (a == OLED_ADDR) ? "OLED" : "inconnu";
      LOG("  0x%02X  %s", a, nom);
      n++;
    }
    yield();
  }
  LOG("  %d peripherique(s)%s", n, n == 0 ? " - verifier SDA/SCL/alimentation" : "");
}

void i2cScan() {
#if DEUX_BUS_I2C
  LOG("Scan I2C bus capteur (D2/D1) :");
  i2cScanBus(1);
  LOG("Scan I2C bus ecran (D6/D7) :");
  i2cScanBus(2);
#else
  LOG("Scan I2C :");
  i2cScanBus(1);
#endif
}

// ============================================================================
//                               Historique
// ============================================================================
uint16_t idxHisto(uint16_t i) { return (histoTete + HISTO_N - histoNb + i) % HISTO_N; }
float histoPm25(uint16_t i) { return histo[idxHisto(i)].pm25x10 / 10.0f; }

void ajouterHisto(float pm25, float pm10) {
  Point& p = histo[histoTete];
  p.ms = millis();
  p.pm25x10 = (uint16_t)min(pm25 * 10.0f, 65000.0f);
  p.pm10x10 = (uint16_t)min(pm10 * 10.0f, 65000.0f);
  histoTete = (histoTete + 1) % HISTO_N;
  if (histoNb < HISTO_N) histoNb++;
}

int tendance() {
  if (histoNb < 2) return 0;
  float a = histoPm25(histoNb - 2), b = histoPm25(histoNb - 1);
  float seuil = max(1.0f, a * 0.15f);
  if (b - a > seuil)  return 1;
  if (a - b > seuil)  return -1;
  return 0;
}

// ============================================================================
//                                  Ecran
// ============================================================================
void texteCentre(const char* s, int y) { display.setCursor((128 - 6 * (int)strlen(s)) / 2, y); display.print(s); }
void texteDroite(const char* s, int y) { display.setCursor(128 - 6 * (int)strlen(s), y); display.print(s); }

bool ecranDoitEtreAllume() {
  if (portailActif || otaEnCours || millis() < reveilEcranJusqua) return true;
  if (ecranForceEteint) return false;
  return !estLaNuit();
}

void basculerEcran() {
  if (ecranAllume) { ecranForceEteint = true; reveilEcranJusqua = 0; }
  else { ecranForceEteint = false; if (estLaNuit()) reveilEcranJusqua = millis() + ECRAN_REVEIL_MS; }
  LOG("Ecran : %s demande", ecranForceEteint ? "extinction" : "allumage");
}

void appliquerEcran(bool allume) {
  if (!oledOk || allume == ecranAllume) return;
  busSelect(2);
  display.ssd1306_command(allume ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
  ecranAllume = allume;
  LOG("Ecran %s", allume ? "allume" : "eteint");
}

// ---- Icones de la barre du haut ----
void iconeWifi(int x, int yb) {        // 11 px de large, bas en yb
  if (portailActif) { display.setCursor(x, yb - 7); display.print("AP"); return; }
  if (WiFi.status() != WL_CONNECTED) {
    display.drawLine(x + 2, yb - 7, x + 8, yb - 1, SSD1306_WHITE);
    display.drawLine(x + 8, yb - 7, x + 2, yb - 1, SSD1306_WHITE);
    return;
  }
  int r = WiFi.RSSI();
  int n = r > -55 ? 4 : r > -65 ? 3 : r > -75 ? 2 : 1;
  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 2, bx = x + i * 3;
    if (i < n) display.fillRect(bx, yb - h + 1, 2, h, SSD1306_WHITE);
    else       display.drawFastHLine(bx, yb, 2, SSD1306_WHITE);
  }
}

void flecheEnvoi(int x, int y) {
  display.fillTriangle(x + 4, y, x, y + 4, x + 8, y + 4, SSD1306_WHITE);
  display.fillRect(x + 3, y + 4, 3, 3, SSD1306_WHITE);
  display.drawFastHLine(x, y + 8, 9, SSD1306_WHITE);
}

void iconeEnvoi(int x, int y) {        // 9x9
  if (!tsActif()) return;
  if (envoiEnAttente) { if ((millis() / 400) % 2) flecheEnvoi(x, y); }
  else if (lastHttpCode == 200) flecheEnvoi(x, y);
  else if (lastHttpCode != 0) {
    display.drawRect(x, y, 9, 9, SSD1306_WHITE);
    display.drawFastVLine(x + 4, y + 2, 3, SSD1306_WHITE);
    display.drawPixel(x + 4, y + 6, SSD1306_WHITE);
  }
}

void iconeVentilo(int cx, int cy) {    // tourne pendant la chauffe et la mesure
  display.drawCircle(cx, cy, 4, SSD1306_WHITE);
  float a = ((millis() / 120) % 8) * (PI / 4);
  int dx = (int)roundf(3 * cosf(a)), dy = (int)roundf(3 * sinf(a));
  display.drawLine(cx - dx, cy - dy, cx + dx, cy + dy, SSD1306_WHITE);
}

void iconeTendance(int x, int y, int t) {   // 7x7
  if (t > 0)      display.fillTriangle(x + 3, y, x, y + 6, x + 6, y + 6, SSD1306_WHITE);
  else if (t < 0) display.fillTriangle(x, y, x + 6, y, x + 3, y + 6, SSD1306_WHITE);
  else { display.drawFastHLine(x, y + 3, 4, SSD1306_WHITE);
         display.fillTriangle(x + 3, y, x + 3, y + 6, x + 6, y + 3, SSD1306_WHITE); }
}

void barreHaut(const char* titre) {
  display.setCursor(0, 3);
  display.print(titre);
  struct tm t;
  if (heureLocale(t)) {
    char h[6];
    strftime(h, sizeof(h), "%H:%M", &t);
    display.setCursor(49, 3);
    display.print(h);
  }
  if (!capteurOk) { display.setCursor(93, 3); display.print("!"); }
  else if (etat != VEILLE) iconeVentilo(95, 6);
  iconeEnvoi(103, 2);
  iconeWifi(116, 10);
  display.drawFastHLine(0, 13, 128, SSD1306_WHITE);
}

// ---- Page 1 : valeur principale ----
const float ECHELLE_MAX = 75.0f;
int xJauge(float v) { return 1 + (int)(constrain(v, 0.0f, ECHELLE_MAX) * 125.0f / ECHELLE_MAX); }

void pagePrincipale() {
  barreHaut("PM2.5");

  char v[8];
  if (dernier.valide) fmtVal(dernier.pm25, v, sizeof(v)); else strcpy(v, "--");
  display.setFont(&FreeSansBold18pt7b);
  display.setCursor(0, 42);
  display.print(v);
  display.setFont(nullptr);

  // Pastille de qualite
  const char* b = !capteurOk ? "CAPTEUR?" : !dernier.valide ? "ATTENTE" : qualiteCourte(dernier.pm25);
  int w = 6 * strlen(b) + 5, x = 128 - w;
  display.fillRoundRect(x, 17, w, 11, 3, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(x + 3, 19);
  display.print(b);
  display.setTextColor(SSD1306_WHITE);

  // Unite et tendance
  display.setCursor(98, 33);
  display.print((char)230);      // µ dans la police de base (cp437)
  display.print("g/m3");
  if (dernier.valide && histoNb >= 2) iconeTendance(87, 33, tendance());

  // Jauge 0-75 avec reperes 9 / 35 / 55
  display.drawRect(0, 46, 128, 6, SSD1306_WHITE);
  if (dernier.valide) display.fillRect(1, 47, xJauge(dernier.pm25), 4, SSD1306_WHITE);
  const float seuils[] = {9, 35, 55};
  for (float s : seuils) display.drawFastVLine(xJauge(s), 47, 4, SSD1306_INVERSE);

  // Ligne du bas : PM1 / PM10 et progression du cycle
  char l[24], a[8], c[8];
  if (dernier.valide) {
    fmtVal(dernier.pm1, a, sizeof(a));
    fmtVal(dernier.pm10, c, sizeof(c));
    snprintf(l, sizeof(l), "PM1 %s PM10 %s", a, c);
    display.setCursor(0, 56);
    display.print(l);
  }
  unsigned long e = millis() - cycleStartMs;
  switch (etat) {
    case CHAUFFE: snprintf(l, sizeof(l), "%lus", (chauffeMs() - min(e, chauffeMs())) / 1000); break;
    case MESURE:  snprintf(l, sizeof(l), "%u/%lu", nbValides, MEASURE_MS / READ_PERIOD_MS); break;
    case VEILLE: {
      unsigned long r = (cycleMs() - min(e, cycleMs())) / 1000;
      snprintf(l, sizeof(l), "%lu:%02lu", r / 60, r % 60);
      break;
    }
  }
  texteDroite(l, 56);
}

// ---- Page 2 : historique ----
void pageHisto() {
  barreHaut("HISTO");
  int n = min<int>(histoNb, 128);
  if (n < 2) {
    texteCentre("Historique", 28);
    texteCentre("en cours...", 40);
    return;
  }
  float vmaxReel = 0;
  for (int i = 0; i < n; i++) vmaxReel = max(vmaxReel, histoPm25(histoNb - n + i));
  float vmax = max(20.0f, vmaxReel) * 1.15f;
  const int y0 = 63, h = 46;
  for (int i = 0; i < n; i++) {
    int y = y0 - (int)(histoPm25(histoNb - n + i) * h / vmax);
    display.drawFastVLine(128 - n + i, y, y0 - y + 1, SSD1306_WHITE);
  }
  int yo = y0 - (int)(15.0f * h / vmax);     // repere OMS 24 h
  for (int x = 0; x < 128; x += 3) display.drawPixel(x, yo, SSD1306_INVERSE);

  char t[16], v[8];
  fmtVal(vmaxReel, v, sizeof(v));
  snprintf(t, sizeof(t), "max %s", v);
  display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
  display.setCursor(0, 16);
  display.print(t);
  unsigned long age = millis() - histo[idxHisto(histoNb - n)].ms;
  snprintf(t, sizeof(t), "-%s", fmtDureeCourte(age));
  display.setCursor(0, 56);
  display.print(t);
  display.setTextColor(SSD1306_WHITE);
}

// ---- Page 3 : informations ----
void pageInfos() {
  barreHaut("INFOS");
  char l[26], a[8], b[8];
  if (dernier.valide) {
    fmtVal(dernier.pm1, a, sizeof(a));
    fmtVal(dernier.pm10, b, sizeof(b));
    snprintf(l, sizeof(l), "PM1 %s  PM10 %s", a, b);
    display.setCursor(0, 16); display.print(l);
    snprintf(l, sizeof(l), ">0.3um %.0f /0.1L", dernier.p03);
    display.setCursor(0, 25); display.print(l);
  } else {
    display.setCursor(0, 16); display.print("Pas encore de mesure");
  }
  display.setCursor(0, 34);
  if (WiFi.status() == WL_CONNECTED) {
    display.print("IP "); display.print(WiFi.localIP().toString());
    display.setCursor(0, 43); display.print(HOSTNAME ".local");
  } else {
    display.print("Wi-Fi deconnecte");
  }
  display.setCursor(0, 52);
  display.print("En marche ");
  display.print(fmtDureeCourte(millis()));
}

// ---- Ecrans speciaux ----
void ecranDemarrage(const char* msg, int pct) {
  if (!oledOk) return;
  busSelect(2);
  display.clearDisplay();
  for (int k = 0; k < 3; k++) {           // logo : trois souffles d'air
    int y = 9 + k * 7;
    for (int x = 0; x < 22; x++) {
      int yy = y + (int)roundf(2 * sinf((x + k * 3) * 0.55f));
      display.drawPixel(3 + x, yy, SSD1306_WHITE);
      display.drawPixel(3 + x, yy + 1, SSD1306_WHITE);
    }
  }
  display.setFont(&FreeSansBold9pt7b);
  display.setCursor(32, 15); display.print("Capteur");
  display.setCursor(32, 32); display.print("d'air");
  display.setFont(nullptr);
  display.setCursor(104, 25); display.print("v" VERSION);
  display.setCursor(0, 42); display.print(msg);
  display.drawRect(0, 55, 128, 7, SSD1306_WHITE);
  display.fillRect(2, 57, constrain(pct, 0, 100) * 124 / 100, 3, SSD1306_WHITE);
  display.display();
}

void ecranPortail() {
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  texteCentre("CONFIG WI-FI", 2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 16); display.print("1. Connecte-toi a :");
  display.setCursor(6, 26); display.print(nomAP);
  display.setCursor(0, 37); display.print("2. Ouvre :");
  display.setCursor(6, 47); display.print("192.168.4.1");
  unsigned long e = (millis() - portailDebutMs) / 1000;
  unsigned long r = PORTAIL_TIMEOUT_S > e ? PORTAIL_TIMEOUT_S - e : 0;
  char t[8];
  snprintf(t, sizeof(t), "%lu:%02lu", r / 60, r % 60);
  texteDroite(t, 56);
}

void ecranOta(unsigned int pct) {
  if (!oledOk) return;
  busSelect(2);
  display.clearDisplay();
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  texteCentre("MISE A JOUR", 2);
  display.setTextColor(SSD1306_WHITE);
  char t[8];
  snprintf(t, sizeof(t), "%u%%", pct);
  display.setFont(&FreeSansBold9pt7b);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(t, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 38);
  display.print(t);
  display.setFont(nullptr);
  display.drawRect(0, 48, 128, 9, SSD1306_WHITE);
  display.fillRect(2, 50, pct * 124 / 100, 5, SSD1306_WHITE);
  display.display();
}

uint8_t pageCourante() {
  if (millis() < pageManuelleJusqua) return pageManuelle;
  static const uint8_t seq[] = {0, 1, 0, 2};
  return seq[(millis() / PAGE_DUREE_MS) % 4];
}

void afficher() {
  if (!oledOk || otaEnCours) return;
  appliquerEcran(ecranDoitEtreAllume());
  if (!ecranAllume) return;
  busSelect(2);
  display.clearDisplay();
  display.setFont(nullptr);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  if (portailActif) ecranPortail();
  else switch (pageCourante()) {
    case 1:  pageHisto(); break;
    case 2:  pageInfos(); break;
    default: pagePrincipale(); break;
  }
  display.display();
}

// ============================================================================
//                                 Capteur
// ============================================================================
// La bibliotheque Adafruit PM25 AQI v2 n'accepte qu'UN seul appel a begin_I2C() :
// les appels suivants renvoient toujours false, meme si le capteur repond.
// On l'appelle donc une seule fois, et on detecte le capteur nous-memes (ACK en 0x12).
bool aqiInitialise = false;

uint8_t sondeCapteur() {               // 0 = repond, 2 = aucune reponse, 4 = bus en erreur
  busSelect(1);
  Wire.beginTransmission(0x12);
  return Wire.endTransmission();
}

const char* texteSonde(uint8_t r) {
  switch (r) {
    case 0:  return "repond";
    case 2:  return "aucune reponse en 0x12 (alimentation, SDA/SCL, SET ?)";
    case 4:  return "erreur de bus (ligne bloquee ?)";
    default: return "erreur I2C";
  }
}

bool capteurDetecte(uint8_t* code) {
  uint8_t r = sondeCapteur();
  if (code) *code = r;
  if (r != 0) return false;
  if (!aqiInitialise) {
    aqi.begin_I2C();
    aqiInitialise = true;
    LOG("Bibliotheque PM25 AQI initialisee");
  }
  return true;
}

bool capteurInit(unsigned long timeoutMs) {
  capteurActif(true);
  unsigned long t0 = millis();
  int essai = 0;
  uint8_t r = 0;
  while (millis() - t0 < timeoutMs) {
    essai++;
    if (capteurDetecte(&r)) {
      LOG("Capteur PMSA003I detecte (essai %d, %lu ms)", essai, millis() - t0);
      return true;
    }
    char m[24];
    snprintf(m, sizeof(m), "Capteur... %lu s", (millis() - t0) / 1000);
    ecranDemarrage(m, 20 + (int)((millis() - t0) * 30 / timeoutMs));
    delay(500);
  }
  LOG("ERREUR : PMSA003I introuvable apres %d essais : %s", essai, texteSonde(r));
  return false;
}

void capteurReinit() {
  nbReinitCapteur++;
  LOG("Reinitialisation du capteur (#%lu)", (unsigned long)nbReinitCapteur);
  capteurActif(false);
  delay(2000);
  i2cDemarre();
  capteurOk = capteurInit(15000);
  i2cScan();
}

void debutCycle() {
  capteurActif(true);
  etat = CHAUFFE;
  cycleStartMs = millis();
  sPm1.reset(); sPm25.reset(); sPm10.reset(); sP03.reset();
  nbValides = nbRejets = 0;
  nbCycles++;
  LOG("---- Cycle %lu : capteur actif, chauffe %lu s ----",
      (unsigned long)nbCycles, chauffeMs() / 1000);
}

void lireCapteur() {
  if (!capteurOk) {
    uint8_t r;
    capteurOk = capteurDetecte(&r);
    if (!capteurOk) {
      nbLecturesKo++;
      static uint32_t cycleSignale = 0;
      if (cycleSignale != nbCycles) {        // un seul message par cycle
        cycleSignale = nbCycles;
        LOG("Capteur absent : %s", texteSonde(r));
      }
      return;
    }
    LOG("Capteur retrouve");
  }
  busSelect(1);

  PM25_AQI_Data d;
  if (!aqi.read(&d)) {
    nbLecturesKo++;
    LOG("Lecture echouee (I2C ou checksum)");
    return;
  }

  bool coherent = d.pm10_env <= d.pm25_env && d.pm25_env <= d.pm100_env && d.pm100_env < 2000;
  if (!coherent) {
    nbRejets++;
    LOG("Lecture rejetee (incoherente) : PM1=%u PM2.5=%u PM10=%u",
        d.pm10_env, d.pm25_env, d.pm100_env);
    return;
  }

  sPm1.ajoute(d.pm10_env);
  sPm25.ajoute(d.pm25_env);
  sPm10.ajoute(d.pm100_env);
  sP03.ajoute(d.particles_03um);
  nbValides++;

#if DEBUG_CHAQUE_LECTURE
  LOG("Lecture %u : env PM1=%u PM2.5=%u PM10=%u | std %u/%u/%u | /0.1L >0.3:%u >0.5:%u >1:%u >2.5:%u >5:%u >10:%u",
      nbValides, d.pm10_env, d.pm25_env, d.pm100_env,
      d.pm10_standard, d.pm25_standard, d.pm100_standard,
      d.particles_03um, d.particles_05um, d.particles_10um,
      d.particles_25um, d.particles_50um, d.particles_100um);
#endif
}

void finMesure() {
  capteurActif(false);
  etat = VEILLE;

  if (nbValides == 0) {
    cyclesSansMesure++;
    LOG("ATTENTION : aucune lecture valide ce cycle (%u rejet(s), %u/%u cycles en echec)",
        nbRejets, cyclesSansMesure, CAPTEUR_MAX_ECHECS);
    if (cyclesSansMesure >= CAPTEUR_MAX_ECHECS) {
      capteurReinit();
      cyclesSansMesure = 0;
      capteurActif(false);
    }
    return;
  }
  cyclesSansMesure = 0;

  dernier.pm1  = (float)sPm1.somme  / nbValides;
  dernier.pm25 = (float)sPm25.somme / nbValides;
  dernier.pm10 = (float)sPm10.somme / nbValides;
  dernier.p03  = (float)sP03.somme  / nbValides;
  dernier.valide = true;
  ajouterHisto(dernier.pm25, dernier.pm10);

  LOG("Moyenne sur %u lecture(s) [%u rejet(s)] :", nbValides, nbRejets);
  LOG("  PM1   %6.1f  (min %u, max %u) ug/m3", dernier.pm1,  sPm1.mini,  sPm1.maxi);
  LOG("  PM2.5 %6.1f  (min %u, max %u) ug/m3 -> %s", dernier.pm25, sPm25.mini, sPm25.maxi, qualite(dernier.pm25));
  LOG("  PM10  %6.1f  (min %u, max %u) ug/m3", dernier.pm10, sPm10.mini, sPm10.maxi);
  LOG("  >0.3um %6.0f /0.1L", dernier.p03);

  if (tsActif()) {
    if (envoiEnAttente) LOG("Envoi precedent non abouti : remplace par la nouvelle moyenne");
    envoiEnAttente = true;
    essaisEnvoi = 0;
  }
  LOG("Capteur en veille, prochaine mesure dans %lu s",
      (cycleMs() - min(millis() - cycleStartMs, cycleMs())) / 1000);
}

// ============================================================================
//                               ThingSpeak
// ============================================================================
void envoyerThingSpeak() {
  lastTsTryMs = millis();
  essaisEnvoi++;

  ThingSpeak.setField(1, dernier.pm1);
  ThingSpeak.setField(2, dernier.pm25);
  ThingSpeak.setField(3, dernier.pm10);
  ThingSpeak.setField(4, dernier.p03);
#if TS_ENVOYER_STATUT
  char statut[80];
  snprintf(statut, sizeof(statut), "v%s, uptime %s, RSSI %d dBm, heap %u",
           VERSION, fmtDuree(millis()), WiFi.RSSI(), ESP.getFreeHeap());
  ThingSpeak.setStatus(statut);
#endif

  unsigned long t0 = millis();
  lastHttpCode = ThingSpeak.writeFields(strtoul(cfg.tsChannel, nullptr, 10), cfg.tsKey);
  unsigned long duree = millis() - t0;

  if (lastHttpCode == 200) {
    nbEnvoisOk++;
    envoiEnAttente = false;
    LOG("ThingSpeak : envoi OK (%lu ms, essai %u)", duree, essaisEnvoi);
  } else {
    nbEnvoisKo++;
    LOG("ThingSpeak : ECHEC code %d = %s (%lu ms, essai %u/%u)",
        lastHttpCode, texteCodeTS(lastHttpCode), duree, essaisEnvoi, 1 + TS_MAX_RETRIES);
    if (essaisEnvoi > TS_MAX_RETRIES) {
      envoiEnAttente = false;
      LOG("ThingSpeak : abandon de cette mesure");
    }
  }
}

void gererEnvoi() {
  if (!envoiEnAttente || !tsActif()) { envoiEnAttente = false; return; }
  if (WiFi.status() != WL_CONNECTED || portailActif) return;
  unsigned long attente = (essaisEnvoi == 0) ? TS_MIN_INTERVAL_MS : TS_RETRY_MS;
  if (lastTsTryMs != 0 && millis() - lastTsTryMs < attente) return;
  envoyerThingSpeak();
}

// ============================================================================
//                                Page web
// ============================================================================
void handleApi() {
  char ssid[40];
  jsonEsc(WiFi.SSID(), ssid, sizeof(ssid));
  char hm[6] = "";
  struct tm t;
  if (heureLocale(t)) strftime(hm, sizeof(hm), "%H:%M", &t);
  unsigned long e = millis() - cycleStartMs;
  unsigned long prochaine = etat == VEILLE ? (cycleMs() - min(e, cycleMs())) / 1000 : 0;

  char b[720];
  snprintf(b, sizeof(b),
    "{\"version\":\"%s\",\"valide\":%s,\"pm1\":%.1f,\"pm25\":%.1f,\"pm10\":%.1f,\"p03\":%.0f,"
    "\"capteur\":%s,\"etat\":\"%s\",\"prochaine_s\":%lu,\"cycle_min\":%u,"
    "\"wifi_ok\":%s,\"ssid\":\"%s\",\"rssi\":%d,\"ip\":\"%s\",\"heure\":\"%s\","
    "\"uptime_s\":%lu,\"heap\":%u,\"ecran\":%s,"
    "\"ts_actif\":%s,\"ts_channel\":\"%s\",\"ts_code\":%d,\"ts_ok\":%lu,\"ts_ko\":%lu,\"ts_attente\":%s,"
    "\"histo\":[",
    VERSION, dernier.valide ? "true" : "false", dernier.pm1, dernier.pm25, dernier.pm10, dernier.p03,
    capteurOk ? "true" : "false", nomEtat(etat), prochaine, cfg.cycleMin,
    WiFi.status() == WL_CONNECTED ? "true" : "false", ssid, WiFi.RSSI(),
    WiFi.localIP().toString().c_str(), hm,
    millis() / 1000, ESP.getFreeHeap(), ecranAllume ? "true" : "false",
    tsActif() ? "true" : "false", cfg.tsChannel, lastHttpCode,
    (unsigned long)nbEnvoisOk, (unsigned long)nbEnvoisKo, envoiEnAttente ? "true" : "false");

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "");
  server.sendContent(b);

  char c[512];
  size_t n = 0;
  unsigned long now = millis();
  for (uint16_t i = 0; i < histoNb; i++) {
    const Point& p = histo[idxHisto(i)];
    n += snprintf(c + n, sizeof(c) - n, "%s[%lu,%.1f,%.1f]", i ? "," : "",
                  (now - p.ms) / 1000, p.pm25x10 / 10.0f, p.pm10x10 / 10.0f);
    if (n > sizeof(c) - 48) { server.sendContent(c); n = 0; }
  }
  if (n) server.sendContent(c);
  server.sendContent("]}");
  server.sendContent("");
}

String optionsHeures(uint8_t sel) {
  String s;
  for (int h = 0; h < 24; h++) {
    s += "<option value=\"" + String(h) + "\"" + (h == sel ? " selected" : "") + ">" + String(h) + " h</option>";
  }
  return s;
}

void handleConfigGet() {
  String p;
  p.reserve(3500);
  p += F("<!doctype html><html lang=\"fr\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
         "<title>Paramètres</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body><main>"
         "<header><h1>Paramètres</h1><a href=\"/\">← Retour</a></header>");
  if (server.hasArg("ok")) p += F("<div class=\"ok\">Paramètres enregistrés.</div>");

  p += F("<form class=\"card\" method=\"post\" action=\"/config\"><h2>ThingSpeak</h2>"
         "<label for=\"c\">Channel ID</label><input id=\"c\" name=\"ts_channel\" inputmode=\"numeric\" value=\"");
  p += htmlEsc(cfg.tsChannel);
  p += F("\"><label for=\"k\">Write API Key</label><input id=\"k\" name=\"ts_key\" autocomplete=\"off\" value=\"");
  p += htmlEsc(cfg.tsKey);
  p += F("\"><div class=\"hint\">Laisse vide pour ne pas envoyer les mesures.</div>"
         "<h2 style=\"margin-top:22px\">Mesure</h2><label for=\"y\">Intervalle entre deux mesures</label><select id=\"y\" name=\"cycle\">");
  const uint8_t cycles[] = {1, 2, 5, 10, 15, 30, 60};
  for (uint8_t c : cycles) {
    p += "<option value=\"" + String(c) + "\"" + (c == cfg.cycleMin ? " selected" : "") + ">" + String(c) + " min</option>";
  }
  p += F("</select><div class=\"hint\">Plus l'intervalle est long, plus le capteur dure longtemps.</div>"
         "<h2 style=\"margin-top:22px\">Écran</h2><label class=\"ck\"><input type=\"checkbox\" name=\"nuit\" value=\"1\"");
  if (cfg.nuitActive) p += F(" checked");
  p += F(">Éteindre l'écran la nuit</label><div class=\"two\"><div><label>De</label><select name=\"nuit_debut\">");
  p += optionsHeures(cfg.nuitDebut);
  p += F("</select></div><div><label>À</label><select name=\"nuit_fin\">");
  p += optionsHeures(cfg.nuitFin);
  p += F("</select></div></div><div class=\"hint\">Le bouton FLASH rallume l'écran 30 s.</div>"
         "<div class=\"btns\"><button class=\"pri\" type=\"submit\">Enregistrer</button></div></form>"
         "<section class=\"card\"><h2>Wi-Fi</h2><dl><dt>Réseau</dt><dd>");
  p += htmlEsc(WiFi.SSID().c_str());
  p += F("</dd><dt>Adresse</dt><dd>");
  p += WiFi.localIP().toString();
  p += F("</dd></dl><div class=\"btns\">"
         "<form method=\"post\" action=\"/wifi\"><button>Changer de réseau</button></form>"
         "<form method=\"post\" action=\"/oublier\" onsubmit=\"return confirm('Oublier le réseau Wi-Fi ? Le capteur redémarrera en mode configuration.')\">"
         "<button class=\"dan\">Oublier le réseau</button></form>"
         "<form method=\"post\" action=\"/restart\"><button>Redémarrer</button></form>"
         "</div><div class=\"hint\">« Changer de réseau » ouvre le réseau de configuration ");
  p += nomAP;
  p += F(" pendant 5 min.</div></section></main></body></html>");
  server.send(200, "text/html; charset=utf-8", p);
}

void handleConfigPost() {
  String digits = chiffres(server.arg("ts_channel"));
  strlcpy(cfg.tsChannel, digits.c_str(), sizeof(cfg.tsChannel));
  String k = server.arg("ts_key");
  k.trim();
  strlcpy(cfg.tsKey, k.c_str(), sizeof(cfg.tsKey));
  if (server.hasArg("cycle")) cfg.cycleMin = constrain(server.arg("cycle").toInt(), 1, 60);
  cfg.nuitActive = server.hasArg("nuit");
  if (server.hasArg("nuit_debut")) cfg.nuitDebut = constrain(server.arg("nuit_debut").toInt(), 0, 23);
  if (server.hasArg("nuit_fin"))   cfg.nuitFin = constrain(server.arg("nuit_fin").toInt(), 0, 23);
  sauverConfig();
  pChannel.setValue(cfg.tsChannel, 11);
  pKey.setValue(cfg.tsKey, 20);
  LOG("Web : parametres modifies (ThingSpeak %s, cycle %u min, nuit %s)",
      tsActif() ? "actif" : "inactif", cfg.cycleMin, cfg.nuitActive ? "oui" : "non");
  server.sendHeader("Location", "/config?ok=1");
  server.send(303);
}

void pageMessage(const char* titre, const char* texte) {
  String p = F("<!doctype html><html lang=\"fr\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
               "<link rel=\"stylesheet\" href=\"/style.css\"></head><body><main><section class=\"card\"><h2>");
  p += titre;
  p += F("</h2><p>");
  p += texte;
  p += F("</p></section></main></body></html>");
  server.send(200, "text/html; charset=utf-8", p);
}

void configurerServeur() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html; charset=utf-8", PAGE_INDEX); });
  server.on("/style.css", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "max-age=86400");
    server.send_P(200, "text/css", CSS_PAGE);
  });
  server.on("/api", HTTP_GET, handleApi);
  server.on("/config", HTTP_GET, handleConfigGet);
  server.on("/config", HTTP_POST, handleConfigPost);
  server.on("/mesure", HTTP_POST, []() {
    if (etat == VEILLE) { LOG("Web : mesure immediate"); debutCycle(); }
    server.send(204);
  });
  server.on("/ecran", HTTP_POST, []() {
    basculerEcran();
    server.send(204);
  });
  server.on("/wifi", HTTP_POST, []() {
    String t = String(F("Connecte-toi au réseau <b>")) + nomAP + F("</b> puis ouvre <b>192.168.4.1</b>. Le portail reste ouvert 5 minutes.");
    pageMessage("Portail de configuration ouvert", t.c_str());
    actionDifferee = 1; actionDiffereeMs = millis();
  });
  server.on("/oublier", HTTP_POST, []() {
    String t = String(F("Le capteur redémarre et ouvre le réseau <b>")) + nomAP + F("</b>.");
    pageMessage("Réseau oublié", t.c_str());
    actionDifferee = 2; actionDiffereeMs = millis();
  });
  server.on("/restart", HTTP_POST, []() {
    pageMessage("Redémarrage", "Recharge la page dans une vingtaine de secondes.");
    actionDifferee = 3; actionDiffereeMs = millis();
  });
  server.onNotFound([]() { server.send(404, "text/plain", "Page introuvable"); });
}

// ============================================================================
//                       Wi-Fi, portail, OTA, heure
// ============================================================================
void sauverParamsPortail() {
  String digits = chiffres(pChannel.getValue());
  strlcpy(cfg.tsChannel, digits.c_str(), sizeof(cfg.tsChannel));
  String k = pKey.getValue();
  k.trim();
  strlcpy(cfg.tsKey, k.c_str(), sizeof(cfg.tsKey));
  sauverConfig();
  LOG("Portail : parametres ThingSpeak enregistres (%s)", tsActif() ? "actif" : "inactif");
}

void configurerWiFiManager() {
  pChannel.setValue(cfg.tsChannel, 11);
  pKey.setValue(cfg.tsKey, 20);
  wm.addParameter(&pInfo);
  wm.addParameter(&pChannel);
  wm.addParameter(&pKey);
  wm.setTitle("Capteur d'air");
  wm.setDarkMode(true);
  std::vector<const char*> menu = {"wifi", "info", "sep", "restart"};
  wm.setMenu(menu);
  wm.setHostname(HOSTNAME);
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(PORTAIL_TIMEOUT_S);
  wm.setConnectTimeout(20);
  wm.setWiFiAutoReconnect(true);
  wm.setCountry("FR");
  wm.setDebugOutput(false);
  wm.setAPCallback([](WiFiManager*) {
    portailActif = true;
    portailDebutMs = millis();
    LOG("Portail ouvert : reseau \"%s\", page http://192.168.4.1 (%d s)", nomAP, PORTAIL_TIMEOUT_S);
  });
  wm.setSaveParamsCallback(sauverParamsPortail);
}

void demarrerPortail() {
  if (portailActif) return;
  if (serveurActif) { server.stop(); serveurActif = false; }
  LOG("Ouverture du portail de configuration Wi-Fi");
  wm.startConfigPortal(nomAP, strlen(PORTAIL_MDP) ? PORTAIL_MDP : nullptr);
}

void gererPortail() {
  if (!portailActif) return;
  wm.process();
  if (!wm.getConfigPortalActive()) {
    portailActif = false;
    LOG("Portail ferme (%s)", WiFi.status() == WL_CONNECTED ? "Wi-Fi connecte" : "delai ecoule");
    if (WiFi.status() == WL_CONNECTED) WiFi.mode(WIFI_STA);
    else { WiFi.mode(WIFI_STA); WiFi.begin(); }
    lastWifiOkMs = millis();
  }
}

void demarrerServices() {
  configTime(FUSEAU, "pool.ntp.org", "time.google.com");

  ArduinoOTA.setHostname(HOSTNAME);
  if (strlen(OTA_MDP)) ArduinoOTA.setPassword(OTA_MDP);
  ArduinoOTA.onStart([]() {
    otaEnCours = true;
    capteurActif(false);
    LOG("OTA : debut de la mise a jour");
    ecranOta(0);
  });
  ArduinoOTA.onProgress([](unsigned int fait, unsigned int total) {
    static unsigned int precPct = 101;
    unsigned int pct = total ? fait / (total / 100 + 1) : 0;
    if (pct > 100) pct = 100;
    if (pct != precPct) { precPct = pct; ecranOta(pct); }
  });
  ArduinoOTA.onEnd([]() { LOG("OTA : termine, redemarrage"); ecranDemarrage("Mise a jour OK", 100); });
  ArduinoOTA.onError([](ota_error_t err) { LOG("OTA : ERREUR %u", (unsigned)err); otaEnCours = false; });
  ArduinoOTA.begin();
  MDNS.addService("http", "tcp", 80);
  servicesOk = true;
  LOG("Services : heure (NTP), mDNS %s.local, mises a jour par Wi-Fi (OTA)", HOSTNAME);
}

void gererWifi() {
  if (evtGotIp) {
    evtGotIp = false;
    nbConnexionsWifi++;
    LOG("Wi-Fi : connecte a \"%s\", IP %s, RSSI %d dBm, canal %d",
        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());
  }
  if (evtDeconnecte) {
    evtDeconnecte = false;
    static int derniereRaison = -1;
    static unsigned long dernierLog = 0;
    if (raisonDeconnexion != derniereRaison || millis() - dernierLog > 60000) {
      derniereRaison = raisonDeconnexion;
      dernierLog = millis();
      const char* aide = "";
      switch (raisonDeconnexion) {
        case 2: case 15: case 202: aide = " (mot de passe incorrect ?)"; break;
        case 201:                  aide = " (reseau introuvable : nom, ou 5 GHz ?)"; break;
        case 200: case 4:          aide = " (signal faible ?)"; break;
      }
      LOG("Wi-Fi : deconnecte, raison %d%s", raisonDeconnexion, aide);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    lastWifiOkMs = millis();
    if (!servicesOk) demarrerServices();
    if (!serveurActif && !portailActif) {
      server.begin();
      serveurActif = true;
      LOG("Page web : http://%s/  ou  http://%s.local/", WiFi.localIP().toString().c_str(), HOSTNAME);
    }
  } else if (!portailActif && WIFI_PORTAIL_APRES_MS > 0 && millis() - lastWifiOkMs > WIFI_PORTAIL_APRES_MS) {
    LOG("Wi-Fi absent depuis %lu min : ouverture du portail", WIFI_PORTAIL_APRES_MS / 60000);
    lastWifiOkMs = millis();
    demarrerPortail();
  }
}

void gererActionsDifferees() {
  if (!actionDifferee || millis() - actionDiffereeMs < 1500) return;
  uint8_t a = actionDifferee;
  actionDifferee = 0;
  if (a == 1) demarrerPortail();
  else if (a == 2) { LOG("Oubli du reseau Wi-Fi et redemarrage"); wm.resetSettings(); delay(200); ESP.restart(); }
  else if (a == 3) { LOG("Redemarrage demande"); delay(200); ESP.restart(); }
}

// ============================================================================
//                                 Bouton
// ============================================================================
void gererBouton() {
  static bool prec = HIGH, longTraite = false;
  static unsigned long t0 = 0;
  bool b = digitalRead(PIN_BOUTON);
  if (prec == HIGH && b == LOW) { t0 = millis(); longTraite = false; }
  if (b == LOW && !longTraite && millis() - t0 > 3000) {
    longTraite = true;
    LOG("Bouton : appui long -> portail Wi-Fi");
    demarrerPortail();
  }
  if (prec == LOW && b == HIGH && !longTraite && millis() - t0 > 40) {
    if (!ecranAllume) {
      reveilEcranJusqua = millis() + ECRAN_REVEIL_MS;
      LOG("Bouton : reveil de l'ecran");
    } else {
      pageManuelle = (pageCourante() + 1) % 3;
      pageManuelleJusqua = millis() + 20000;
      if (reveilEcranJusqua) reveilEcranJusqua = millis() + ECRAN_REVEIL_MS;
    }
    lastDrawMs = 0;
  }
  prec = b;
}

// ============================================================================
//                            Diagnostic / commandes
// ============================================================================
void afficherInfosDemarrage() {
  Serial.println();
  Serial.println(F("============================================"));
  Serial.println(F(" Capteur de qualite de l'air v" VERSION));
  Serial.println(F("============================================"));
  LOG("Raison du redemarrage : %s", ESP.getResetReason().c_str());
  LOG("Puce %08X, CPU %u MHz, flash %u Ko", ESP.getChipId(), ESP.getCpuFreqMHz(),
      ESP.getFlashChipRealSize() / 1024);
  LOG("Core %s, SDK %s", ESP.getCoreVersion().c_str(), ESP.getSdkVersion());
  LOG("MAC %s, heap libre %u octets", WiFi.macAddress().c_str(), ESP.getFreeHeap());
#if DEUX_BUS_I2C
  LOG("I2C : capteur D2/D1, ecran D6/D7");
#else
  LOG("I2C : bus unique D2/D1");
#endif
}

void afficherEtat() {
  unsigned long e = millis() - cycleStartMs;
  Serial.println(F("------------------ ETAT ------------------"));
  LOG("Uptime %s, heap %u octets", fmtDuree(millis()), ESP.getFreeHeap());
  LOG("Etat %s depuis %lu s, cycle %lu, intervalle %u min",
      nomEtat(etat), e / 1000, (unsigned long)nbCycles, cfg.cycleMin);
  LOG("Capteur %s, %lu reinit, %lu lecture(s) en echec",
      capteurOk ? "OK" : "ABSENT", (unsigned long)nbReinitCapteur, (unsigned long)nbLecturesKo);
  if (portailActif)
    LOG("Portail ouvert : %s", nomAP);
  else if (WiFi.status() == WL_CONNECTED)
    LOG("Wi-Fi OK : %s, %s, RSSI %d dBm, %lu connexion(s)", WiFi.SSID().c_str(),
        WiFi.localIP().toString().c_str(), WiFi.RSSI(), (unsigned long)nbConnexionsWifi);
  else
    LOG("Wi-Fi DECONNECTE (statut %d)", WiFi.status());
  if (tsActif())
    LOG("ThingSpeak canal %s : %lu OK, %lu echec(s), dernier code %d (%s)%s", cfg.tsChannel,
        (unsigned long)nbEnvoisOk, (unsigned long)nbEnvoisKo, lastHttpCode,
        lastHttpCode ? texteCodeTS(lastHttpCode) : "aucun envoi", envoiEnAttente ? ", envoi en attente" : "");
  else
    LOG("ThingSpeak non configure");
  LOG("Ecran %s, historique %u point(s)", ecranAllume ? "allume" : "eteint", histoNb);
  if (dernier.valide)
    LOG("Derniere moyenne : PM1 %.1f, PM2.5 %.1f, PM10 %.1f ug/m3",
        dernier.pm1, dernier.pm25, dernier.pm10);
  Serial.println(F("------------------------------------------"));
}

void afficherAide() {
  Serial.println(F("Commandes : h aide | s etat | m mesure | p page | e ecran | i scan I2C | w portail Wi-Fi | x oublier Wi-Fi | r redemarrage"));
}

void gererCommandes() {
  while (Serial.available()) {
    char c = tolower(Serial.read());
    switch (c) {
      case 'h': afficherAide(); break;
      case 's': afficherEtat(); break;
      case 'i': i2cScan(); break;
      case 'p':
        pageManuelle = (pageCourante() + 1) % 3;
        pageManuelleJusqua = millis() + 20000;
        LOG("Page %u", pageManuelle + 1);
        break;
      case 'e': basculerEcran(); break;
      case 'm':
        if (etat == VEILLE) { LOG("Commande : mesure immediate"); debutCycle(); }
        else LOG("Commande ignoree : mesure deja en cours (%s)", nomEtat(etat));
        break;
      case 'w': demarrerPortail(); break;
      case 'x':
        LOG("Commande : oubli du reseau Wi-Fi et redemarrage");
        wm.resetSettings();
        delay(200);
        ESP.restart();
        break;
      case 'r':
        LOG("Commande : redemarrage");
        delay(100);
        ESP.restart();
        break;
      case '\r': case '\n': case ' ': break;
      default: LOG("Commande '%c' inconnue (h pour l'aide)", c);
    }
  }
}

// ============================================================================
//                              Setup / loop
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  afficherInfosDemarrage();

#if UTILISER_SET
  pinMode(PIN_SET, OUTPUT);
#endif
  capteurActif(true);
  pinMode(PIN_BOUTON, INPUT_PULLUP);
  snprintf(nomAP, sizeof(nomAP), PORTAIL_PREFIXE "%04X", (unsigned)(ESP.getChipId() & 0xFFFF));

  i2cDemarre();
  busSelect(2);
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (oledOk) {
    display.cp437(true);
    display.setTextColor(SSD1306_WHITE);
    LOG("OLED OK (0x%02X)", OLED_ADDR);
  } else {
    LOG("ERREUR : OLED introuvable en 0x%02X, on continue sans ecran", OLED_ADDR);
  }
  ecranDemarrage("Demarrage...", 5);

  chargerConfig();
  ecranDemarrage("Bus I2C...", 15);
  i2cScan();

  capteurOk = capteurInit(CAPTEUR_INIT_TIMEOUT);
  if (!capteurOk) LOG("Le programme continue : nouvel essai a chaque cycle");

  hGotIp = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP&) { evtGotIp = true; });
  hDisconnected = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& e) {
    raisonDeconnexion = e.reason;
    evtDeconnecte = true;
  });

  ecranDemarrage("Connexion Wi-Fi...", 60);
  configurerWiFiManager();
  configurerServeur();
  ThingSpeak.begin(client);
  LOG("Wi-Fi : connexion au reseau enregistre...");
  if (wm.autoConnect(nomAP, strlen(PORTAIL_MDP) ? PORTAIL_MDP : nullptr)) {
    LOG("Wi-Fi : connecte");
  } else {
    LOG("Wi-Fi : pas de reseau enregistre ou joignable -> portail \"%s\"", nomAP);
  }
  lastWifiOkMs = millis();
  ecranDemarrage("Pret", 100);
  delay(400);

  afficherAide();
  debutCycle();
}

void loop() {
  unsigned long now = millis();
  unsigned long e = now - cycleStartMs;

  if (servicesOk) ArduinoOTA.handle();
  if (otaEnCours) return;

  gererCommandes();
  gererBouton();
  gererPortail();
  gererWifi();
  gererActionsDifferees();
  if (serveurActif) server.handleClient();

  switch (etat) {
    case CHAUFFE:
      if (e >= chauffeMs()) {
        etat = MESURE;
        lastReadMs = now - READ_PERIOD_MS;   // premiere lecture immediate
        LOG("Chauffe terminee, mesure pendant %lu s", MEASURE_MS / 1000);
      }
      break;

    case MESURE:
      if (now - lastReadMs >= READ_PERIOD_MS) {
        lastReadMs = now;
        lireCapteur();
      }
      if (e >= chauffeMs() + MEASURE_MS) finMesure();
      break;

    case VEILLE:
      if (e >= cycleMs()) debutCycle();
      break;
  }

  gererEnvoi();

  unsigned long periode = (etat == VEILLE && !envoiEnAttente && !portailActif) ? 1000 : 200;
  if (now - lastDrawMs >= periode) {
    lastDrawMs = now;
    afficher();
  }
}
