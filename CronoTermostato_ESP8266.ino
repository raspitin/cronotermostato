#include <LCDWIKI_GUI.h>

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <time.h>

// WiFi
const char* ssid = "FRITZ!Box 7530 AL 2.4";
const char* password = "39461458263071769651";

// Relè
const int relayPin = 0;
bool relayState = false;

// EEPROM
#define EEPROM_SIZE 512
#define SCHEDULES_ADDR 0

// Server
ESP8266WebServer server(80);

// Giorni della settimana
const char* giorniSettimana[] = {"Dom", "Lun", "Mar", "Mer", "Gio", "Ven", "Sab"};

// Struttura programmazione
struct Schedule {
  bool used;
  int dayOfWeek;
  int hour;
  int minute;
  bool turnOn;
};

#define NUM_SCHEDULES 5
Schedule schedules[NUM_SCHEDULES];

// ------------------- NTP -------------------
void initNTP() {
  configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
  Serial.println("Sincronizzazione NTP...");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\nOra sincronizzata!");
  Serial.println(ctime(&now));
}

tm getLocalTime() {
  time_t now = time(nullptr);
  return *localtime(&now);
}

// ------------------- EEPROM -------------------
void saveSchedulesToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(SCHEDULES_ADDR, schedules);
  EEPROM.commit();
  EEPROM.end();
  Serial.println("Programmazioni salvate in EEPROM.");
}

void loadSchedulesFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(SCHEDULES_ADDR, schedules);
  EEPROM.end();
  Serial.println("Programmazioni caricate da EEPROM.");
}

// ------------------- HTML -------------------
String createMainPage() {
  tm timeInfo = getLocalTime();
  time_t now = time(nullptr);

  String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <meta charset='utf-8'/>
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
      <title>Cronotermostato ESP8285</title>
      <style>
        body { font-family: Arial; margin: 0; padding: 0; background: #f0f0f0; }
        main { padding: 10px; max-width: 100%; overflow-x: auto; }
        section { background: #fff; margin: 10px 0; padding: 10px; border-radius: 5px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        table { width: 100%; border-collapse: collapse; font-size: 16px; min-width: 480px; }
        th, td { padding: 8px; text-align: center; border: 1px solid #ddd; }
        input, select, button { font-size: 16px; padding: 6px; margin: 2px; width: auto; }
        button { background: #333; color: white; border: none; border-radius: 4px; }
        @media screen and (max-width: 600px) {
          table { font-size: 14px; }
          input, select, button { font-size: 14px; }
        }
      </style>
    </head>
    <body>
      <h1 style="background:#333;color:#fff;padding:10px;">Cronotermostato ESP8285</h1>
      <main>
  )rawliteral";

  html += "<script>let now = new Date(" + String(now * 1000) + ");</script>";

  html += R"rawliteral(
    <section>
      <h2>Ora Corrente</h2>
      <p><strong>Data/ora:</strong> <span id="time"></span></p>
      <script>
        function pad(n) { return n < 10 ? "0" + n : n; }
        const giorni = ["Dom", "Lun", "Mar", "Mer", "Gio", "Ven", "Sab"];
        setInterval(() => {
          now.setSeconds(now.getSeconds() + 1);
          let s = giorni[now.getDay()] + " " +
                  pad(now.getHours()) + ":" +
                  pad(now.getMinutes()) + ":" +
                  pad(now.getSeconds());
          document.getElementById("time").textContent = s;
        }, 1000);
      </script>
    </section>
  )rawliteral";

  html += "<section><h2>Controllo Manuale</h2>";
  html += "<p>Relè: <strong>" + String(relayState ? "ACCESO" : "SPENTO") + "</strong></p>";
  html += "<form action='/' method='POST'>";
  html += "<button type='submit' name='relay' value='" + String(relayState ? "OFF" : "ON") + "'>";
  String testoBottone = relayState ? "Spegni" : "Accendi";
  html += testoBottone + "</button></form></section>";

  html += "<section><h2>Programmazioni</h2>";
  html += "<form action='/' method='POST'><table><tr><th>Attiva</th><th>Giorno</th><th>Ora</th><th>Min</th><th>Azione</th></tr>";

  for (int i = 0; i < NUM_SCHEDULES; i++) {
    html += "<tr>";
    html += "<td><input type='checkbox' name='used" + String(i) + "'";
    if (schedules[i].used) html += " checked";
    html += "></td>";

    html += "<td><select name='day" + String(i) + "'>";
    for (int g = 0; g < 7; g++) {
      html += "<option value='" + String(g) + "'";
      if (schedules[i].dayOfWeek == g) html += " selected";
      html += ">" + String(giorniSettimana[g]) + "</option>";
    }
    html += "</select></td>";

    html += "<td><input type='number' name='hour" + String(i) + "' min='0' max='23' value='" + schedules[i].hour + "'></td>";
    html += "<td><input type='number' name='minute" + String(i) + "' min='0' max='59' value='" + schedules[i].minute + "'></td>";
    html += "<td><select name='action" + String(i) + "'>";
    html += "<option value='on'" + String(schedules[i].turnOn ? " selected" : "") + ">Accendi</option>";
    html += "<option value='off'" + String(!schedules[i].turnOn ? " selected" : "") + ">Spegni</option>";
    html += "</select></td></tr>";
  }

  html += "</table><input type='submit' name='schedule' value='Aggiorna Programmazioni'></form></section>";
  html += "</main></body></html>";
  return html;
}

// ------------------- Web -------------------
void handleRoot() {
  server.send(200, "text/html", createMainPage());
}

void handlePost() {
  if (server.hasArg("relay")) {
    String cmd = server.arg("relay");
    relayState = (cmd == "ON");
    digitalWrite(relayPin, relayState ? LOW : HIGH);
    Serial.println(relayState ? "Relè acceso da web." : "Relè spento da web.");
  }

  if (server.hasArg("schedule")) {
    for (int i = 0; i < NUM_SCHEDULES; i++) {
      schedules[i].used = server.hasArg("used" + String(i));
      schedules[i].dayOfWeek = server.arg("day" + String(i)).toInt();
      schedules[i].hour = server.arg("hour" + String(i)).toInt();
      schedules[i].minute = server.arg("minute" + String(i)).toInt();
      schedules[i].turnOn = (server.arg("action" + String(i)) == "on");
    }
    saveSchedulesToEEPROM();
  }

  handleRoot();
}

// ------------------- Setup -------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH); // spento (attivo LOW)
  relayState = false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connessione WiFi");

  unsigned long startAttemptTime = millis();
  const unsigned long timeout = 10000;

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
    delay(500); Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connesso. IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi non disponibile, attivo modalità Access Point.");
    WiFi.softAP("ESP8285_AP");
    Serial.println("AP attivo. SSID: ESP8285_AP | IP: 192.168.4.1");
  }

  initNTP();
  loadSchedulesFromEEPROM();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/", HTTP_POST, handlePost);
  server.begin();
  Serial.println("Web server avviato.");
}

// ------------------- Loop -------------------
void loop() {
  server.handleClient();

  static int lastMinute = -1;
  tm now = getLocalTime();

  if (now.tm_min != lastMinute) {
    lastMinute = now.tm_min;
    Serial.printf("Ora: %02d:%02d - Giorno: %d\n", now.tm_hour, now.tm_min, now.tm_wday);
    for (int i = 0; i < NUM_SCHEDULES; i++) {
      if (schedules[i].used &&
          schedules[i].dayOfWeek == now.tm_wday &&
          schedules[i].hour == now.tm_hour &&
          schedules[i].minute == now.tm_min) {
        relayState = schedules[i].turnOn;
        digitalWrite(relayPin, relayState ? LOW : HIGH);
        Serial.printf("Schedule #%d => %s\n", i, relayState ? "ACCESO" : "SPENTO");
      }
    }
  }
}
