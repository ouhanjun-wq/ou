// ESP-NOW radio link (broadcast, fixed channel, no pairing).
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "shared.h"

static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static int lastCmdSeq = -1, lastParamSeq = -1;
static uint8_t telemSeq = 0;

static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  (void)info;
  if (len < (int)sizeof(proto::Header)) return;
  const uint8_t net = (uint8_t)P.net_id;

  switch (data[2]) {
    case proto::PKT_CONTROL: {
      proto::ControlPacket c;
      if (!proto::open(data, len, proto::PKT_CONTROL, net, c)) return;
      portENTER_CRITICAL(&gMux);
      gRadioIn.thr = constrain(c.thr, 0, 1000) / 1000.0f;
      gRadioIn.roll = constrain(c.roll, -500, 500) / 500.0f;
      gRadioIn.pitch = constrain(c.pitch, -500, 500) / 500.0f;
      gRadioIn.yaw = constrain(c.yaw, -500, 500) / 500.0f;
      gRadioIn.mode = c.mode;
      gRadioIn.armReq = c.armed != 0;
      gLastRadioMs = millis();
      portEXIT_CRITICAL(&gMux);
      gRadioPktCount = gRadioPktCount + 1;
      break;
    }
    case proto::PKT_COMMAND: {
      proto::CommandPacket c;
      if (!proto::open(data, len, proto::PKT_COMMAND, net, c)) return;
      if (c.h.seq == lastCmdSeq) return;  // ground station repeats each command 3x
      lastCmdSeq = c.h.seq;
      if (c.cmd == proto::CMD_TURN) {
        portENTER_CRITICAL(&gMux);
        gPendingTurn += c.arg;
        portEXIT_CRITICAL(&gMux);
      } else if (c.cmd == proto::CMD_CALIB_GYRO) {
        gCalibRequest = true;
      } else if (c.cmd == proto::CMD_SAVE) {
        gSaveRequest = true;
      }
      break;
    }
    case proto::PKT_PARAM: {
      proto::ParamPacket pp;
      if (!proto::open(data, len, proto::PKT_PARAM, net, pp)) return;
      if (pp.h.seq == lastParamSeq) return;
      lastParamSeq = pp.h.seq;
      pp.name[sizeof(pp.name) - 1] = '\0';
      paramSet(pp.name, pp.value);
      break;
    }
    default:
      break;
  }
}

bool linkBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(proto::WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(onRecv);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, kBroadcast, 6);
  peer.channel = proto::WIFI_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

void linkSendTelemetry(proto::TelemetryPacket& t) {
  proto::seal(t, proto::PKT_TELEMETRY, (uint8_t)P.net_id, telemSeq++);
  esp_now_send(kBroadcast, reinterpret_cast<const uint8_t*>(&t), sizeof(t));
}
