#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// --- AP Credentials ---
const char* ssid = "ESP32_Hotspot";
const char* password = "12345678";

// --- Pin Definitions ---
#define DHTPIN 4            // DHT11 Data pin
#define DHTTYPE DHT11       // DHT 11
#define MOISTURE_PIN 35     // Analog pin for Soil Moisture
#define RELAY_PIN 26        // Pin for the Relay Module
#define PUMP_BUTTON_PIN 27  // Physical pump button: connect between GPIO27 and GND
#define MODE_BUTTON_PIN 14  // Physical mode button: connect between GPIO14 and GND
#define START_INC_BUTTON_PIN 25 // Increase automatic ON threshold by 10%
#define STOP_DEC_BUTTON_PIN 33  // Decrease automatic OFF threshold by 10%
#define SIM800_RX_PIN 16    // ESP32 RX2
#define SIM800_TX_PIN 17    // ESP32 TX2

// --- OLED Display ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Objects ---
DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);
HardwareSerial sim800l(2); // Use Hardware Serial 2 for the SIM800L

// --- Global Variables ---
float temperature = 0.0;
float humidity = 0.0;
int soilMoistureRaw = 0;
int soilMoisturePercent = 0;
bool pumpIsOn = false;      // Tracks the logical state of the pump

// --- SIM800L Variables ---
String networkName = "Wait..."; // Stores "Kolkata" etc.
String signalStrength = "0";    // Stores "14" etc.
String callerNumber = "";       // Last incoming caller number

// --- Call password security ---
const String CALL_PASSWORD = "2580";   // Change this password
const int MAX_PASSWORD_ATTEMPTS = 3;
const unsigned long CALL_AUTH_TIMEOUT = 30000UL;
String enteredCallPassword = "";
bool callActive = false;
bool callAuthenticated = false;
int failedPasswordAttempts = 0;
unsigned long callStartMillis = 0;

// --- Settings Variables ---
bool isAutoMode = true;           // Default to Auto mode
int moistureStartThreshold = 10;  // Turn ON when moisture drops to this (%)
int moistureStopThreshold = 40;   // Turn OFF when moisture hits this (%)

unsigned long previousMillis = 0;
unsigned long previousSimMillis = 0;
const long sensorInterval = 2000; // Update sensors every 2 seconds
const long simInterval = 10000;   // Check signal/network every 10 seconds

// Physical button debounce
int lastButtonReading = HIGH;
int stableButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// Physical mode button debounce
int lastModeButtonReading = HIGH;
int stableModeButtonState = HIGH;
unsigned long lastModeDebounceTime = 0;

// Physical threshold button debounce
int lastStartIncReading = HIGH;
int stableStartIncState = HIGH;
unsigned long lastStartIncDebounceTime = 0;
int lastStopDecReading = HIGH;
int stableStopDecState = HIGH;
unsigned long lastStopDecDebounceTime = 0;

void updateOLED();
void handlePhysicalButton();
void handleModeButton();
void handleThresholdButtons();
String sendATCommand(String command, const int timeout);
void sendSensorSMS(const String &recipient);
char extractDTMFDigit(const String &response);
void processCallDTMF(char keyPressed);
void hangUpCall();
void resetCallSecurity();

// ==========================================
// HTML & CSS FOR THE WEBSITE
// ==========================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Smart Plant Monitor</title>
  <style>
    :root {
      --primary: #8bc34a;       
      --bg: #0a140a;            
      --card-bg: #162c15;       
      --text: #e8f5e9;     
    }
    body {
      font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
      background-color: var(--bg);
      color: var(--text);
      text-align: center;
      margin: 0;
      padding: 20px;
    }
    h1 {
      color: var(--primary);
      margin-bottom: 30px;
      font-weight: 600;
    }
    .grid {
      display: flex;
      flex-wrap: wrap;
      justify-content: center;
      gap: 20px;
      max-width: 900px;
      margin: 0 auto;
    }
    .card {
      background: var(--card-bg);
      padding: 30px 20px;
      border-radius: 20px;
      box-shadow: 0 10px 20px rgba(0, 0, 0, 0.4);
      flex: 1 1 200px;
      transition: transform 0.2s ease;
      border: 1px solid #2b5329;
      display: flex;
      flex-direction: column;
      align-items: center;
    }
    .label {
      font-size: 1.1rem;
      color: #8da48a; 
      text-transform: uppercase;
      letter-spacing: 1px;
      margin-top: 15px;
    }
    
    /* Circular Progress */
    .circular-chart { display: block; margin: 0 auto; max-width: 140px; max-height: 140px; }
    .circle-bg { fill: none; stroke: #0a140a; stroke-width: 3.8; }
    .circle { fill: none; stroke-width: 2.8; stroke-linecap: round; transition: stroke-dasharray 0.8s ease-out; }
    .temp-circle { stroke: #8bc34a; } 
    .hum-circle { stroke: #5db038; }  
    .moist-circle { stroke: #aed581; }
    .percentage-text { fill: var(--text); font-family: 'Segoe UI', sans-serif; font-size: 0.5em; font-weight: bold; text-anchor: middle; dominant-baseline: central; }

    /* Control Panel */
    .control-panel {
      flex-basis: 100%;
      background: var(--card-bg);
      padding: 20px;
      border-radius: 20px;
      box-shadow: 0 10px 20px rgba(0, 0, 0, 0.4);
      margin-top: 10px;
      border: 1px solid #2b5329;
    }
    
    .settings-panel {
      background: #112211;
      padding: 20px 15px;
      border-radius: 10px;
      margin-top: 20px;
      border: 1px solid #1a3319;
    }

    /* Pump Button */
    button {
      background-color: #3a7d22;
      color: var(--text);
      border: none;
      padding: 15px 40px;
      font-size: 1.2rem;
      font-weight: bold;
      border-radius: 10px;
      cursor: pointer;
      transition: all 0.3s ease;
      box-shadow: 0 4px 6px rgba(0,0,0,0.3);
      width: 100%;
      max-width: 300px;
    }
    button:hover { background-color: #5db038; }
    button:active { transform: scale(0.98); }
    button.pump-on { background-color: #1a3319; border: 1px solid #e74c3c; color: #e74c3c; }
    button.disabled { opacity: 0.5; cursor: not-allowed; }

    /* Custom Toggle Switch */
    .switch { position: relative; display: inline-block; width: 60px; height: 34px; margin: 0 15px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #1a3319; transition: .4s; border-radius: 34px; border: 1px solid #2b5329; }
    .slider:before { position: absolute; content: ""; height: 26px; width: 26px; left: 3px; bottom: 3px; background-color: #8da48a; transition: .4s; border-radius: 50%; }
    input:checked + .slider { background-color: #3a7d22; }
    input:checked + .slider:before { transform: translateX(26px); background-color: white; }
    .mode-label { vertical-align: super; font-weight: bold; font-size: 1.1rem; transition: color 0.3s;}

    /* Sliders */
    input[type=range] { -webkit-appearance: none; width: 100%; background: transparent; }
    input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; height: 20px; width: 20px; border-radius: 50%; background: #8bc34a; cursor: pointer; margin-top: -8px; }
    input[type=range]::-webkit-slider-runnable-track { width: 100%; height: 5px; cursor: pointer; background: #2b5329; border-radius: 5px; }
  </style>
</head>
<body>
  <div style="display:flex; justify-content:flex-end; max-width:900px; margin:0 auto 10px;">
    <select id="languageSelect" onchange="changeLanguage()" style="padding:8px 12px; border-radius:8px; background:#162c15; color:#e8f5e9; border:1px solid #2b5329;">
      <option value="en">English</option>
      <option value="hi">हिन्दी</option>
      <option value="bn">বাংলা</option>
    </select>
  </div>
  <h1 data-i18n="title">Smart Plant Monitor</h1>
  <div class="grid">
    
    <div class="card">
      <svg class="circular-chart" viewBox="0 0 36 36">
        <path class="circle-bg" d="M18 2.0845 a 15.9155 15.9155 0 0 1 0 31.831 a 15.9155 15.9155 0 0 1 0 -31.831"/>
        <path class="circle temp-circle" id="temp-ring" stroke-dasharray="0, 100" d="M18 2.0845 a 15.9155 15.9155 0 0 1 0 31.831 a 15.9155 15.9155 0 0 1 0 -31.831"/>
        <text x="18" y="18" class="percentage-text" id="temp-val">--°C</text>
      </svg>
      <div class="label" data-i18n="temperature">Temperature</div>
    </div>

    <div class="card">
      <svg class="circular-chart" viewBox="0 0 36 36">
        <path class="circle-bg" d="M18 2.0845 a 15.9155 15.9155 0 0 1 0 31.831 a 15.9155 15.9155 0 0 1 0 -31.831"/>
        <path class="circle hum-circle" id="hum-ring" stroke-dasharray="0, 100" d="M18 2.0845 a 15.9155 15.9155 0 0 1 0 31.831 a 15.9155 15.9155 0 0 1 0 -31.831"/>
        <text x="18" y="18" class="percentage-text" id="hum-val">--%</text>
      </svg>
      <div class="label" data-i18n="humidity">Humidity</div>
    </div>

    <div class="card">
      <svg class="circular-chart" viewBox="0 0 36 36">
        <path class="circle-bg" d="M18 2.0845 a 15.9155 15.9155 0 0 1 0 31.831 a 15.9155 15.9155 0 0 1 0 -31.831"/>
        <path class="circle moist-circle" id="moist-ring" stroke-dasharray="0, 100" d="M18 2.0845 a 15.9155 15.9155 0 0 1 0 31.831 a 15.9155 15.9155 0 0 1 0 -31.831"/>
        <text x="18" y="18" class="percentage-text" id="moist-val">--%</text>
      </svg>
      <div class="label" data-i18n="soilMoisture">Soil Moisture</div>
    </div>
    
    <div class="control-panel">
      <div class="label" data-i18n="pumpControl" style="margin-bottom: 15px; margin-top: 0;">Water Pump Control</div>
      <button id="pumpBtn" onclick="togglePump()"><span data-i18n="turnPumpOn">Turn Pump ON</span></button>
      <div id="status-msg" style="color: #e74c3c; margin-top: 10px; font-size: 0.95rem; font-weight: bold;"></div>
      
      <div class="settings-panel">
        
        <div style="display: flex; justify-content: center; align-items: center; margin-bottom: 25px;">
          <span class="mode-label" id="lblManual" style="color: #657e63;" data-i18n="manual">Manual</span>
          <label class="switch">
            <input type="checkbox" id="modeToggle" onchange="sendSettings()" checked>
            <span class="slider"></span>
          </label>
          <span class="mode-label" id="lblAuto" style="color: #8bc34a;" data-i18n="autoMode">Auto Mode</span>
        </div>

        <div id="thresholdDiv">
          <label style="color: var(--primary); font-size: 0.95rem;"><span data-i18n="startText">Turn ON when moisture drops to:</span> <span id="threshStartDisplay" style="font-weight: bold; color: white;">10</span>%</label><br>
          <input type="range" id="threshStartSlider" min="0" max="95" value="10" style="width: 85%; margin-bottom: 20px; margin-top: 10px;" onchange="sendSettings()" oninput="document.getElementById('threshStartDisplay').innerText=this.value">
          <br>
          <label style="color: var(--primary); font-size: 0.95rem;"><span data-i18n="stopText">Turn OFF when moisture hits:</span> <span id="threshStopDisplay" style="font-weight: bold; color: white;">40</span>%</label><br>
          <input type="range" id="threshStopSlider" min="5" max="100" value="40" style="width: 85%; margin-top: 10px;" onchange="sendSettings()" oninput="document.getElementById('threshStopDisplay').innerText=this.value">
        </div>
      </div>
    </div>
  </div>

  <script>
    const translations = {
      en: {title:"Smart Plant Monitor", temperature:"Temperature", humidity:"Humidity", soilMoisture:"Soil Moisture", pumpControl:"Water Pump Control", turnPumpOn:"Turn Pump ON", turnPumpOff:"Turn Pump OFF", manual:"Manual", autoMode:"Auto Mode", startText:"Turn ON when moisture drops to:", stopText:"Turn OFF when moisture hits:", manualWarning:"Switch to Manual mode to use this button."},
      hi: {title:"स्मार्ट प्लांट मॉनिटर", temperature:"तापमान", humidity:"नमी", soilMoisture:"मिट्टी की नमी", pumpControl:"पानी पंप नियंत्रण", turnPumpOn:"पंप चालू करें", turnPumpOff:"पंप बंद करें", manual:"मैनुअल", autoMode:"ऑटो मोड", startText:"नमी कम होने पर चालू करें:", stopText:"नमी पहुँचने पर बंद करें:", manualWarning:"इस बटन का उपयोग करने के लिए मैनुअल मोड चुनें।"},
      bn: {title:"স্মার্ট প্ল্যান্ট মনিটর", temperature:"তাপমাত্রা", humidity:"আর্দ্রতা", soilMoisture:"মাটির আর্দ্রতা", pumpControl:"জল পাম্প নিয়ন্ত্রণ", turnPumpOn:"পাম্প চালু করুন", turnPumpOff:"পাম্প বন্ধ করুন", manual:"ম্যানুয়াল", autoMode:"অটো মোড", startText:"আর্দ্রতা কমে গেলে চালু করুন:", stopText:"আর্দ্রতা পৌঁছালে বন্ধ করুন:", manualWarning:"এই বোতাম ব্যবহার করতে ম্যানুয়াল মোড নির্বাচন করুন।"}
    };

    function changeLanguage() {
      const lang = document.getElementById('languageSelect').value;
      localStorage.setItem('language', lang);
      document.querySelectorAll('[data-i18n]').forEach(el => {
        const key = el.getAttribute('data-i18n');
        if (translations[lang][key]) el.textContent = translations[lang][key];
      });
      syncUIState(document.getElementById('modeToggle').checked, document.getElementById('pumpBtn').classList.contains('pump-on'));
    }

    function loadLanguage() {
      const lang = localStorage.getItem('language') || 'en';
      document.getElementById('languageSelect').value = lang;
      changeLanguage();
    }

    function syncUIState(isAuto, pumpOn) {
      const btn = document.getElementById('pumpBtn');
      
      // Update Button state
      if(pumpOn) {
        btn.innerText = '<span data-i18n="turnPumpOff">Turn Pump OFF</span>';
        btn.classList.add('pump-on');
      } else {
        btn.innerText = '<span data-i18n="turnPumpOn">Turn Pump ON</span>';
        btn.classList.remove('pump-on');
      }

      // Update Auto/Manual UI state
      if (isAuto) {
        btn.classList.add('disabled');
        document.getElementById('thresholdDiv').style.opacity = '1';
        document.getElementById('thresholdDiv').style.pointerEvents = 'auto';
        document.getElementById('lblManual').style.color = '#657e63';
        document.getElementById('lblAuto').style.color = '#8bc34a';
      } else {
        btn.classList.remove('disabled');
        document.getElementById('thresholdDiv').style.opacity = '0.3';
        document.getElementById('thresholdDiv').style.pointerEvents = 'none';
        document.getElementById('lblManual').style.color = '#8bc34a';
        document.getElementById('lblAuto').style.color = '#657e63';
      }
    }

    // Toggle the pump via API
    function togglePump() {
      fetch('/toggle')
        .then(response => response.text())
        .then(state => {
          if (state === 'blocked_auto') {
            document.getElementById('status-msg').innerText = translations[document.getElementById('languageSelect').value].manualWarning;
            setTimeout(() => { document.getElementById('status-msg').innerText = ''; }, 3000);
          } else {
            document.getElementById('status-msg').innerText = '';
            syncUIState(document.getElementById('modeToggle').checked, state === '1');
          }
        })
        .catch(err => console.error(err));
    }

    // Send new settings to ESP32
    function sendSettings() {
      const mode = document.getElementById('modeToggle').checked ? 1 : 0;
      let start = parseInt(document.getElementById('threshStartSlider').value);
      let stop = parseInt(document.getElementById('threshStopSlider').value);
      
      // Prevent user from setting START higher than STOP
      if (start >= stop) {
         stop = start + 5;
         if (stop > 100) stop = 100;
         document.getElementById('threshStopSlider').value = stop;
         document.getElementById('threshStopDisplay').innerText = stop;
      }

      syncUIState(mode, document.getElementById('pumpBtn').classList.contains('pump-on'));
      document.getElementById('status-msg').innerText = ''; // Clear warnings

      fetch(`/settings?mode=${mode}&start=${start}&stop=${stop}`);
    }

    loadLanguage();

    // Automatically fetch new data every 2 seconds
    setInterval(function() {
      fetch('/data')
        .then(response => response.json())
        .then(data => {
          // Update Rings
          let tempPercent = Math.min((data.temperature / 50) * 100, 100); 
          document.getElementById('temp-ring').setAttribute('stroke-dasharray', `${tempPercent}, 100`);
          document.getElementById('temp-val').textContent = data.temperature.toFixed(1) + '°C';

          document.getElementById('hum-ring').setAttribute('stroke-dasharray', `${data.humidity}, 100`);
          document.getElementById('hum-val').textContent = data.humidity.toFixed(1) + '%';

          document.getElementById('moist-ring').setAttribute('stroke-dasharray', `${data.moisture}, 100`);
          document.getElementById('moist-val').textContent = data.moisture + '%';

          const isAuto = (data.mode === 1);
          syncUIState(isAuto, data.pump === 1);

          // Sync Settings Sliders (Only if user isn't actively sliding them)
          if(document.activeElement !== document.getElementById('threshStartSlider') && 
             document.activeElement !== document.getElementById('threshStopSlider')) {
            
            document.getElementById('modeToggle').checked = isAuto;
            document.getElementById('threshStartSlider').value = data.start;
            document.getElementById('threshStartDisplay').innerText = data.start;
            document.getElementById('threshStopSlider').value = data.stop;
            document.getElementById('threshStopDisplay').innerText = data.stop;
          }
        })
        .catch(err => console.error(err));
    }, 2000);
  </script>
</body>
</html>
)rawliteral";

// --- Web Server Routing Functions ---
void handleRoot() {
    server.send(200, "text/html", index_html);
}

void handleData() {
    // Create a JSON string with the latest sensor, relay, and settings data
    String json = "{";
    json += "\"temperature\":" + String(temperature) + ",";
    json += "\"humidity\":" + String(humidity) + ",";
    json += "\"moisture\":" + String(soilMoisturePercent) + ",";
    json += "\"pump\":" + String(pumpIsOn ? 1 : 0) + ",";
    json += "\"mode\":" + String(isAutoMode ? 1 : 0) + ",";
    json += "\"start\":" + String(moistureStartThreshold) + ",";
    json += "\"stop\":" + String(moistureStopThreshold);
    json += "}";
    server.send(200, "application/json", json);
}

void handleSettings() {
    // Check incoming parameters from Web UI and update globals
    if (server.hasArg("mode")) {
        isAutoMode = (server.arg("mode") == "1");
    }
    if (server.hasArg("start")) {
        moistureStartThreshold = server.arg("start").toInt();
    }
    if (server.hasArg("stop")) {
        moistureStopThreshold = server.arg("stop").toInt();
    }
    server.send(200, "text/plain", "OK");
    updateOLED(); // Update screen immediately when settings change
}

void handleToggle() {
    // If we are in AUTO mode, prevent the manual button from working
    if (isAutoMode) {
        server.send(200, "text/plain", "blocked_auto");
        return;
    }

    pumpIsOn = !pumpIsOn;  // Toggle logical state
    digitalWrite(RELAY_PIN, pumpIsOn ? LOW : HIGH); // Active Low
    server.send(200, "text/plain", String(pumpIsOn ? 1 : 0));
    updateOLED();
}

void setup() {
    Serial.begin(115200);
    
    // Initialize GSM
    sim800l.begin(9600, SERIAL_8N1, SIM800_RX_PIN, SIM800_TX_PIN);

    // 1. Initialize Relay
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH); // Active Low - start OFF

    pinMode(PUMP_BUTTON_PIN, INPUT_PULLUP); // Button pressed = LOW
    pinMode(MODE_BUTTON_PIN, INPUT_PULLUP); // Mode button pressed = LOW
    pinMode(START_INC_BUTTON_PIN, INPUT_PULLUP); // Increase START threshold
    pinMode(STOP_DEC_BUTTON_PIN, INPUT_PULLUP);  // Decrease STOP threshold

    // 2. Initialize DHT
    dht.begin();
    
    // 3. Initialize OLED
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;);
    }
    
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.println("Initializing GSM...");
    display.display();
    
    // Give SIM800L time to boot up and lock onto BSNL
    delay(5000); 

    // Setup SIM800L Call Features
    sendATCommand("ATS0=1", 1000);   // Auto-answer after 1 ring
    sendATCommand("AT+DDET=1", 1000); // Enable DTMF Decoder
    sendATCommand("AT+CLIP=1", 1000);  // Enable caller-ID reporting
    
    display.clearDisplay();
    display.setCursor(0, 20);
    display.println("Starting System...");
    display.display();
    delay(1000);
    
    // 4. Start WiFi Access Point
    Serial.println("Configuring Access Point...");
    WiFi.softAP(ssid, password);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("Hotspot started! IP Address: ");
    Serial.println(IP);

    // 5. Start Web Server
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.on("/toggle", handleToggle);
    server.on("/settings", handleSettings);
    server.begin();
    Serial.println("HTTP server started");
}

void loop() {
    handlePhysicalButton();
    handleModeButton();
    handleThresholdButtons();
    server.handleClient();

    // Read every available SIM800L line, not just one line per loop.
    while (sim800l.available()) {
        String response = sim800l.readStringUntil('\n');
        response.trim();
        if (response.length() == 0) continue;

        Serial.print("SIM800L: ");
        Serial.println(response);

        if (response.startsWith("+CLIP:")) {
            int firstQuote = response.indexOf('"');
            int secondQuote = response.indexOf('"', firstQuote + 1);
            if (firstQuote >= 0 && secondQuote > firstQuote) {
                callerNumber = response.substring(firstQuote + 1, secondQuote);
                callActive = true;
                callAuthenticated = false;
                enteredCallPassword = "";
                failedPasswordAttempts = 0;
                callStartMillis = millis();
                Serial.println("Incoming call. Enter password.");
            }
        }

        if (response.startsWith("+DTMF:")) {
            char keyPressed = extractDTMFDigit(response);
            if (keyPressed >= '0' && keyPressed <= '9') {
                Serial.print("DTMF PRESSED: ");
                Serial.println(keyPressed);
                processCallDTMF(keyPressed);
            }
        }

        if (response.indexOf("NO CARRIER") >= 0 ||
            response.indexOf("BUSY") >= 0 ||
            response.indexOf("NO ANSWER") >= 0) {
            resetCallSecurity();
        }
    }

    if (callActive && !callAuthenticated &&
        millis() - callStartMillis >= CALL_AUTH_TIMEOUT) {
        Serial.println("Password timeout. Hanging up.");
        hangUpCall();
    }

    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= sensorInterval) {
        previousMillis = currentMillis;
        humidity = dht.readHumidity();
        temperature = dht.readTemperature();
        soilMoistureRaw = analogRead(MOISTURE_PIN);
        soilMoisturePercent = map(soilMoistureRaw, 4095, 1500, 0, 100);
        soilMoisturePercent = constrain(soilMoisturePercent, 0, 100);

        if (isnan(humidity) || isnan(temperature)) {
            humidity = 0;
            temperature = 0;
            Serial.println("Failed to read from DHT sensor!");
        }

        if (isAutoMode) {
            if (!pumpIsOn && soilMoisturePercent <= moistureStartThreshold) {
                pumpIsOn = true;
                digitalWrite(RELAY_PIN, LOW);
                Serial.println("Moisture below START threshold. Pump ON.");
            } else if (pumpIsOn && soilMoisturePercent >= moistureStopThreshold) {
                pumpIsOn = false;
                digitalWrite(RELAY_PIN, HIGH);
                Serial.println("Moisture reached STOP threshold. Pump OFF.");
            }
        }

        updateOLED();
        Serial.printf("Temp: %.1fC | Hum: %.1f%% | Moist: %d%% | Pump: %s | Mode: %s | Start: %d%% | Stop: %d%%\n",
                      temperature, humidity, soilMoisturePercent,
                      pumpIsOn ? "ON" : "OFF", isAutoMode ? "AUTO" : "MANUAL",
                      moistureStartThreshold, moistureStopThreshold);
    }

    if (currentMillis - previousSimMillis >= simInterval) {
        previousSimMillis = currentMillis;
        String csqResponse = sendATCommand("AT+CSQ", 1000);
        int csqIndex = csqResponse.indexOf("+CSQ: ");
        if (csqIndex != -1) {
            int commaIndex = csqResponse.indexOf(",", csqIndex);
            if (commaIndex > csqIndex) signalStrength = csqResponse.substring(csqIndex + 6, commaIndex);
        }

        String copsResponse = sendATCommand("AT+COPS?", 2000);
        int quoteStart = copsResponse.indexOf('"');
        int quoteEnd = copsResponse.lastIndexOf('"');
        if (quoteStart != -1 && quoteEnd > quoteStart) networkName = copsResponse.substring(quoteStart + 1, quoteEnd);
        else networkName = "Searching...";
        updateOLED();
    }
}

char extractDTMFDigit(const String &response) {
    int colon = response.indexOf(':');
    if (colon < 0) return '\0';
    for (int i = colon + 1; i < response.length(); i++) {
        char c = response.charAt(i);
        if (c >= '0' && c <= '9') return c;
    }
    return '\0';
}

void resetCallSecurity() {
    callActive = false;
    callAuthenticated = false;
    enteredCallPassword = "";
    failedPasswordAttempts = 0;
    callStartMillis = 0;
}

void hangUpCall() {
    Serial.println("Hanging up call...");
    sim800l.println("ATH");
    delay(500);
    resetCallSecurity();
}

void processCallDTMF(char keyPressed) {
    if (!callActive) {
        Serial.println("Ignoring DTMF: no active call.");
        return;
    }

    if (!callAuthenticated) {
        enteredCallPassword += keyPressed;
        Serial.print("Password digits received: ");
        Serial.println(enteredCallPassword.length());

        if (enteredCallPassword.length() == CALL_PASSWORD.length()) {
            if (enteredCallPassword == CALL_PASSWORD) {
                callAuthenticated = true;
                enteredCallPassword = "";
                failedPasswordAttempts = 0;
                Serial.println("Password correct. Remote control unlocked.");
            } else {
                failedPasswordAttempts++;
                enteredCallPassword = "";
                Serial.print("Wrong password attempt: ");
                Serial.println(failedPasswordAttempts);

                if (failedPasswordAttempts >= MAX_PASSWORD_ATTEMPTS) {
                    Serial.println("Maximum failed attempts reached. Disconnecting.");
                    delay(300);
                    hangUpCall();
                }
            }
        }
        return;
    }

    if (keyPressed == '1') {
        isAutoMode = false;
        pumpIsOn = true;
        digitalWrite(RELAY_PIN, LOW);
        Serial.println("Authenticated call: Pump ON, Manual mode.");
    } else if (keyPressed == '2') {
        isAutoMode = false;
        pumpIsOn = false;
        digitalWrite(RELAY_PIN, HIGH);
        Serial.println("Authenticated call: Pump OFF, Manual mode.");
    } else if (keyPressed == '3') {
        isAutoMode = !isAutoMode;
        Serial.println(isAutoMode ? "Authenticated call: AUTO mode." : "Authenticated call: MANUAL mode.");
    } else if (keyPressed == '4') {
        if (callerNumber.length() > 0) sendSensorSMS(callerNumber);
    } else {
        Serial.println("Unknown authenticated command.");
    }
    updateOLED();
}

// --- Helper Functions ---
void handlePhysicalButton() {
    int reading = digitalRead(PUMP_BUTTON_PIN);

    if (reading != lastButtonReading) {
        lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > debounceDelay) {
        if (reading != stableButtonState) {
            stableButtonState = reading;

            // Act only when the button is pressed
            if (stableButtonState == LOW) {
                // A physical press puts the system into Manual mode
                isAutoMode = false;
                pumpIsOn = !pumpIsOn;
                digitalWrite(RELAY_PIN, pumpIsOn ? LOW : HIGH); // Active Low
                Serial.println(pumpIsOn ? "Physical button: PUMP ON" : "Physical button: PUMP OFF");
                updateOLED();
            }
        }
    }

    lastButtonReading = reading;
}

void handleModeButton() {
    int reading = digitalRead(MODE_BUTTON_PIN);

    if (reading != lastModeButtonReading) {
        lastModeDebounceTime = millis();
    }

    if ((millis() - lastModeDebounceTime) > debounceDelay) {
        if (reading != stableModeButtonState) {
            stableModeButtonState = reading;

            // Act only when the mode button is pressed
            if (stableModeButtonState == LOW) {
                isAutoMode = !isAutoMode;

                // When switching to Auto, let the sensor logic decide the pump state
                if (isAutoMode) {
                    Serial.println("Physical mode button: AUTO MODE");
                } else {
                    Serial.println("Physical mode button: MANUAL MODE");
                }

                updateOLED();
            }
        }
    }

    lastModeButtonReading = reading;
}


void handleThresholdButtons() {
    unsigned long now = millis();

    // Increase automatic pump-ON threshold by exactly 10% per press.
    int startReading = digitalRead(START_INC_BUTTON_PIN);
    if (startReading != lastStartIncReading) {
        lastStartIncDebounceTime = now;
    }
    if ((now - lastStartIncDebounceTime) > debounceDelay) {
        if (startReading != stableStartIncState) {
            stableStartIncState = startReading;
            if (stableStartIncState == LOW) {
                moistureStartThreshold += 10;
                // After reaching 100%, the next press wraps back to 20%.
                if (moistureStartThreshold > 100) {
                    moistureStartThreshold = 20;
                }

                Serial.print("START threshold increased to: ");
                Serial.print(moistureStartThreshold);
                Serial.println("%");
                updateOLED();
            }
        }
    }
    lastStartIncReading = startReading;

    // Decrease automatic pump-OFF threshold by exactly 10% per press.
    int stopReading = digitalRead(STOP_DEC_BUTTON_PIN);
    if (stopReading != lastStopDecReading) {
        lastStopDecDebounceTime = now;
    }
    if ((now - lastStopDecDebounceTime) > debounceDelay) {
        if (stopReading != stableStopDecState) {
            stableStopDecState = stopReading;
            if (stableStopDecState == LOW) {
                moistureStopThreshold -= 10;
                if (moistureStopThreshold < 0) {
                    moistureStopThreshold = 0;
                }

                Serial.print("STOP threshold decreased to: ");
                Serial.print(moistureStopThreshold);
                Serial.println("%");
                updateOLED();
            }
        }
    }
    lastStopDecReading = stopReading;
}

void updateOLED() {
    display.clearDisplay();
    
    display.setTextSize(1);
    
    // --- Top Bar (Title + Network/Signal) ---
    display.setCursor(0, 0);
    display.print("CALL-FARM");
    
    // Right align the signal and network info
    display.setCursor(65, 0);
    display.print(networkName);
    display.print(" S:");
    display.print(signalStrength);
    
    // Separator line
    display.drawLine(0, 10, 128, 10, WHITE);

    // --- Sensor Data ---
    display.setCursor(0, 16);
    display.print("Temp:  ");
    display.print(temperature, 1);
    display.println(" C");

    display.setCursor(0, 28);
    display.print("Humid: ");
    display.print(humidity, 1);
    display.println(" %");

    display.setCursor(0, 40);
    display.print("Moist: ");
    display.print(soilMoisturePercent);
    display.println(" %");

    // --- Enhanced Pump Status Display ---
    display.setCursor(0, 52);
    display.print("Pump: ");
    if (pumpIsOn) {
        display.print("ON");
    } else {
        display.print("OFF");
    }

    // Show current mode & thresholds on OLED
    if (isAutoMode) {
        display.print(" [Auto:");
        display.print(moistureStartThreshold);
        display.print("-");
        display.print(moistureStopThreshold);
        display.println("%]");
    } else {
        display.println(" [Man]");
    }

    display.display();
}

void sendSensorSMS(const String &recipient) {
    if (recipient.length() == 0) return;

    String message = "CALL-FARM STATUS\n";
    message += "Temp: " + String(temperature, 1) + " C\n";
    message += "Humidity: " + String(humidity, 1) + " %\n";
    message += "Soil Moisture: " + String(soilMoisturePercent) + " %\n";
    message += "Pump: " + String(pumpIsOn ? "ON" : "OFF") + "\n";
    message += "Mode: " + String(isAutoMode ? "AUTO" : "MANUAL") + "\n";
    message += "Threshold: " + String(moistureStartThreshold) + "-" + String(moistureStopThreshold) + " %";

    sim800l.println("AT+CMGF=1");
    delay(500);
    sim800l.print("AT+CMGS=\"");
    sim800l.print(recipient);
    sim800l.println("\"");
    delay(500);
    sim800l.print(message);
    sim800l.write(26); // Ctrl+Z
    delay(5000);
    Serial.println("Sensor SMS sent/requested.");
}

String sendATCommand(String command, const int timeout) {
    String response = "";
    sim800l.println(command);
    long int time = millis();
    while ((time + timeout) > millis()) {
      while (sim800l.available()) {
        char c = sim800l.read();
        response += c;
      }
    }
    response.trim();
    return response;
}