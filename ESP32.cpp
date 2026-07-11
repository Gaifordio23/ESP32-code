#include "DHT.h"
#include <WiFi.h>
#include "ThingSpeak.h"
#include "HX711.h" 
#include <driver/i2s.h> 
#include <driver/adc.h> 

#define DHTPIN 4     
#define DHTTYPE DHT22   
DHT dht(DHTPIN, DHTTYPE);

#define MQ135_PIN 34 

const int LOADCELL_DOUT_PIN = 25; 
const int LOADCELL_SCK_PIN = 18;
HX711 scale;

#define I2S_SCK     14   
#define I2S_WS      27   
#define I2S_SD      26   

#define SAMPLE_RATE 11025   
#define I2S_PORT    I2S_NUM_0
#define BUFFER_SIZE 512     

const char* ssid     = "lab32";      
const char* password = "70814088";  

unsigned long myChannelNumber = 3413045; 
const char* myWriteAPIKey = "CYGTV4LDOEDPS1MI";       

WiFiClient client;

#define TIME_TO_SAMPLE_N_SECONDS 10        
#define TIME_TO_SLEEP  300        
#define uS_TO_S_FACTOR 1000000ULL  

#define RXD2 32          
#define TXD2 33          

const char* apn = "internet";            

#define GSM_BAUD_RATE 9600

bool waitForResponse(String expected, int timeout);
bool testGSMBaud(int baud);
void detectAndSetAPN();

void init_microphone() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX), 
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,       
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,         
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_SIZE,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE, 
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
}

bool waitForResponse(String expected, int timeout) {
  unsigned long start = millis();
  String response = "";
  while(millis() - start < timeout) {
    if(Serial2.available()) {
      char c = Serial2.read();
      response += c;
      if(c == '\n' || c == '\r') {
        Serial.print("[GSM] ");
        Serial.println(response);
        response = "";
      }
      if(response.indexOf(expected) != -1) {
        return true;
      }
    }
    delay(10);
  }
  return false;
}

bool testGSMBaud(int baud) {
  Serial2.begin(baud, SERIAL_8N1, RXD2, TXD2);
  delay(300);
  Serial2.println("AT");
  delay(300);
  
  if(Serial2.available()) {
    String response = "";
    while(Serial2.available()) {
      response += (char)Serial2.read();
    }
    if(response.indexOf("OK") != -1) {
      return true;
    }
  }
  return false;
}

void detectAndSetAPN() {
  Serial2.println("AT+COPS?"); 
  delay(500);
  
  String response = "";
  unsigned long start = millis();
  while(millis() - start < 1000) {
    if(Serial2.available()) {
      response += (char)Serial2.read();
    }
  }
  
  Serial.print("[GSM] Carrier detection: ");
  Serial.println(response);
  
  if(response.indexOf("Vodafone") != -1) {
    strcpy((char*)apn, "live.vodafone.com");
  } else if(response.indexOf("Orange") != -1) {
    strcpy((char*)apn, "net");
  } else if(response.indexOf("Telekom") != -1) {
    strcpy((char*)apn, "internet");
  } else if(response.indexOf("Digi") != -1) {
    strcpy((char*)apn, "internet");
  } else {
  }
  
  Serial.print("[GSM] Selected APN: ");
  Serial.println(apn);
}

void sendGPRSData(float temp, float hum, int gas, long weight, float rms, int status) {
  Serial.println("\n[GSM-GPRS] Initializing backup cellular web data channel...");
  
#ifdef GSM_POWER_PIN
  pinMode(GSM_POWER_PIN, OUTPUT);
  digitalWrite(GSM_POWER_PIN, HIGH);
  delay(2000); 
#endif
  
  int baudRates[] = {9600, 115200, 19200, 4800, 57600};
  bool gsmFound = false;
  for(int i = 0; i < 5; i++) {
    Serial.print("[GSM] Testing baud rate: ");
    Serial.println(baudRates[i]);
    if(testGSMBaud(baudRates[i])) {
      Serial.println("[GSM] Module found!");
      gsmFound = true;
      break;
    }
  }
  
  if(!gsmFound) {
    Serial.println("[ERROR] GSM module not responding on any baud rate!");
    return;
  }
  
  Serial2.println("AT");
  if(!waitForResponse("OK", 1000)) {
    Serial.println("[ERROR] GSM module not responding to AT!");
    return;
  }
  
  Serial2.println("AT+CPIN?");
  delay(500);
  if(!waitForResponse("READY", 2000)) {
    Serial.println("[ERROR] SIM not ready or needs PIN!");
    return;
  }
  
  Serial2.println("AT+CSQ");
  delay(500);
  
  Serial2.println("AT+CREG?");
  delay(500);
  
  detectAndSetAPN();
  
  Serial2.println("AT+SAPBR=3,1=\"Contype\",\"GPRS\"");
  if(!waitForResponse("OK", 1000)) {
    Serial.println("[ERROR] Failed to set bearer type!");
    return;
  }
  
  Serial2.print("AT+SAPBR=3,1=\"APN\",\"");
  Serial2.print(apn);
  Serial2.println("\"");
  if(!waitForResponse("OK", 1000)) {
    Serial.println("[ERROR] Failed to set APN!");
    return;
  }
  
  Serial2.println("AT+SAPBR=1,1");
  if(!waitForResponse("OK", 5000)) {
    Serial.println("[ERROR] Failed to open GPRS bearer!");
    return;
  }
  
  Serial2.println("AT+SAPBR=2,1");
  delay(500);
  
  Serial2.println("AT+HTTPINIT");
  if(!waitForResponse("OK", 1000)) {
    Serial.println("[ERROR] HTTP initialization failed!");
    return;
  }
  
  Serial2.println("AT+HTTPPARA=\"CID\",1");
  if(!waitForResponse("OK", 1000)) {
    Serial.println("[ERROR] Failed to set HTTP CID!");
    return;
  }
  
  String url = "http://api.thingspeak.com/update?api_key=" + String(myWriteAPIKey) +
               "&field1=" + String(temp) +
               "&field2=" + String(hum) +
               "&field3=" + String(gas) +
               "&field4=" + String(weight) +
               "&field5=" + String(rms) +
               "&field6=" + String(status);
               
  if(url.length() > 400) {
    Serial.println("[WARNING] URL too long, using POST instead of GET");
    Serial2.println("AT+HTTPPARA=\"URL\",\"http://api.thingspeak.com/update\"");
    if(!waitForResponse("OK", 1000)) return;
    
    Serial2.println("AT+HTTPPARA=\"CONTENT\",\"application/x-www-form-urlencoded\"");
    if(!waitForResponse("OK", 1000)) return;
    
    String postData = "api_key=" + String(myWriteAPIKey) +
                      "&field1=" + String(temp) +
                      "&field2=" + String(hum) +
                      "&field3=" + String(gas) +
                      "&field4=" + String(weight) +
                      "&field5=" + String(rms) +
                      "&field6=" + String(status);
    
    Serial2.print("AT+HTTPDATA=");
    Serial2.println(postData.length());
    if(!waitForResponse("DOWNLOAD", 3000)) return;
    
    Serial2.print(postData);
    if(!waitForResponse("OK", 3000)) return;
    
    Serial2.println("AT+HTTPACTION=1"); 
  } else {
    Serial2.print("AT+HTTPPARA=\"URL\",\"");
    Serial2.print(url);
    Serial2.println("\"");
    if(!waitForResponse("OK", 1000)) return;
    
    Serial2.println("AT+HTTPACTION=0"); 
  }
  
  if(!waitForResponse("+HTTPACTION:", 10000)) {
    Serial.println("[ERROR] HTTP request timed out!");
  } else {
    Serial.println("[GPRS SUCCESS] Packet streamed over cellular data grid!");
  }
  
  Serial2.println("AT+HTTPTERM");
  waitForResponse("OK", 1000);
  
  Serial2.println("AT+SAPBR=0,1");
  waitForResponse("OK", 1000);
  
#ifdef GSM_POWER_PIN
  digitalWrite(GSM_POWER_PIN, LOW);
#endif
  
  Serial.println("[GSM] Module powered down");
}

void setup() {
  pinMode(LOADCELL_SCK_PIN, OUTPUT);
  digitalWrite(LOADCELL_SCK_PIN, LOW);

  Serial.begin(115200);
  
  while(!Serial) { delay(10); }
  delay(1500); 
  Serial.println("\n===== INTEGRATED MULTI-SENSOR NODE AWAKE =====");

  dht.begin();
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  ThingSpeak.begin(client); 
  
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11); 

  init_microphone(); 

  Serial.print("Waiting for HX711 to become ready...");
  int retryAttempts = 0;
  bool sensorFound = false;
  
  while (retryAttempts < 10) {
    if (scale.is_ready()) {
      sensorFound = true;
      break;
    }
    delay(100); 
    Serial.print(".");
    retryAttempts++;
  }

  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  int rawGasValue = adc1_get_raw(ADC1_CHANNEL_6);
  
  long finalWeightGrams = 0; 
  if (sensorFound) {
    Serial.println(" [OK] Connected!");
    
    scale.set_offset(-42371);   
    scale.set_scale(295.050262);  
    
    finalWeightGrams = scale.get_units(5); 
    if (finalWeightGrams < 0) { finalWeightGrams = 0; } 
    
  } else {
    Serial.println("\n[ERROR] HX711 scale not found. Check wiring.");
  }

  int32_t sampleBuffer[BUFFER_SIZE];
  size_t bytesRead = 0;
  
  double totalSquaredSum = 0;
  long zeroCrossings = 0;
  int16_t lastSample = 0;
  long totalSamplesProcessed = 0;

  unsigned long startSampleTime = millis();
  Serial.println("Sampling digital audio stream for multi-feature extraction... ");
  
  while (millis() - startSampleTime < (TIME_TO_SAMPLE_N_SECONDS * 1000)) {
    esp_err_t audioResult = i2s_read(I2S_PORT, &sampleBuffer, sizeof(sampleBuffer), &bytesRead, portMAX_DELAY);
    
    if (audioResult == ESP_OK && bytesRead > 0) {
      int samplesCount = bytesRead / sizeof(int32_t);
      for (int i = 0; i < samplesCount; i++) {
        sampleBuffer[i] >>= 14; 
        int16_t currentSample = (int16_t)sampleBuffer[i];
        
        totalSquaredSum += (double)(currentSample * currentSample);
        if ((lastSample < 0 && currentSample >= 0) || (lastSample > 0 && currentSample <= 0)) {
          zeroCrossings++;
        }
        lastSample = currentSample;
        totalSamplesProcessed++;
      }
    }
  }

  double rmsEnergy = sqrt(totalSquaredSum / totalSamplesProcessed);
  double zeroCrossingRate = (double)zeroCrossings / totalSamplesProcessed;

  Serial.print("[OK] Computed RMS Energy: "); Serial.println(rmsEnergy);
  Serial.print("[OK] Computed Zero Crossing Rate: "); Serial.println(zeroCrossingRate);
  
  int beeActivityStatus = 0; 
  String behaviorMessage = "";

  if (rmsEnergy <= 40.0) {
    beeActivityStatus = 1;
    behaviorMessage = "Low Activity Baseline / Ambient Silence";
  } 
  else if (rmsEnergy > 40.0 && zeroCrossingRate >= 0.04 && zeroCrossingRate < 0.12) {
    beeActivityStatus = 2;
    behaviorMessage = "Standard Energy Pattern: Regular Colony Buzz";
  } 
  else if (rmsEnergy > 30.0 && zeroCrossingRate >= 0.12) {
    beeActivityStatus = 3;
    behaviorMessage = "High Spectral Density: Queenless Stress Hum Detected!";
  } 
  else {
    beeActivityStatus = 1; 
    behaviorMessage = "Unclassified Environment Sound";
  }

  Serial.print("[EMBEDDED EDGE PROCESSING] State: "); Serial.println(behaviorMessage);

  Serial.print("DHT Temp: "); Serial.println(temperature);
  Serial.print("DHT Hum: "); Serial.println(humidity);
  Serial.print("Raw Gas ADC Value: "); Serial.println(rawGasValue);
  Serial.print("Calibrated Weight (Grams): "); Serial.println(finalWeightGrams);

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("[ERROR] DHT22 sensor read failure. Aborting data push.");
  } else {
    Serial.print("Connecting to Wi-Fi: ");
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }

    if(WiFi.status() == WL_CONNECTED) {
      Serial.println("\n[OK] Connected to gateway!");

      ThingSpeak.setField(1, temperature);
      ThingSpeak.setField(2, humidity);
      ThingSpeak.setField(3, rawGasValue); 
      ThingSpeak.setField(4, String(finalWeightGrams)); 
      ThingSpeak.setField(5, (float)rmsEnergy);         
      ThingSpeak.setField(6, beeActivityStatus);       

      Serial.println("[CLOUD] Streaming data points directly to cloud log...");
      int responseCode = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);

      if(responseCode == 200) {
        Serial.println("[SUCCESS] Log entry updated successfully!");
      } else {
        Serial.print("[ERROR] Push failed. HTTP Error Code: "); Serial.println(responseCode);
      }
    } 
    else {
      Serial.println("\n[ERROR] Wi-Fi link failure. Initiating cellular GPRS failover strategy...");
      sendGPRSData(temperature, humidity, rawGasValue, finalWeightGrams, (float)rmsEnergy, beeActivityStatus); 
    }
  }

  i2s_driver_uninstall(I2S_PORT);

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  Serial.println("Entering Deep Sleep lifecycle phase...");
  Serial.flush();
  esp_deep_sleep_start();
}

void loop() {
}