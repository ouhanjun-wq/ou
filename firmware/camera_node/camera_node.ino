// Optional camera node for the butterfly: Seeed XIAO ESP32S3 Sense (OV2640 / OV3660).
// Board: XIAO_ESP32S3, "PSRAM: OPI PSRAM" enabled, "USB CDC On Boot: Enabled".
//
// Runs on its own board so video never competes with the flight controller for CPU.
// Joins the ground station's Wi-Fi ("Butterfly-GS") with a fixed address and serves
//   http://192.168.4.50:81/stream   MJPEG video (shown on the phone dashboard)
//   http://192.168.4.50:81/capture  single JPEG
// Low resolution (QVGA) keeps the 2.4 GHz channel free for the flight control link.
#include <WiFi.h>

#include "esp_camera.h"
#include "esp_http_server.h"

constexpr const char* AP_SSID = "Butterfly-GS";   // must match ground_station.ino
constexpr const char* AP_PASS = "butterfly123";
const IPAddress CAM_IP(192, 168, 4, 50), GATEWAY(192, 168, 4, 1), SUBNET(255, 255, 255, 0);

constexpr framesize_t FRAME_SIZE = FRAMESIZE_QVGA;  // 320 x 240
constexpr int JPEG_QUALITY = 14;                    // 10 (best) .. 63 (smallest)
constexpr uint32_t MIN_FRAME_MS = 80;               // cap at ~12 fps

// Camera pins of the XIAO ESP32S3 Sense expansion board
constexpr int XCLK_GPIO = 10, SIOD_GPIO = 40, SIOC_GPIO = 39;
constexpr int Y9_GPIO = 48, Y8_GPIO = 11, Y7_GPIO = 12, Y6_GPIO = 14;
constexpr int Y5_GPIO = 16, Y4_GPIO = 18, Y3_GPIO = 17, Y2_GPIO = 15;
constexpr int VSYNC_GPIO = 38, HREF_GPIO = 47, PCLK_GPIO = 13;

static httpd_handle_t server = nullptr;

static esp_err_t streamHandler(httpd_req_t* req) {
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  char part[80];
  esp_err_t res = ESP_OK;
  uint32_t last = 0;
  while (res == ESP_OK) {
    const uint32_t wait = millis() - last;
    if (wait < MIN_FRAME_MS) delay(MIN_FRAME_MS - wait);
    last = millis();
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) return ESP_FAIL;
    const int n = snprintf(part, sizeof(part),
                           "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", (unsigned)fb->len);
    res = httpd_resp_send_chunk(req, part, n);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, "\r\n", 2);
    esp_camera_fb_return(fb);
  }
  return res;
}

static esp_err_t captureHandler(httpd_req_t* req) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { httpd_resp_send_500(req); return ESP_FAIL; }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  const esp_err_t res = httpd_resp_send(req, (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return res;
}

static bool cameraBegin() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO; c.pin_d1 = Y3_GPIO; c.pin_d2 = Y4_GPIO; c.pin_d3 = Y5_GPIO;
  c.pin_d4 = Y6_GPIO; c.pin_d5 = Y7_GPIO; c.pin_d6 = Y8_GPIO; c.pin_d7 = Y9_GPIO;
  c.pin_xclk = XCLK_GPIO;
  c.pin_pclk = PCLK_GPIO;
  c.pin_vsync = VSYNC_GPIO;
  c.pin_href = HREF_GPIO;
  c.pin_sccb_sda = SIOD_GPIO;
  c.pin_sccb_scl = SIOC_GPIO;
  c.pin_pwdn = -1;
  c.pin_reset = -1;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAME_SIZE;
  c.jpeg_quality = JPEG_QUALITY;
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;
  return esp_camera_init(&c) == ESP_OK;
}

static void serverBegin() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = 81;
  cfg.ctrl_port = 32769;
  if (httpd_start(&server, &cfg) != ESP_OK) { Serial.println("HTTP server FAILED"); return; }
  httpd_uri_t stream = {};
  stream.uri = "/stream";
  stream.method = HTTP_GET;
  stream.handler = streamHandler;
  httpd_register_uri_handler(server, &stream);
  httpd_uri_t capture = {};
  capture.uri = "/capture";
  capture.method = HTTP_GET;
  capture.handler = captureHandler;
  httpd_register_uri_handler(server, &capture);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  if (!psramFound()) Serial.println("PSRAM not found: enable \"OPI PSRAM\" in the Tools menu");
  Serial.println(cameraBegin() ? "camera OK" : "camera init FAILED (check the Sense board connector)");

  WiFi.mode(WIFI_STA);
  WiFi.config(CAM_IP, GATEWAY, SUBNET);
  WiFi.setSleep(false);
  WiFi.begin(AP_SSID, AP_PASS);
  serverBegin();
  Serial.printf("joining \"%s\" as %s ...\n", AP_SSID, CAM_IP.toString().c_str());
}

void loop() {
  static uint32_t lastCheck = 0, lastAttempt = 0;
  static bool wasConnected = false;
  const uint32_t now = millis();
  if (now - lastCheck > 1000) {
    lastCheck = now;
    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected != wasConnected) {
      Serial.println(connected ? "Wi-Fi connected, stream at http://192.168.4.50:81/stream" : "Wi-Fi lost, retrying");
      wasConnected = connected;
      lastAttempt = now;
    }
    if (!connected && now - lastAttempt > 8000) {   // give each attempt time to finish
      WiFi.reconnect();
      lastAttempt = now;
    }
  }
  delay(10);
}
