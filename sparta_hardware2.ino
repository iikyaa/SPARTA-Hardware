// Library LCD I2C 20x4
#include <LiquidCrystal_I2C.h>
// Batas ambang sensor ultrasonik untuk mendeteksi kendaraan
#define treshold 7.2
// Library servo dan koneksi WiFi + Firebase
#include <ESP32Servo.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
// NTP Time Library
#include <time.h>

// Konfigurasi koneksi WiFi
#define WIFI_SSID "kyy"
#define WIFI_PASSWORD "asdfghjkl27"

// Konfigurasi Firebase
#define FIREBASE_HOST "https://sparta-60d0f-default-rtdb.firebaseio.com/"  // URL database Firebase
#define FIREBASE_AUTH "AIzaSyB8PktXa1nlULc0QSG_ANBBlEyC_6qo28Q"            // Secret key Firebase

// Konfigurasi NTP
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;  // GMT+7 untuk Indonesia (7 jam * 3600 detik)
const int daylightOffset_sec = 0;     // Tidak ada daylight saving time di Indonesia

// Objek konfigurasi Firebase
FirebaseConfig config;
FirebaseAuth auth;
FirebaseData firebaseData;
// Waktu pengiriman data Firebase
unsigned long previousMillis = 0;
const long interval = 2000;  // Kirim data setiap 2 detik

// Variabel untuk input sensor & tombol
int ir1;
int ir2;
int led1 = 5;
int led2 = 17;
int sig;
int button1, button2;
int tersedia; // slot tersedia

// PIN HCSR04 (sensor ultrasonik)
const int trigPin1 = 32;
const int echoPin1 = 33;
const int trigPin2 = 25;
const int echoPin2 = 26;
const int trigPin3 = 27;
const int echoPin3 = 14;
const int trigPin4 = 12;
const int echoPin4 = 13;

// LCD konfigurasi
int lcdColumns = 20;
int lcdRows = 4;
LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);

// Status slot parkir
bool slot1;
bool slot2;
bool slot3;
bool slot4;

// Status slot parkir sebelumnya untuk deteksi perubahan
bool prevSlot1 = false;
bool prevSlot2 = false;
bool prevSlot3 = false;
bool prevSlot4 = false;

// Variabel jarak HCSR04
#define SOUND_SPEED 0.034
long duration;
float distanceCm1;
float distanceCm2;
float distanceCm3;
float distanceCm4;

// Servo motor gate
static const int servo1Pin = 4;
static const int servo2Pin = 16;
Servo servo1;
Servo servo2;

// Variabel hitung status
int Total = 4;
int p1, p2, p3, p4;
char names[4][4];

// Variabel untuk tracking history parkir harian
int dailyParkingCount = 0;        // Total kendaraan yang parkir hari ini
int dailyExitCount = 0;           // Total kendaraan yang keluar hari ini
int previousOccupiedSlots = 0;    // Jumlah slot terisi sebelumnya
String currentDate = "";          // Tanggal saat ini
bool historyInitialized = false;  // Flag untuk inisialisasi history

// Variabel untuk tracking gate activity
bool gateInActive = false;        // Status gate masuk aktif
bool gateOutActive = false;       // Status gate keluar aktif
unsigned long gateInStartTime = 0;
unsigned long gateOutStartTime = 0;

// Counter untuk ID unik setiap transaksi
int transactionCounter = 0;

// Fungsi untuk mendapatkan tanggal saat ini (YYYY-MM-DD)
String getCurrentDate() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "1970-01-01";
  }
  
  char dateString[32];
  strftime(dateString, sizeof(dateString), "%Y-%m-%d", &timeinfo);
  return String(dateString);
}

// Fungsi untuk mendapatkan waktu dalam format string
String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Failed to obtain time";
  }
  
  char timeString[64];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeString);
}

// Fungsi untuk mendapatkan epoch time
unsigned long getEpochTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return 0;
  }
  time(&now);
  return now;
}

// Fungsi untuk mencatat kendaraan masuk
void recordCarEntry() {
  transactionCounter++;
  String timestamp = getFormattedTime();
  unsigned long epochTime = getEpochTime();
  
  // Simpan ke Firebase dengan struktur terorganisir
  String entryPath = "/parkiran/history/" + currentDate + "/entries/" + String(transactionCounter);
  
  Firebase.setString(firebaseData, entryPath + "/timestamp", timestamp);
  Firebase.setInt(firebaseData, entryPath + "/epochTime", epochTime);
  Firebase.setString(firebaseData, entryPath + "/type", "ENTRY");
  Firebase.setInt(firebaseData, entryPath + "/availableSlots", tersedia);
  Firebase.setInt(firebaseData, entryPath + "/occupiedSlots", Total - tersedia);
  Firebase.setString(firebaseData, entryPath + "/id", "CAR_" + String(transactionCounter));
  
  // Update counter harian
  dailyParkingCount++;
  
  // Simpan juga ke realtime entries untuk monitoring
  String realtimeEntryPath = "/parkiran/realtime/lastEntry";
  Firebase.setString(firebaseData, realtimeEntryPath + "/timestamp", timestamp);
  Firebase.setString(firebaseData, realtimeEntryPath + "/type", "ENTRY");
  Firebase.setInt(firebaseData, realtimeEntryPath + "/id", transactionCounter);
  
  Serial.println("=== KENDARAAN MASUK (GATE TERBUKA) ===");
  Serial.print("ID: CAR_");
  Serial.println(transactionCounter);
  Serial.print("Waktu: ");
  Serial.println(timestamp);
  Serial.print("Slot tersedia saat masuk: ");
  Serial.println(tersedia);
  Serial.print("Total masuk hari ini: ");
  Serial.println(dailyParkingCount);
  Serial.println("* Slot tersedia akan berkurang saat kendaraan benar-benar parkir");
}

// Fungsi untuk mencatat kendaraan keluar
void recordCarExit() {
  transactionCounter++;
  String timestamp = getFormattedTime();
  unsigned long epochTime = getEpochTime();
  
  // Simpan ke Firebase dengan struktur terorganisir
  String exitPath = "/parkiran/history/" + currentDate + "/exits/" + String(transactionCounter);
  
  Firebase.setString(firebaseData, exitPath + "/timestamp", timestamp);
  Firebase.setInt(firebaseData, exitPath + "/epochTime", epochTime);
  Firebase.setString(firebaseData, exitPath + "/type", "EXIT");
  Firebase.setInt(firebaseData, exitPath + "/availableSlots", tersedia);
  Firebase.setInt(firebaseData, exitPath + "/occupiedSlots", Total - tersedia);
  Firebase.setString(firebaseData, exitPath + "/id", "CAR_" + String(transactionCounter));
  
  // Update counter harian
  dailyExitCount++;
  
  // Simpan juga ke realtime exits untuk monitoring
  String realtimeExitPath = "/parkiran/realtime/lastExit";
  Firebase.setString(firebaseData, realtimeExitPath + "/timestamp", timestamp);
  Firebase.setString(firebaseData, realtimeExitPath + "/type", "EXIT");
  Firebase.setInt(firebaseData, realtimeExitPath + "/id", transactionCounter);
  
  Serial.println("=== KENDARAAN KELUAR ===");
  Serial.print("ID: CAR_");
  Serial.println(transactionCounter);
  Serial.print("Waktu: ");
  Serial.println(timestamp);
  Serial.print("Slot tersedia: ");
  Serial.println(tersedia);
  Serial.print("Total keluar hari ini: ");
  Serial.println(dailyExitCount);
}

// Fungsi untuk mencatat perubahan status slot individual
void recordSlotChange(int slotNumber, bool isOccupied, String action) {
  String timestamp = getFormattedTime();
  String slotPath = "/parkiran/history/" + currentDate + "/slots/slot_" + String(slotNumber);
  
  Firebase.setString(firebaseData, slotPath + "/lastUpdate", timestamp);
  Firebase.setBool(firebaseData, slotPath + "/occupied", isOccupied);
  Firebase.setString(firebaseData, slotPath + "/lastAction", action);
  
  Serial.print("Slot ");
  Serial.print(slotNumber);
  Serial.print(" - ");
  Serial.print(action);
  Serial.print(" pada ");
  Serial.println(timestamp);
}

// Fungsi untuk reset counter harian jika tanggal berubah
void checkDateChange() {
  String newDate = getCurrentDate();
  
  if (currentDate != newDate) {
    // Simpan summary data hari sebelumnya ke Firebase
    if (historyInitialized && currentDate != "") {
      String summaryPath = "/parkiran/history/" + currentDate + "/summary";
      Firebase.setInt(firebaseData, summaryPath + "/totalEntries", dailyParkingCount);
      Firebase.setInt(firebaseData, summaryPath + "/totalExits", dailyExitCount);
      Firebase.setInt(firebaseData, summaryPath + "/netChange", dailyParkingCount - dailyExitCount);
      Firebase.setString(firebaseData, summaryPath + "/date", currentDate);
      
      Serial.println("=== SUMMARY HARI SEBELUMNYA ===");
      Serial.print("Tanggal: ");
      Serial.println(currentDate);
      Serial.print("Total masuk: ");
      Serial.println(dailyParkingCount);
      Serial.print("Total keluar: ");
      Serial.println(dailyExitCount);
      Serial.print("Net change: ");
      Serial.println(dailyParkingCount - dailyExitCount);
    }
    
    // Reset counter untuk hari baru
    currentDate = newDate;
    dailyParkingCount = 0;
    dailyExitCount = 0;
    previousOccupiedSlots = 0;
    transactionCounter = 0;
    historyInitialized = true;
    
    Serial.println("=== HARI BARU ===");
    Serial.print("Tanggal: ");
    Serial.println(currentDate);
    Serial.println("Semua counter direset ke 0");
  }
}

// Fungsi untuk mendeteksi perubahan slot dan mencatat history
void detectSlotChanges() {
  // Deteksi perubahan slot 1
  if (slot1 != prevSlot1) {
    if (slot1) {
      recordSlotChange(1, true, "OCCUPIED");
    } else {
      recordSlotChange(1, false, "VACATED");
    }
    prevSlot1 = slot1;
  }
  
  // Deteksi perubahan slot 2
  if (slot2 != prevSlot2) {
    if (slot2) {
      recordSlotChange(2, true, "OCCUPIED");
    } else {
      recordSlotChange(2, false, "VACATED");
    }
    prevSlot2 = slot2;
  }
  
  // Deteksi perubahan slot 3
  if (slot3 != prevSlot3) {
    if (slot3) {
      recordSlotChange(3, true, "OCCUPIED");
    } else {
      recordSlotChange(3, false, "VACATED");
    }
    prevSlot3 = slot3;
  }
  
  // Deteksi perubahan slot 4
  if (slot4 != prevSlot4) {
    if (slot4) {
      recordSlotChange(4, true, "OCCUPIED");
    } else {
      recordSlotChange(4, false, "VACATED");
    }
    prevSlot4 = slot4;
  }
}

// Fungsi untuk menampilkan waktu di Serial Monitor
void printTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

// Fungsi pengecekan jarak kendaraan dari sensor ultrasoni
void checkavailable() {
  // HCSR04 1
  digitalWrite(trigPin1, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin1, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin1, LOW);
  duration = pulseIn(echoPin1, HIGH);
  distanceCm1 = duration * SOUND_SPEED / 2;
  
  // HCSR04 2
  digitalWrite(trigPin2, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin2, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin2, LOW);
  duration = pulseIn(echoPin2, HIGH);
  distanceCm2 = duration * SOUND_SPEED / 2;
  
  // HCSR04 3
  digitalWrite(trigPin3, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin3, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin3, LOW);
  duration = pulseIn(echoPin3, HIGH);
  distanceCm3 = duration * SOUND_SPEED / 2;
  
  // HCSR04 4
  digitalWrite(trigPin4, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin4, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin4, LOW);
  duration = pulseIn(echoPin4, HIGH);
  distanceCm4 = duration * SOUND_SPEED / 2;

  // SLOT 1 BAWAH
  if (distanceCm1 < treshold) {
    slot1 = true;
    p1 = 1;
  } else {
    slot1 = false;
    p1 = 0;
  }
  
  // SLOT 2 BAWAH
  if (distanceCm2 < treshold) {
    slot2 = true;
    p2 = 1;
  } else {
    slot2 = false;
    p2 = 0;
  }
  
  // SLOT 1 ATAS
  if (distanceCm3 < treshold) {
    slot3 = true;
    p3 = 1;
  } else {
    slot3 = false;
    p3 = 0;
  }
  
  // SLOT 2 ATAS
  if (distanceCm4 < treshold) {
    slot4 = true;
    p4 = 1;
  } else {
    slot4 = false;
    p4 = 0;
  }
}

// Fungsi menampilkan slot parkir yang kosong
void checkspot() {
  if (p1 == 0) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Silahkan Parkir di");
    lcd.setCursor(0, 1);
    lcd.print("SLOT 1");
    delay(3000);
    lcd.clear();
    return;
  }
  if (p2 == 0) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Silahkan Parkir di");
    lcd.setCursor(0, 1);
    lcd.print("SLOT 2");
    delay(3000);
    lcd.clear();
    return;
  }
  if (p3 == 0) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Silahkan Parkir di");
    lcd.setCursor(0, 1);
    lcd.print("SLOT 3");
    delay(3000);
    lcd.clear();
    return;
  }
  if (p4 == 0) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Silahkan Parkir di");
    lcd.setCursor(0, 1);
    lcd.print("SLOT 4");
    delay(3000);
    lcd.clear();
    return;
  }
}

void setup() {
  Serial.begin(115200);
  
  // Konfigurasi PIN
  pinMode(trigPin1, OUTPUT);
  pinMode(echoPin1, INPUT);
  pinMode(trigPin2, OUTPUT);
  pinMode(echoPin2, INPUT);
  pinMode(trigPin3, OUTPUT);
  pinMode(echoPin3, INPUT);
  pinMode(trigPin4, OUTPUT);
  pinMode(echoPin4, INPUT);
  pinMode(led1, OUTPUT);
  pinMode(led2, OUTPUT);
  pinMode(23, OUTPUT);
  pinMode(ir1, INPUT);
  pinMode(ir2, INPUT);
  pinMode(19, INPUT_PULLUP);
  pinMode(18, INPUT_PULLUP);
  digitalWrite(led1, LOW);
  digitalWrite(led2, LOW);
  
  // Koneksi WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Menghubungkan ke WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("\nTerhubung ke WiFi!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Inisialisasi dan sinkronisasi NTP
  Serial.println("Menginisialisasi NTP...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  // Tunggu hingga waktu tersinkronisasi
  Serial.print("Menunggu sinkronisasi NTP");
  while (!time(nullptr)) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("\nWaktu berhasil disinkronisasi!");
  printTime();

  // Konfigurasi Firebase
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;

  // Inisialisasi Firebase
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  // Setup servo awal (posisi tertutup)
  servo1.attach(servo1Pin);
  servo2.attach(servo2Pin);
  servo1.write(90);
  servo2.write(90);
  
  // Baca kondisi slot awal
  checkavailable();
  
  // Inisialisasi status slot sebelumnya
  prevSlot1 = slot1;
  prevSlot2 = slot2;
  prevSlot3 = slot3;
  prevSlot4 = slot4;
  
  // Inisialisasi history parkir harian
  currentDate = getCurrentDate();
  previousOccupiedSlots = p1 + p2 + p3 + p4;
  tersedia = Total - previousOccupiedSlots;
  historyInitialized = true;

  Serial.println("=== SISTEM HISTORY PARKIR DIINISIALISASI ===");
  Serial.print("Tanggal: ");
  Serial.println(currentDate);
  Serial.print("Slot terisi saat startup: ");
  Serial.println(previousOccupiedSlots);
  Serial.print("Slot tersedia: ");
  Serial.println(tersedia);
  
  // Simpan status awal ke Firebase
  String initPath = "/parkiran/history/" + currentDate + "/initialization";
  Firebase.setString(firebaseData, initPath + "/timestamp", getFormattedTime());
  Firebase.setInt(firebaseData, initPath + "/initialOccupiedSlots", previousOccupiedSlots);
  Firebase.setInt(firebaseData, initPath + "/availableSlots", tersedia);
  
  lcd.init();
  lcd.backlight();
  
  // Inisialisasi LCD
  lcd.setCursor(0, 0);
  lcd.print("     HELLO USER");
  lcd.setCursor(0, 1);
  lcd.print("YOUR DEVICE IS READY");
  delay(2000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("     TUGAS AKHIR");
  lcd.setCursor(0, 1);
  lcd.print("  SISTEM IoT PARKIR");
  delay(2000);
  lcd.clear();
}

void loop() {
  // Baca sensor dan tombol
  ir1 = analogRead(34);
  ir2 = analogRead(35);
  button1 = digitalRead(19);
  button2 = digitalRead(18);
  sig = digitalRead(23);
  
  // Deteksi kendaraan dari IR
  if (ir1 > 1000 && ir2 > 1000) {
    digitalWrite(23, HIGH);
  } else {
    digitalWrite(23, LOW);
  }
  
  Serial.println(sig);
  Serial.println(ir1);
  Serial.println(ir2);
  
  // Hitung slot tersedia
  tersedia = Total - p1 - p2 - p3 - p4;
  
  // Kontrol gerbang masuk (servo1)
  if (tersedia == 0) {
    servo1.write(90);  // Servo tetap pada posisi 90 derajat (slot penuh)
    if (gateInActive) {
      gateInActive = false;
      Serial.println("Gate masuk ditutup - Parkir penuh");
    }
  } else {
    // Jika ada slot tersedia
    if (sig == HIGH && button1 == LOW && !gateInActive) {
      // Kendaraan terdeteksi dan tombol ditekan, buka gate
      checkspot();
      servo1.write(0);
      gateInActive = true;
      gateInStartTime = millis();
      
      // Catat kendaraan masuk (tapi belum kurangi slot tersedia)
      recordCarEntry();
      
      Serial.println("Gate masuk dibuka");
    }
    
    // Tutup gate setelah delay atau tombol dilepas
    if (gateInActive && (sig == LOW || button1 == HIGH || millis() - gateInStartTime > 10000)) {
      servo1.write(90);
      gateInActive = false;
      Serial.println("Gate masuk ditutup");
    }
  }
  
  // Kontrol gerbang keluar (servo2)
  if (button2 == LOW && tersedia != Total && !gateOutActive) {
    servo2.write(180);
    gateOutActive = true;
    gateOutStartTime = millis();
    
    // Catat kendaraan keluar
    recordCarExit();

    
    Serial.println("Gate keluar dibuka");
  }
  
  // Tutup gate keluar setelah delay
  if (gateOutActive && millis() - gateOutStartTime > 7000) {
    servo2.write(90);
    gateOutActive = false;
    Serial.println("Gate keluar ditutup");
  }

  // LED indikator: merah jika penuh, hijau jika tersedia
  if (tersedia == 0) {
    digitalWrite(led2, HIGH);
    digitalWrite(led1, LOW);
  } else {
    digitalWrite(led2, LOW);
    digitalWrite(led1, HIGH);
  }
  
  // Cek sensor dan deteksi perubahan slot
  checkavailable();
  detectSlotChanges();
  
  // Periksa perubahan tanggal
  checkDateChange();
  
  // Tampilkan status slot di LCD
  lcd.setCursor(0, 0);
  lcd.print("SLOT 1: ");
  lcd.print(slot1 ? "terisi" : "kosong");
  
  lcd.setCursor(0, 1);
  lcd.print("SLOT 2: ");
  lcd.print(slot2 ? "terisi" : "kosong");
  
  lcd.setCursor(0, 2);
  lcd.print("SLOT 3: ");
  lcd.print(slot3 ? "terisi" : "kosong");
  
  lcd.setCursor(0, 3);
  lcd.print("SLOT 4: ");
  lcd.print(slot4 ? "terisi" : "kosong");

  // Kirim data ke Firebase setiap 2 detik
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    
    String currentTime = getFormattedTime();
    
    // Kirim status slot
    Firebase.setBool(firebaseData, "/parkiran/slot_1", slot1);
    Firebase.setBool(firebaseData, "/parkiran/slot_2", slot2);
    Firebase.setBool(firebaseData, "/parkiran/slot_3", slot3);
    Firebase.setBool(firebaseData, "/parkiran/slot_4", slot4);
    
    // Kirim data umum
    Firebase.setInt(firebaseData, "/parkiran/tersedia", tersedia);
    Firebase.setString(firebaseData, "/parkiran/lastUpdate", currentTime);
    Firebase.setString(firebaseData, "/parkiran/currentDate", currentDate);
    
    // Kirim data history harian
    Firebase.setInt(firebaseData, "/parkiran/todayEntries", dailyParkingCount);
    Firebase.setInt(firebaseData, "/parkiran/todayExits", dailyExitCount);
    Firebase.setInt(firebaseData, "/parkiran/todayNetChange", dailyParkingCount - dailyExitCount);
    
    // Kirim status gate
    Firebase.setBool(firebaseData, "/parkiran/gateInActive", gateInActive);
    Firebase.setBool(firebaseData, "/parkiran/gateOutActive", gateOutActive);
    
    Serial.println("Data berhasil dikirim ke Firebase");
    Serial.print("Tersedia: ");
    Serial.print(tersedia);
    Serial.print(" | Masuk: ");
    Serial.print(dailyParkingCount);
    Serial.print(" | Keluar: ");
    Serial.println(dailyExitCount);
  }
}