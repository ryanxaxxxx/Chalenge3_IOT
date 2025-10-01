 
// Wokwi / Arduino Uno: 4 LEDs, 4 resistores, 1 botão, 1 LCD 16x2 via I2C (0x27), DHT22, Buzzer
// Com registro persistente em EEPROM do histórico de dados e status dos componentes
 
// Cada clique no botão alterna: acende o próximo LED e troca a mensagem do LCD.
 
// Sequência LCD: "Moto Localizada" -> "Moto OK" -> "Alerta Ativado" -> "Sistema Seguro" -> repete.
 
// No estado "Alerta Ativado" (LED amarelo): ativa buzzer e exibe temperatura no LCD
 
// Pressão longa (>1s) desliga todos os LEDs e buzzer.
 
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <EEPROM.h>
 
// Configuração do LCD I2C 
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Configuração do sensor DHT22
#define DHT_PIN 6
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

// salva os  dados históricos em EEPROM
struct HistoryRecord {
  float temperature;
  float humidity;
  uint8_t ledStates;     
  uint8_t systemState;    
  bool buzzerActive;
  unsigned long timestamp; 
};

struct SystemConfig {
  uint16_t totalRecords;
  uint16_t currentIndex;
  uint32_t bootCount;
  uint8_t lastSystemState;
};


#define CONFIG_ADDR 0
#define HISTORY_START_ADDR sizeof(SystemConfig)
#define MAX_RECORDS 20  


const uint8_t ledPins[] = {2, 3, 4, 5};
const uint8_t NUM_LEDS = sizeof(ledPins) / sizeof(ledPins[0]);
const uint8_t buttonPin = 7;
const uint8_t buzzerPin = 8;
 
// Debounce e detecção de pressão longa
const unsigned long DEBOUNCE_MS = 50;
const unsigned long LONG_PRESS_MS = 1000;
const unsigned long TEMP_UPDATE_MS = 5000;  
const unsigned long SAVE_INTERVAL_MS = 30000; 
 
bool ledState[4] = {false, false, false, false};
uint8_t nextLedIndex = 0;
 
int lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long buttonDownTime = 0;
bool buttonHeld = false;
 
//  mensagem do LCD e temperatura
uint8_t messageIndex = 0;
unsigned long lastTempUpdate = 0;
unsigned long lastSave = 0;
unsigned long bootTime = 0;
float temperature = 0.0;
float humidity = 0.0;
bool buzzerActive = false;

SystemConfig config;
 
void setup() {
  Serial.begin(9600);
  bootTime = millis();
  
 
  loadConfig();
  config.bootCount++;
  
 
  for (uint8_t i = 0; i < NUM_LEDS; ++i) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], LOW);
  }
 
  
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);
  
  
  dht.begin();
 
  
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sistema pronto");
  lcd.setCursor(0, 1);
  lcd.print("Boot #");
  lcd.print(config.bootCount);
  delay(2000);
  
  
  restoreLastState();
  
  
  saveConfig();
  
  Serial.println("Sistema iniciado");
  Serial.print("Boot count: ");
  Serial.println(config.bootCount);
  Serial.print("Registros salvos: ");
  Serial.println(config.totalRecords);
}
 
void loop() {
  handleButton();
  updateTemperature();
  handleBuzzer();
  periodicSave();
}

void loadConfig() {
  EEPROM.get(CONFIG_ADDR, config);
  
  
  if (config.totalRecords > MAX_RECORDS || config.currentIndex > MAX_RECORDS) {
    Serial.println("Inicializando EEPROM...");
    config.totalRecords = 0;
    config.currentIndex = 0;
    config.bootCount = 0;
    config.lastSystemState = 0;
    saveConfig();
  }
}

void saveConfig() {
  EEPROM.put(CONFIG_ADDR, config);
}

void restoreLastState() {
  messageIndex = config.lastSystemState;
  
  
  if (messageIndex < 4) {
    
    for (uint8_t i = 0; i <= messageIndex; i++) {
      if (i == messageIndex) {
        lcd.clear();
        displayCurrentState();
        break;
      }
    }
  }
}

void saveHistoryRecord() {
  HistoryRecord record;
  record.temperature = temperature;
  record.humidity = humidity;
  record.systemState = messageIndex;
  record.buzzerActive = buzzerActive;
  record.timestamp = (millis() - bootTime) / 60000; 
  
  
  record.ledStates = 0;
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    if (ledState[i]) {
      record.ledStates |= (1 << i);
    }
  }
  
  
  uint16_t addr = HISTORY_START_ADDR + (config.currentIndex * sizeof(HistoryRecord));
  
  
  EEPROM.put(addr, record);
  
  
  config.currentIndex = (config.currentIndex + 1) % MAX_RECORDS;
  if (config.totalRecords < MAX_RECORDS) {
    config.totalRecords++;
  }
  
  saveConfig();
  
  Serial.print("Registro salvo #");
  Serial.print(config.totalRecords);
  Serial.print(" - T:");
  Serial.print(temperature);
  Serial.print("°C H:");
  Serial.print(humidity);
  Serial.print("% Estado:");
  Serial.println(messageIndex);
}

void printHistory() {
  Serial.println("\n=== HISTÓRICO EEPROM ===");
  Serial.print("Total de registros: ");
  Serial.println(config.totalRecords);
  Serial.print("Boot count: ");
  Serial.println(config.bootCount);
  
  for (uint16_t i = 0; i < config.totalRecords; i++) {
    HistoryRecord record;
    uint16_t addr = HISTORY_START_ADDR + (i * sizeof(HistoryRecord));
    EEPROM.get(addr, record);
    
    Serial.print("Reg ");
    Serial.print(i);
    Serial.print(": T=");
    Serial.print(record.temperature);
    Serial.print("°C H=");
    Serial.print(record.humidity);
    Serial.print("% LEDs=");
    Serial.print(record.ledStates, BIN);
    Serial.print(" Estado=");
    Serial.print(record.systemState);
    Serial.print(" Buzzer=");
    Serial.print(record.buzzerActive ? "ON" : "OFF");
    Serial.print(" Time=");
    Serial.print(record.timestamp);
    Serial.println("min");
  }
  Serial.println("========================\n");
}

void periodicSave() {
  if (millis() - lastSave >= SAVE_INTERVAL_MS) {
    saveHistoryRecord();
    lastSave = millis();
    
    
    if (config.totalRecords % 5 == 0) {
      printHistory();
    }
  }
}

void handleButton() {
  int reading = digitalRead(buttonPin);
 
  
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
 
  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    
    static int stableState = HIGH;
    if (reading != stableState) {
      stableState = reading;
      if (stableState == LOW) {
        
        buttonDownTime = millis();
        buttonHeld = true;
      } else {
        
        unsigned long pressDuration = millis() - buttonDownTime;
        if (pressDuration >= LONG_PRESS_MS) {
          allOff();
          printHistory(); 
        } else {
          toggleNextLed();
          toggleMessage();
          
          config.lastSystemState = messageIndex;
          saveConfig();
          
          saveHistoryRecord();
        }
        buttonHeld = false;
      }
    }
  }
 
  lastButtonState = reading;
}

void updateTemperature() {
  if (millis() - lastTempUpdate >= TEMP_UPDATE_MS) {
    float newTemp = dht.readTemperature();
    float newHum = dht.readHumidity();
    
    if (!isnan(newTemp) && !isnan(newHum)) {
      temperature = newTemp;
      humidity = newHum;
    }
    
    lastTempUpdate = millis();
    
    
    if (buzzerActive) {
      displayAlertWithTemp();
    }
  }
}

void handleBuzzer() {
  
  if (buzzerActive && ledState[2]) {
    
    static unsigned long lastBuzzerToggle = 0;
    static bool buzzerOn = false;
    
    if (millis() - lastBuzzerToggle >= 500) {
      buzzerOn = !buzzerOn;
      if (buzzerOn) {
        tone(buzzerPin, 1000); 
      } else {
        noTone(buzzerPin);
      }
      lastBuzzerToggle = millis();
    }
  } else {
    noTone(buzzerPin);
  }
}
 

void toggleNextLed() {
  ledState[nextLedIndex] = !ledState[nextLedIndex];
  digitalWrite(ledPins[nextLedIndex], ledState[nextLedIndex] ? HIGH : LOW);
 
  nextLedIndex = (nextLedIndex + 1) % NUM_LEDS;
}
 

void toggleMessage() {
  lcd.clear();
  
  switch (messageIndex) {
    case 0:
      lcd.setCursor(0, 0);
      lcd.print("Moto Localizada");
      buzzerActive = false;
      break;
    case 1:
      lcd.setCursor(0, 0);
      lcd.print("Moto OK");
      buzzerActive = false;
      break;
    case 2:
      buzzerActive = true;
      displayAlertWithTemp();
      break;
    case 3:
      lcd.setCursor(0, 0);
      lcd.print("Sistema Seguro");
      buzzerActive = false;
      break;
  }
 
  
  messageIndex = (messageIndex + 1) % 4;
}

void displayCurrentState() {
  switch (messageIndex) {
    case 0:
      lcd.setCursor(0, 0);
      lcd.print("Moto Localizada");
      break;
    case 1:
      lcd.setCursor(0, 0);
      lcd.print("Moto OK");
      break;
    case 2:
      displayAlertWithTemp();
      break;
    case 3:
      lcd.setCursor(0, 0);
      lcd.print("Sistema Seguro");
      break;
  }
}

void displayAlertWithTemp() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Alerta Ativado");
  
  
  lcd.setCursor(0, 1);
  if (!isnan(temperature) && !isnan(humidity)) {
    lcd.print("T:");
    lcd.print(temperature, 1);
    lcd.print("C H:");
    lcd.print(humidity, 0);
    lcd.print("%");
  } else {
    lcd.print("Sensor erro!");
  }
}
 

void allOff() {
  for (uint8_t i = 0; i < NUM_LEDS; ++i) {
    ledState[i] = false;
    digitalWrite(ledPins[i], LOW);
  }
  
  buzzerActive = false;
  noTone(buzzerPin);
  messageIndex = 0;
  
  
  config.lastSystemState = 0;
  saveConfig();
  saveHistoryRecord(); 
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Todos: DESLIGADOS");
  lcd.setCursor(0, 1);
  lcd.print("Historico salvo");
}