#include <Arduino.h>
#include <M5StickC.h>
#ifdef ESP32
#include <WiFi.h>
#include <WiFiMulti.h>
#include <AsyncTCP.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "secret.h"

#define LED_BUILTIN 10

WiFiMulti wifiMulti;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const char* PARAM_MESSAGE = "message";

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

void handleJsonRequest(JsonDocument& req, JsonDocument& resp)
{
  resp["result"] = "";
  if(req["led"]=="on"){
    digitalWrite(LED_BUILTIN, LOW);
    resp["led"] = digitalRead(LED_BUILTIN);
    resp["result"] = "OK";
  }
  if(req["led"]=="off"){
    digitalWrite(LED_BUILTIN, HIGH);
    resp["led"] = digitalRead(LED_BUILTIN);
    resp["result"] = "OK";
  }
  if(req["led"]=="toggle"){
    int led = digitalRead(LED_BUILTIN);
    digitalWrite(LED_BUILTIN, led==HIGH ? LOW : HIGH);
    resp["led"] = digitalRead(LED_BUILTIN);
    resp["result"] = "OK";
  }
}

void onEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  if(type == WS_EVT_CONNECT){
    //client connected
    printf("ws[%s][%u] connect\n", server->url(), client->id());

    StaticJsonDocument<JSON_OBJECT_SIZE(2)> doc;
    doc["msg"] = "hello";
    doc["id"] = client->id();
    size_t len = measureJson(doc);
    AsyncWebSocketMessageBuffer * buffer = ws.makeBuffer(len);
    serializeJson(doc, (char*)buffer->get(), len+1);
    client->text(buffer);

    client->ping();
  } else if(type == WS_EVT_DISCONNECT){
    //client disconnected
    printf("ws[%s][%u] disconnect: %u\n", server->url(), client->id(), client->id());
  } else if(type == WS_EVT_ERROR){
    //error was received from the other end
    printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if(type == WS_EVT_PONG){
    //pong message was received (in response to a ping request maybe)
    printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
  } else if(type == WS_EVT_DATA){
    //data packet
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    if(info->final && info->index == 0 && info->len == len){
      //the whole message is in a single frame and we got all of it's data
      printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", info->len);
      if(info->opcode == WS_TEXT){
        data[len] = 0; // IS IT SAFE??
        printf("%s\n", (char*)data);

        StaticJsonDocument<JSON_OBJECT_SIZE(10)> req;
        DeserializationError error = deserializeJson(req, data);
        if(error){
          printf("ws[%s][%u] DeserializationError %s\n", server->url(), client->id(), error.c_str());
        }else{
          StaticJsonDocument<JSON_OBJECT_SIZE(10)> resp;
          handleJsonRequest(req, resp);
          size_t len = measureJson(resp);
          AsyncWebSocketMessageBuffer * buffer = ws.makeBuffer(len);
          serializeJson(resp, (char*)buffer->get(), len+1);
          client->text(buffer);
        }
      }
    } else {
      //message is comprised of multiple frames or the frame is split into multiple packets
      printf("framed message: info->final=%d info->index=%lld info->len=%llu",
        info->final, info->index, info->len);
    }
  }
}

String rtc_string()
{
  RTC_TimeTypeDef time;
  RTC_DateTypeDef date;
  M5.Rtc.GetTime(&time);
  M5.Rtc.GetData(&date);
  char s[20];
  sprintf(s, "%04d-%02d-%02d %02d:%02d:%02d",
    date.Year, date.Month, date.Date,
    time.Hours, time.Minutes, time.Seconds
  );
  return String(s);
}

void update_display()
{
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setRotation(3);
  M5.Lcd.setTextFont(2);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(WHITE, BLACK);

  M5.Lcd.setCursor(1, 1);

  M5.Lcd.printf("ssid=%s\n", WiFi.SSID().c_str());
  M5.Lcd.printf("localIP=%s\n", WiFi.localIP().toString().c_str());
  M5.Lcd.printf("%lu\n", millis()/1000);
}

void textAllWriteAvailable(AsyncWebSocketMessageBuffer *buffer)
{
  for(const auto& c: ws.getClients()){
    if(!c->queueIsFull()){
      c->text(buffer);
    }else{
      printf("ws[%s][%u] queueIsFull\n", ws.url(), c->id());
    }
  }
}

#include "index_html.h"

void setup() {
  WIFI_MULTI_ADDAPS();
  M5.begin();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  while(1){
    if(wifiMulti.run()==WL_CONNECTED){
      break;
    }
    delay(1000);
  }

  printf("ssid=%s localIP=%s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());

  ws.onEvent(onEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // Send a GET request to <IP>/get?message=<message>
  server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String message;
    if (request->hasParam(PARAM_MESSAGE)) {
      message = request->getParam(PARAM_MESSAGE)->value();
    } else {
      message = "No message sent";
    }
    request->send(200, "text/plain", "Hello, GET: " + message);
  });

  server.onNotFound(notFound);

  server.begin();

  M5.IMU.Init();
}

void loop()
{
  unsigned long ms  = millis();
  unsigned long sec = ms / 1000;
  static unsigned long prev_ms         = 0;
  static unsigned long prev_sec        = 0;
  static unsigned long loop_count      = 0;
  static unsigned long prev_loop_count = 0;
  if(prev_sec < sec){
    ws.cleanupClients();

    StaticJsonDocument<JSON_OBJECT_SIZE(10)> root;
    root["rtc"] = rtc_string();
    root["loop/sec"] = int(1000*(loop_count-prev_loop_count)/(ms-prev_ms));
    size_t len = measureJson(root);
    AsyncWebSocketMessageBuffer * buffer = ws.makeBuffer(len);
    serializeJson(root, (char*)buffer->get(), len+1);
    textAllWriteAvailable(buffer);

    String pingData = String(ms);
    ws.pingAll((uint8_t*)pingData.c_str(), strlen(pingData.c_str())+1);

    prev_ms = ms;
    prev_sec = sec;
    prev_loop_count = loop_count;

    update_display();
  }

  static unsigned long prev_imu_ms = 0;
  if(0<ws.count() && prev_imu_ms+100<ms){
    static float accX  = 0.0F;
    static float accY  = 0.0F;
    static float accZ  = 0.0F;
    static float gyroX = 0.0F;
    static float gyroY = 0.0F;
    static float gyroZ = 0.0F;
    static float pitch = 0.0F;
    static float roll  = 0.0F;
    static float yaw   = 0.0F;
    M5.IMU.getAccelData(&accX, &accY, &accZ);
    M5.IMU.getGyroData(&gyroX, &gyroY, &gyroZ);
    M5.IMU.getAhrsData(&pitch, &roll, &yaw);

    StaticJsonDocument<JSON_OBJECT_SIZE(13)> root;
    root["ms"] = ms;
    JsonObject acc = root.createNestedObject("acc");
    acc["x"] = accX;
    acc["y"] = accY;
    acc["z"] = accZ;
    JsonObject gyro = root.createNestedObject("gyro");
    gyro["x"] = gyroX;
    gyro["y"] = gyroY;
    gyro["z"] = gyroZ;
    JsonObject ahrs = root.createNestedObject("ahrs");
    ahrs["pitch"] = pitch;
    ahrs["roll"]  = roll;
    ahrs["yaw"]   = yaw;
    size_t len = measureJson(root);
    AsyncWebSocketMessageBuffer * buffer = ws.makeBuffer(len);
    serializeJson(root, (char*)buffer->get(), len+1);
    textAllWriteAvailable(buffer);
    prev_imu_ms = ms;
  }

  loop_count++;
}
