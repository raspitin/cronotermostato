/*
 * Cronotermostato ESP8266
 * Versione Rielaborata (v6)
 * - Con WiFiManager
 * - Con ottimizzazione EEPROM
 * - Con fix CONTENT_LENGTH_UNKNOWN
 * - Con interfaccia PICO.CSS
 * - Con controllo manuale via AJAX (Niente refresh pagina)
 */

// Core ESP
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <time.h>

// Librerie aggiunte per WiFiManager
#include <DNSServer.h>
#include <WiFiManager.h>

// Pin Relè (GPIO0 per modulo ESP-01S Relay)
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
// (Invariato)
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
// (Invariato)
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


// ------------------- Web -------------------

/*
 * =================================================================
 * FUNZIONE handleRoot() - VERSIONE AJAX
 * =================================================================
 * - Modificato l'articolo "Controllo Manuale" (niente form, ID aggiunti)
 * - Aggiunto script JS per AJAX/Fetch alla fine della pagina
 */
void handleRoot() {
  // 1. Dichiara che la lunghezza non è nota (forza il 'chunked transfer')
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  
  // 2. Invia l'header 200 OK e il Content-Type.
  server.send(200, "text/html", "");

  // 3. Invio Head e CSS (statico) - VERSIONE PICO.CSS
  server.sendContent(R"rawliteral(
    <!DOCTYPE html>
    <html data-theme="light">
    <head>
      <meta charset='utf-8'/>
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
      <title>Cronotermostato ESP8285</title>
      
      <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/@picocss/pico@2/css/pico.min.css"/>
      
      <style>
        main.container {
          max-width: 800px; 
          margin: 0 auto;
          padding: 1rem;
        }
        /* Stile per il bottone AJAX quando è disabilitato */
        #relay-toggle-btn:disabled {
          cursor: wait;
        }
      </style>
    </head>
    <body>
      <main class="container">
        <h1 style="text-align:center;">Cronotermostato ESP8285</h1>
  )rawliteral");

  // 4. Invio Ora Corrente (dinamico)
  time_t now = time(nullptr);
  server.sendContent("<script>let now = new Date(");
  server.sendContent(String(now * 1000));
  server.sendContent(");</script>");
  
  // Usiamo <article>
  server.sendContent(R"rawliteral(
    <article>
      <h2>Ora Corrente</h2>
      <p><strong>Data/ora:</strong> <span id="time"></span></p>
      <script>
        function pad(n) { return n < 10 ? "0" + n : n; }
        const giorni = ["Dom", "Lun", "Mar", "Mer", "Gio", "Ven", "Sab"];
        setInterval(() => {
          now.setSeconds(now.getSeconds() + 1);
          let s = 
                  giorni[now.getDay()] + " " +
                  pad(now.getHours()) + ":" +
                  pad(now.getMinutes()) + ":" +
                  pad(now.getSeconds());
          document.getElementById("time").textContent = s;
        }, 1000);
      </script>
    </article>
  )rawliteral");

  // 5. Invio Controllo Manuale (dinamico) - MODIFICATO PER AJAX
  // Usiamo <article>
  server.sendContent("<article><h2>Controllo Manuale</h2>");
  // Aggiunto ID "relay-status-text" per aggiornamento via JS
  server.sendContent("<p>Relè: <strong id=\"relay-status-text\">");
  server.sendContent(relayState ? "ACCESO" : "SPENTO");
  server.sendContent("</strong></p>");
  // Il form è stato rimosso.
  // Il bottone ora ha type="button", un ID e un attributo "data-value"
  server.sendContent("<button type=\"button\" id=\"relay-toggle-btn\" data-value=\""); 
  server.sendContent(relayState ? "OFF" : "ON");
  server.sendContent("\">");
  server.sendContent(relayState ? "Spegni" : "Accendi");
  server.sendContent("</button></article>"); // Chiusura <article>

  // 6. Invio Programmazioni (dinamico)
  // (Invariato, questo resta un form normale)
  server.sendContent("<article><h2>Programmazioni</h2>");
  server.sendContent("<form action='/' method='POST'><div class=\"overflow-auto\">");
  server.sendContent("<table><tr><th>Attiva</th><th>Giorno</th><th>Ora</th><th>Min</th><th>Azione</th></tr>");

  for (int i = 0; i < NUM_SCHEDULES; i++) {
    server.sendContent("<tr>");
    
    server.sendContent("<td><input type='checkbox' name='used");
    server.sendContent(String(i));
    server.sendContent("'");
    if (schedules[i].used) server.sendContent(" checked");
    server.sendContent("></td>");
    
    server.sendContent("<td><select name='day");
    server.sendContent(String(i));
    server.sendContent("'>");
    for (int g = 0; g < 7; g++) {
      server.sendContent("<option value='");
      server.sendContent(String(g));
      server.sendContent("'");
      if (schedules[i].dayOfWeek == g) server.sendContent(" selected");
      server.sendContent(">");
      server.sendContent(giorniSettimana[g]);
      server.sendContent("</option>");
    }
    server.sendContent("</select></td>");

    server.sendContent("<td><input type='number' name='hour");
    server.sendContent(String(i));
    server.sendContent("' min='0' max='23' value='");
    server.sendContent(String(schedules[i].hour));
    server.sendContent("'></td>");

    server.sendContent("<td><input type='number' name='minute");
    server.sendContent(String(i));
    server.sendContent("' min='0' max='59' value='");
    server.sendContent(String(schedules[i].minute));
    server.sendContent("'></td>");

    server.sendContent("<td><select name='action");
    server.sendContent(String(i));
    server.sendContent("'>");
    server.sendContent("<option value='on'");
    if (schedules[i].turnOn) server.sendContent(" selected");
    server.sendContent(">Accendi</option>");
    server.sendContent("<option value='off'");
    if (!schedules[i].turnOn) server.sendContent(" selected");
    server.sendContent(">Spegni</option>");
    server.sendContent("</select></td></tr>");
  }

  server.sendContent("</table></div><input type='submit' name='schedule' value='Aggiorna Programmazioni'></form></article>");

  // 7. Invio Chiusura Pagina E SCRIPT AJAX
  
  // --- INIZIO SCRIPT AJAX ---
  server.sendContent(R"rawliteral(
    <script>
      document.getElementById('relay-toggle-btn').addEventListener('click', function() {
        const btn = this;
        const statusText = document.getElementById('relay-status-text');
        const nextAction = btn.getAttribute('data-value'); // "ON" o "OFF"

        // Disabilita il bottone per evitare doppi click
        btn.disabled = true;
        btn.textContent = '...';

        // Prepara i dati da inviare (come se fosse un form)
        const formData = new FormData();
        formData.append('relay', nextAction);

        fetch('/relay', { // Invia al nuovo endpoint /relay
          method: 'POST',
          body: formData
        })
        .then(response => response.json())
        .then(data => {
          // 'data' è il JSON: {"relayState": true/false}
          if (data.relayState) {
            // È ACCESO
            statusText.textContent = 'ACCESO';
            btn.textContent = 'Spegni';
            btn.setAttribute('data-value', 'OFF');
          } else {
            // È SPENTO
            statusText.textContent = 'SPENTO';
            btn.textContent = 'Accendi';
            btn.setAttribute('data-value', 'ON');
          }
          // Riabilita il bottone
          btn.disabled = false;
        })
        .catch(error => {
          console.error('Errore:', error);
          statusText.textContent = 'ERRORE';
          btn.disabled = false;
        });
      });
    </script>
  )rawliteral");
  // --- FINE SCRIPT AJAX ---

  server.sendContent("</main></body></html>");
}

/*
 * =================================================================
 * FUNZIONE handlePost() - MODIFICATA
 * =================================================================
 * Ora gestisce SOLO le programmazioni.
 * Il controllo relè è stato spostato in handleRelay()
 */
void handlePost() {
  
  // Il blocco 'if (server.hasArg("relay"))' è stato RIMOSSO

  if (server.hasArg("schedule")) {
    bool changed = false; 
    
    for (int i = 0; i < NUM_SCHEDULES; i++) {
      bool newUsed = server.hasArg("used" + String(i));
      int newDay = server.arg("day" + String(i)).toInt();
      int newHour = server.arg("hour" + String(i)).toInt();
      int newMin = server.arg("minute" + String(i)).toInt();
      bool newAction = (server.arg("action" + String(i)) == "on");

      if (schedules[i].used != newUsed ||
          schedules[i].dayOfWeek != newDay ||
          schedules[i].hour != newHour ||
          schedules[i].minute != newMin ||
          schedules[i].turnOn != newAction) 
      {
        changed = true;
        schedules[i].used = newUsed;
        schedules[i].dayOfWeek = newDay;
        schedules[i].hour = newHour;
        schedules[i].minute = newMin;
        schedules[i].turnOn = newAction;
      }
    }
    
    if (changed) {
      saveSchedulesToEEPROM();
    } else {
      Serial.println("Programmazioni non modificate, EEPROM non scritta.");
    }
  }

  // Ricarica la pagina per mostrare le programmazioni aggiornate
  handleRoot();
}

/*
 * =================================================================
 * FUNZIONE handleRelay() - NUOVA
 * =================================================================
 * Gestisce la chiamata AJAX dal pulsante di controllo manuale
 * e risponde solo con un JSON.
 */
void handleRelay() {
  if (server.hasArg("relay")) {
    String cmd = server.arg("relay");
    relayState = (cmd == "ON");
    digitalWrite(relayPin, relayState ? LOW : HIGH);
    Serial.println(relayState ? "Relè acceso da web (AJAX)." : "Relè spento da web (AJAX).");
  }
  
  // Rispondi con il nuovo stato in JSON
  server.sendHeader("Cache-Control", "no-cache"); // Evita che il browser memorizzi lo stato
  String json = "{\"relayState\":" + String(relayState ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}


// ------------------- Setup -------------------
// MODIFICATO
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH);
  relayState = false;

  WiFiManager wifiManager;
  wifiManager.setConnectTimeout(10); 
  Serial.println("Avvio portale di configurazione WiFi...");
  
  if (!wifiManager.autoConnect("CronoTermostato_AP")) {
    Serial.println("Timeout connessione. Riavvio...");
    delay(3000);
    ESP.restart();
  }
  
  Serial.println("\nWiFi connesso. IP: " + WiFi.localIP().toString());

  initNTP();
  loadSchedulesFromEEPROM(); 

  server.on("/", HTTP_GET, handleRoot);
  server.on("/", HTTP_POST, handlePost); // Gestisce SOLO le programmazioni
  server.on("/relay", HTTP_POST, handleRelay); // <-- NUOVO ENDPOINT PER AJAX
  
  server.begin();
  Serial.println("Web server avviato.");
}

// ------------------- Loop -------------------
// (Invariato)
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
        // Non sovrascrive lo stato se l'utente lo ha appena cambiato manualmente?
        // Questa è una logica da rivedere, ma per ora la lasciamo così.
        relayState = schedules[i].turnOn;
        digitalWrite(relayPin, relayState ? LOW : HIGH);
        Serial.printf("Schedule #%d => %s\n", i, relayState ? "ACCESO" : "SPENTO");
      }
    }
  }
}
