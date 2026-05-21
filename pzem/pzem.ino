#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Firebase_ESP_Client.h>
#include <PZEM004Tv30.h>
#include <SoftwareSerial.h>

#include <OneWire.h>
#include <DallasTemperature.h>

// ======================
// WIFI & FIREBASE
// ======================
#define WIFI_SSID "al"
#define WIFI_PASSWORD "12345678"
#define DATABASE_SECRET "LbzMZt1fazYnt2R42C8HHB9baDGbl2ak9POCZ3H7"
#define DATABASE_URL "https://pzem-4ffd6-default-rtdb.firebaseio.com/"

// ======================
// PIN CONFIG
// ======================
#define PIN_RX D5
#define PIN_TX D6
#define PIN_RELAY D1
#define DS_PIN D2

// ======================
// THRESHOLD
// ======================
const float MAX_SUHU = 70.0;
const float MIN_VOLT = 180.0;
const float MAX_VOLT = 240.0;

// ======================
// SENSOR INIT
// ======================
SoftwareSerial pzemSWSerial(PIN_RX, PIN_TX);
PZEM004Tv30 pzem(pzemSWSerial);

OneWire oneWire(DS_PIN);
DallasTemperature sensors(&oneWire);

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ======================
// SETUP
// ======================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_RELAY, OUTPUT);
  // RELAY NO (default LOW = OFF saat booting demi keamanan)
  digitalWrite(PIN_RELAY, LOW);

  sensors.begin();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");

  config.database_url = DATABASE_URL;
  config.signer.tokens.legacy_token = DATABASE_SECRET;
  Firebase.begin(&config, &auth);
}

// ======================
// LOOP
// ======================
void loop() {
  delay(5000);

  // ======================
  // PZEM - MEMBACA SEMUA PARAMETER UNTUK DASHBOARD W.H.A.T
  // ======================
  float voltage   = pzem.voltage();
  float current   = pzem.current();
  float power     = pzem.power();
  float energy    = pzem.energy();
  float frequency = pzem.frequency();
  float pf        = pzem.pf();

  // Bersihkan nilai jika PZEM gagal membaca atau bernilai NaN
  if (isnan(voltage))   voltage = 0;
  if (isnan(current))   current = 0;
  if (isnan(power))     power = 0;
  if (isnan(energy))    energy = 0;
  if (isnan(frequency)) frequency = 0;
  if (isnan(pf))        pf = 0;

  // ======================
  // DS18B20 (SUHU)
  // ======================
  sensors.requestTemperatures();
  float suhu = sensors.getTempCByIndex(0);

  if (suhu == DEVICE_DISCONNECTED_C) {
    suhu = 0; // Fallback aman
  }

  // ======================
  // FAULT DETECTION
  // ======================
  // Catatan: Jika tidak ada beban tetapi kabel listrik terhubung, voltage tetap terbaca (>0). 
  // Jika voltage == 0, berarti alat tidak tercolok atau kabel RX/TX PZEM terbalik.
  bool sensorFault = (voltage == 0); 

  bool overSuhu   = (suhu >= MAX_SUHU);
  bool overVolt   = (voltage > MAX_VOLT);
  bool underVolt  = (voltage < MIN_VOLT && voltage > 0); // Hanya underVolt jika ada tegangan masuk listrik drop

  // Jika fault sensor aktif, sistem akan memutus relay sebagai bentuk safety
  bool TRIP = overSuhu || overVolt || underVolt || sensorFault;

  // ======================
  // RELAY LOGIC (NO)
  // ======================
  if (TRIP) {
    digitalWrite(PIN_RELAY, LOW); // CUT OFF Listrik
    Serial.println("!!! CUT OFF / TRIP ACTIVE !!!");
  } else {
    digitalWrite(PIN_RELAY, HIGH); // SAFE - NYALAKAN ALIRAN
    Serial.println("System SAFE");
  }

  // ======================
  // FIREBASE (SET UPDATE NODE)
  // ======================
  FirebaseJson json;

  // Memasukkan data terstruktur yang dikonsumsi Dashboard
  json.set("voltage", voltage);
  json.set("current", current);
  json.set("power", power);
  json.set("energy", energy);
  json.set("frequency", frequency);
  json.set("pf", pf);
  json.set("suhu", suhu); // Menggunakan "suhu" saja, menghapus "temperature"
  json.set("biaya", energy * 1444.70);
  json.set("relay", TRIP ? "OFF" : "ON");

  // updateNode mengganti nilai key yang ada. 
  // Untuk menghapus data 'temperature' usang di Firebase root, gunakan perintah delete di bawah ini sekali
  Firebase.RTDB.updateNode(&fbdo, "/", &json);

  // Menghapus key 'temperature' yang lama di Firebase secara otomatis
  Firebase.RTDB.deleteNode(&fbdo, "/temperature");

  // ======================
  // DEBUG VIA SERIAL MONITOR
  // ======================
  Serial.printf(
    "V: %.1f V | A: %.2f A | P: %.1f W | PF: %.2f | S: %.1f °C | RELAY: %s\n",
    voltage, current, power, pf, suhu, TRIP ? "OFF" : "ON"
  );
  Serial.println("--------------------------------------------------");
}