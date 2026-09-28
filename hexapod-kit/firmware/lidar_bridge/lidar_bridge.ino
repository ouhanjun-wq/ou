// M1C1-Mini lidar -> Wi-Fi bridge on a Seeed XIAO ESP32S3.
//
// The XIAO does not decode anything: it passes the lidar's serial bytes both ways over one TCP
// connection (port 3333). The PC-side ROS 2 node (hexapod-kit/ros2/m1c1_lidar) connects, sends the
// start command and turns the packets into /scan. When the PC disconnects the lidar is stopped.
//
// Board: "esp32" by Espressif (official package), board "XIAO_ESP32S3".
// Wiring (lidar -> XIAO):  5V -> 5V,  GND -> GND,  TX -> D7 (RX),  RX -> D6 (TX).
//   The XIAO's pins are 3.3 V only: measure the lidar TX line first (see docs/lidar-mapping.md).
//
// USB serial 115200 (Arduino serial monitor, "Newline"):
//   wifi <ssid> <password>   join your router (saved in flash, then restarts)
//   wifi off                 forget it: the XIAO opens its own hotspot "hexapod-lidar"
//   status                   IP address, client, bytes forwarded
//   start / stop             spin the lidar up / down by hand (test without the PC)
#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

static const uint16_t kPort = 3333;
static const char* kApSsid = "hexapod-lidar";
static const char* kApPass = "hexapod123";      // at least 8 characters
static const uint32_t kLidarBaud = 115200;
static const uint8_t kStart[] = {0xAA, 0x55, 0xF0, 0x0F};
static const uint8_t kStop[] = {0xAA, 0x55, 0xF5, 0x0A};

#ifdef LED_BUILTIN
static const int kLed = LED_BUILTIN;            // XIAO ESP32S3: yellow user LED, active low
#else
static const int kLed = -1;
#endif

static HardwareSerial& lidar = Serial1;
static WiFiServer server(kPort);
static WiFiClient client;
static bool haveClient = false;               // WiFiClient's bool() means "connected", so track it ourselves
static String ssid, pass;
static bool apMode = false;
static uint32_t bytesToPc = 0, bytesToLidar = 0, lastDataMs = 0;

static void led(bool on) {
  if (kLed >= 0) digitalWrite(kLed, on ? LOW : HIGH);
}

static void lidarCmd(const uint8_t* cmd, size_t n) {
  lidar.write(cmd, n);
  lidar.flush();
}

static String ipString() {
  return apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}

static void startWifi() {
  Preferences prefs;
  prefs.begin("lidar", true);
  ssid = prefs.getString("ssid", "");
  pass = prefs.getString("pass", "");
  prefs.end();

  WiFi.persistent(false);
  if (ssid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.printf("joining Wi-Fi \"%s\"", ssid.c_str());
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; ++i) {
      delay(250);
      Serial.print('.');
    }
    Serial.println();
  }
  if (WiFi.status() == WL_CONNECTED) {
    apMode = false;
  } else {
    if (ssid.length()) Serial.println("could not join - opening the hotspot instead");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApSsid, kApPass);
    apMode = true;
  }
  WiFi.setSleep(false);                         // lower latency, steadier stream
  MDNS.begin("hexapod-lidar");
  server.begin();
  server.setNoDelay(true);
}

static void printStatus() {
  if (apMode)
    Serial.printf("hotspot \"%s\" (password %s), IP %s, port %u\n", kApSsid, kApPass, ipString().c_str(), kPort);
  else
    Serial.printf("Wi-Fi \"%s\", IP %s, port %u  (use this IP as host:=...)\n", ssid.c_str(), ipString().c_str(), kPort);
  Serial.printf("client: %s | to PC %lu bytes, to lidar %lu bytes\n",
                haveClient ? client.remoteIP().toString().c_str() : "none",
                (unsigned long)bytesToPc, (unsigned long)bytesToLidar);
}

static void handleLine(String line) {
  line.trim();
  if (line.startsWith("wifi ")) {
    String rest = line.substring(5);
    rest.trim();
    Preferences prefs;
    prefs.begin("lidar", false);
    if (rest == "off") {
      prefs.remove("ssid");
      prefs.remove("pass");
      Serial.println("Wi-Fi forgotten, restarting into hotspot mode");
    } else {
      const int sp = rest.indexOf(' ');
      prefs.putString("ssid", sp < 0 ? rest : rest.substring(0, sp));
      prefs.putString("pass", sp < 0 ? "" : rest.substring(sp + 1));
      Serial.println("saved, restarting");
    }
    prefs.end();
    delay(300);
    ESP.restart();
  } else if (line == "status") {
    printStatus();
  } else if (line == "start") {
    lidarCmd(kStart, sizeof kStart);
    Serial.println("lidar start sent");
  } else if (line == "stop") {
    lidarCmd(kStop, sizeof kStop);
    Serial.println("lidar stop sent");
  } else if (line.length()) {
    Serial.println("commands: wifi <ssid> <password> | wifi off | status | start | stop");
  }
}

static void pollUsb() {
  static String buf;
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (buf.length()) handleLine(buf);
      buf = "";
    } else if (buf.length() < 120) {
      buf += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  if (kLed >= 0) pinMode(kLed, OUTPUT);
  led(false);
  lidar.setRxBufferSize(4096);
  lidar.begin(kLidarBaud, SERIAL_8N1, D7, D6);  // RX = D7, TX = D6
  delay(500);
  lidarCmd(kStop, sizeof kStop);                // quiet until a PC connects
  startWifi();
  Serial.println("\n=== M1C1 lidar bridge ===");
  printStatus();
}

void loop() {
  pollUsb();

  // One PC at a time: a new connection replaces the old one.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  WiFiClient incoming = server.accept();
#else
  WiFiClient incoming = server.available();
#endif
  if (incoming) {
    if (haveClient) client.stop();
    client = incoming;
    client.setNoDelay(true);
    haveClient = true;
    Serial.printf("PC connected: %s\n", client.remoteIP().toString().c_str());
  }
  if (haveClient && !client.connected()) {
    client.stop();
    haveClient = false;
    lidarCmd(kStop, sizeof kStop);
    Serial.println("PC disconnected - lidar stopped");
  }

  uint8_t buf[512];
  size_t n = lidar.available();
  if (n) {
    n = lidar.readBytes(buf, n > sizeof buf ? sizeof buf : n);
    if (haveClient) {
      client.write(buf, n);
      bytesToPc += n;
    }
    lastDataMs = millis();
  }
  if (haveClient) {
    int m = client.available();
    if (m > 0) {
      m = client.read(buf, m > (int)sizeof buf ? sizeof buf : m);
      if (m > 0) {
        lidar.write(buf, m);
        bytesToLidar += m;
      }
    }
  }

  // LED: on while lidar data flows, slow blink while waiting for a PC.
  const uint32_t now = millis();
  if (now - lastDataMs < 200) led(true);
  else led((now / 500) % 2);

  static uint32_t lastReport = 0;
  if (!haveClient && now - lastReport > 5000) {
    lastReport = now;
    Serial.printf("waiting for the PC on %s:%u\n", ipString().c_str(), kPort);
  }
  if (!n) delay(1);
}
