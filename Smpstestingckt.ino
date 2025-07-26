#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_MAX31856.h>
#include <SPI.h>

const char* ssid = "Innotronix_AP";
const char* password = "Test@123";

const int relayPins[] = {7, 4, 37, 38, 5, 35, 6, 36};
const int totalRelays = sizeof(relayPins) / sizeof(relayPins[0]);

// Multiplexer configuration
const int muxChannels[] = {0, 3, 1, 2, 4, 5, 6, 7};  // MUX channel mapping for each relay
const int muxA = 10;      // MUX control pin A
const int muxB = 11;      // MUX control pin B  
const int muxC = 12;      // MUX control pin C
const int muxSIG = 1;     // ADC1_CH1 on GPIO1 - MUX signal pin

// MAX31856 Thermocouple configuration
#define MAX31856_CS   13
#define MAX31856_SCK  18
#define MAX31856_MISO 17
#define MAX31856_MOSI 21
SPIClass mySPI(FSPI);
Adafruit_MAX31856 max31856 = Adafruit_MAX31856(MAX31856_CS, &mySPI);

// Voltage sensing parameters
const float voltageThreshold = 2.0;  // Minimum voltage to consider SMPS as "OK" (adjust as needed)
const float voltageDividerRatio = 11.0;  // Adjust based on your voltage divider (R1+R2)/R2
const float adcMaxVoltage = 3.3;     // ESP32 ADC reference voltage
const int adcResolution = 4095;      // 12-bit ADC resolution

// Temperature variables
float currentTemperature = 0.0;
unsigned long lastTempRead = 0;
const unsigned long tempReadInterval = 2000; // Read temperature every 2 seconds

WebServer server(80);

bool toggling[8] = {false};
bool relayState[8] = {false};
unsigned long lastToggleTime[8] = {0};
unsigned long toggleStartTime[8] = {0};
int toggleOnTime[8] = {0};
int toggleOffTime[8] = {0};
int toggleCycles[8] = {0};
int completedCycles[8] = {0};

bool groupToggling = false;
unsigned long groupLastToggle = 0;
unsigned long groupStartTime = 0;
int groupOnTime = 0;
int groupOffTime = 0;
int groupCycles = 0;
int groupCompletedCycles = 0;
bool groupState = false;
int groupRelays[4] = {-1, -1, -1, -1};

// Variables to track test completion status
bool testCompleted[8] = {false};
String testStatus[8] = {"Not Tested", "Not Tested", "Not Tested", "Not Tested", 
                       "Not Tested", "Not Tested", "Not Tested", "Not Tested"};
float lastVoltageReading[8] = {0.0};

// Function declarations
void handleRoot();
void handleControl();
void handleGroupToggle();
void handleStatus();
void handleGroupStatus();
void handleRelayStatus();
void handleVoltageStatus();
void handleTemperatureStatus();
float readVoltage(int channel);
void selectMuxChannel(int channel);
String getSMPSStatusWithVoltage(int relayIndex);
void readTemperature();

// Function to select multiplexer channel
void selectMuxChannel(int channel) {
  digitalWrite(muxA, (channel & 0x01) ? HIGH : LOW);
  digitalWrite(muxB, (channel & 0x02) ? HIGH : LOW);
  digitalWrite(muxC, (channel & 0x04) ? HIGH : LOW);
  delayMicroseconds(10); // Small delay for MUX switching
}

// Function to read voltage from specific MUX channel
float readVoltage(int relayIndex) {
  if (relayIndex < 0 || relayIndex >= totalRelays) return 0.0;
  
  selectMuxChannel(muxChannels[relayIndex]);
  delay(10); // Allow settling time
  
  int adcValue = analogRead(muxSIG);
  float voltage = (adcValue * adcMaxVoltage / adcResolution) * voltageDividerRatio;
  
  lastVoltageReading[relayIndex] = voltage;
  return voltage;
}

// Function to read temperature from MAX31856
void readTemperature() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastTempRead >= tempReadInterval) {
    double temp = max31856.readThermocoupleTemperature();
    if (!isnan(temp)) {
      currentTemperature = temp;
    }
    lastTempRead = currentMillis;
  }
}

// Function to get SMPS status based on relay state and voltage sensing
String getSMPSStatus(int relayIndex) {
  if (testCompleted[relayIndex]) {
    return testStatus[relayIndex];
  }
  
  bool relayOn = !relayState[relayIndex]; // Since relays are NC, LOW = SMPS ON
  
  if (relayOn) {
    return "ON";
  } else {
    return "OFF";
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize relay pins
  for (int i = 0; i < totalRelays; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
    relayState[i] = false;
  }

  // Initialize MUX control pins
  pinMode(muxA, OUTPUT);
  pinMode(muxB, OUTPUT);
  pinMode(muxC, OUTPUT);
  pinMode(muxSIG, INPUT);

  // Initialize MAX31856 thermocouple
  mySPI.begin(MAX31856_SCK, MAX31856_MISO, MAX31856_MOSI, MAX31856_CS);
  if (!max31856.begin()) {
    Serial.println("Could not initialize MAX31856. Check connections.");
  } else {
    Serial.println("MAX31856 initialized successfully.");
  }

  // Initialize test status
  for (int i = 0; i < totalRelays; i++) {
    testCompleted[i] = false;
    testStatus[i] = "Not Tested";
    lastVoltageReading[i] = 0.0;
  }

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/control", handleControl);
  server.on("/groupToggle", handleGroupToggle);
  server.on("/status", handleStatus);
  server.on("/groupStatus", handleGroupStatus);
  server.on("/relayStatus", handleRelayStatus);
  server.on("/voltageStatus", handleVoltageStatus);
  server.on("/temperatureStatus", handleTemperatureStatus);
  server.begin();
}

void loop() {
  server.handleClient();
  readTemperature(); // Read temperature periodically
  unsigned long currentMillis = millis();

  for (int i = 0; i < totalRelays; i++) {
    if (toggling[i]) {
      // During ON time: SMPS should be ON (relay LOW)
      // During OFF time: SMPS should be OFF (relay HIGH)
      if (!relayState[i] && currentMillis - lastToggleTime[i] >= toggleOnTime[i]) {
        // Switch from ON to OFF (relay LOW to HIGH)
        digitalWrite(relayPins[i], HIGH);
        relayState[i] = true;
        lastToggleTime[i] = currentMillis;
      } else if (relayState[i] && currentMillis - lastToggleTime[i] >= toggleOffTime[i]) {
        if (toggleCycles[i] > 0 && completedCycles[i] >= toggleCycles[i]) {
          toggling[i] = false;
          // End with SMPS ON (relay LOW)
          digitalWrite(relayPins[i], LOW);
          relayState[i] = false;
          
          // Check final voltage to determine if SMPS is actually OK
          delay(100); // Allow time for SMPS to stabilize
          float finalVoltage = readVoltage(i);
          testCompleted[i] = true;
          
          if (finalVoltage >= voltageThreshold) {
            testStatus[i] = "Test Complete - SMPS OK";
          } else {
            testStatus[i] = "Test Complete - SMPS FAILED";
          }
        } else {
          // Switch from OFF to ON (relay HIGH to LOW)
          digitalWrite(relayPins[i], LOW);
          relayState[i] = false;
          lastToggleTime[i] = currentMillis;
          completedCycles[i]++;
        }
      }
    }
  }

  if (groupToggling) {
    // groupState true = SMPS ON (relay LOW), groupState false = SMPS OFF (relay HIGH)
    if ((groupState && currentMillis - groupLastToggle >= groupOnTime) ||
        (!groupState && currentMillis - groupLastToggle >= groupOffTime)) {
      
      if (!groupState) {
        groupCompletedCycles++;
        // Check if we've completed all cycles
        if (groupCycles > 0 && groupCompletedCycles >= groupCycles) {
          groupToggling = false;
          for (int i = 0; i < 4; i++) {
            int r = groupRelays[i];
            if (r >= 0 && r < totalRelays) {
              // End with SMPS ON (relay LOW)
              digitalWrite(relayPins[r], LOW);
              relayState[r] = false;
              
              // Check final voltage for each relay in group
              delay(100);
              float finalVoltage = readVoltage(r);
              testCompleted[r] = true;
              
              if (finalVoltage >= voltageThreshold) {
                testStatus[r] = "Group Test Complete - SMPS OK";
              } else {
                testStatus[r] = "Group Test Complete - SMPS FAILED";
              }
            }
          }
          return;
        }
      }
      
      groupState = !groupState;
      for (int i = 0; i < 4; i++) {
        int r = groupRelays[i];
        if (r >= 0 && r < totalRelays) {
          // groupState true = SMPS ON (relay LOW), groupState false = SMPS OFF (relay HIGH)
          digitalWrite(relayPins[r], groupState ? LOW : HIGH);
          relayState[r] = !groupState;
        }
      }
      groupLastToggle = currentMillis;
    }
  }
}

void handleRoot() {
  String html = R"rawliteral(
    <!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      * { box-sizing: border-box; margin: 0; padding: 0; }
      body { 
        font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; 
        background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
        height: 100vh;
        overflow: hidden;
        color: #333;
      }
      .header { 
        background: rgba(255,255,255,0.95); 
        padding: 12px 0; 
        text-align: center; 
        box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        backdrop-filter: blur(10px);
      }
      .header h2 { 
        color: #2c3e50; 
        font-size: 24px; 
        font-weight: 600;
        margin: 0;
      }
      .main-container { 
        display: flex; 
        height: calc(100vh - 60px);
        gap: 12px;
        padding: 12px;
      }
      .relays-section { 
        flex: 0 0 50%;
        background: rgba(255,255,255,0.95);
        border-radius: 16px;
        padding: 16px;
        overflow-y: auto;
        backdrop-filter: blur(10px);
        box-shadow: 0 8px 32px rgba(0,0,0,0.1);
      }
      .relays-grid { 
        display: grid; 
        grid-template-columns: repeat(4, 1fr);
        gap: 12px;
        height: 100%;
      }
      .relay { 
        background: linear-gradient(145deg, #ffffff, #f8f9fa);
        border-radius: 12px; 
        padding: 12px;
        box-shadow: 0 4px 15px rgba(0,0,0,0.08);
        border: 1px solid rgba(255,255,255,0.2);
        display: flex;
        flex-direction: column;
        justify-content: space-between;
        transition: transform 0.2s ease;
      }
      .relay:hover { transform: translateY(-2px); }
      .relay h3 { 
        color: #2c3e50; 
        font-size: 14px; 
        font-weight: 600;
        margin-bottom: 8px;
        text-align: center;
      }
      .status-display { 
        font-size: 11px; 
        margin-bottom: 8px;
        text-align: center;
        min-height: 20px;
      }
      .voltage-display {
        font-size: 10px;
        color: #666;
        text-align: center;
        margin-bottom: 4px;
      }
      .btn { 
        padding: 4px 8px; 
        margin: 1px; 
        border: none; 
        border-radius: 6px; 
        color: white; 
        cursor: pointer; 
        font-size: 10px;
        font-weight: 500;
        transition: all 0.2s ease;
        text-decoration: none;
        display: inline-block;
        text-align: center;
      }
      .btn:hover { transform: translateY(-1px); }
      .on { background: linear-gradient(145deg, #e74c3c, #c0392b); }
      .off { background: linear-gradient(145deg, #27ae60, #229954); }
      .toggle { background: linear-gradient(145deg, #3498db, #2980b9); }
      .stop { background: linear-gradient(145deg, #95a5a6, #7f8c8d); }
      .control-buttons { 
        display: flex; 
        gap: 2px; 
        margin-bottom: 8px;
        justify-content: center;
      }
      .toggle-form { 
        background: rgba(248,249,250,0.7);
        padding: 8px;
        border-radius: 8px;
        margin-bottom: 6px;
      }
      .form-field { 
        display: flex; 
        align-items: center; 
        justify-content: space-between; 
        margin-bottom: 4px;
      }
      .form-label { 
        font-size: 9px; 
        font-weight: 500; 
        color: #555; 
      }
      .form-input-group {
        display: flex;
        align-items: center;
        gap: 2px;
      }
      input[type=number] { 
        width: 35px; 
        padding: 2px 4px; 
        border: 1px solid #ddd; 
        border-radius: 4px; 
        font-size: 10px;
        text-align: center;
      }
      input[type=submit] { 
        padding: 4px 8px; 
        font-size: 10px; 
        width: 100%;
        margin-top: 4px;
      }
      .relay-timer { 
        font-size: 7px; 
        color: #007bff; 
        text-align: center;
        min-height: 50px;
        background: rgba(248,249,250,0.5);
        border-radius: 6px;
        padding: 4px;
        margin: 4px 0;
      }
      .group-section { 
        flex: 0 0 30%;
        background: rgba(255,255,255,0.95);
        border-radius: 16px;
        padding: 16px;
        backdrop-filter: blur(10px);
        box-shadow: 0 8px 32px rgba(0,0,0,0.1);
        overflow-y: auto;
      }
      .group-title { 
        color: #2c3e50; 
        font-size: 18px; 
        font-weight: 600;
        margin-bottom: 16px;
        text-align: center;
      }
      .checkbox-grid { 
        display: grid; 
        grid-template-columns: repeat(4, 1fr);
        gap: 8px; 
        margin: 12px 0; 
      }
      .checkbox-item { 
        display: flex; 
        align-items: center; 
        gap: 4px;
        font-size: 11px;
      }
      .checkbox-item input[type=checkbox] {
        transform: scale(0.8);
      }
      .group-form-field {
        display: flex;
        align-items: center;
        justify-content: space-between;
        margin: 12px 0;
      }
      .group-input-group {
        display: flex;
        align-items: center;
        gap: 4px;
        font-size: 12px;
      }
      .group-input-group input[type=number] {
        width: 45px;
      }
      .status-box { 
        background: rgba(248,249,250,0.8); 
        border: 1px solid #dee2e6; 
        border-radius: 8px; 
        padding: 12px; 
        margin: 12px 0;
        font-size: 12px;
      }
      .progress { font-weight: bold; color: #007bff; }
      .time-info { color: #6c757d; font-size: 11px; margin-top: 8px; }
      .status-on { color: #27ae60; font-weight: bold; }
      .status-off { color: #e74c3c; font-weight: bold; }
      .status-complete { color: #27ae60; font-weight: bold; }
      .status-fail { color: #e74c3c; font-weight: bold; }
      .estimated-time { 
        color: #007bff; 
        font-weight: bold; 
        margin: 8px 0; 
        font-size: 11px;
        text-align: center;
      }
      .group-btn {
        width: 100%;
        padding: 8px;
        font-size: 12px;
        margin: 8px 0;
      }
      .note {
        font-size: 10px;
        color: #666;
        text-align: center;
        margin: 8px 0;
      }
      
      /* Temperature Section */
      .temperature-section {
        flex: 0 0 20%;
        background: rgba(255,255,255,0.95);
        border-radius: 16px;
        padding: 16px;
        backdrop-filter: blur(10px);
        box-shadow: 0 8px 32px rgba(0,0,0,0.1);
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
      }
      .temperature-title {
        color: #2c3e50;
        font-size: 18px;
        font-weight: 600;
        margin-bottom: 20px;
        text-align: center;
      }
      .temperature-display {
        background: linear-gradient(145deg, #ff6b6b, #ee5a24);
        color: white;
        padding: 20px;
        border-radius: 50%;
        width: 120px;
        height: 120px;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        box-shadow: 0 8px 25px rgba(255, 107, 107, 0.3);
        margin-bottom: 20px;
      }
      .temp-value {
        font-size: 28px;
        font-weight: bold;
        margin-bottom: 5px;
      }
      .temp-unit {
        font-size: 16px;
        opacity: 0.9;
      }
      .temp-status {
        text-align: center;
        font-size: 14px;
        color: #2c3e50;
        padding: 10px;
        background: rgba(248,249,250,0.8);
        border-radius: 8px;
        width: 100%;
      }

      /* Scrollbar Styling */
      ::-webkit-scrollbar { width: 6px; }
      ::-webkit-scrollbar-track { background: rgba(0,0,0,0.1); border-radius: 3px; }
      ::-webkit-scrollbar-thumb { background: rgba(0,0,0,0.3); border-radius: 3px; }
      ::-webkit-scrollbar-thumb:hover { background: rgba(0,0,0,0.5); }
    </style>
    <script>
      setInterval(() => {
        fetch("/status").then(r => r.json()).then(data => {
          data.forEach((status, i) => {
            const statusElement = document.getElementById("s" + i);
            let statusClass = "";
            if (status.includes("Complete") && status.includes("OK")) {
              statusClass = "status-complete";
            } else if (status.includes("FAILED")) {
              statusClass = "status-fail";
            } else if (status.includes("ON")) {
              statusClass = "status-on";
            } else if (status.includes("OFF")) {
              statusClass = "status-off";
            }
            statusElement.innerHTML = `<span class="${statusClass}">${status}</span>`;
          });
        });
        
        // Fetch temperature data
        fetch("/temperatureStatus").then(r => r.json()).then(data => {
          const tempValue = document.getElementById("tempValue");
          const tempStatus = document.getElementById("tempStatus");
          
          tempValue.textContent = data.temperature.toFixed(1);
          
          let statusText = "Normal";
          let statusColor = "#27ae60";
          
          if (data.temperature > 80) {
            statusText = "High Temperature";
            statusColor = "#e74c3c";
          } else if (data.temperature > 60) {
            statusText = "Elevated Temperature";
            statusColor = "#f39c12";
          } else if (data.temperature < 0) {
            statusText = "Sensor Error";
            statusColor = "#95a5a6";
          }
          
          tempStatus.innerHTML = `<span style="color: ${statusColor}; font-weight: bold;">${statusText}</span>`;
        });
        
        // Fetch individual relay status for timing info
        fetch("/relayStatus").then(r => r.json()).then(data => {
          data.forEach((relay, i) => {
            const statusDiv = document.getElementById("relayTimer" + i);
            if (relay.active) {
              const elapsedMins = Math.floor(relay.elapsedTime / 60);
              const elapsedSecs = relay.elapsedTime % 60;
              
              let timeInfo = "";
              if (relay.total > 0) {
                // Finite cycles
                const remainingCycles = relay.total - relay.completed;
                const totalSeconds = relay.estimatedTime;
                const remainingTime = Math.max(0, totalSeconds - relay.elapsedTime);
                const remainingMins = Math.floor(remainingTime / 60);
                const remainingSecs = remainingTime % 60;
                const totalMins = Math.floor(totalSeconds / 60);
                const totalSecsDisplay = totalSeconds % 60;
                
                timeInfo = `
                  <div style='color: #007bff; font-weight: bold; font-size: 7px; margin: 4px 0; line-height: 1.2;'>
                    <div style='color: #28a745;'>Cycle: ${relay.completed}/${relay.total}</div>
                    <div style='color: #6c757d;'>Remaining Cycles: ${remainingCycles}</div>
                    <div style='color: #17a2b8;'>Elapsed: ${elapsedMins}:${String(elapsedSecs).padStart(2, '0')}</div>
                    <div style='color: #fd7e14;'>Remaining: ${remainingMins}:${String(remainingSecs).padStart(2, '0')}</div>
                    <div style='color: #6f42c1;'>Total: ${totalMins}:${String(totalSecsDisplay).padStart(2, '0')}</div>
                  </div>`;
              } else {
                // Infinite cycles
                timeInfo = `
                  <div style='color: #007bff; font-weight: bold; font-size: 7px; margin: 4px 0; line-height: 1.2;'>
                    <div style='color: #28a745;'>Cycle: ${relay.completed}/∞</div>
                    <div style='color: #6c757d;'>Remaining Cycles: ∞</div>
                    <div style='color: #17a2b8;'>Elapsed: ${elapsedMins}:${String(elapsedSecs).padStart(2, '0')}</div>
                    <div style='color: #dc3545;'>Running Infinite...</div>
                  </div>`;
              }
              statusDiv.innerHTML = timeInfo;
            } else {
              statusDiv.innerHTML = "";
            }
          });
        });
        
        fetch("/groupStatus").then(r => r.json()).then(data => {
          const statusDiv = document.getElementById("groupStatus");
          if (data.active) {
            const elapsedMins = Math.floor(data.elapsedTime / 60);
            const elapsedSecs = data.elapsedTime % 60;
            
            let timeInfo = "";
            if (data.total > 0) {
              // Finite cycles
              const remainingCycles = data.total - data.completed;
              const remainingTime = Math.max(0, data.estimatedTime - data.elapsedTime);
              const remainingMins = Math.floor(remainingTime / 60);
              const remainingSecs = remainingTime % 60;
              const totalMins = Math.floor(data.estimatedTime / 60);
              const totalSecs = data.estimatedTime % 60;
              
              timeInfo = `
                <div class='time-info' style='font-size: 11px; line-height: 1.3;'>
                  <div style='color: #28a745; font-weight: bold;'>Cycle: ${data.completed}/${data.total} | Remaining Cycles: ${remainingCycles}</div>
                  <div style='color: #17a2b8;'>Elapsed: ${elapsedMins}:${String(elapsedSecs).padStart(2, '0')} | 
                  <span style='color: #fd7e14;'>Remaining: ${remainingMins}:${String(remainingSecs).padStart(2, '0')}</span></div>
                  <div style='color: #6f42c1;'>Total Estimated: ${totalMins}:${String(totalSecs).padStart(2, '0')}</div>
                </div>`;
            } else {
              // Infinite cycles
              timeInfo = `
                <div class='time-info' style='font-size: 11px; line-height: 1.3;'>
                  <div style='color: #28a745; font-weight: bold;'>Cycle: ${data.completed}/∞ | Remaining: ∞ cycles</div>
                  <div style='color: #17a2b8;'>Elapsed: ${elapsedMins}:${String(elapsedSecs).padStart(2, '0')}</div>
                  <div style='color: #dc3545; font-weight: bold;'>Running Infinite Cycles...</div>
                </div>`;
            }
            
            statusDiv.innerHTML = `
              <div class='status-box'>
                <div class='progress'>Group Toggle Active</div>
                <div style='margin: 8px 0; color: #495057;'>Current State: SMPS ${data.state}</div>
                ${timeInfo}
              </div>`;
          } else {
            statusDiv.innerHTML = "";
          }
        });
      }, 1000);
      
      function calculateEstimatedTime() {
        const onTime = parseInt(document.getElementById('groupOnTime').value) || 0;
        const offTime = parseInt(document.getElementById('groupOffTime').value) || 0;
        const cycles = parseInt(document.getElementById('groupCycles').value) || 0;
        
        if (onTime > 0 && offTime > 0 && cycles > 0) {
          const totalSeconds = cycles * (onTime + offTime);
          const mins = Math.floor(totalSeconds / 60);
          const secs = totalSeconds % 60;
          document.getElementById('estimatedTime').innerHTML = 
            `<div class="estimated-time">Est: ${mins}:${String(secs).padStart(2, '0')}</div>`;
        } else {
          document.getElementById('estimatedTime').innerHTML = '';
        }
      }
      
      function calculateSingleEstimatedTime(relayIndex) {
        const onTime = parseInt(document.getElementById('onTime' + relayIndex).value) || 0;
        const offTime = parseInt(document.getElementById('offTime' + relayIndex).value) || 0;
        const cycles = parseInt(document.getElementById('cycles' + relayIndex).value) || 0;
        
        if (onTime > 0 && offTime > 0 && cycles > 0) {
          const totalSeconds = cycles * (onTime + offTime);
          const mins = Math.floor(totalSeconds / 60);
          const secs = totalSeconds % 60;
          document.getElementById('singleEstimatedTime' + relayIndex).innerHTML = 
            `<div class="estimated-time">Est: ${mins}:${String(secs).padStart(2, '0')}</div>`;
        } else {
          document.getElementById('singleEstimatedTime' + relayIndex).innerHTML = '';
        }
      }
    </script>
    </head><body>
    <div class="header">
      <h2>SMPS Relay Control System with Voltage Sensing & Temperature Monitoring</h2>
    </div>
    <div class="main-container">
      <div class="relays-section">
        <div class="relays-grid">
  )rawliteral";

  for (int i = 0; i < totalRelays; i++) {  
    String status = getSMPSStatus(i);
    html += "<div class='relay'>";
    html += "<h3>Relay " + String(i + 1) + "</h3>";
    html += "<div class='status-display'><strong>SMPS: </strong><span id='s" + String(i) + "'>" + status + "</span></div>";
    
    html += "<div class='control-buttons'>";
    html += "<a class='btn off' href='/control?relay=" + String(i) + "&action=on'>ON</a>";
    html += "<a class='btn on' href='/control?relay=" + String(i) + "&action=off'>OFF</a>";
    html += "</div>";
    
    html += "<div class='toggle-form'>";
    html += "<form action='/control' method='get'>";
    html += "<input type='hidden' name='relay' value='" + String(i) + "'>";
    html += "<input type='hidden' name='action' value='toggle'>";
    
    html += "<div class='form-field'>";
    html += "<span class='form-label'>ON:</span>";
    html += "<div class='form-input-group'><input type='number' id='onTime" + String(i) + "' name='ontime' min='1' max='60' onchange='calculateSingleEstimatedTime(" + String(i) + ")' required><span>s</span></div>";
    html += "</div>";
    
    html += "<div class='form-field'>";
    html += "<span class='form-label'>OFF:</span>";
    html += "<div class='form-input-group'><input type='number' id='offTime" + String(i) + "' name='offtime' min='1' max='60' onchange='calculateSingleEstimatedTime(" + String(i) + ")' required><span>s</span></div>";
    html += "</div>";
    
    html += "<div class='form-field'>";
    html += "<span class='form-label'>Cycles:</span>";
    html += "<div class='form-input-group'><input type='number' id='cycles" + String(i) + "' name='cycles' min='1' max='1000' onchange='calculateSingleEstimatedTime(" + String(i) + ")'></div>";
    html += "</div>";
    
    html += "<div id='singleEstimatedTime" + String(i) + "'></div>";
    
    html += "<div id='relayTimer" + String(i) + "' class='relay-timer'></div>";
    
    html += "<input class='btn toggle' type='submit' value='Start'>";
    html += "</form>";
    html += "</div>";
    
    html += "<a class='btn stop' href='/control?relay=" + String(i) + "&action=stop'>STOP</a>";
    html += "</div>";
  }

  html += R"rawliteral(
        </div>
      </div>
      
      <div class="group-section">
        <h3 class="group-title">Group Toggle</h3>
        <div id='groupStatus'></div>
        
        <form action='/groupToggle' method='get'>
          <div style='margin-bottom: 16px;'>
            <strong style='font-size: 12px;'>Select Relays:</strong>
            <div class='checkbox-grid'>
  )rawliteral";

  for (int i = 0; i < totalRelays; i++) {
    html += "<div class='checkbox-item'><input type='checkbox' name='relay' value='" + String(i) + "'><label>R" + String(i + 1) + "</label></div>";
  }

  html += R"rawliteral(
            </div>
          </div>
          
          <div class='group-form-field'>
            <div class='group-input-group'>
              <span>ON:</span>
              <input type='number' id='groupOnTime' name='ontime' min='1' max='60' onchange='calculateEstimatedTime()' required>
              <span>s</span>
            </div>
          </div>
          
          <div class='group-form-field'>
            <div class='group-input-group'>
              <span>OFF:</span>
              <input type='number' id='groupOffTime' name='offtime' min='1' max='60' onchange='calculateEstimatedTime()' required>
              <span>s</span>
            </div>
          </div>
          
          <div class='group-form-field'>
            <div class='group-input-group'>
              <span>Cycles:</span>
              <input type='number' id='groupCycles' name='cycles' min='0' max='1000' onchange='calculateEstimatedTime()' placeholder='∞'>
            </div>
          </div>
          
          <div class='note'>0 or empty = infinite</div>
          
          <div id='estimatedTime'></div>
          <input class='btn toggle group-btn' type='submit' value='Start Group Toggle'>
        </form>
        
        <form action='/groupToggle' method='get'>
          <input type='hidden' name='stop' value='1'>
          <input class='btn stop group-btn' type='submit' value='Stop Group Toggle'>
        </form>
        
        <div style='margin-top: 20px; padding: 10px; background: rgba(248,249,250,0.8); border-radius: 8px;'>
          <h4 style='font-size: 12px; margin-bottom: 8px; color: #2c3e50;'>Configuration:</h4>
          <div style='font-size: 10px; color: #666;'>
            <div>DC Voltage Detection: " + String(voltageThreshold, 1) + "V Threshold</div>
          </div>
        </div>
      </div>
      
      <div class="temperature-section">
        <h3 class="temperature-title">SMPS Temperature</h3>
        <div class="temperature-display">
          <div class="temp-value" id="tempValue">--</div>
          <div class="temp-unit">°C</div>
        </div>
        <div class="temp-status" id="tempStatus">Reading...</div>
        <div style='margin-top: 15px; padding: 8px; background: rgba(248,249,250,0.8); border-radius: 6px;'>
          <div style='font-size: 10px; color: #666; text-align: center;'>
            <div>Thermocouple Sensor</div>
            <div>MAX31856 Module</div>
          </div>
        </div>
      </div>
    </div>
    </body></html>
  )rawliteral";

  server.send(200, "text/html", html);
}

void handleControl() {
  int relay = server.arg("relay").toInt();
  String action = server.arg("action");
  int onTime = server.hasArg("ontime") ? server.arg("ontime").toInt() * 1000 : 0;
  int offTime = server.hasArg("offtime") ? server.arg("offtime").toInt() * 1000 : 0;
  int cycles = server.hasArg("cycles") ? server.arg("cycles").toInt() : 0;

  if (relay >= 0 && relay < totalRelays) {
    int activeRelays = 0;
    for (int i = 0; i < totalRelays; i++) if (relayState[i]) activeRelays++;

    if (action == "on") {
      // SMPS ON = Relay LOW (NC contact)
      toggling[relay] = false;
      digitalWrite(relayPins[relay], LOW);
      relayState[relay] = false;
      testCompleted[relay] = false;
      testStatus[relay] = "Not Tested";
    } else if (action == "off") {
      // SMPS OFF = Relay HIGH (NC contact)
      if (!relayState[relay] && activeRelays < 4) {
        toggling[relay] = false;
        digitalWrite(relayPins[relay], HIGH);
        relayState[relay] = true;
        testCompleted[relay] = false;
        testStatus[relay] = "Not Tested";
      }
    } else if (action == "toggle" && onTime > 0 && offTime > 0 && activeRelays < 4) {
      toggleOnTime[relay] = onTime;
      toggleOffTime[relay] = offTime;
      toggleCycles[relay] = cycles;
      completedCycles[relay] = 0;
      toggling[relay] = true;
      testCompleted[relay] = false;
      testStatus[relay] = "Testing...";
      toggleStartTime[relay] = millis();
      // Start with SMPS ON (Relay LOW)
      digitalWrite(relayPins[relay], LOW);
      relayState[relay] = false;
      lastToggleTime[relay] = millis();
    } else if (action == "stop") {
      toggling[relay] = false;
      // Stop = SMPS ON (Relay LOW)
      digitalWrite(relayPins[relay], LOW);
      relayState[relay] = false;
      testCompleted[relay] = false;
      testStatus[relay] = "Not Tested";
    }
  }

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleGroupToggle() {
  if (server.hasArg("stop")) {
    groupToggling = false;
    for (int i = 0; i < 4; i++) {
      if (groupRelays[i] >= 0) {
        // Stop = SMPS ON (Relay LOW)
        digitalWrite(relayPins[groupRelays[i]], LOW);
        relayState[groupRelays[i]] = false;
        testCompleted[groupRelays[i]] = false;
        testStatus[groupRelays[i]] = "Not Tested";
        groupRelays[i] = -1;
      }
    }
  } else {
    int idx = 0;
    for (int i = 0; i < server.args(); i++) {
      if (server.argName(i) == "relay" && idx < 4) {
        groupRelays[idx++] = server.arg(i).toInt();
      }
    }

    groupOnTime = server.arg("ontime").toInt() * 1000;
    groupOffTime = server.arg("offtime").toInt() * 1000;
    groupCycles = server.hasArg("cycles") ? server.arg("cycles").toInt() : 0;
    groupCompletedCycles = 0;
    groupToggling = true;
    groupState = true; // Start with SMPS ON state
    groupLastToggle = millis();
    groupStartTime = millis();

    for (int i = 0; i < 4; i++) {
      if (groupRelays[i] >= 0) {
        // Start with SMPS ON (Relay LOW)
        digitalWrite(relayPins[groupRelays[i]], LOW);
        relayState[groupRelays[i]] = false;
        testCompleted[groupRelays[i]] = false;
        testStatus[groupRelays[i]] = "Group Testing...";
      }
    }
  }

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleStatus() {
  String json = "[";
  for (int i = 0; i < totalRelays; i++) {
    String status = getSMPSStatus(i);
    json += "\"" + status + "\"";
    if (i < totalRelays - 1) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleVoltageStatus() {
  String json = "[";
  for (int i = 0; i < totalRelays; i++) {
    float voltage = readVoltage(i);
    json += String(voltage, 2);
    if (i < totalRelays - 1) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleGroupStatus() {
  String json = "{";
  json += "\"active\":" + String(groupToggling ? "true" : "false") + ",";
  json += "\"completed\":" + String(groupCompletedCycles) + ",";
  json += "\"total\":" + String(groupCycles) + ",";
  json += "\"state\":\"" + String(groupState ? "ON" : "OFF") + "\"";

  if (groupToggling) {
    unsigned long elapsedTime = (millis() - groupStartTime) / 1000;
    unsigned long estimatedTotal = groupCycles > 0 ? (groupCycles * (groupOnTime + groupOffTime)) / 1000 : 0;

    json += ",\"elapsedTime\":" + String(elapsedTime);
    json += ",\"estimatedTime\":" + String(estimatedTotal);
  }

  json += "}";
  server.send(200, "application/json", json);
}

void handleRelayStatus() {
  String json = "[";
  unsigned long currentTime = millis();

  for (int i = 0; i < totalRelays; i++) {
    json += "{";
    json += "\"active\":" + String(toggling[i] ? "true" : "false") + ",";
    json += "\"completed\":" + String(completedCycles[i]) + ",";
    json += "\"total\":" + String(toggleCycles[i]);

    if (toggling[i]) {
      unsigned long elapsedTime = (currentTime - toggleStartTime[i]) / 1000;
      unsigned long estimatedTotal = toggleCycles[i] > 0 ? (toggleCycles[i] * (toggleOnTime[i] + toggleOffTime[i])) / 1000 : 0;

      json += ",\"elapsedTime\":" + String(elapsedTime);
      json += ",\"estimatedTime\":" + String(estimatedTotal);
    }

    json += "}";
    if (i < totalRelays - 1) json += ",";
  }

  json += "]";
  server.send(200, "application/json", json);
}

void handleTemperatureStatus() {
  String json = "{";
  json += "\"temperature\":" + String(currentTemperature, 2);
  json += "}";
  server.send(200, "application/json", json);
}