/*
   Orologio 7-SEGMENTI NB-IoT per Inkplate 5 V2 - VERSIONE ULTRA-REALISTICA
   SINCRONIZZATA
   - File unico completo con sincronizzazione NB-IoT tramite SIM7002E
   - Cifre ESTREME (90-175px) per occupare 99.8% della larghezza
   - FORMATO 24H PULITO - NIENTE AM/PM!
   - SINCRONIZZAZIONE AUTOMATICA NB-IoT con NITZ
   - Comunicazione UART con SIM7002E su GPIO13 (TX) e GPIO14 (RX)
   - Correzione automatica ora legale/solare per Italia
   - ZERO byte consumati dopo sincronizzazione iniziale
   - SPAZI AUMENTATI OVUNQUE con GAP REALISTICI tra segmenti
   - Layout ottimizzato per RESPIRAZIONE VISIVA TOTALE
*/

#include "Inkplate.h"
#include "config.h"
#include <DNSServer.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

// Librerie font eleganti per la poesia
#include "FreeSerifBoldItalic24pt7b.h"
#include "FreeSerifItalic12pt7b.h"
#include "FreeSerifItalic18pt7b.h"
#include "FreeSerifItalic24pt7b.h"

// === CONFIGURAZIONE RTC PCF85063A ===
#define PCF85063A_ADDRESS 0x51
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

// === CONFIGURAZIONE NB-IoT SIM7002E ===
HardwareSerial sim7002e(1); // Usa UART1 dell'ESP32
#define SIM_TX_PIN 13       // GPIO13 - TX verso SIM7002E RX (RIPRISTINATO)
#define SIM_RX_PIN 14       // GPIO14 - RX da SIM7002E TX (RIPRISTINATO)
#define SIM_PWR_PIN 12      // GPIO12 - Pin PWR per accensione SIM7002E
#define SIM_BAUDRATE 115200 // Stesso baudrate del Python funzionante

// === CONFIGURAZIONI SINCRONIZZAZIONE ===
bool nbiotEnabled = true;    // Abilita sincronizzazione NB-IoT
bool nbiotConnected = false; // Stato effettivo connessione modulo NB-IoT
int networkLatencyMs =
    1500; // Compensazione latenza rete (1.5 secondi) - DEPRECATED
unsigned long syncInterval = 30000; // Intervallo sync (30 secondi)
unsigned long lastSyncTime = 0;
bool isNitzSynced = false;

// === CONFIGURAZIONI POESIA ===
String currentPoem = "";
String currentMood = "";
bool poemFetchInProgress = false;
unsigned long poemFetchStartTime = 0;
TaskHandle_t poemTaskHandle = NULL;
unsigned long lastPoemFetchTime = 0;
bool newPoemReadyToDisplay = false;

// === WATCHDOG PER RESET ESP32 ===
Preferences preferences;           // Memoria non volatile per persistenza
int moduleFailureCount = 0;        // Contatore fallimenti modulo SIM7002E
const int MAX_MODULE_FAILURES = 3; // Max fallimenti prima di reset ESP32
unsigned long bootTime = 0;        // Tempo di avvio per evitare reset immediato

// === VARIABILI CONFIGURAZIONE POESIA SALVATE ===
String configLang = "it";
String configAuthor = "";
String configStyle = "";

// === WEBSERVER E CAPTIVE PORTAL ===
WebServer server(80);
const byte DNS_PORT = 53;
DNSServer dnsServer;
bool APModeActive = false;
unsigned long apStartTime = 0;

void handleRoot() {
  String html =
      "<html><head><meta charset='utf-8'><meta name='viewport' "
      "content='width=device-width, initial-scale=1'>"
      "<style>"
      "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif; "
      "background:#f4f4f9; color:#333; display:flex; flex-direction:column; "
      "align-items:center; margin:0; padding:20px;}"
      "h1{color:#1a1a2e; margin-bottom:10px;}"
      ".card{background:white; padding:20px; border-radius:12px; box-shadow:0 "
      "4px 10px rgba(0,0,0,0.1); width:100%; max-width:400px;}"
      "label{font-weight:bold; display:block; margin-top:15px; "
      "margin-bottom:5px;}"
      "input,select{width:100%; padding:10px; border:1px solid #ccc; "
      "border-radius:6px; box-sizing:border-box; font-size:16px;}"
      "button{background:#4caf50; color:white; border:none; padding:12px; "
      "border-radius:6px; width:100%; font-size:18px; margin-top:20px; "
      "cursor:pointer; font-weight:bold; transition:0.3s;}"
      "button:hover{background:#45a049;}"
      ".footer{margin-top:20px; font-size:12px; color:#888;}"
      "</style></head><body>"
      "<h1>⚙️ Impostazioni Orologio</h1>"
      "<div class='card'>"
      "<form action='/save' method='POST'>"
      "<label for='lang'>Lingua (es. it, en, fr, es):</label>"
      "<select name='lang' id='lang'>"
      "<option value='it' " +
      String(configLang == "it" ? "selected" : "") +
      ">Italiano 🇮🇹</option>"
      "<option value='en' " +
      String(configLang == "en" ? "selected" : "") +
      ">English 🇬🇧</option>"
      "<option value='fr' " +
      String(configLang == "fr" ? "selected" : "") +
      ">Français 🇫🇷</option>"
      "<option value='es' " +
      String(configLang == "es" ? "selected" : "") +
      ">Español 🇪🇸</option>"
      "<option value='de' " +
      String(configLang == "de" ? "selected" : "") +
      ">Deutsch 🇩🇪</option>"
      "</select>"
      "<label for='author'>Autore (es. Dante, Shakespeare):</label>"
      "<input type='text' id='author' name='author' placeholder='Lascia vuoto "
      "per IA standard' value='" +
      configAuthor +
      "'>"
      "<label for='style'>Stile (es. haiku, romantico, cyberpunk):</label>"
      "<input type='text' id='style' name='style' placeholder='Lascia vuoto "
      "per standard' value='" +
      configStyle +
      "'>"
      "<button type='submit'>Salva e Riavvia 🚀</button>"
      "</form></div>"
      "<div class='footer'>Orologio Inchiostro Elettronico - NB-IoT</div>"
      "</body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("lang"))
    configLang = server.arg("lang");
  if (server.hasArg("author"))
    configAuthor = server.arg("author");
  if (server.hasArg("style"))
    configStyle = server.arg("style");

  preferences.begin("clock_cfg", false);
  preferences.putString("lang", configLang);
  preferences.putString("author", configAuthor);
  preferences.putString("style", configStyle);
  preferences.end();

  String html = "<html><head><meta charset='utf-8'><meta name='viewport' "
                "content='width=device-width, initial-scale=1'>"
                "<style>body{font-family:sans-serif; text-align:center; "
                "padding:50px; background:#f4f4f9;}</style></head>"
                "<body><h2>✅ Salvataggio Completato!</h2><p>L'orologio si sta "
                "riavviando con le nuove impostazioni.</p></body></html>";
  server.send(200, "text/html", html);

  delay(2000);
  ESP.restart(); // Riavvia l'ESP32 con le nuove configurazioni salvate
}

// === SISTEMA DI SINCRONIZZAZIONE SEMPLICE ===
// Un singolo comando AT+CCLK? con compensazione diretta della latenza

// === VARIABILI TEMPO ATTUALE ===
int currentHour = 19; // Ora iniziale (verrà sincronizzata)
int currentMinute = 4;
int currentSecond = 30;
int currentDay = 2;
int currentMonth = 6;
int currentYear = 2025;

// === VARIABILI GUI DASHBOARD ===
String lastSyncTimeStr = "Mai";
int currentRSSI = 99; // 99 = Nessun segnale
String signalQuality = "---";
String connectionStatus = "Disconnesso";
String operatorName = "---";
String locationInfo = "---";
int currentBER = -1; // Bit Error Rate
unsigned long lastDisplayUpdate = 0;
unsigned long minutesSinceSync = 0;

// === TASK ASINCRONI PER NON BLOCCARE IL DISPLAY ===
TaskHandle_t syncTaskHandle = NULL;
bool syncInProgress = false;
bool syncResultAvailable = false;
bool lastSyncSuccessful = false;

// === VARIABILI NB-IoT SEMPLICI ===

// Configurazione display
Inkplate display(INKPLATE_1BIT);

// Variabili timing
unsigned long lastSecondUpdate = 0;
int lastDisplaySecond = -1;
int lastDisplayMinute = -1;
int lastDisplayHour = -1;
int lastPoemFetchMinute = -1;
SemaphoreHandle_t modemMutex =
    NULL; // Protezione per accesso concorrente al modem
SemaphoreHandle_t displayMutex = NULL; // Protezione display Inkplate

// Dimensioni segmenti 7-segment ESTREME ULTRA-REALISTICHE 24H CON RESPIRAZIONE
// TOTALE
struct SegmentSize {
  int bigWidth;       // Larghezza segmento ore/minuti
  int bigHeight;      // Altezza segmento ore/minuti
  int bigThickness;   // Spessore segmento ore/minuti (SUPER CONTROLLATO)
  int smallWidth;     // Larghezza segmento secondi
  int smallHeight;    // Altezza segmento secondi
  int smallThickness; // Spessore segmento secondi
  int digitSpacing;   // Spazio tra cifre
  int colonSpacing;   // Spazio per i due punti
};

SegmentSize seg;

// Posizioni layout
int hoursX, minutesX, secondsX, baselineY;
int colonX1; // Posizione primo ":"

// === STATO PRECEDENTE PER AGGIORNAMENTI SELETTIVI ===
struct DisplayState {
  int lastHour = -1, lastMinute = -1, lastSecond = -1;
  int lastDay = -1, lastMonth = -1, lastYear = -1;
  int lastDayOfWeek = -1;
  String lastSyncTimeStr = "";
  String lastSignalQuality = "";
  String lastOperatorName = "";
  String lastLocationInfo = "";
  bool lastSyncStatus = false;
  bool lastNitzSynced = false;
  int lastRSSI = 99;
};
DisplayState prevState;

// === DICHIARAZIONI FUNZIONI DISPLAY ===
void calculateImprovedSevenSegmentLayout();
void updateTime();
void drawFullSevenSegmentClock();
void updateSevenSegmentDisplay();
void updateSevenSegmentDisplayOptimized(); // Solo per sincronizzazione
void updateDateDisplayPartial();           // Aggiornamento selettivo della data
int calculateDayOfWeek(int day, int month,
                       int year);       // Calcolo giorno settimana
String getTranslatedDay(int dayOfWeek); // Giorno tradotto
String getTranslatedMonth(int month);   // Mese tradotto
void cleanPoemArea(int startY);         // Anti-ghosting area poesia
bool hasDateChanged();                  // Verifica cambio data
void drawBigDigit(int digit, int x, int y);
void drawSmallDigit(int digit, int x, int y);
void drawSevenSegmentDigit(int digit, int x, int y, int w, int h,
                           int thickness);
void drawRealisticHorizontalSegment(int x, int y, int w, int thickness);
void drawRealisticVerticalSegment(int x, int y, int h, int thickness);
void drawThickHorizontalSegment(int x, int y, int w, int thickness);
void drawThickVerticalSegment(int x, int y, int h, int thickness);
void drawImprovedColon(int x, int y);
void drawGsmSignalBars(int x, int y, int rssi);
void eraseDigitArea(int x, int y, int w, int h);
void setSevenSegmentTime(int hour, int minute, int second);
void adjustSegmentThickness(int newThickness);
void printLayoutInfo();

// === STRUTTURE DATI ===
// Struct per contenere l'orario parsato da NITZ con fuso orario
struct NITZ_Time {
  bool valid;
  int year, month, day, hour, minute, second;
  bool hasTimezone; // Se il fuso orario è presente nella risposta NITZ
  int timezoneOffsetQuarters; // Offset fuso orario in quarti d'ora (es: +04 =
                              // UTC+1)
  int timezoneHours;          // Offset fuso orario in ore (calcolato da
                              // timezoneOffsetQuarters)
};

// === DICHIARAZIONI FUNZIONI GUI ===
void updateDashboardInfo();
void updateDashboardInfoOptimized(); // Solo cambiamenti
void updateSyncInfoPartial();
void updateNetworkInfoPartial();

// === DICHIARAZIONI FUNZIONI NB-IoT ===
bool checkModuleOnline();
void powerOnModule();
bool initNBIoT();
bool connectNBIoT();
String sendATCommand(String command, unsigned long timeout = 3000);
NITZ_Time getNITZTime();
int calculateItalyDSTOffset(int month, int day); // Calcolo DST Italia fallback
void applyNetworkTimezone(NITZ_Time &nitz); // Applica fuso orario dalla rete

// === FUNZIONI OTTIMIZZAZIONE SEGNALE DEBOLE ===
struct SignalInfo {
  int rssi;
  int ber;
  String quality;
  int ceLevel;
  bool isWeakSignal;
};
SignalInfo getDetailedSignalInfo();
void configureWeakSignalOptimization(SignalInfo &signal);
String getCoverageEnhancementStats();
// syncWithNITZ() rimossa - logica integrata in handleNBIoTSync()
// isDaylightSavingTime() rimossa - ora usa gestione automatica fuso orario
String getSignalQuality();
String getOperatorName();
String getLocationInfo();
void handleNBIoTSync();

void syncTaskFunction(void *parameter); // Task asincrono
void startAsyncSync();
void checkSyncResult();
void printNBIoTStatus();

// === DICHIARAZIONI FUNZIONI POESIA ===
void startAsyncPoemFetch();
void poemTaskFunction(void *parameter);
String extractJsonString(String json, String key);
void drawPoemArea(int startY);

// === FUNZIONI WATCHDOG ===
void loadModuleFailureCount();
void saveModuleFailureCount();

// === SISTEMA SINCRONIZZAZIONE SEMPLICE ===
// Un singolo comando NITZ con compensazione diretta latenza

// === DICHIARAZIONI FUNZIONI RTC ===
bool initRTC();
void setRTCTime(int year, int month, int day, int hour, int minute, int second);
void setRTCTimeSynchronized(int year, int month, int day, int hour, int minute,
                            int second, unsigned long nitzStartTime);
void getRTCTime(int &year, int &month, int &day, int &hour, int &minute,
                int &second);
byte bcdToDec(byte val);
byte decToBcd(byte val);

// === SISTEMA SINCRONIZZAZIONE RTC-DRIVEN (SOLUZIONE DEFINITIVA) ===
struct RTCSync {
  int lastSecond;      // Ultimo secondo letto dal RTC
  bool waitingForSync; // In attesa di sincronizzazione NITZ
  int targetHour, targetMinute, targetSecond; // Orario target da NITZ
  int targetYear, targetMonth, targetDay;     // Data target da NITZ
  bool hasTargetTime;          // True se abbiamo un target time da applicare
  unsigned long nitzStartTime; // Per il timing preciso
};
RTCSync rtcSync = {-1, false, 0, 0, 0, 0, 0, 0, false, 0};

// === NUOVE FUNZIONI RTC-DRIVEN ===
bool checkRTCSecondChanged();
void scheduleNITZSync(int targetY, int targetMo, int targetD, int targetH,
                      int targetM, int targetS, unsigned long nitzStart);
void processRTCDrivenSync();

void setup() {
  Serial.begin(115200);
  delay(1000);

  bootTime = millis(); // Registra tempo di boot

  // Carica contatore fallimenti dalla memoria non volatile
  loadModuleFailureCount();

  Serial.println(
      "=== OROLOGIO NB-IoT 7-SEGMENTI ULTRA-REALISTICO SINCRONIZZATO ===");
  Serial.printf("Schermo: %dx%d pixel\n", display.width(), display.height());
  Serial.printf("🔄 Boot - Failure count: %d/%d\n", moduleFailureCount,
                MAX_MODULE_FAILURES);

  // Inizializza display
  display.begin();
  display.clearDisplay();

  // Carica le preferenze utente salvate
  preferences.begin("clock_cfg", true);
  configLang = preferences.getString("lang", "it");
  configAuthor = preferences.getString("author", "");
  configStyle = preferences.getString("style", "");
  preferences.end();

  // MODALITÀ CONFIGURAZIONE Wi-Fi AP SEMPRE ATTIVA AL BOOT
  // Parte automaticamente in background per permettere la configurazione
  Serial.println("⚙️ MODALITÀ CONFIGURAZIONE AP AVVIATA IN BACKGROUND...");

  WiFi.mode(WIFI_AP);
  WiFi.softAP("OROLOGIO-CONFIG");
  Serial.println("📶 Access Point avviato: OROLOGIO-CONFIG, IP: 192.168.4.1");

  server.on("/", handleRoot);
  server.on("/save", handleSave);

  // Captive Portal: redirige qualsiasi richiesta non trovata alla root '/'
  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/", true); // 302 Redirect
    server.send(302, "text/plain", "");
  });

  server.begin();

  APModeActive = true;
  apStartTime = millis();

  // Avvia il server DNS per dirottare tutte le richieste verso l'ESP32
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  // L'avvio prosegue immediatamente senza bloccare!

  // Inizializza RTC PCF85063A
  Serial.println("🕰️ Inizializzazione RTC PCF85063A...");

  // Crea Mutex per il modem (deve essere fatto prima di iniziare i task)
  modemMutex = xSemaphoreCreateRecursiveMutex();
  displayMutex = xSemaphoreCreateRecursiveMutex();

  if (modemMutex == NULL || displayMutex == NULL) {
    Serial.println("❌ ERRORE: Impossibile creare i Mutex!");
  }

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (initRTC()) {
    Serial.println("✅ RTC inizializzato correttamente");
    // Carica ora dal RTC come base
    int rtcYear, rtcMonth, rtcDay, rtcHour, rtcMinute, rtcSecond;
    getRTCTime(rtcYear, rtcMonth, rtcDay, rtcHour, rtcMinute, rtcSecond);

    currentYear = rtcYear;
    currentMonth = rtcMonth;
    currentDay = rtcDay;
    currentHour = rtcHour;
    currentMinute = rtcMinute;
    currentSecond = rtcSecond;

    Serial.printf("🕰️ Ora caricata da RTC: %02d/%02d/%04d %02d:%02d:%02d\n",
                  rtcDay, rtcMonth, rtcYear, rtcHour, rtcMinute, rtcSecond);
  } else {
    Serial.println("⚠️ RTC non trovato, uso ora di default");
  }

  // RITARDO DI PROTEZIONE: Aspetta che il WiFi si stabilizzi prima di avviare
  // il modem Questo evita picchi di corrente eccessivi al boot
  delay(3000);

  Serial.println("📡 Inizializzazione NB-IoT SIM7002E...");
  Serial.printf("🔌 UART: ESP32 GPIO%d(TX) -> SIM7002E RX\n", SIM_TX_PIN);
  Serial.printf("🔌 UART: ESP32 GPIO%d(RX) -> SIM7002E TX\n", SIM_RX_PIN);
  Serial.printf("⚙️ Baudrate: %d, Config: 8N1\n", SIM_BAUDRATE);

  // Inizializza UART1 con pin specifici - CONFIGURAZIONE SEMPLICE
  sim7002e.begin(SIM_BAUDRATE, SERIAL_8N1, SIM_RX_PIN, SIM_TX_PIN);
  delay(2000);

  Serial.println("📡 Inizializzazione NB-IoT...");

  // Inizializza pin PWR per controllo accensione
  pinMode(SIM_PWR_PIN, OUTPUT);
  digitalWrite(SIM_PWR_PIN, LOW);

  // Controlla se modulo è acceso, se no lo accende
  if (!checkModuleOnline()) {
    Serial.println("🔌 Modulo SIM7002E offline, invio impulso accensione...");
    powerOnModule();
  } else {
    Serial.println("✅ Modulo SIM7002E già online al boot");
    moduleFailureCount = 0;   // Reset contatore fallimenti se già funzionante
    saveModuleFailureCount(); // Salva il reset
  }

  if (initNBIoT()) {
    Serial.println("✅ NB-IoT inizializzato correttamente");
    nbiotConnected = true; // Modulo inizializzato e funzionante
    // Sincronizzazione iniziale con compensazione della latenza verrà
    // eseguita dopo il primo disegno completo, per garantire layout pronto
  } else {
    Serial.println("❌ Errore inizializzazione NB-IoT, modalità offline");
    nbiotEnabled = false;
    nbiotConnected = false;
  }

  // Calcola dimensioni OTTIMALI per riempire il display
  calculateImprovedSevenSegmentLayout();

  // Primo disegno completo
  drawFullSevenSegmentClock();

  // Sincronizzazione iniziale con compensazione latenza (layout già calcolato)
  if (nbiotEnabled) {
    handleNBIoTSync();
    lastSyncTime = millis();
    // Fetch poesia al boot
    startAsyncPoemFetch();
  }

  Serial.println("🚀 OROLOGIO NB-IoT 7-SEGMENTI ULTRA-REALISTICO AVVIATO!");
  Serial.printf("⏰ Ora corrente: %02d:%02d:%02d (24H) %s\n", currentHour,
                currentMinute, currentSecond,
                isNitzSynced ? "[NITZ SYNC]" : "[MANUALE]");
  Serial.println(
      "📟 Stile: Display 7-segmenti con SPAZI AUMENTATI e sync NB-IoT!");
  // 3. Info NB-IoT (se abilitato)
  if (nbiotEnabled) {
    // Aggiungi l'icona del segnale GSM in alto a destra
    drawGsmSignalBars(display.width() - 80, 20, currentRSSI);
    printNBIoTStatus();
  }
}

void loop() {
  if (APModeActive) {
    dnsServer.processNextRequest(); // Cattura le richieste DNS
    server.handleClient();
    if (millis() - apStartTime > 300000 &&
        WiFi.softAPgetStationNum() ==
            0) { // 5 minuti di tempo + controllo connessi
      Serial.println("⏱️ Timeout configurazione AP (5 min) e nessun utente "
                     "connesso. Spengo Access Point.");
      dnsServer.stop();
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_OFF);
      APModeActive = false;
    }
  }

  // === NUOVA ARCHITETTURA RTC-DRIVEN (SOLUZIONE DEFINITIVA) ===
  // Invece di basarci su millis(), seguiamo il battito del PCF85063A

  if (checkRTCSecondChanged()) {
    // Il PCF85063A ha cambiato secondo - aggiorniamo immediatamente!
    Serial.printf("🔄 RTC tick: %02d:%02d:%02d\n", currentHour, currentMinute,
                  currentSecond);

    // Processa eventuali sincronizzazioni NITZ schedulate
    processRTCDrivenSync();

    // Watchdog per evitare che il flag della poesia rimanga bloccato
    // indefinitamente
    if (poemFetchInProgress &&
        (millis() - poemFetchStartTime > 240000)) { // 4 minuti max
      Serial.println("🚨 WATCHDOG POESIA: Timeout superato, forzo il reset del "
                     "flag inProgress!");
      poemFetchInProgress = false;
    }

    // --- TRIGGER POESIA OGNI MEZZ'ORA (ES: 9:00, 9:30, 10:00) ---
    if (nbiotEnabled && (currentMinute == 0 || currentMinute == 30) &&
        currentMinute != lastPoemFetchMinute && !poemFetchInProgress) {
      lastPoemFetchMinute = currentMinute;
      startAsyncPoemFetch();
    }

    updateDashboardInfo();
    // Usa versione ottimizzata durante sync asincrono
    if (syncInProgress) {
      updateSevenSegmentDisplayOptimized();
    } else {
      updateSevenSegmentDisplay();
    }
  }

  // --- AGGIORNAMENTO SCHERMO SE NUOVA POESIA ---
  if (newPoemReadyToDisplay) {
    newPoemReadyToDisplay = false;
    Serial.println("🔄 Nuova poesia pronta: faccio un partial-refresh solo "
                   "dell'area in basso.");

    // Ricalcoliamo Y in base al layout
    int clockBaselineY = 100;
    int dateY = clockBaselineY + seg.bigHeight + 80;
    int infoY = dateY + 60; // Più spazio sotto la data per non cancellarla

    // Esegue SOLO il disegno della porzione della poesia
    if (displayMutex != NULL)
      xSemaphoreTakeRecursive(displayMutex, portMAX_DELAY);

    // Routine Anti-Ghosting prima di disegnare
    cleanPoemArea(infoY);

    // Disegno effettivo
    drawPoemArea(infoY);

    if (displayMutex != NULL)
      xSemaphoreGiveRecursive(displayMutex);
  }

  // Sincronizzazione NB-IoT asincrona ogni minuto
  unsigned long currentMillis = millis();
  if (nbiotEnabled && (currentMillis - lastSyncTime >= syncInterval)) {
    startAsyncSync();
    lastSyncTime = currentMillis;
  }

  // Controlla risultati sincronizzazione asincrona
  checkSyncResult();

  // Refresh completo eliminato - usiamo solo partial updates per evitare
  // flickering

  delay(50);
}

void calculateImprovedSevenSegmentLayout() {
  int screenWidth = display.width();   // Es: 1280
  int screenHeight = display.height(); // Es: 720

  Serial.printf("📐 Display rilevato: %dx%d pixel\n", screenWidth,
                screenHeight);

  // Calcola dimensioni OTTIMALI per riempire il display
  int availableWidth = screenWidth * 0.9; // 90% della larghezza

  // Calcola dimensioni ottimali per 6 cifre + 2 due-punti
  int totalElements = 6; // HH MM SS
  int spacing = 10;
  int colonSpace = 15;

  // Dimensioni adattive basate sulle dimensioni dello schermo - INGRANDITE del
  // 20%
  seg.bigWidth = (int)((80 < availableWidth / 10 ? 80 : availableWidth / 10) *
                       1.2f);                  // +20%
  seg.bigHeight = (int)(seg.bigWidth * 1.44f); // +20% (1.2 * 1.2 = 1.44)
  seg.bigThickness =
      (int)((6 > seg.bigWidth / 12 ? 6 : seg.bigWidth / 12) * 1.2f); // +20%
  seg.digitSpacing =
      (int)(spacing * 2 * 1.2f); // +20% Aumentato spazio tra cifre
  seg.colonSpacing =
      (int)(colonSpace * 1.5f * 1.2f); // +20% Aumentato spazio per i due punti

  // Layout HH:MM:SS completo
  int totalWidth =
      (seg.bigWidth * 6) + (seg.digitSpacing * 5) + (seg.colonSpacing * 2);

  Serial.println("📐 Layout Orologio Completo HH:MM:SS:");
  Serial.printf("   - Cifre: %dx%d, spessore=%d\n", seg.bigWidth, seg.bigHeight,
                seg.bigThickness);
  Serial.printf("   - Larghezza totale: %dpx su %dpx disponibili\n", totalWidth,
                availableWidth);
  Serial.printf("   - Spazi: cifre=%dpx, due-punti=%dpx\n", seg.digitSpacing,
                seg.colonSpacing);
}

void updateTime() {
  // Legge sempre l'orario dal PCF85063A che è più preciso e autocorregge il
  // drift Non usa più il timer interno ESP32

  int rtcYear, rtcMonth, rtcDay, rtcHour, rtcMinute, rtcSecond;
  getRTCTime(rtcYear, rtcMonth, rtcDay, rtcHour, rtcMinute, rtcSecond);

  // Aggiorna le variabili globali con i valori del PCF85063A
  currentYear = rtcYear;
  currentMonth = rtcMonth;
  currentDay = rtcDay;
  currentHour = rtcHour;
  currentMinute = rtcMinute;
  currentSecond = rtcSecond;

  // Debug per verificare lettura PCF85063A
  static int lastSecondPrinted = -1;
  if (currentSecond != lastSecondPrinted) {
    lastSecondPrinted = currentSecond;
    // Serial.printf("⏱️ PCF85063A: %02d:%02d:%02d\n", currentHour,
    // currentMinute, currentSecond);
  }
}

void drawFullSevenSegmentClock() {
  if (displayMutex != NULL)
    xSemaphoreTakeRecursive(displayMutex, portMAX_DELAY);
  display.clearDisplay();

  // === ORARIO PRINCIPALE CON SECONDI - FORMATO GRANDE ===

  // Posiziona l'orologio più in alto
  int clockBaselineY = 100;

  // Calcola posizioni per HH:MM (solo ore e minuti)
  int totalClockWidth =
      seg.bigWidth * 4 + seg.digitSpacing * 3 + seg.colonSpacing;
  int clockStartX = (display.width() - totalClockWidth) / 2;

  // Posizioni per HH:MM
  int hour24 = currentHour;

  // Ore HH
  drawBigDigit(hour24 / 10, clockStartX, clockBaselineY);
  drawBigDigit(hour24 % 10, clockStartX + seg.bigWidth + seg.digitSpacing,
               clockBaselineY);

  // Calcola posizione minuti prima
  int minutesX =
      clockStartX + seg.bigWidth * 2 + seg.digitSpacing * 2 + seg.colonSpacing;

  // Due punti ":" - PERFETTAMENTE CENTRATI tra fine ore e inizio minuti
  int endOfHours = clockStartX + seg.bigWidth * 2 + seg.digitSpacing;
  int startOfMinutes = minutesX;
  int firstColonX = endOfHours + ((startOfMinutes - endOfHours) / 2) - 5;
  drawImprovedColon(firstColonX, clockBaselineY);

  // Minuti MM - posizione calcolata sopra
  drawBigDigit(currentMinute / 10, minutesX, clockBaselineY);
  drawBigDigit(currentMinute % 10, minutesX + seg.bigWidth + seg.digitSpacing,
               clockBaselineY);

  // === DATA E STATO NB-IoT - FONT ELEGANTE ===
  display.setFont(&FreeSerifItalic24pt7b);
  display.setTextSize(1); // Moltiplicatore raddoppiato come richiesto
  char dateString[48];
  int dayOfWeek = calculateDayOfWeek(currentDay, currentMonth, currentYear);

  sprintf(dateString, "%s %02d %s %04d", getTranslatedDay(dayOfWeek).c_str(),
          currentDay, getTranslatedMonth(currentMonth).c_str(), currentYear);

  int dateX = clockStartX;
  int dateY = clockBaselineY + seg.bigHeight + 80;
  display.setCursor(dateX, dateY);
  display.print(dateString);

  // === INFORMAZIONI SINCRONIZZAZIONE DETTAGLIATE O POESIA ===
  int infoY = dateY + 60; // Più spazio tra date e poesia

  Serial.printf("🖥️ DEBUG Display: isNitzSynced=%s, lastSyncTime=%lu, "
                "lastSyncTimeStr='%s'\n",
                isNitzSynced ? "true" : "false", lastSyncTime,
                lastSyncTimeStr.c_str());

  if (currentPoem != "") {
    drawPoemArea(infoY); // Disegna la poesia coprendo l'area info
  }

  // === CORNICE SEMPLICE ===
  display.drawRect(10, 10, display.width() - 20, display.height() - 20, BLACK);

  // === ICONA GSM PERSISTENTE ===
  if (nbiotEnabled) {
    drawGsmSignalBars(display.width() - 80, 20, currentRSSI);
  }

  // Aggiornamento completo
  display.display();
  if (displayMutex != NULL)
    xSemaphoreGiveRecursive(displayMutex);

  // Salva valori per partial updates
  lastDisplayHour = currentHour;
  lastDisplayMinute = currentMinute;
  lastDisplaySecond = currentSecond;

  // Inizializza stato precedente per tracking della data
  prevState.lastDay = currentDay;
  prevState.lastMonth = currentMonth;
  prevState.lastYear = currentYear;
  prevState.lastDayOfWeek =
      calculateDayOfWeek(currentDay, currentMonth, currentYear);

  Serial.printf("🖥️  Orologio: %02d:%02d:%02d %s\n", hour24, currentMinute,
                currentSecond, isNitzSynced ? "[NITZ]" : "[LOCAL]");
}

void updateDashboardInfo() {
  // Aggiorna le variabili per la dashboard GUI

  // Calcola minuti dalla ultima sincronizzazione
  if (lastSyncTime > 0) {
    minutesSinceSync = (millis() - lastSyncTime) / 60000;
  }

  // lastSyncTimeStr viene aggiornato direttamente in handleNBIoTSync()
  // quando avviene la sincronizzazione con l'ora corrente NITZ

  // Aggiorna stato connessione
  if (isNitzSynced) {
    connectionStatus = "Connesso";
  } else if (nbiotEnabled && nbiotConnected) {
    connectionStatus = "In attesa";
  } else if (nbiotEnabled && !nbiotConnected) {
    connectionStatus = "Connettendo";
  } else {
    connectionStatus = "Offline";
  }
}

void updateDashboardInfoOptimized() {
  // Versione ottimizzata: calcola solo se necessario, nessun ridisegno
  static unsigned long lastCalcTime = 0;

  // Calcola solo ogni 5 secondi durante sincronizzazione
  if (millis() - lastCalcTime >= 5000) {
    lastCalcTime = millis();

    // Calcola minuti dalla ultima sincronizzazione solo se necessario
    if (lastSyncTime > 0) {
      minutesSinceSync = (millis() - lastSyncTime) / 60000;
    }

    // Aggiorna stato connessione solo se cambiato
    String newStatus;
    if (isNitzSynced) {
      newStatus = "Connesso";
    } else if (nbiotEnabled && nbiotConnected) {
      newStatus = "In attesa";
    } else if (nbiotEnabled && !nbiotConnected) {
      newStatus = "Connettendo";
    } else {
      newStatus = "Offline";
    }

    if (newStatus != connectionStatus) {
      connectionStatus = newStatus;
    }
  }
}

void updateSyncInfoPartial() {
  return; // Disabilitato, nessuna info tecnica sul display
  // Aggiorna solo le informazioni di sincronizzazione con partial update
  if (!isNitzSynced || lastSyncTime == 0)
    return;

  if (currentPoem != "")
    return; // Non sovrascrivere l'area se stiamo mostrando la poesia

  unsigned long secondsSinceSync = (millis() - lastSyncTime) / 1000;

  // Coordinate per le info sync (corrispondenti a quelle in
  // drawFullSevenSegmentClock)
  int clockBaselineY = 100;
  int dateY = clockBaselineY + seg.bigHeight + 80;
  int infoY = dateY + 60;

  // Aggiorna qualità se cambiata
  static unsigned long lastQualityCheck = 0;
  if (secondsSinceSync != lastQualityCheck) {
    lastQualityCheck = secondsSinceSync;

    // Cancella area qualità
    display.fillRect(0, infoY + 42, display.width(), 30,
                     WHITE); // Aggiustato per nuova posizione

    display.setTextSize(1);
    // Ridisegna qualità
    const char *qualityText;
    if (secondsSinceSync < 180) {
      qualityText = "Qualita: ECCELLENTE";
    } else if (secondsSinceSync < 300) {
      qualityText = "Qualita: BUONA";
    } else if (secondsSinceSync < 600) {
      qualityText = "Qualita: ACCETTABILE";
    } else {
      qualityText = "Qualita: DA RISINC";
    }

    int refWidth = strlen("Ultima sync: XX:XX:XX") * 7.2; // Size 1
    int refX = (display.width() - refWidth) / 2;
    display.setCursor(refX, infoY + 50); // Allineata a sinistra
    display.print(signalQuality.c_str());
  }
}

void updateNetworkInfoPartial() {
  return; // Disabilitato, nessuna info tecnica sul display
  // Aggiorna solo le informazioni di rete con partial update
  if (!isNitzSynced) {
    Serial.println("⚠️ Partial update saltato: NITZ non sincronizzato");
    return;
  }

  if (currentPoem != "")
    return; // Non sovrascrivere l'area se stiamo mostrando la poesia

  // Coordinate per le info sync (corrispondenti a quelle in
  // drawFullSevenSegmentClock)
  int clockBaselineY = 100;
  int dateY = clockBaselineY + seg.bigHeight + 80;
  int infoY = dateY + 60;

  if (displayMutex != NULL)
    xSemaphoreTakeRecursive(displayMutex, portMAX_DELAY);
  // Cancella e aggiorna prima riga: Orario ultima sincronizzazione
  display.fillRect(0, infoY - 12, display.width(), 25, WHITE);
  display.setTextSize(1);
  char syncInfo[50];
  sprintf(syncInfo, "Ultima sync: %s", lastSyncTimeStr.c_str());
  int syncInfoWidth = strlen(syncInfo) * 7.2; // Size 1
  int syncInfoX = (display.width() - syncInfoWidth) / 2;
  display.setCursor(syncInfoX, infoY);
  display.print(syncInfo);

  // Cancella e aggiorna seconda riga: Operatore e posizione
  display.fillRect(0, infoY + 20, display.width(), 25, WHITE);
  char networkInfo[80];
  sprintf(networkInfo, "%s - %s", operatorName.c_str(), locationInfo.c_str());
  display.setCursor(syncInfoX, infoY + 25);
  display.print(networkInfo);

  // Forza partial update
  display.partialUpdate();
  if (displayMutex != NULL)
    xSemaphoreGiveRecursive(displayMutex);
}

void drawGsmSignalBars(int x, int y, int rssi) {
  // Nota: Mutex gestito dal chiamante o internamente se necessario
  // Essendo chiamata spesso da drawFull o networkUpdate, non serve prenderlo
  // qui se già preso fuori

  // Dimensioni icona: circa 40x26 pixel
  int barWidth = 6;
  int barSpacing = 3;
  int maxBarHeight = 26;

  // Sfondo bianco per pulire l'area
  display.fillRect(x - 5, y - 5, 50, 35, WHITE);

  // Calcola numero di barre (0 a 4)
  int bars = 0;
  if (rssi == 99 || rssi < 0)
    bars = 0;
  else if (rssi >= 15)
    bars = 4; // Ottimo
  else if (rssi >= 10)
    bars = 3; // Buono
  else if (rssi >= 5)
    bars = 2; // Discreto
  else
    bars = 1; // Debole

  // Disegna le 4 barre (grigie o nere a seconda se l'RSSI le copre)
  for (int i = 0; i < 4; i++) {
    int barHeight = (i + 1) * (maxBarHeight / 4);
    int barX = x + (i * (barWidth + barSpacing));
    int barY = y + (maxBarHeight - barHeight);

    if (i < bars) {
      // Barra piena (Segnale presente)
      display.fillRect(barX, barY, barWidth, barHeight, BLACK);
    } else {
      // Profilo barra (Segnale mancante)
      display.drawRect(barX, barY, barWidth, barHeight, BLACK);
    }
  }

  // Se nessun segnale (99), disegna una piccola X rossa/nera
  if (rssi == 99) {
    display.drawLine(x - 5, y, x + 35, y + 26, BLACK);
    display.drawLine(x + 35, y, x - 5, y + 26, BLACK);
  }
}

// === FUNZIONE MIGLIORATA PER I DUE PUNTI ===
void drawImprovedColon(int x, int y) {
  // Due punti più grandi e ben centrati
  int dotSize = seg.bigThickness * 1.5; // Dimensione aumentata
  int centerY = y + seg.bigHeight / 2;
  int spacing = seg.bigHeight / 4;

  // Posiziona i punti centrati verticalmente
  int dot1Y = centerY - spacing;
  int dot2Y = centerY + spacing;

  display.fillCircle(x + dotSize / 2, dot1Y, dotSize / 2, BLACK);
  display.fillCircle(x + dotSize / 2, dot2Y, dotSize / 2, BLACK);
}

void updateSevenSegmentDisplay() {
  bool hoursChanged = (currentHour != lastDisplayHour);
  bool minutesChanged = (currentMinute != lastDisplayMinute);
  bool daysChanged = (currentDay != prevState.lastDay);
  bool secondsChanged = (currentSecond != lastDisplaySecond);
  bool needsPartialUpdate = false;

  if (hoursChanged || minutesChanged || daysChanged) {
    int totalClockWidth =
        seg.bigWidth * 4 + seg.digitSpacing * 3 + seg.colonSpacing;
    int clockStartX = (display.width() - totalClockWidth) / 2;
    int clockBaselineY = 100;
    int dateY = clockBaselineY + seg.bigHeight + 80;
    int infoY = dateY + 60;

    // AL PASSAGGIO DEL MINUTO: Refresh totale della parte superiore (sopra il
    // rettangolo della poesia/info)
    display.fillRect(0, 0, display.width(), infoY - 10, WHITE);

    // Ripristina cornice superiore cancellata dal wipe
    display.drawRect(10, 10, display.width() - 20, display.height() - 20,
                     BLACK);

    // Ripristina Icona GSM cancellata dal wipe
    if (nbiotEnabled) {
      drawGsmSignalBars(display.width() - 80, 20, currentRSSI);
    }

    // --- Ridisegna l'orologio (HH:MM) ---
    int hour24 = currentHour;
    drawBigDigit(hour24 / 10, clockStartX, clockBaselineY);
    drawBigDigit(hour24 % 10, clockStartX + seg.bigWidth + seg.digitSpacing,
                 clockBaselineY);

    int minutesX = clockStartX + seg.bigWidth * 2 + seg.digitSpacing * 2 +
                   seg.colonSpacing;
    int endOfHours = clockStartX + seg.bigWidth * 2 + seg.digitSpacing;

    drawBigDigit(currentMinute / 10, minutesX, clockBaselineY);
    drawBigDigit(currentMinute % 10, minutesX + seg.bigWidth + seg.digitSpacing,
                 clockBaselineY);

    // --- Ridisegna forzatamente la Data ---
    display.setFont(&FreeSerifItalic24pt7b);
    display.setTextSize(1); // Moltiplicatore raddoppiato come richiesto
    char dateString[40];
    int dayOfWeek = calculateDayOfWeek(currentDay, currentMonth, currentYear);

    sprintf(dateString, "%s %02d %s %04d", getTranslatedDay(dayOfWeek).c_str(),
            currentDay, getTranslatedMonth(currentMonth).c_str(), currentYear);
    int repDateY = clockBaselineY + seg.bigHeight + 80;
    display.setCursor(clockStartX, repDateY); // Allineata a sinistra
    display.print(dateString);

    // Salva le variabili
    prevState.lastDay = currentDay;
    prevState.lastMonth = currentMonth;
    prevState.lastYear = currentYear;
    prevState.lastDayOfWeek = dayOfWeek;

    lastDisplayHour = currentHour;
    lastDisplayMinute = currentMinute;
    needsPartialUpdate = true;

    Serial.printf(
        "🕑 Minuti aggiornati - Wipe area superiore. Nuovo orario: %02d:%02d\n",
        hour24, currentMinute);
  }

  int totalClockWidth =
      seg.bigWidth * 4 + seg.digitSpacing * 3 + seg.colonSpacing;
  int clockStartX = (display.width() - totalClockWidth) / 2;
  int clockBaselineY = 100;
  int minutesX =
      clockStartX + seg.bigWidth * 2 + seg.digitSpacing * 2 + seg.colonSpacing;
  int endOfHours = clockStartX + seg.bigWidth * 2 + seg.digitSpacing;
  int firstColonX = endOfHours + ((minutesX - endOfHours) / 2) - 5;

  if (secondsChanged) {
    if (currentSecond % 2 == 0) {
      drawImprovedColon(firstColonX, clockBaselineY);
    } else {
      int dotSize = seg.bigThickness * 1.5;
      display.fillRect(firstColonX, clockBaselineY - 10, dotSize + 2,
                       seg.bigHeight + 20, WHITE);
    }
    lastDisplaySecond = currentSecond;
    needsPartialUpdate = true;
  }

  // Partial update solo se qualcosa è cambiato
  if (needsPartialUpdate) {
    display.partialUpdate();
  }
}

void updateSevenSegmentDisplayOptimized() {
  bool secondsChanged = (currentSecond != lastDisplaySecond);
  bool minutesChanged = (currentMinute != lastDisplayMinute);

  if (secondsChanged || minutesChanged) {
    int totalClockWidth =
        seg.bigWidth * 4 + seg.digitSpacing * 3 + seg.colonSpacing;
    int clockStartX = (display.width() - totalClockWidth) / 2;
    int clockBaselineY = 100;

    int minutesX = clockStartX + seg.bigWidth * 2 + seg.digitSpacing * 2 +
                   seg.colonSpacing;
    int endOfHours = clockStartX + seg.bigWidth * 2 + seg.digitSpacing;
    int firstColonX = endOfHours + ((minutesX - endOfHours) / 2) - 5;

    // Se è cambiato il minuto, ridisegniamo specificamente il blocco dei minuti
    // per non congelare l'ora
    if (minutesChanged) {
      // Cancelliamo l'area dei minuti
      display.fillRect(minutesX, clockBaselineY - 10,
                       seg.bigWidth * 2 + seg.digitSpacing, seg.bigHeight + 20,
                       WHITE);
      drawBigDigit(currentMinute / 10, minutesX, clockBaselineY);
      drawBigDigit(currentMinute % 10,
                   minutesX + seg.bigWidth + seg.digitSpacing, clockBaselineY);

      // Cancelliamo le ore
      display.fillRect(clockStartX, clockBaselineY - 10,
                       seg.bigWidth * 2 + seg.digitSpacing, seg.bigHeight + 20,
                       WHITE);
      drawBigDigit(currentHour / 10, clockStartX, clockBaselineY);
      drawBigDigit(currentHour % 10,
                   clockStartX + seg.bigWidth + seg.digitSpacing,
                   clockBaselineY);

      lastDisplayMinute = currentMinute;
      lastDisplayHour = currentHour;
    }

    // Lampeggio ogni secondo pari/dispari
    if (currentSecond % 2 == 0) {
      drawImprovedColon(firstColonX, clockBaselineY);
    } else {
      // Cancella colon
      int dotSize = seg.bigThickness * 1.5;
      display.fillRect(firstColonX, clockBaselineY - 10, dotSize + 2,
                       seg.bigHeight + 20, WHITE);
    }

    lastDisplaySecond = currentSecond;
    display.partialUpdate();
  }
}

void drawBigDigit(int digit, int x, int y) {
  drawSevenSegmentDigit(digit, x, y, seg.bigWidth, seg.bigHeight,
                        seg.bigThickness);
}

void drawSmallDigit(int digit, int x, int y) {
  drawSevenSegmentDigit(digit, x, y, seg.smallWidth, seg.smallHeight,
                        seg.smallThickness);
}

void drawSevenSegmentDigit(int digit, int x, int y, int w, int h,
                           int thickness) {
  // Definisce quali segmenti sono accesi per ogni cifra
  // Segmenti: a=top, b=top-right, c=bottom-right, d=bottom, e=bottom-left,
  // f=top-left, g=middle
  bool segments[10][7] = {
      {1, 1, 1, 1, 1, 1, 0}, // 0: a,b,c,d,e,f
      {0, 1, 1, 0, 0, 0, 0}, // 1: b,c
      {1, 1, 0, 1, 1, 0, 1}, // 2: a,b,d,e,g
      {1, 1, 1, 1, 0, 0, 1}, // 3: a,b,c,d,g
      {0, 1, 1, 0, 0, 1, 1}, // 4: b,c,f,g
      {1, 0, 1, 1, 0, 1, 1}, // 5: a,c,d,f,g
      {1, 0, 1, 1, 1, 1, 1}, // 6: a,c,d,e,f,g
      {1, 1, 1, 0, 0, 0, 0}, // 7: a,b,c
      {1, 1, 1, 1, 1, 1, 1}, // 8: tutti
      {1, 1, 1, 1, 0, 1, 1}  // 9: a,b,c,d,f,g
  };

  if (digit < 0 || digit > 9)
    return;

  int halfH = h / 2;

  // === CALCOLO GAP REALISTICI AUMENTATI ===
  // Gap più larghi per maggiore realismo
  int gap = (thickness > 8) ? 5 : 4;       // Gap AUMENTATO tra segmenti
  int cornerGap = (thickness > 8) ? 6 : 5; // Gap AUMENTATO agli angoli
  int middleGap = gap + 1;                 // Gap extra per segmento centrale

  // DISEGNA SEGMENTI CON GAP REALISTICI AUMENTATI e ANGOLI EVIDENTI

  // a - segmento TOP (con gap angolari EVIDENTI)
  if (segments[digit][0]) {
    drawRealisticHorizontalSegment(x + cornerGap, y, w - (cornerGap * 2),
                                   thickness);
  }

  // b - segmento TOP-RIGHT (con gap angolari EVIDENTI)
  if (segments[digit][1]) {
    drawRealisticVerticalSegment(x + w - thickness, y + cornerGap,
                                 halfH - cornerGap - middleGap / 2, thickness);
  }

  // c - segmento BOTTOM-RIGHT (con gap angolari EVIDENTI)
  if (segments[digit][2]) {
    drawRealisticVerticalSegment(x + w - thickness, y + halfH + middleGap / 2,
                                 halfH - cornerGap - middleGap / 2, thickness);
  }

  // d - segmento BOTTOM (con gap angolari EVIDENTI)
  if (segments[digit][3]) {
    drawRealisticHorizontalSegment(x + cornerGap, y + h - thickness,
                                   w - (cornerGap * 2), thickness);
  }

  // e - segmento BOTTOM-LEFT (con gap angolari EVIDENTI)
  if (segments[digit][4]) {
    drawRealisticVerticalSegment(x, y + halfH + middleGap / 2,
                                 halfH - cornerGap - middleGap / 2, thickness);
  }

  // f - segmento TOP-LEFT (con gap angolari EVIDENTI)
  if (segments[digit][5]) {
    drawRealisticVerticalSegment(x, y + cornerGap,
                                 halfH - cornerGap - middleGap / 2, thickness);
  }

  // g - segmento MIDDLE (con gap angolari EVIDENTI e separazione maggiore)
  if (segments[digit][6]) {
    drawRealisticHorizontalSegment(x + cornerGap, y + halfH - thickness / 2,
                                   w - (cornerGap * 2), thickness);
  }
}

// === FUNZIONI REALISTICHE PER SEGMENTI CON GAP ===
void drawRealisticHorizontalSegment(int x, int y, int w, int thickness) {
  // Segmento orizzontale con gap realistici per aspetto autentico
  display.fillRect(x, y, w, thickness, BLACK);

  // Aggiungi piccoli arrotondamenti agli angoli se lo spessore lo permette
  if (thickness >= 6) {
    display.fillCircle(x + 2, y + thickness / 2, 1, BLACK);
    display.fillCircle(x + w - 3, y + thickness / 2, 1, BLACK);
  }
}

void drawRealisticVerticalSegment(int x, int y, int h, int thickness) {
  // Segmento verticale con gap realistici per aspetto autentico
  display.fillRect(x, y, thickness, h, BLACK);

  // Aggiungi piccoli arrotondamenti agli angoli se lo spessore lo permette
  if (thickness >= 6) {
    display.fillCircle(x + thickness / 2, y + 2, 1, BLACK);
    display.fillCircle(x + thickness / 2, y + h - 3, 1, BLACK);
  }
}

// === FUNZIONI LEGACY MANTENUTE PER RETROCOMPATIBILITÀ ===
void drawThickHorizontalSegment(int x, int y, int w, int thickness) {
  // Richiama la nuova funzione realistica
  drawRealisticHorizontalSegment(x, y, w, thickness);
}

void drawThickVerticalSegment(int x, int y, int h, int thickness) {
  // Richiama la nuova funzione realistica
  drawRealisticVerticalSegment(x, y, h, thickness);
}

void eraseDigitArea(int x, int y, int w, int h) {
  // Cancella l'area esatta della cifra. Usare dimensioni esatte previene
  // l'accumulo di pixel neri residui o sovrapposizioni errate sull'E-ink.
  display.fillRect(x, y, w, h, WHITE);
}

// === FUNZIONE HELPER PER IMPOSTARE ORARIO ===
void setSevenSegmentTime(int hour, int minute, int second) {
  currentHour = hour;
  currentMinute = minute;
  currentSecond = second;

  // Reset valori per forzare aggiornamento completo
  lastDisplayHour = -1;
  lastDisplayMinute = -1;
  lastDisplaySecond = -1;

  Serial.printf("⏰ Orario impostato: %02d:%02d:%02d\n", hour, minute, second);

  // Ridisegna completamente il display
  drawFullSevenSegmentClock();
}

// === FUNZIONE PER CALIBRARE DIMENSIONI IN TEMPO REALE ===
void adjustSegmentThickness(int newThickness) {
  if (newThickness >= 2 && newThickness <= 15) {
    seg.bigThickness = newThickness;
    seg.smallThickness = newThickness * 0.8;

    Serial.printf("📏 Spessore segmenti aggiornato: %d\n", newThickness);

    // Ridisegna con nuovo spessore
    drawFullSevenSegmentClock();
  }
}

// === FUNZIONE DI DEBUG PER TESTARE LAYOUT ===
void printLayoutInfo() {
  Serial.println("=== INFO LAYOUT 7-SEGMENTI ===");
  Serial.printf("Display: %dx%d pixel\n", display.width(), display.height());
  Serial.printf("Segmenti grandi: %dx%d, spessore=%d\n", seg.bigWidth,
                seg.bigHeight, seg.bigThickness);
  Serial.printf("Segmenti piccoli: %dx%d, spessore=%d\n", seg.smallWidth,
                seg.smallHeight, seg.smallThickness);
  Serial.printf("Posizioni X: Ore=%d, Colon=%d, Minuti=%d, Secondi=%d\n",
                hoursX, colonX1, minutesX, secondsX);
  Serial.printf("Baseline Y: %d\n", baselineY);

  int totalLayoutWidth = secondsX + seg.smallWidth * 2 - hoursX;
  Serial.printf("Larghezza totale layout: %d pixel (%.1f%% del display)\n",
                totalLayoutWidth,
                (float)totalLayoutWidth / display.width() * 100);
}

// === FUNZIONE PER CALCOLO APPROSSIMATO DI SQRT ===
unsigned long sqrt_approx(unsigned long x) {
  if (x == 0)
    return 0;
  if (x < 4)
    return 1;

  unsigned long guess = x >> 1; // Inizia con x/2
  unsigned long better_guess;

  // Metodo di Newton-Raphson per approssimazione radice quadrata
  do {
    better_guess = (guess + x / guess) >> 1;
    if (better_guess >= guess)
      break;
    guess = better_guess;
  } while (abs((long)guess - (long)better_guess) > 1);

  return guess;
}

// ===================================================================
// FUNZIONI NB-IoT SIM7002E - CONVERSIONE DA PYTHON
// ===================================================================

bool checkModuleOnline() {
  Serial.println("🔍 Controllo se modulo SIM7002E è online...");

  // Inizializza UART1 se non ancora fatto
  sim7002e.begin(SIM_BAUDRATE, SERIAL_8N1, SIM_RX_PIN, SIM_TX_PIN);
  delay(500);

  // Prova comando AT con timeout breve
  String response = sendATCommand("AT", 1000);
  bool online = (response.indexOf("OK") >= 0);

  if (online) {
    Serial.println("✅ Modulo SIM7002E online");
  } else {
    Serial.println("❌ Modulo SIM7002E non risponde");
  }

  return online;
}

void powerOnModule() {
  Serial.println("🔌 Invio impulso PWR per accendere SIM7002E...");

  // Impulso HIGH di 2 secondi sul pin PWR
  digitalWrite(SIM_PWR_PIN, HIGH);
  delay(2000);
  digitalWrite(SIM_PWR_PIN, LOW);

  // Attendi che il modulo si avvii
  delay(5000);

  // Riprova il controllo con tentativi multipli
  for (int i = 0; i < 10; i++) {
    Serial.printf("🔄 Tentativo %d/10 di connessione al modulo...\n", i + 1);

    if (checkModuleOnline()) {
      Serial.println("✅ Modulo SIM7002E acceso correttamente!");
      moduleFailureCount = 0;   // Reset contatore fallimenti
      saveModuleFailureCount(); // Salva il reset
      return;
    }

    delay(2000);
  }

  Serial.println("❌ ERRORE: Modulo SIM7002E non risponde dopo impulso PWR");

  moduleFailureCount++;
  saveModuleFailureCount(); // Salva il fallimento
  Serial.printf("🔄 Fallimenti modulo: %d/%d\n", moduleFailureCount,
                MAX_MODULE_FAILURES);

  // Evita reset immediato al boot - aspetta almeno 2 minuti
  if (moduleFailureCount >= MAX_MODULE_FAILURES &&
      (millis() - bootTime) > 120000) {
    Serial.println("🚨 TROPPI FALLIMENTI MODULO - RESET ESP32 IN 10 SECONDI");
    Serial.println(
        "🚨 Questo dovrebbe risolvere problemi di inizializzazione hardware");

    // Mostra messaggio sul display prima del reset
    display.clearDisplay();
    display.setTextColor(BLACK);
    display.setTextSize(2);
    display.setCursor(50, 50);
    display.print("MODULO SIM7002E");
    display.setCursor(50, 80);
    display.print("NON RISPONDE");
    display.setCursor(50, 120);
    display.print("RESET ESP32...");
    display.display();

    delay(10000);  // Attendi 10 secondi per permettere la lettura
    ESP.restart(); // Reset ESP32
  }

  nbiotEnabled = false; // Disabilita NB-IoT se non risponde
  nbiotConnected = false;
}

bool initNBIoT() {
  Serial.println("🔧 Inizializzazione modulo SIM7002E...");

  // Test comunicazione base
  String response = sendATCommand("AT");
  if (response.indexOf("OK") < 0) {
    Serial.println("❌ Modulo non risponde al comando AT");
    return false;
  }

  Serial.println("✅ Comunicazione con modulo stabilita");

  // Configurazione NITZ (Network Identity and Time Zone)
  Serial.println("🕐 Configurazione NITZ e Power Management...");
  sendATCommand("AT+CTZU=1"); // Abilita aggiornamento automatico orario
  sendATCommand("AT+CTZR=1"); // Abilita report cambi orario
  sendATCommand("AT+CLTS=1"); // Abilita timestamp locale

  // DISABILITA PSM DI DEFAULT per evitare sleep improvvisi durante la sync
  sendATCommand("AT+CPSMS=0");
  sendATCommand("AT+CFUN=1"); // Forza funzionalità completa

  delay(2000);

  Serial.println("✅ Configurazione NITZ e Power Management completata");
  return true;
}

bool connectNBIoT() {
  Serial.println("📡 Tentativo connessione NB-IoT con strategia adattiva...");

  // Assicura che il modulo sia sveglio disabilitando PSM all'inizio della
  // connessione
  sendATCommand("AT+CPSMS=0");
  delay(100);

  // Verifica registrazione rete iniziale
  String response = sendATCommand("AT+CREG?");

  // Se non risorgono byte, il modulo potrebbe essere bloccato in PSM o spento
  if (response.length() == 0) {
    Serial.println("⚠️ Modulo silenzioso. Tentativo di risveglio hardware...");
    digitalWrite(SIM_PWR_PIN, HIGH);
    delay(500); // Impulso breve per risveglio
    digitalWrite(SIM_PWR_PIN, LOW);
    delay(2000); // Attesa assestamento

    // Riprova comando
    response = sendATCommand("AT+CREG?");
  }

  if (response.indexOf("+CREG: 0,1") >= 0 ||
      response.indexOf("+CREG: 0,5") >= 0) {
    Serial.println("✅ Già registrato sulla rete");
    nbiotConnected = true; // Conferma connessione attiva
    return true;
  }

  // Analizza qualità segnale per strategia adattiva
  SignalInfo signalInfo = getDetailedSignalInfo();
  Serial.printf("📊 Segnale rilevato: %s (RSSI: %d)\n",
                signalInfo.quality.c_str(), signalInfo.rssi);

  // Configura ottimizzazioni per segnale debole
  configureWeakSignalOptimization(signalInfo);

  // Parametri strategia adattiva basata sul segnale
  int maxAttempts;
  int retryDelay;
  int progressInterval;

  if (signalInfo.rssi == 99) {
    // No Signal - strategia conservativa
    maxAttempts = 600;     // 20 minuti (2s * 600)
    retryDelay = 2000;     // 2 secondi
    progressInterval = 30; // Report ogni minuto
    Serial.println("🚨 No Signal - Modalità ricerca estesa (20 min)");
  } else if (signalInfo.quality == "Critico") {
    // Segnale Critico - strategia massima aggressività
    maxAttempts = 900;     // 15 minuti (1s * 900)
    retryDelay = 1000;     // 1 secondo
    progressInterval = 60; // Report ogni minuto
    Serial.println("⚠️ Segnale Critico - Modalità aggressiva (15 min)");
  } else if (signalInfo.quality == "Debole") {
    // Segnale Debole - strategia aggressiva
    maxAttempts = 600;     // 10 minuti (1s * 600)
    retryDelay = 1000;     // 1 secondo
    progressInterval = 30; // Report ogni 30s
    Serial.println("📶 Segnale Debole - Modalità intensiva (10 min)");
  } else if (signalInfo.quality == "Moderato") {
    // Segnale Moderato - strategia intermedia
    maxAttempts = 150;     // 5 minuti (2s * 150)
    retryDelay = 2000;     // 2 secondi
    progressInterval = 15; // Report ogni 30s
    Serial.println("📶 Segnale Moderato - Modalità standard (5 min)");
  } else {
    // Segnale Forte - strategia normale
    maxAttempts = 90;      // 3 minuti (2s * 90)
    retryDelay = 2000;     // 2 secondi
    progressInterval = 15; // Report ogni 30s
    Serial.println("📶 Segnale Forte - Modalità normale (3 min)");
  }

  // Loop adattivo di registrazione rete
  for (int i = 0; i < maxAttempts; i++) {
    // Delay non-bloccante con aggiornamento continuo orologio
    unsigned long delayStart = millis();
    unsigned long lastUpdate = millis();
    while (millis() - delayStart < retryDelay) {
      if (millis() - lastUpdate >= 1000) {
        lastUpdate = millis();
        updateTime();
        if (displayMutex != NULL)
          xSemaphoreTakeRecursive(displayMutex, portMAX_DELAY);
        updateSevenSegmentDisplayOptimized();
        if (displayMutex != NULL)
          xSemaphoreGiveRecursive(displayMutex);
      }
      delay(50);
    }

    response = sendATCommand("AT+CREG?");
    if (response.indexOf("+CREG: 0,1") >= 0 ||
        response.indexOf("+CREG: 0,5") >= 0) {
      Serial.printf(
          "✅ Registrazione completata dopo %d tentativi (%.1f min)\n", i + 1,
          (i + 1) * retryDelay / 60000.0);

      nbiotConnected = true;         // Connessione riuscita
      currentRSSI = signalInfo.rssi; // Memorizza RSSI attuale

      // Verifica CE Level raggiunto
      String ceStats = getCoverageEnhancementStats();
      if (ceStats.indexOf("CE") >= 0) {
        Serial.println("📊 Coverage Enhancement finale:");
        Serial.println(ceStats);
      }

      return true;
    }

    // Report progresso adattivo
    if ((i + 1) % progressInterval == 0) {
      float elapsed = (i + 1) * retryDelay / 60000.0;
      float total = maxAttempts * retryDelay / 60000.0;
      Serial.printf("⏳ Registrazione... %.1f/%.1f min (tentativo %d/%d)\n",
                    elapsed, total, i + 1, maxAttempts);

      // Ogni minuto, ricontrolla qualità segnale per adattare strategia
      if ((i + 1) % (60000 / retryDelay) == 0) {
        SignalInfo newSignal = getDetailedSignalInfo();
        if (newSignal.rssi != signalInfo.rssi) {
          Serial.printf("📡 Segnale cambiato: %s → %s (RSSI: %d → %d)\n",
                        signalInfo.quality.c_str(), newSignal.quality.c_str(),
                        signalInfo.rssi, newSignal.rssi);
          signalInfo = newSignal;
        }

        // Aggiorna CE stats
        String currentCE = getCoverageEnhancementStats();
        if (currentCE.indexOf("CE") >= 0) {
          Serial.println("📊 CE Stats correnti:");
          Serial.println(currentCE);
        }
      }
    }
  }

  Serial.printf("❌ Timeout registrazione rete dopo %.1f minuti\n",
                maxAttempts * retryDelay / 60000.0);
  nbiotConnected = false; // Connessione fallita
  return false;
}

String sendATCommand(String command, unsigned long timeout) {
  if (modemMutex != NULL) {
    xSemaphoreTakeRecursive(modemMutex, portMAX_DELAY);
  }

  // Ridotto output durante sincronizzazione per non rallentare
  if (command != "AT+CCLK?") {
    Serial.printf("📤 TX (GPIO%d): %s\n", SIM_TX_PIN, command.c_str());
  }

  // Pulisci buffer di ricezione
  int discarded = 0;
  while (sim7002e.available()) {
    sim7002e.read();
    discarded++;
  }
  if (discarded > 0) {
    Serial.printf("🗑️ Scartati %d byte dal buffer\n", discarded);
  }

  // Invia comando
  sim7002e.print(command);
  sim7002e.print("\r\n");
  sim7002e.flush(); // Forza invio immediato

  Serial.printf("⏱️ Attendo risposta per %lu ms su GPIO%d...\n", timeout,
                SIM_RX_PIN);

  // Attendi risposta
  String response = "";
  unsigned long startTime = millis();
  int bytesReceived = 0;

  // Variabili per aggiornamento continuo orologio durante attesa
  unsigned long lastClockUpdate = millis();

  while (millis() - startTime < timeout) {
    if (sim7002e.available()) {
      char c = sim7002e.read();
      bytesReceived++;

      if (c != '\r') { // Ignora carriage return
        response += c;
      }

      // Check per fine risposta
      if (response.indexOf("OK") >= 0 || response.indexOf("ERROR") >= 0 ||
          response.indexOf("+CCLK:") >= 0 || response.indexOf("+CSQ:") >= 0 ||
          response.indexOf("+CREG:") >= 0) {
        delay(100); // Piccola pausa per ricevere eventuali dati aggiuntivi

        // Leggi eventuali byte rimanenti
        while (sim7002e.available()) {
          char extra = sim7002e.read();
          if (extra != '\r')
            response += extra;
          bytesReceived++;
        }
        break;
      }
    }

    // AGGIORNA SOLO L'OROLOGIO OGNI SECONDO DURANTE L'ATTESA (non le info sync)
    unsigned long now = millis();
    if (now - lastClockUpdate >= 1000) {
      lastClockUpdate = now;
      updateTime();
      // Solo aggiornamento selettivo dei secondi, non tutto il dashboard
      updateSevenSegmentDisplayOptimized();
    }

    delay(10);
  }

  // Ridotto output per comandi frequenti durante sync
  if (bytesReceived > 0 && command != "AT+CCLK?") {
    Serial.printf("📥 RX (GPIO%d): '%s' (%d bytes)\n", SIM_RX_PIN,
                  response.c_str(), bytesReceived);
  }

  if (modemMutex != NULL) {
    xSemaphoreGiveRecursive(modemMutex);
  }

  if (bytesReceived == 0) {
    Serial.println("⚠️ NESSUN BYTE RICEVUTO - Possibili cause:");
    Serial.println("   1. Cablaggio errato");
    Serial.println("   2. SIM7002E spento o non alimentato");
    Serial.println("   3. Baudrate errato");
    Serial.println("   4. Pin UART in conflitto");
  }

  return response;
}

// === SISTEMA DI SINCRONIZZAZIONE SEMPLICE E VELOCE ===

NITZ_Time getNITZTime() {
  Serial.println("🕐 Richiesta orario NITZ al modulo...");
  NITZ_Time nitz_time = {false, 0, 0,     0, 0,
                         0,     0, false, 0, 0}; // Inizializza come non valido

  String response = sendATCommand(
      "AT+CCLK?",
      5000); // Timeout ridotto ma con aggiornamento continuo orologio

  Serial.printf("🔍 Risposta CCLK ricevuta: '%s'\n", response.c_str());

  if (response.indexOf("+CCLK:") >= 0) {
    int cclkPos = response.indexOf("+CCLK:");
    int dataStart = cclkPos + 7; // Salta "+CCLK: "
    String timeData = response.substring(dataStart);

    // Pulizia risposta: rimuovi "OK" finale e spazi
    int okPos = timeData.indexOf("OK");
    if (okPos >= 0) {
      timeData = timeData.substring(0, okPos);
    }
    timeData.trim();

    Serial.printf("🔧 Stringa pulita: '%s'\n", timeData.c_str());

    // TENTATIVO 1: Formato con fuso orario SENZA virgolette (più comune)
    // +CCLK: 25/09/04,14:50:04+08
    int parsed_items = sscanf(
        timeData.c_str(), "%d/%d/%d,%d:%d:%d%d", &nitz_time.year,
        &nitz_time.month, &nitz_time.day, &nitz_time.hour, &nitz_time.minute,
        &nitz_time.second, &nitz_time.timezoneOffsetQuarters);

    if (parsed_items == 7) {
      // Parsing completo riuscito - fuso orario presente
      nitz_time.year += 2000;
      nitz_time.valid = true;
      nitz_time.hasTimezone = true;
      nitz_time.timezoneHours =
          nitz_time.timezoneOffsetQuarters / 4; // Converti quarti in ore

      Serial.printf("✅ NITZ Completo (senza virgolette): %04d/%02d/%02d "
                    "%02d:%02d:%02d UTC%+d (offset: %+d quarti)\n",
                    nitz_time.year, nitz_time.month, nitz_time.day,
                    nitz_time.hour, nitz_time.minute, nitz_time.second,
                    nitz_time.timezoneHours, nitz_time.timezoneOffsetQuarters);
    } else {
      // TENTATIVO 2: Formato con fuso orario CON virgolette
      // +CCLK: "25/01/16,15:30:45+04"
      parsed_items = sscanf(
          timeData.c_str(), "\"%d/%d/%d,%d:%d:%d%d\"", &nitz_time.year,
          &nitz_time.month, &nitz_time.day, &nitz_time.hour, &nitz_time.minute,
          &nitz_time.second, &nitz_time.timezoneOffsetQuarters);

      if (parsed_items == 7) {
        // Parsing completo con virgolette
        nitz_time.year += 2000;
        nitz_time.valid = true;
        nitz_time.hasTimezone = true;
        nitz_time.timezoneHours = nitz_time.timezoneOffsetQuarters / 4;

        Serial.printf("✅ NITZ Completo (con virgolette): %04d/%02d/%02d "
                      "%02d:%02d:%02d UTC%+d (offset: %+d quarti)\n",
                      nitz_time.year, nitz_time.month, nitz_time.day,
                      nitz_time.hour, nitz_time.minute, nitz_time.second,
                      nitz_time.timezoneHours,
                      nitz_time.timezoneOffsetQuarters);
      } else {
        // TENTATIVO 3: Formato base senza fuso orario SENZA virgolette
        // +CCLK: 25/01/16,15:30:45
        parsed_items =
            sscanf(timeData.c_str(), "%d/%d/%d,%d:%d:%d", &nitz_time.year,
                   &nitz_time.month, &nitz_time.day, &nitz_time.hour,
                   &nitz_time.minute, &nitz_time.second);

        if (parsed_items == 6) {
          // Parsing base senza timezone
          nitz_time.year += 2000;
          nitz_time.valid = true;
          nitz_time.hasTimezone = false;
          nitz_time.timezoneOffsetQuarters = 0;
          nitz_time.timezoneHours = 0;

          Serial.printf("✅ NITZ Base (senza timezone, senza virgolette): "
                        "%04d/%02d/%02d %02d:%02d:%02d\n",
                        nitz_time.year, nitz_time.month, nitz_time.day,
                        nitz_time.hour, nitz_time.minute, nitz_time.second);
        } else {
          // TENTATIVO 4: Formato base senza fuso orario CON virgolette
          // +CCLK: "25/01/16,15:30:45"
          parsed_items =
              sscanf(timeData.c_str(), "\"%d/%d/%d,%d:%d:%d\"", &nitz_time.year,
                     &nitz_time.month, &nitz_time.day, &nitz_time.hour,
                     &nitz_time.minute, &nitz_time.second);

          if (parsed_items == 6) {
            // Parsing base con virgolette
            nitz_time.year += 2000;
            nitz_time.valid = true;
            nitz_time.hasTimezone = false;
            nitz_time.timezoneOffsetQuarters = 0;
            nitz_time.timezoneHours = 0;

            Serial.printf("✅ NITZ Base (senza timezone, con virgolette): "
                          "%04d/%02d/%02d %02d:%02d:%02d\n",
                          nitz_time.year, nitz_time.month, nitz_time.day,
                          nitz_time.hour, nitz_time.minute, nitz_time.second);
          } else {
            Serial.printf("❌ Errore: TUTTI i formati NITZ falliti. Ultima "
                          "prova parsed items: %d\n",
                          parsed_items);
            Serial.printf("❌ Stringa finale da parsare: '%s'\n",
                          timeData.c_str());
            nitz_time.valid = false;
          }
        }
      }
    }
  } else {
    Serial.println("❌ Errore: risposta +CCLK: non trovata.");
    nitz_time.valid = false;
  }

  return nitz_time;
}

// syncWithNITZ() rimossa - logica integrata direttamente in handleNBIoTSync()

// isDaylightSavingTime() rimossa - ora usa setenv() per gestione automatica DST

String getSignalQuality() {
  String response = sendATCommand("AT+CSQ");

  if (response.indexOf("+CSQ:") >= 0) {
    int rssiIdx = response.indexOf(":") + 1;
    int commaIdx = response.indexOf(",", rssiIdx);

    if (rssiIdx > 0 && commaIdx > rssiIdx) {
      int rssi = response.substring(rssiIdx, commaIdx).toInt();
      currentRSSI = rssi; // Salva per la GUI (icona)

      // Se l'RSSI è cambiato significativamente, aggiorna l'icona sul display
      // (Partial Update)
      if (abs(currentRSSI - prevState.lastRSSI) > 2 ||
          (currentRSSI == 99 && prevState.lastRSSI != 99)) {
        if (displayMutex != NULL)
          xSemaphoreTakeRecursive(displayMutex, portMAX_DELAY);
        drawGsmSignalBars(display.width() - 80, 20, currentRSSI);
        display.partialUpdate();
        if (displayMutex != NULL)
          xSemaphoreGiveRecursive(displayMutex);
        prevState.lastRSSI = currentRSSI;
      }

      // Estrai anche il BER (Bit Error Rate)
      int ber = -1;
      int spaceIdx = response.indexOf(" ", commaIdx);
      if (spaceIdx == -1)
        spaceIdx = response.length();

      String berStr = response.substring(commaIdx + 1, spaceIdx);
      berStr.trim();
      currentBER = berStr.toInt(); // Salva per la GUI

      if (rssi == 99)
        return "No Signal";

      String quality;
      if (rssi >= 7)
        quality = "Eccellente"; // ≥ -99 dBm (NB-IoT standard)
      else if (rssi >= 2)
        quality = "Buono"; // ≥ -109 dBm (NB-IoT standard)
      else if (rssi >= -1)
        quality = "Accettabile"; // ≥ -115 dBm (NB-IoT standard)
      else
        quality = "Debole"; // < -115 dBm

      // Aggiungi valore dBm e BER
      int dbm = -113 + 2 * rssi;
      char result[80];
      if (ber >= 0 && ber <= 7) {
        // BER valido (0-7 secondo standard GSM)
        snprintf(result, sizeof(result), "%s (%ddBm BER:%d)", quality.c_str(),
                 dbm, ber);
      } else {
        // BER non valido, mostra solo dBm
        snprintf(result, sizeof(result), "%s (%ddBm)", quality.c_str(), dbm);
      }
      return String(result);
    }
  }

  currentRSSI = 99; // Valore non valido
  currentBER = -1;  // Valore non valido
  return "Sconosciuto";
}

// === IMPLEMENTAZIONE OTTIMIZZAZIONI SEGNALE DEBOLE ===

SignalInfo getDetailedSignalInfo() {
  SignalInfo info;
  info.rssi = 99; // Default: No Signal
  info.ber = -1;
  info.quality = "Sconosciuto";
  info.ceLevel = -1;
  info.isWeakSignal = true;

  // Ottieni RSSI e BER
  String response = sendATCommand("AT+CSQ");
  if (response.indexOf("+CSQ:") >= 0) {
    int rssiIdx = response.indexOf(":") + 1;
    int commaIdx = response.indexOf(",", rssiIdx);

    if (rssiIdx > 0 && commaIdx > rssiIdx) {
      info.rssi = response.substring(rssiIdx, commaIdx).toInt();

      // Estrai BER
      int spaceIdx = response.indexOf(" ", commaIdx);
      if (spaceIdx == -1)
        spaceIdx = response.length();
      String berStr = response.substring(commaIdx + 1, spaceIdx);
      berStr.trim();
      info.ber = berStr.toInt();
      currentRSSI = info.rssi; // Aggiorna globale
      currentBER = info.ber;   // Aggiorna globale

      // Classifica qualità segnale per strategia adattiva
      if (info.rssi == 99) {
        info.quality = "No Signal";
        info.isWeakSignal = true;
      } else if (info.rssi >= 7) {
        info.quality = "Forte"; // ≥ -99 dBm - strategia normale
        info.isWeakSignal = false;
      } else if (info.rssi >= 2) {
        info.quality = "Moderato"; // -109 a -99 dBm - strategia intermedia
        info.isWeakSignal = false;
      } else if (info.rssi >= 0) {
        info.quality = "Debole"; // -115 a -109 dBm - strategia aggressiva
        info.isWeakSignal = true;
      } else {
        info.quality = "Critico"; // < -115 dBm - strategia massima
        info.isWeakSignal = true;
      }
    }
  }

  return info;
}

String getCoverageEnhancementStats() {
  String response = sendATCommand("AT+NUESTATS?");
  if (response.indexOf("CELL") >= 0 || response.indexOf("RADIO") >= 0) {
    return response;
  }
  return "CE Stats non disponibili";
}

void configureWeakSignalOptimization(SignalInfo &signal) {
  Serial.printf("🔧 Configurazione per segnale %s (RSSI: %d)\n",
                signal.quality.c_str(), signal.rssi);

  if (signal.isWeakSignal) {
    // Configura eDRX per segnali deboli
    Serial.println("📡 Attivazione eDRX per segnale debole...");
    sendATCommand(
        "AT+CEDRXS=1,5,\"0010\""); // eDRX per NB-IoT con ciclo ottimizzato
    delay(500);

    // Configura PSM per segnali critici
    if (signal.quality == "Critico" || signal.rssi == 99) {
      Serial.println(
          "🔋 Attivazione PSM per segnale critico (Active Time: 30s)...");
      // AT+CPSMS=1,,,"00000010","00001111" -> 20s periodic TAU, 30s Active Time
      // Usiamo "00001111" per T3324 (Active Time) che corrisponde a 15 * 2 = 30
      // secondi
      sendATCommand("AT+CPSMS=1,,,\"00000010\",\"00001111\"");
      delay(500);
    }

    // Ottimizzazioni specifiche per NB-IoT
    sendATCommand("AT+CFUN=1"); // Assicura funzionalità completa
    delay(1000);
  }

  // Monitora CE Level
  String ceStats = getCoverageEnhancementStats();
  if (ceStats.indexOf("CE") >= 0) {
    Serial.println("📊 Coverage Enhancement attivo:");
    Serial.println(ceStats);
  }
}

String getOperatorName() {
  String response = sendATCommand("AT+COPS?");

  if (response.indexOf("+COPS:") >= 0) {
    // Formato: +COPS: 0,0,"Vodafone IT",7 oppure +COPS: 0,2,"22210",9
    int firstQuote = response.indexOf("\"");
    int secondQuote = response.indexOf("\"", firstQuote + 1);

    if (firstQuote >= 0 && secondQuote > firstQuote) {
      String operator_code = response.substring(firstQuote + 1, secondQuote);

      // Decodifica codici numerici italiani
      if (operator_code == "22210")
        return "Vodafone IT";
      else if (operator_code == "22201")
        return "TIM";
      else if (operator_code == "22288")
        return "WindTre";
      else if (operator_code == "22299")
        return "3 Italia";
      else
        return operator_code; // Restituisce il codice se non riconosciuto
    }
  }

  return "Sconosciuto";
}

String getLocationInfo() {
  // Ottieni informazioni cella per determinare la posizione approssimativa
  String response = sendATCommand("AT+CEREG?");

  if (response.indexOf("+CEREG:") >= 0) {
    // Formato: +CEREG: 0,5 (registrato, roaming) o +CEREG:
    // 2,1,"7EF4","01A2D001",7
    if (response.indexOf(",1") >= 0) {
      return "IT collegato (GMT+2)";
    } else if (response.indexOf(",5") >= 0) {
      return "IT roaming (GMT+2)";
    } else if (response.indexOf(",2") >= 0) {
      return "Ricerca rete...";
    } else if (response.indexOf(",3") >= 0) {
      return "Accesso negato";
    } else {
      return "Stato rete: " +
             String(response.charAt(response.indexOf(",") + 1));
    }
  }

  return "Non disponibile";
}

void handleNBIoTSync() {
  Serial.println("\n🔄 Avvio procedura di sincronizzazione NITZ...");

  if (!connectNBIoT()) {
    Serial.println("⚠️ Connessione NB-IoT fallita. Sincronizzazione annullata.");
    isNitzSynced = false;
    nbiotConnected = false; // Aggiorna stato connessione
    signalQuality = "Errore Conn.";
    return;
  }

  // --- Inizio calcolo latenza ---
  unsigned long requestStartTime = millis();
  NITZ_Time nitz = getNITZTime();
  unsigned long responseEndTime = millis();
  // --- Fine calcolo latenza ---

  if (nitz.valid) {
    // 1. CALCOLO LATENZA (Round Trip Time)
    unsigned long roundTripTime = responseEndTime - requestStartTime;
    unsigned long oneWayLatencyMs = roundTripTime / 2;

    Serial.printf("⏱️ RTT: %lu ms, Latenza unidirezionale: %lu ms\n",
                  roundTripTime, oneWayLatencyMs);
    Serial.printf("🕐 NITZ ricevuto: %02d:%02d:%02d %s\n", nitz.hour,
                  nitz.minute, nitz.second,
                  nitz.hasTimezone ? "(con timezone)" : "(UTC)");

    // 2. APPLICA FUSO ORARIO DALLA RETE O FALLBACK ITALIA
    applyNetworkTimezone(nitz);

    // 3. COMPENSAZIONE LATENZA E OFFSET FINALE
    unsigned long totalElapsed = millis() - requestStartTime;
    int totalCompensationSec = (totalElapsed + 500) / 1000;

    Serial.printf("🔧 Compensazione latenza: +%d secondi (offset +1s gestito "
                  "da setRTCTimeSynchronized)\n",
                  totalCompensationSec);

    // Calcola l'orario finale sincronizzato (senza aggiornare le variabili
    // globali)
    int syncYear = nitz.year;
    int syncMonth = nitz.month;
    int syncDay = nitz.day;
    int syncHour = nitz.hour + (totalCompensationSec / 3600);
    int syncMinute = nitz.minute + ((totalCompensationSec % 3600) / 60);
    int syncSecond = nitz.second + (totalCompensationSec %
                                    60); // Compensazione latenza (l'offset +1 è
                                         // gestito da setRTCTimeSynchronized)

    // Gestisci overflow secondi/minuti/ore
    if (syncSecond >= 60) {
      syncSecond -= 60;
      syncMinute++;
    }
    if (syncMinute >= 60) {
      syncMinute -= 60;
      syncHour++;
    }
    if (syncHour >= 24) {
      syncHour -= 24;
      syncDay++;
      // Gestione semplificata cambio giorno
      if (syncDay > 31) {
        syncDay = 1;
        syncMonth++;
        if (syncMonth > 12) {
          syncMonth = 1;
          syncYear++;
        }
      }
    }

    Serial.printf("✅ ORA FINALE SINCRONIZZATA: %02d:%02d:%02d\n", syncHour,
                  syncMinute, syncSecond);

    isNitzSynced = true;
    lastSyncTime = millis(); // Aggiorna il timestamp dell'ultima sync

    // Aggiorna info per il display
    char timeStr[20];
    sprintf(timeStr, "%02d:%02d:%02d", syncHour, syncMinute, syncSecond);
    lastSyncTimeStr = String(timeStr);

    signalQuality = getSignalQuality();
    operatorName = getOperatorName();
    locationInfo = getLocationInfo();

    // === SINCRONIZZAZIONE RTC-DRIVEN (NUOVA ARCHITETTURA) ===
    Serial.println("📅 Scheduling NITZ sync con architettura RTC-driven");
    scheduleNITZSync(syncYear, syncMonth, syncDay, syncHour, syncMinute,
                     syncSecond, requestStartTime);

    updateDashboardInfo();
    updateNetworkInfoPartial(); // Forza aggiornamento info di rete sul display
    updateDateDisplayPartial(); // Aggiorna solo se data cambiata

  } else {
    Serial.println("⚠️ Sincronizzazione fallita, NITZ non valido.");
    isNitzSynced = false;
    signalQuality = "Errore NITZ";
  }
}

// === TASK ASINCRONO PER SINCRONIZZAZIONE NON BLOCCANTE ===
void syncTaskFunction(void *parameter) {
  Serial.println("🔄 Task sincronizzazione avviato in background...");

  // Copia la logica di handleNBIoTSync() qui dentro
  if (!connectNBIoT()) {
    Serial.println("⚠️ Connessione NB-IoT fallita. Sincronizzazione annullata.");
    lastSyncSuccessful = false;
    nbiotConnected = false; // Aggiorna stato connessione
    signalQuality = "Errore Conn.";
    syncResultAvailable = true;
    syncInProgress = false;
    vTaskDelete(NULL); // Termina il task
    return;
  }

  unsigned long requestStartTime = millis();
  NITZ_Time nitz = getNITZTime();
  unsigned long responseEndTime = millis();

  if (nitz.valid) {
    unsigned long roundTripTime = responseEndTime - requestStartTime;
    unsigned long oneWayLatencyMs = roundTripTime / 2;

    Serial.printf("⏱️ RTT: %lu ms, Latenza unidirezionale: %lu ms\n",
                  roundTripTime, oneWayLatencyMs);
    Serial.printf("🕐 NITZ ricevuto: %02d:%02d:%02d %s\n", nitz.hour,
                  nitz.minute, nitz.second,
                  nitz.hasTimezone ? "(con timezone)" : "(UTC)");

    // APPLICA FUSO ORARIO DALLA RETE O FALLBACK ITALIA
    applyNetworkTimezone(nitz);

    // COMPENSAZIONE LATENZA E OFFSET FINALE
    unsigned long totalElapsed = millis() - requestStartTime;
    int totalCompensationSec = (totalElapsed + 500) / 1000;

    Serial.printf("🔧 Compensazione latenza: +%d secondi (offset +1s gestito "
                  "da setRTCTimeSynchronized)\n",
                  totalCompensationSec);

    // Calcola l'orario finale sincronizzato (task asincrono - senza aggiornare
    // variabili globali)
    int syncYear = nitz.year;
    int syncMonth = nitz.month;
    int syncDay = nitz.day;
    int syncHour = nitz.hour + (totalCompensationSec / 3600);
    int syncMinute = nitz.minute + ((totalCompensationSec % 3600) / 60);
    int syncSecond = nitz.second + (totalCompensationSec %
                                    60); // Compensazione latenza (l'offset +1 è
                                         // gestito da setRTCTimeSynchronized)

    // Gestisci overflow secondi/minuti/ore
    if (syncSecond >= 60) {
      syncSecond -= 60;
      syncMinute++;
    }
    if (syncMinute >= 60) {
      syncMinute -= 60;
      syncHour++;
    }
    if (syncHour >= 24) {
      syncHour -= 24;
      syncDay++;
      // Gestione semplificata cambio giorno
      if (syncDay > 31) {
        syncDay = 1;
        syncMonth++;
        if (syncMonth > 12) {
          syncMonth = 1;
          syncYear++;
        }
      }
    }

    Serial.printf("✅ ORA FINALE TASK ASINCRONO: %02d:%02d:%02d\n", syncHour,
                  syncMinute, syncSecond);

    isNitzSynced = true;
    lastSyncTime = millis();

    // Aggiorna info per il display
    char timeStr[20];
    sprintf(timeStr, "%02d:%02d:%02d", syncHour, syncMinute, syncSecond);
    lastSyncTimeStr = String(timeStr);

    // Queste operazioni NON sono bloccanti perché il display continua nel main
    // thread
    signalQuality = getSignalQuality();
    operatorName = getOperatorName();
    locationInfo = getLocationInfo();

    // === SINCRONIZZAZIONE RTC-DRIVEN (TASK ASINCRONO) ===
    Serial.println("📅 Task: Scheduling NITZ sync con architettura RTC-driven");
    scheduleNITZSync(syncYear, syncMonth, syncDay, syncHour, syncMinute,
                     syncSecond, requestStartTime);

    // Nota: updateDateDisplayPartial() verrà chiamato nel main thread in
    // checkSyncResult()
    lastSyncSuccessful = true;
    Serial.println("✅ Sincronizzazione task completata con successo");

  } else {
    Serial.println("⚠️ Sincronizzazione task fallita, NITZ non valido.");
    lastSyncSuccessful = false;
    signalQuality = "Errore NITZ";
  }

  syncResultAvailable = true;
  syncInProgress = false;
  vTaskDelete(NULL); // Termina il task quando finito
}

void startAsyncSync() {
  if (syncInProgress) {
    Serial.println("⚠️ Sincronizzazione già in corso, saltata");
    return;
  }

  syncInProgress = true;
  syncResultAvailable = false;

  // Crea task con stack di 8KB e priorità 1 (bassa per non interferire con
  // display)
  xTaskCreate(syncTaskFunction, // Funzione del task
              "SyncTask",       // Nome del task
              8192,             // Stack size (8KB)
              NULL,             // Parametri
              1,                // Priorità (1 = bassa)
              &syncTaskHandle   // Handle del task
  );

  Serial.println("🚀 Task sincronizzazione avviato in background");
}

void checkSyncResult() {
  if (syncResultAvailable) {
    syncResultAvailable = false;
    if (lastSyncSuccessful) {
      Serial.println("✅ Sincronizzazione riuscita, aggiorno display...");
      isNitzSynced = true;
      lastSyncTime = millis();
      updateDashboardInfo();      // Keep existing dashboard update
      updateNetworkInfoPartial(); // Keep existing network info update
      updateDateDisplayPartial(); // Aggiorna solo se data cambiata

      // OTTIMIZZAZIONE: Spegni l'Access Point dopo una sincronizzazione
      // riuscita, MA SOLO SE sono passati almeno 30s dal boot e non ci sono
      // utenti. Questo garantisce a Marcello la finestra di connessione
      // richiesta.
      if (APModeActive) {
        if (millis() - bootTime > 30000 && WiFi.softAPgetStationNum() == 0) {
          Serial.println("🔌 Sincronizzazione riuscita e nessun utente "
                         "connesso: spengo Access Point.");
          WiFi.softAPdisconnect(true);
          WiFi.mode(WIFI_OFF);
          APModeActive = false;
        } else if (millis() - bootTime <= 30000) {
          Serial.println("ℹ️ Sync riuscita, ma mantengo AP attivo per finestra "
                         "iniziale di 30s.");
        } else {
          Serial.println("ℹ️ Sync riuscita, ma mantengo AP attivo perché ci "
                         "sono utenti connessi.");
        }
      }
    } else {
      Serial.println("❌ Sincronizzazione fallita (background task)");
    }
    Serial.printf("📊 Risultato sync: %s\n",
                  lastSyncSuccessful ? "SUCCESSO" : "FALLITO");
  }

  // Timeout automatico per l'Access Point (5 min) se nessun utente è connesso
  if (APModeActive && (millis() - bootTime > 300000) &&
      WiFi.softAPgetStationNum() == 0) {
    Serial.println("🔌 Timeout Access Point (nessun utente): spengo per "
                   "risparmio energetico");
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    APModeActive = false;
  }
}

void printNBIoTStatus() {
  Serial.println("\n📊 === STATO NB-IoT ===");
  Serial.printf("🔌 UART: GPIO%d(TX) <-> GPIO%d(RX)\n", SIM_TX_PIN, SIM_RX_PIN);
  Serial.printf("📡 Modulo: SIM7002E @ %d baud\n", SIM_BAUDRATE);
  Serial.printf("🌐 NITZ: %s\n",
                isNitzSynced ? "Sincronizzato" : "Non sincronizzato");
  Serial.printf("🌍 Fuso orario: UNIVERSALE (rete + fallback Italia)\n");
  Serial.printf("🕐 Gestione DST: Automatica dalla rete o Italia\n");
  Serial.printf("🔄 Intervallo sync: %lu secondi\n", syncInterval / 1000);
  Serial.printf("📊 Consumo dati: ZERO byte (solo NITZ)\n");

  // Info sistema sincronizzazione universale
  Serial.printf("⚡ Sistema sync: UNIVERSALE + Veloce\n");
  Serial.printf("📊 Metodo: NITZ + fuso rete + compensazione latenza\n");
  Serial.printf("✅ Stato: Funziona ovunque nel mondo\n");

  Serial.println("========================\n");
}

// ===================================================================
// FUNZIONI WATCHDOG PER PERSISTENZA CONTATORE FALLIMENTI
// ===================================================================

void loadModuleFailureCount() {
  preferences.begin("watchdog",
                    false); // Namespace "watchdog", modalità read-write
  moduleFailureCount =
      preferences.getInt("failures", 0); // Default 0 se non esiste
  preferences.end();
  Serial.printf("🔄 Caricato contatore fallimenti: %d\n", moduleFailureCount);
}

void saveModuleFailureCount() {
  preferences.begin("watchdog",
                    false); // Namespace "watchdog", modalità read-write
  preferences.putInt("failures", moduleFailureCount);
  preferences.end();
  Serial.printf("💾 Salvato contatore fallimenti: %d\n", moduleFailureCount);
}

// === IMPLEMENTAZIONE SISTEMA LATENZA AVANZATO NTP-LIKE ===
// Le funzioni sono state sostituite dal nuovo sistema
// performAdvancedNITZSync() che utilizza algoritmi multi-campione con
// filtraggio statistico e consenso

// ===================================================================
// FUNZIONI RTC PCF85063A - OROLOGIO INTERNO I2C
// ===================================================================

bool initRTC() {
  Wire.beginTransmission(PCF85063A_ADDRESS);
  byte error = Wire.endTransmission();

  if (error == 0) {
    Serial.println("🕰️ PCF85063A rilevato su I2C");
    return true;
  } else {
    Serial.printf("❌ PCF85063A non trovato (errore: %d)\n", error);
    return false;
  }
}

void setRTCTime(int year, int month, int day, int hour, int minute,
                int second) {
  Wire.beginTransmission(PCF85063A_ADDRESS);
  Wire.write(0x04); // Registro secondi
  Wire.write(decToBcd(second));
  Wire.write(decToBcd(minute));
  Wire.write(decToBcd(hour));
  Wire.write(decToBcd(day));
  Wire.write(1); // Giorno settimana (1-7, fisso a Lunedì)
  Wire.write(decToBcd(month));
  Wire.write(decToBcd(year - 2000)); // Anno (formato 2 cifre)
  Wire.endTransmission();

  Serial.printf("🕰️ RTC aggiornato: %02d/%02d/%04d %02d:%02d:%02d\n", day,
                month, year, hour, minute, second);
}

/**
 * @brief Imposta l'ora sull'RTC PCF85063A con una sincronizzazione ad alta
 * precisione.
 *
 * Questa funzione è stata riscritta per ottenere una sincronizzazione precisa
 * al millisecondo. La logica è la seguente:
 * 1.  Converte l'orario di destinazione (da NITZ) in un timestamp Unix
 * (`time_t`).
 * 2.  Aggiunge 1 secondo a questo timestamp per calcolare l'orario del
 * "prossimo secondo". Questo sarà il nostro target di scrittura.
 * 3.  Calcola quanti millisecondi mancano all'inizio del prossimo secondo
 * reale usando `millis()`. Ad esempio, se `millis()` è 12345, mancano 1000 -
 * 345 = 655 ms al secondo successivo.
 * 4.  Attende per questo breve intervallo di tempo (es. 655 ms). Questo è un
 * ritardo minimo e non bloccante.
 * 5.  Al termine del ritardo, ci troviamo esattamente (o quasi) all'inizio di
 * un nuovo secondo.
 * 6.  In questo preciso istante, scrive sull'RTC l'orario target calcolato al
 * punto 2.
 *
 * Questo metodo garantisce che l'RTC venga impostato con l'ora corretta nel
 * momento esatto in cui quell'ora diventa valida, eliminando l'imprecisione
 * della logica precedente.
 */
void setRTCTimeSynchronized(int year, int month, int day, int hour, int minute,
                            int second, unsigned long nitzStartTime) {
  Serial.println("🎯 Avvio sincronizzazione RTC ad alta precisione...");
  Serial.printf("   - Orario NITZ ricevuto: %04d-%02d-%02d %02d:%02d:%02d\n",
                year, month, day, hour, minute, second);

  // Calcola il tempo trascorso dal momento della ricezione NITZ
  unsigned long current_millis = millis();
  unsigned long elapsed_since_nitz =
      current_millis - nitzStartTime; // nitzStartTime è il momento di inizio
                                      // della richiesta NITZ

  // Converti l'orario NITZ in timestamp Unix
  struct tm nitz_tm = {0};
  nitz_tm.tm_year = year - 1900;
  nitz_tm.tm_mon = month - 1;
  nitz_tm.tm_mday = day;
  nitz_tm.tm_hour = hour;
  nitz_tm.tm_min = minute;
  nitz_tm.tm_sec = second;
  time_t nitz_time = mktime(&nitz_tm);

  // Aggiungi il tempo trascorso per ottenere l'orario attuale reale
  nitz_time += (elapsed_since_nitz / 1000); // Converti ms in secondi

  // Riconverti per ottenere l'orario corretto ora
  struct tm *current_tm = localtime(&nitz_time);
  int current_year = current_tm->tm_year + 1900;
  int current_month = current_tm->tm_mon + 1;
  int current_day = current_tm->tm_mday;
  int current_hour = current_tm->tm_hour;
  int current_minute = current_tm->tm_min;
  int current_second = current_tm->tm_sec;

  // Calcola l'orario del prossimo secondo (quello che scriveremo
  // effettivamente)
  time_t next_second_time = nitz_time + 1;
  struct tm *next_tm = localtime(&next_second_time);
  int write_year = next_tm->tm_year + 1900;
  int write_month = next_tm->tm_mon + 1;
  int write_day = next_tm->tm_mday;
  int write_hour = next_tm->tm_hour;
  int write_minute = next_tm->tm_min;
  int write_second = next_tm->tm_sec;

  Serial.printf("   - Tempo trascorso dalla ricezione NITZ: %lu ms\n",
                elapsed_since_nitz);
  Serial.printf("   - Orario corrente calcolato: %02d:%02d:%02d\n",
                current_hour, current_minute, current_second);
  Serial.printf("   - Scriverò al prossimo secondo: %02d:%02d:%02d\n",
                write_hour, write_minute, write_second);

  // === SINCRONIZZAZIONE PRECISA CON RTC TICK ===
  // Invece di usare delay impreciso, aspetta il prossimo cambio secondo del
  // PCF85063A
  Serial.println(
      "   - Sincronizzazione precisa: attendo il prossimo tick RTC...");

  // Leggi il secondo corrente
  int rtcYear, rtcMonth, rtcDay, rtcHour, rtcMinute, rtcSecond;
  getRTCTime(rtcYear, rtcMonth, rtcDay, rtcHour, rtcMinute, rtcSecond);
  int currentRTCSecond = rtcSecond;

  // Attendi fino al cambio del secondo RTC (sincronizzazione perfetta)
  unsigned long waitStart = millis();
  unsigned long lastDisplayUpdate = millis();
  while (true) {
    getRTCTime(rtcYear, rtcMonth, rtcDay, rtcHour, rtcMinute, rtcSecond);
    if (rtcSecond != currentRTCSecond) {
      // Il PCF85063A ha appena cambiato secondo - momento perfetto per
      // scrivere!
      Serial.printf(
          "   - ✅ RTC tick rilevato (%02d->%02d), scrivo immediatamente!\n",
          currentRTCSecond, rtcSecond);
      break;
    }
    if (millis() - waitStart > 1500) { // Timeout di sicurezza
      Serial.println("   - ⚠️ Timeout attesa RTC tick, procedo comunque");
      break;
    }

    // Aggiorna display ogni 100ms durante l'attesa per evitare blocchi
    // visibili
    if (millis() - lastDisplayUpdate > 100) {
      // Aggiorna le variabili globali con l'ora RTC attuale
      currentHour = rtcHour;
      currentMinute = rtcMinute;
      currentSecond = rtcSecond;

      // Aggiorna solo il display dell'orario senza stravolgere
      if (displayMutex != NULL)
        xSemaphoreTakeRecursive(displayMutex, portMAX_DELAY);
      updateSevenSegmentDisplay();
      if (displayMutex != NULL)
        xSemaphoreGiveRecursive(displayMutex);
      lastDisplayUpdate = millis();
    }

    delayMicroseconds(100); // Polling veloce ma non intensivo
  }

  // Scrivi l'orario nell'RTC
  Wire.beginTransmission(PCF85063A_ADDRESS);
  Wire.write(0x04); // Indirizzo del registro dei secondi
  Wire.write(decToBcd(write_second));
  Wire.write(decToBcd(write_minute));
  Wire.write(decToBcd(write_hour));
  Wire.write(decToBcd(write_day));
  Wire.write(1); // Giorno della settimana (1-7, fisso a Lunedì)
  Wire.write(decToBcd(write_month));
  Wire.write(decToBcd(write_year - 2000)); // Anno in formato a 2 cifre
  Wire.endTransmission();

  Serial.printf(
      "✅ RTC SINCRONIZZATO: %02d:%02d:%02d scritto al momento giusto\n",
      write_hour, write_minute, write_second);
}

// === IMPLEMENTAZIONE OBSOLETA RIMOSSA ===
// Transizione graduale sostituita con architettura RTC-driven

// === IMPLEMENTAZIONE RTC-DRIVEN (SOLUZIONE DEFINITIVA) ===

bool checkRTCSecondChanged() {
  // Legge l'RTC e controlla se il secondo è cambiato
  int rtcYear, rtcMonth, rtcDay, rtcHour, rtcMinute, rtcSecond;
  getRTCTime(rtcYear, rtcMonth, rtcDay, rtcHour, rtcMinute, rtcSecond);

  // DEBUG: Log periodico per verificare lettura RTC
  static int debugCounter = 0;
  if (++debugCounter >= 20) { // Ogni 20 chiamate
    Serial.printf("🔍 DEBUG RTC Read: %02d:%02d:%02d (lastSecond=%d)\n",
                  rtcHour, rtcMinute, rtcSecond, rtcSync.lastSecond);
    debugCounter = 0;
  }

  if (rtcSecond != rtcSync.lastSecond) {
    // Il secondo è cambiato! Aggiorna variabili globali
    Serial.printf(
        "⏰ RTC Second Changed: %02d:%02d:%02d -> Updating global vars\n",
        rtcHour, rtcMinute, rtcSecond);

    rtcSync.lastSecond = rtcSecond;
    currentYear = rtcYear;
    currentMonth = rtcMonth;
    currentDay = rtcDay;
    currentHour = rtcHour;
    currentMinute = rtcMinute;
    currentSecond = rtcSecond;

    // DEBUG: Verifica che le variabili globali siano state aggiornate
    Serial.printf("✅ Global vars updated: current=%02d:%02d:%02d\n",
                  currentHour, currentMinute, currentSecond);

    return true;
  }
  return false;
}

void scheduleNITZSync(int targetY, int targetMo, int targetD, int targetH,
                      int targetM, int targetS, unsigned long nitzStart) {
  rtcSync.waitingForSync = true;
  rtcSync.targetYear = targetY;
  rtcSync.targetMonth = targetMo;
  rtcSync.targetDay = targetD;
  rtcSync.targetHour = targetH;
  rtcSync.targetMinute = targetM;
  rtcSync.targetSecond = targetS;
  rtcSync.hasTargetTime = true;
  rtcSync.nitzStartTime = nitzStart;

  Serial.printf("📅 NITZ schedulato: target %04d-%02d-%02d %02d:%02d:%02d\n",
                targetY, targetMo, targetD, targetH, targetM, targetS);
}

void processRTCDrivenSync() {
  // Debug stato sync ogni 10 secondi
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 10000) {
    lastDebug = millis();
    Serial.printf("🔍 processRTCDrivenSync() stato: waitingForSync=%s, "
                  "hasTargetTime=%s\n",
                  rtcSync.waitingForSync ? "true" : "false",
                  rtcSync.hasTargetTime ? "true" : "false");
  }

  if (!rtcSync.waitingForSync || !rtcSync.hasTargetTime)
    return;

  Serial.printf("🎯 processRTCDrivenSync() chiamata - current=%02d:%02d:%02d, "
                "target=%02d:%02d:%02d\n",
                currentHour, currentMinute, currentSecond, rtcSync.targetHour,
                rtcSync.targetMinute, rtcSync.targetSecond);

  // Calcola differenza tra ora attuale RTC e target NITZ
  int currentTotalSec = currentHour * 3600 + currentMinute * 60 + currentSecond;
  int targetTotalSec = rtcSync.targetHour * 3600 + rtcSync.targetMinute * 60 +
                       rtcSync.targetSecond;

  long diffSec = targetTotalSec - currentTotalSec;

  // Gestisce cambio giorno
  if (diffSec > 43200)
    diffSec -= 86400;
  else if (diffSec < -43200)
    diffSec += 86400;

  Serial.printf("🔍 Diff RTC vs NITZ: %ld sec\n", diffSec);

  // Condizioni per il sync:
  // 1. Differenza piccola (≤ 2 minuti) - drift normale dell'RTC
  // 2. Differenza grande (≥ 10 minuti) - RTC probabilmente resettato a
  // 00:00:00
  bool shouldSync = (abs(diffSec) <= 120) ||
                    (abs(diffSec) >= 600); // 120 sec = 2 min, 600 sec = 10 min

  if (shouldSync) {
    if (abs(diffSec) <= 120) {
      Serial.printf("🎯 Differenza piccola %ld sec - Sync per drift RTC\n",
                    diffSec);
    } else {
      Serial.printf("🔄 Differenza grande %ld sec - Sync per reset RTC\n",
                    diffSec);
    }
    rtcSync.waitingForSync = false;
    rtcSync.hasTargetTime = false;

    // Calcola orario compensato per latenza e scrive RTC
    unsigned long elapsed = millis() - rtcSync.nitzStartTime;
    int compensationSec = (elapsed + 500) / 1000;

    int finalHour = rtcSync.targetHour;
    int finalMinute = rtcSync.targetMinute;
    int finalSecond = rtcSync.targetSecond + compensationSec;

    // Gestisci overflow
    if (finalSecond >= 60) {
      finalSecond -= 60;
      finalMinute++;
      if (finalMinute >= 60) {
        finalMinute -= 60;
        finalHour++;
        if (finalHour >= 24) {
          finalHour -= 24;
        }
      }
    }

    Serial.printf("✅ Sync RTC-driven: %02d:%02d:%02d\n", finalHour,
                  finalMinute, finalSecond);
    setRTCTimeSynchronized(rtcSync.targetYear, rtcSync.targetMonth,
                           rtcSync.targetDay, finalHour, finalMinute,
                           finalSecond, rtcSync.nitzStartTime);

    // Aggiorna immediatamente le variabili globali leggendo dal PCF85063A
    // appena sincronizzato
    Serial.printf("🔧 PRIMA updateTime(): current=%02d:%02d:%02d\n",
                  currentHour, currentMinute, currentSecond);
    updateTime();
    Serial.printf("🔧 DOPO updateTime(): current=%02d:%02d:%02d\n", currentHour,
                  currentMinute, currentSecond);

    // IMPORTANTE: Forza reset del tracking RTC per rilevare immediatamente il
    // nuovo orario
    rtcSync.lastSecond =
        -1; // Reset per forzare checkRTCSecondChanged() a rilevare il cambio

    // Forza aggiornamento completo del display per mostrare il nuovo orario
    if (displayMutex != NULL)
      xSemaphoreTakeRecursive(displayMutex, portMAX_DELAY);
    updateSevenSegmentDisplay();
    updateDashboardInfo();
    updateNetworkInfoPartial();
    updateDateDisplayPartial();
    if (displayMutex != NULL)
      xSemaphoreGiveRecursive(displayMutex);

    isNitzSynced = true;
    lastSyncTime = millis();

    char timeStr[20];
    sprintf(timeStr, "%02d:%02d:%02d", finalHour, finalMinute, finalSecond);
    lastSyncTimeStr = String(timeStr);

    Serial.printf("🏁 processRTCDrivenSync() COMPLETATA - display ora dovrebbe "
                  "mostrare: %02d:%02d:%02d\n",
                  currentHour, currentMinute, currentSecond);
  } else {
    // Differenza intermedia (2-10 min) - probabile errore, mantieni RTC
    Serial.printf("⚠️ Differenza intermedia %ld sec - Mantiene RTC attuale "
                  "(possibile errore NITZ)\n",
                  diffSec);
    rtcSync.waitingForSync = false;
    rtcSync.hasTargetTime = false;
  }
}

void getRTCTime(int &year, int &month, int &day, int &hour, int &minute,
                int &second) {
  Wire.beginTransmission(PCF85063A_ADDRESS);
  Wire.write(0x04); // Registro secondi
  Wire.endTransmission();

  Wire.requestFrom(PCF85063A_ADDRESS, 7);

  if (Wire.available() >= 7) {
    second = bcdToDec(Wire.read() & 0x7F); // Maschera stop bit
    minute = bcdToDec(Wire.read() & 0x7F);
    hour = bcdToDec(Wire.read() & 0x3F); // Formato 24h
    day = bcdToDec(Wire.read() & 0x3F);
    Wire.read(); // Salta giorno settimana
    month = bcdToDec(Wire.read() & 0x1F);
    year = bcdToDec(Wire.read()) + 2000; // Converte in formato 4 cifre
  } else {
    Serial.println("❌ Errore lettura RTC");
    // Valori di default
    year = 2025;
    month = 1;
    day = 1;
    hour = 12;
    minute = 0;
    second = 0;
  }
}

byte bcdToDec(byte val) { return (val / 16 * 10) + (val % 16); }

byte decToBcd(byte val) { return (val / 10 * 16) + (val % 10); }

// ===================================================================
// FUNZIONI GESTIONE FUSO ORARIO E NITZ - CALCOLO UNIVERSALE
// ===================================================================

int calculateItalyDSTOffset(int month, int day) {
  // Calcolo ora legale Italia come fallback
  // Restituisce: 1 = UTC+1 (inverno), 2 = UTC+2 (estate)

  bool isDST = false;
  if (month >= 4 && month <= 9) {
    isDST = true; // Certamente ora legale da aprile a settembre
  } else if (month == 3 || month == 10) {
    // Calcolo semplificato: assumiamo ora legale dal 25 marzo al 25 ottobre
    if (month == 3 && day >= 25)
      isDST = true;
    if (month == 10 && day <= 25)
      isDST = true;
  }

  return isDST ? 2 : 1; // UTC+2 estate, UTC+1 inverno
}

void applyNetworkTimezone(NITZ_Time &nitz) {
  // Applica il fuso orario dalla rete se disponibile, altrimenti usa fallback
  // Italia

  int offsetHours = 0;
  String timezoneSource = "";

  if (nitz.hasTimezone) {
    // Usa fuso orario dalla rete (universale)
    offsetHours = nitz.timezoneHours;
    timezoneSource = "Rete";
    Serial.printf("🌍 Fuso orario dalla rete: UTC%+d (offset: %+d quarti)\n",
                  offsetHours, nitz.timezoneOffsetQuarters);

    // CONTROLLO DI COERENZA per Italia: verifica se l'offset rete è corretto
    // per la stagione
    int expectedItalyOffset = calculateItalyDSTOffset(nitz.month, nitz.day);
    if (offsetHours != expectedItalyOffset) {
      Serial.printf("⚠️ ATTENZIONE: Offset rete (%+d) diverso da DST Italia "
                    "(%+d) per %s\n",
                    offsetHours, expectedItalyOffset,
                    (expectedItalyOffset == 1) ? "ORA SOLARE" : "ORA LEGALE");
      Serial.printf("🤔 La rete potrebbe fornire offset fisso o essere "
                    "configurata male\n");

      // Opzione: decommentare la riga sotto per forzare il calcolo Italia
      // invece della rete offsetHours = expectedItalyOffset; timezoneSource =
      // "Italia (forzato)";
    } else {
      Serial.printf("✅ Offset rete coerente con DST Italia: %s\n",
                    (offsetHours == 1) ? "ORA SOLARE" : "ORA LEGALE");
    }
  } else {
    // Fallback: calcolo Italia
    offsetHours = calculateItalyDSTOffset(nitz.month, nitz.day);
    timezoneSource = "Italia (fallback)";
    Serial.printf("🇮🇹 Fuso orario fallback Italia: UTC%+d (%s)\n", offsetHours,
                  (offsetHours == 1) ? "ORA SOLARE" : "ORA LEGALE");
  }

  // CORREZIONE: Il tempo NITZ è SEMPRE UTC, l'offset va SEMPRE applicato
  // Applica SEMPRE l'offset per convertire UTC→Locale
  Serial.printf("🔧 Applicazione offset: UTC %02d:%02d:%02d + %+dh → ",
                nitz.hour, nitz.minute, nitz.second, offsetHours);

  nitz.hour += offsetHours;

  // Gestisci overflow ore
  if (nitz.hour >= 24) {
    nitz.hour -= 24;
    nitz.day++;
    // Gestione semplificata cambio giorno
    if (nitz.day > 31) {
      nitz.day = 1;
      nitz.month++;
      if (nitz.month > 12) {
        nitz.month = 1;
        nitz.year++;
      }
    }
    Serial.printf("Locale %02d:%02d:%02d (giorno successivo)\n", nitz.hour,
                  nitz.minute, nitz.second);
  } else if (nitz.hour < 0) {
    nitz.hour += 24;
    nitz.day--;
    if (nitz.day < 1) {
      nitz.day = 31; // Semplificato
      nitz.month--;
      if (nitz.month < 1) {
        nitz.month = 12;
        nitz.year--;
      }
    }
    Serial.printf("Locale %02d:%02d:%02d (giorno precedente)\n", nitz.hour,
                  nitz.minute, nitz.second);
  } else {
    Serial.printf("Locale %02d:%02d:%02d\n", nitz.hour, nitz.minute,
                  nitz.second);
  }

  // Salva informazioni per debug
  Serial.printf("✅ Fuso orario: %s, Ora finale: %02d:%02d:%02d\n",
                timezoneSource.c_str(), nitz.hour, nitz.minute, nitz.second);
}

// ===================================================================
// FUNZIONI GESTIONE DATA E GIORNO SETTIMANA - AGGIORNAMENTI SELETTIVI
// ===================================================================

int calculateDayOfWeek(int day, int month, int year) {
  // Algoritmo semplificato e più affidabile per calcolo giorno settimana
  // Restituisce: 0=Domenica, 1=Lunedì, 2=Martedì, ... 6=Sabato

  // Aggiusta mese e anno per algoritmo di Zeller
  if (month < 3) {
    month += 12;
    year--;
  }

  int k = year % 100; // Anno del secolo
  int j = year / 100; // Secolo

  // Formula di Zeller standard
  int h = (day + ((13 * (month + 1)) / 5) + k + (k / 4) + (j / 4) - 2 * j) % 7;

  // Zeller restituisce: 0=Sabato, 1=Domenica, 2=Lunedì, ..., 6=Venerdì
  // Dobbiamo convertire a: 0=Domenica, 1=Lunedì, 2=Martedì, ..., 6=Sabato
  int dayOfWeek = (h + 6) % 7; // Shift di 6 per ottenere Dom=0

  // Gestisci risultati negativi
  if (dayOfWeek < 0)
    dayOfWeek += 7;

  return dayOfWeek;
}

String getTranslatedDay(int dayOfWeek) {
  if (dayOfWeek < 0 || dayOfWeek > 6)
    return "";

  const char *set_it[] = {"Domenica", "Lunedi",  "Martedi", "Mercoledi",
                          "Giovedi",  "Venerdi", "Sabato"};
  const char *set_en[] = {"Sunday",   "Monday", "Tuesday", "Wednesday",
                          "Thursday", "Friday", "Saturday"};
  const char *set_fr[] = {"Dimanche", "Lundi",    "Mardi", "Mercredi",
                          "Jeudi",    "Vendredi", "Samedi"};
  const char *set_es[] = {"Domingo", "Lunes",   "Martes", "Miercoles",
                          "Jueves",  "Viernes", "Sabado"};
  const char *set_de[] = {"Sonntag",    "Montag",  "Dienstag", "Mittwoch",
                          "Donnerstag", "Freitag", "Samstag"};

  if (configLang == "en")
    return String(set_en[dayOfWeek]);
  if (configLang == "fr")
    return String(set_fr[dayOfWeek]);
  if (configLang == "es")
    return String(set_es[dayOfWeek]);
  if (configLang == "de")
    return String(set_de[dayOfWeek]);
  return String(set_it[dayOfWeek]);
}

String getTranslatedMonth(int month) {
  if (month < 1 || month > 12)
    return "";

  const char *set_it[] = {"",        "Gennaio",   "Febbraio", "Marzo",
                          "Aprile",  "Maggio",    "Giugno",   "Luglio",
                          "Agosto",  "Settembre", "Ottobre",  "Novembre",
                          "Dicembre"};
  const char *set_en[] = {
      "",     "January", "February",  "March",   "April",    "May",     "June",
      "July", "August",  "September", "October", "November", "December"};
  const char *set_fr[] = {"",        "Janvier",  "Fevrier", "Mars", "Avril",
                          "Mai",     "Juin",     "Juillet", "Aout", "Septembre",
                          "Octobre", "Novembre", "Decembre"};
  const char *set_es[] = {"",         "Enero",      "Febrero", "Marzo",
                          "Abril",    "Mayo",       "Junio",   "Julio",
                          "Agosto",   "Septiembre", "Octubre", "Noviembre",
                          "Diciembre"};
  const char *set_de[] = {
      "",     "Januar", "Februar",   "Marz",    "April",    "Mai",     "Juni",
      "Juli", "August", "September", "Oktober", "November", "Dezember"};

  if (configLang == "en")
    return String(set_en[month]);
  if (configLang == "fr")
    return String(set_fr[month]);
  if (configLang == "es")
    return String(set_es[month]);
  if (configLang == "de")
    return String(set_de[month]);
  return String(set_it[month]);
}

void cleanPoemArea(int startY) {
  // Anti-ghosting: flash area (nero-bianco) prima di disegnare
  // Nota: Protezione Mutex gestita dal chiamante

  Serial.println("🧼 Eseguo routine Anti-Ghosting area poesia...");

  // Flash Nero
  display.fillRect(0, startY, display.width(), display.height() - startY,
                   BLACK);
  display.partialUpdate();
  delay(200);

  // Flash Bianco
  display.fillRect(0, startY, display.width(), display.height() - startY,
                   WHITE);
  display.partialUpdate();
  delay(100);

  // Secondo flash per sicurezza (opzionale ma consigliato per ghosting
  // ostinato)
  display.fillRect(0, startY, display.width(), display.height() - startY,
                   BLACK);
  display.partialUpdate();
  delay(100);
  display.fillRect(0, startY, display.width(), display.height() - startY,
                   WHITE);
  display.partialUpdate();
}

bool hasDateChanged() {
  // Verifica se la data è cambiata rispetto all'ultimo aggiornamento
  bool dateChanged =
      (currentDay != prevState.lastDay || currentMonth != prevState.lastMonth ||
       currentYear != prevState.lastYear);

  if (dateChanged) {
    Serial.printf("📅 Data cambiata: %02d/%02d/%04d → %02d/%02d/%04d\n",
                  prevState.lastDay, prevState.lastMonth, prevState.lastYear,
                  currentDay, currentMonth, currentYear);
  }

  return dateChanged;
}

void updateDateDisplayPartial() {
  // Aggiornamento selettivo della data quando cambia
  if (!hasDateChanged()) {
    return; // Nessun cambio, esci
  }

  // Calcola nuovo giorno della settimana
  int newDayOfWeek = calculateDayOfWeek(currentDay, currentMonth, currentYear);
  bool dayOfWeekChanged = (newDayOfWeek != prevState.lastDayOfWeek);

  if (dayOfWeekChanged) {
    const char *giorni[] = {"Dom", "Lun", "Mar", "Mer", "Gio", "Ven", "Sab"};
    Serial.printf("📅 Giorno settimana cambiato: %s → %s\n",
                  (prevState.lastDayOfWeek >= 0 && prevState.lastDayOfWeek < 7)
                      ? giorni[prevState.lastDayOfWeek]
                      : "N/A",
                  giorni[newDayOfWeek]);
  }

  // Coordinate per la data (corrispondenti a drawFullSevenSegmentClock)
  int clockBaselineY = 100;
  int dateY = clockBaselineY + seg.bigHeight + 80;

  if (displayMutex != NULL)
    xSemaphoreTakeRecursive(displayMutex, portMAX_DELAY);
  // Cancella area della data
  display.fillRect(0, dateY - 6, display.width(), 50, WHITE);

  // Calcola x inizio orologio per allinearla a sinistra
  int totalClockWidth =
      seg.bigWidth * 4 + seg.digitSpacing * 3 + seg.colonSpacing;
  int clockStartX = (display.width() - totalClockWidth) / 2;

  // Ridisegna la data aggiornata
  display.setFont(&FreeSerifItalic24pt7b);
  display.setTextSize(1);
  char dateString[40];
  sprintf(dateString, "%s %02d %s %04d", getTranslatedDay(newDayOfWeek).c_str(),
          currentDay, getTranslatedMonth(currentMonth).c_str(), currentYear);

  int dateX = clockStartX;
  display.setCursor(dateX, dateY);
  display.print(dateString);

  // Forza partial update
  display.partialUpdate();
  if (displayMutex != NULL)
    xSemaphoreGiveRecursive(displayMutex);

  // Aggiorna stato precedente
  prevState.lastDay = currentDay;
  prevState.lastMonth = currentMonth;
  prevState.lastYear = currentYear;
  prevState.lastDayOfWeek = newDayOfWeek;

  Serial.printf("🔄 Data aggiornata sul display: %s\n", dateString);
}

// ===================================================================
// FUNZIONI POESIA E HTTP
// ===================================================================

String extractJsonString(String json, String key) {
  String searchKey = "\"" + key + "\":\"";
  int startIndex = json.indexOf(searchKey);
  if (startIndex == -1)
    return "";

  startIndex += searchKey.length();
  int endIndex = startIndex;
  bool escaped = false;

  while (endIndex < json.length()) {
    char c = json.charAt(endIndex);
    if (escaped) {
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '"') {
      break;
    }
    endIndex++;
  }

  if (endIndex >= json.length())
    return "";

  String extracted = json.substring(startIndex, endIndex);
  extracted.replace("\\n", "\n");
  extracted.replace("\\\"", "\"");

  // Pulisce la stringa da eventuale formattazione markdown
  extracted.replace("**", "");

  return extracted;
}

// SIM7020 restituisce il payload HTTP codificato in hex (es. "7b2274...")
String hexToStr(String hex) {
  String text = "";
  for (int i = 0; i < hex.length(); i += 2) {
    if (i + 1 < hex.length()) {
      // Sostituzione manuale del carattere apostrofo tipografico unicode (’ =
      // E2 80 99 in hex)
      if (i + 5 < hex.length()) {
        if (hex.substring(i, i + 6).equalsIgnoreCase("e28099")) {
          text += '\'';
          i += 4; // Salta gli altri 2 byte (4 cifre esadecimali) formanti il
                  // carattere
          continue;
        }
      }

      // I font Adafruit GFX "FreeSerif" standard non contengono lo spazio
      // esteso UTF-8 (vocali accentate) Sostituiamo le vocali accentate
      // italiane in UTF-8 con la vocale + apostrofo (a', e', i', o', u')
      if (i + 3 < hex.length()) {
        String seq = hex.substring(i, i + 4);
        seq.toLowerCase();
        if (seq == "c3a0") {
          text += "a'";
          i += 2;
          continue;
        } // à
        if (seq == "c3a8" || seq == "c3a9") {
          text += "e'";
          i += 2;
          continue;
        } // è, é
        if (seq == "c3ac") {
          text += "i'";
          i += 2;
          continue;
        } // ì
        if (seq == "c3b2") {
          text += "o'";
          i += 2;
          continue;
        } // ò
        if (seq == "c3b9") {
          text += "u'";
          i += 2;
          continue;
        } // ù
        if (seq == "c380") {
          text += "A'";
          i += 2;
          continue;
        } // À
        if (seq == "c388" || seq == "c389") {
          text += "E'";
          i += 2;
          continue;
        } // È, É
        if (seq == "c38c") {
          text += "I'";
          i += 2;
          continue;
        } // Ì
        if (seq == "c392") {
          text += "O'";
          i += 2;
          continue;
        } // Ò
        if (seq == "c399") {
          text += "U'";
          i += 2;
          continue;
        } // Ù
      }

      String byteString = hex.substring(i, i + 2);
      char c = (char)strtol(byteString.c_str(), NULL, 16);
      text += c;
    }
  }
  return text;
}

void poemTaskFunction(void *parameter) {
  bool success = false;
  int retries = 2; // Tentativi massimi

  while (retries > 0 && !success) {
    Serial.printf("📜 Task Poesia: Inizio fetch (Tentativi rimasti: %d)...\n",
                  retries);

    // Prende il controllo del modem per tutta la durata della transazione HTTP
    if (modemMutex != NULL) {
      xSemaphoreTakeRecursive(modemMutex, portMAX_DELAY);
    }

    if (!checkModuleOnline()) {
      Serial.println("⚠️ Modulo non online per fetch poesia.");
      if (modemMutex != NULL)
        xSemaphoreGiveRecursive(modemMutex);
      delay(2000);
      retries--;
      continue;
    }

    // 0. Attiva il contesto PDP (Rete Dati) - FONDAMENTALE per errore -2
    Serial.println("🌐 Attivazione contesto PDP (AT+CNACT=1,1)...");
    sendATCommand("AT+CNACT=1,1", 5000);

    // Pulisce istanze HTTP rimaste aperte (se esistono) sull'ID 0
    sendATCommand("AT+CHTTPDESTROY=0", 2000);

    // URL encoding base rudimentale per l'esp32 senza lib enormi (sostituire
    // spazi con %20)
    String safeAuthor = configAuthor;
    safeAuthor.replace(" ", "%20");
    String safeStyle = configStyle;
    safeStyle.replace(" ", "%20");

    // Arrotonda il minuto alla mezz'ora esatta per ottenere la poesia "giusta"
    int fetchMinute = (currentMinute < 30) ? 0 : 30;

    char urlCmd[256];
    sprintf(urlCmd, "AT+CHTTPSEND=0,0,\"/rima?lang=%s&hour=%d&minute=%d",
            configLang.c_str(), currentHour, fetchMinute);

    // 1. Crea l'istanza HTTP usando l'URL configurato in config.h
    String createRes = sendATCommand(
        "AT+CHTTPCREATE=\"" + String(POEM_SERVER_URL) + "\"", 4000);
    if (createRes.indexOf("OK") == -1) {
      Serial.println("❌ Errore creazione istanza HTTP (CHTTPCREATE)");
      if (modemMutex != NULL)
        xSemaphoreGiveRecursive(modemMutex);
      delay(2000);
      retries--;
      continue;
    }

    // 2. Connette al server (usa l'ID 0 che è il default del SIM7020)
    String conRes = sendATCommand("AT+CHTTPCON=0", 6000);
    if (conRes.indexOf("OK") == -1) {
      Serial.println("❌ Errore connessione HTTP (CHTTPCON)");
      sendATCommand("AT+CHTTPDESTROY=0", 2000);
      if (modemMutex != NULL)
        xSemaphoreGiveRecursive(modemMutex);
      delay(2000);
      retries--;
      continue;
    }

    // Crea URL dinamico aggiungendo author e style se presenti
    String fullUrlCmd = String(urlCmd);
    if (safeAuthor != "") {
      fullUrlCmd += "&author=" + safeAuthor;
    }
    if (safeStyle != "") {
      fullUrlCmd += "&style=" + safeStyle;
    }
    fullUrlCmd += "\""; // Chiude le virgolette dell'AT command

    // Invia il comando SEND - il modulo risponde subito con OK,
    // poi la risposta HTTP arriva in modo asincrono via URC +CHTTPNMIC
    String sendRes = sendATCommand(fullUrlCmd, 5000);

    if (sendRes.indexOf("OK") == -1) {
      Serial.printf("❌ Errore invio CHTTPSEND: %s\n", sendRes.c_str());
      sendATCommand("AT+CHTTPDISCON=0", 3000);
      sendATCommand("AT+CHTTPDESTROY=0", 2000);
      poemFetchInProgress = false;
      vTaskDelete(NULL);
      return;
    }

    // Ora aspettiamo la risposta asincrona +CHTTPNMIC (fino a 30 secondi)
    Serial.println("⏳ Attendo risposta asincrona +CHTTPNMIC dal server...");
    String asyncResponse = "";
    unsigned long asyncStart = millis();
    unsigned long lastDataTime = millis();
    const unsigned long ASYNC_TIMEOUT_MS = 30000; // 30 secondi max
    bool receivingNmic = false;

    while (millis() - asyncStart < ASYNC_TIMEOUT_MS) {
      while (sim7002e.available()) {
        char c = sim7002e.read();
        if (c != '\r') {
          asyncResponse += c;
        }
        lastDataTime = millis();
      }

      if (!receivingNmic && asyncResponse.indexOf("+CHTTPNMIC:") >= 0) {
        receivingNmic = true;
      }

      // Se abbiamo iniziato a ricevere dati e passano 1000ms in totale
      // silenzio, significa che la trasmissione è conclusa per intero (niente
      // buffer overflows)
      if (receivingNmic && (millis() - lastDataTime > 1000)) {
        break;
      }

      if (asyncResponse.indexOf("+CHTTPERR:") >= 0) {
        break;
      }

      delay(10); // Pausa minima vitale per non bloccare il watchdog
    }

    Serial.printf("📥 Risposta asincrona (%d bytes): %s\n",
                  asyncResponse.length(), asyncResponse.c_str());

    // Il payload del SIM7020 è in formato esadecimale nell'URC +CHTTPNMIC
    // A volte diviso su più righe, quindi facciamo parsing globale
    String hexPayload = "";
    int searchIndex = 0;

    while (true) {
      int nmicIndex = asyncResponse.indexOf("+CHTTPNMIC:", searchIndex);
      if (nmicIndex == -1)
        break;

      int commaCount = 0;
      int hexStartIndex = -1;
      for (int i = nmicIndex; i < asyncResponse.length(); i++) {
        if (asyncResponse.charAt(i) == ',') {
          commaCount++;
          if (commaCount == 4) {
            hexStartIndex = i + 1;
            break;
          }
        }
      }

      if (hexStartIndex != -1) {
        for (int i = hexStartIndex; i < asyncResponse.length(); i++) {
          char c = asyncResponse.charAt(i);
          if (c == '\r' || c == '\n')
            break; // Fine del blocco URC
          hexPayload += c;
        }
      }
      searchIndex = nmicIndex + 11;
    }

    if (hexPayload.length() > 0) {
      Serial.printf(
          "🔍 Formato HEX aggregato (%d chars): %s...\n", hexPayload.length(),
          hexPayload.substring(0, min(30, (int)hexPayload.length())).c_str());

      // Decodifica Hex -> Testo normale JSON (con conversione apostrofi attiva)
      String jsonPayload = hexToStr(hexPayload);
      Serial.printf("📖 JSON Decodificato:\n%s\n", jsonPayload.c_str());

      // Ora possiamo fare il parsing del JSON in chiaro
      String rhyme = extractJsonString(jsonPayload, "rhyme");
      String humanAuthor = extractJsonString(jsonPayload, "human_author");

      // Pulizia dell'autore se l'API ritorna la stringa "null"
      if (humanAuthor == "null")
        humanAuthor = "";

      if (rhyme != "") {
        Serial.printf("📜 POESIA RICEVUTA (Autore: %s):\n%s\n",
                      humanAuthor != "" ? humanAuthor.c_str() : "Sconosciuto",
                      rhyme.c_str());
        currentPoem = rhyme;
        currentMood =
            humanAuthor; // Ricicliamo la variabile globale currentMood
                         // per mostrare l'autore sul display
        lastPoemFetchTime = millis();
        newPoemReadyToDisplay = true;
        success = true; // Fetch riuscito!
      } else {
        Serial.println("❌ Parsing JSON poesia fallito");
      }
    } else if (asyncResponse.length() == 0) {
      Serial.printf("❌ Nessuna risposta HTTP ricevuta entro il timeout\n");
    } else {
      Serial.println(
          "❌ Impossibile trovare i dati hex della risposta CHTTPNMIC");
    }

    // 4. Disconnette e distrugge l'istanza
    sendATCommand("AT+CHTTPDISCON=0", 3000);
    sendATCommand("AT+CHTTPDESTROY=0", 2000);

    if (modemMutex != NULL) {
      xSemaphoreGiveRecursive(modemMutex);
    }

    if (!success) {
      Serial.println(
          "⚠️ Fetch fallito, attendo 5 secondi prima di riprovare...");
      delay(5000);
      retries--;
    }
  }

  poemFetchInProgress = false;
  vTaskDelete(NULL);
}

void startAsyncPoemFetch() {
  if (poemFetchInProgress)
    return;
  poemFetchInProgress = true;
  poemFetchStartTime = millis();

  xTaskCreate(poemTaskFunction, "PoemTask", 8192, NULL, 1, &poemTaskHandle);
  Serial.println("🚀 Task fetch poesia avviato in background");
}

void drawPoemArea(int startY) {
  // Nota: Protezione Mutex gestita dal chiamante
  // Cancella l'area per la poesia
  display.fillRect(0, startY, display.width(), display.height() - startY,
                   WHITE);

  int marginX = 20;
  int h = display.height() - startY - 20;
  if (h < 10)
    return; // Evita disegno se non c'è spazio

  // Disegno una cornice elegante
  display.drawRect(marginX, startY, display.width() - marginX * 2, h, BLACK);
  display.drawRect(marginX + 2, startY + 2, display.width() - marginX * 2 - 4,
                   h - 4, BLACK);

  // Imposta font elegante (Bold Italic 24pt)
  display.setFont(&FreeSerifBoldItalic24pt7b);
  display.setTextSize(1);
  display.setTextColor(BLACK);

  int textY = startY + 50; // Offset iniziale Y per la baseline del font 24pt
  int textX = marginX + 30;
  int maxWidth = display.width() - (marginX * 2) - 60; // Padding interno
  int lineHeight = 46; // Altezza di riga per il font 24pt

  // Pulisce la poesia da eventuali newline per far avvolgere tutto in
  // automatico
  String safePoem = currentPoem;
  safePoem.replace("\n", " ");

  String currentLine = "";
  int i = 0;
  int lineCount = 1; // Contatore rigoroso delle righe

  // Logica di word-wrapping con ritorno a capo automatico sulle virgole
  while (i < safePoem.length()) {
    int nextSpace = safePoem.indexOf(' ', i);
    if (nextSpace == -1)
      nextSpace = safePoem.length();

    String word = safePoem.substring(i, nextSpace);
    bool forceNewline = false;
    if (word.endsWith(",")) {
      forceNewline = true;
    }

    String testLine = currentLine;
    if (currentLine.length() > 0)
      testLine += " ";
    testLine += word;

    int16_t x1, y1;
    uint16_t w, hBounds;
    display.getTextBounds(testLine.c_str(), 0, 0, &x1, &y1, &w, &hBounds);

    if (w > maxWidth && currentLine.length() > 0) {
      if (textY > display.height() - 60 || lineCount >= 5) {
        // Ultima riga disponibile, tronca e aggiungi i puntini
        currentLine += "...";
        display.setCursor(textX, textY);
        display.print(currentLine);
        currentLine = ""; // Svuotiamo per non stamparlo di nuovo
        break;
      }

      // Se la riga sfora, stampa la riga corrente e vai a capo
      display.setCursor(textX, textY);
      display.print(currentLine);

      currentLine = word;
      textY += lineHeight;
      lineCount++;

      if (forceNewline) {
        // Se questa parola ha una virgola, la stampiamo da sola e forziamo a
        // capo di nuovo
        if (textY > display.height() - 60 || lineCount >= 5) {
          currentLine += "...";
          display.setCursor(textX, textY);
          display.print(currentLine);
          currentLine = "";
          break;
        }
        display.setCursor(textX, textY);
        display.print(currentLine);
        currentLine = "";
        textY += lineHeight;
        lineCount++;
      }
    } else {
      // Altrimenti la includiamo nella riga
      currentLine = testLine;
      if (forceNewline) {
        // C'è una virgola, stampiamo questa riga e andiamo a capo
        if (textY > display.height() - 60 || lineCount >= 5) {
          currentLine += "...";
          display.setCursor(textX, textY);
          display.print(currentLine);
          currentLine = "";
          break;
        } else {
          display.setCursor(textX, textY);
          display.print(currentLine);
          currentLine = "";
          textY += lineHeight;
          lineCount++;
        }
      }
    }

    i = nextSpace + 1; // Salta lo spazio
  }

  // Stampa eventuali parole rimaste nell'ultima riga se c'e' ancora spazio
  if (currentLine.length() > 0 && textY <= display.height() - 40) {
    display.setCursor(textX, textY);
    display.print(currentLine);
  }

  // Disegna l'autore/mood in basso a destra con font più piccolo (12pt)
  if (currentMood != "") {
    display.setFont(&FreeSerifItalic12pt7b);
    display.setTextSize(1);

    int16_t bx, by;
    uint16_t bw, bh;
    String moodStr = "- " + currentMood;
    display.getTextBounds(moodStr.c_str(), 0, 0, &bx, &by, &bw, &bh);

    int startX = display.width() - marginX - bw - 20;
    if (startX < marginX)
      startX = marginX + 10;

    display.setCursor(startX, display.height() - 25);
    display.print(moodStr);
  }

  // Ripristina font di default per il resto del display
  display.setFont();
  display.partialUpdate();
}
