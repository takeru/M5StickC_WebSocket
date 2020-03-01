#include <Arduino.h>
#include <M5StickC.h>
#ifdef ESP32
#include <WiFi.h>
#include <AsyncTCP.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#define LED_BUILTIN 10

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const char* PARAM_MESSAGE = "message";

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

void handleJsonRequest(JsonObject& req, JsonObject& resp)
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

#define os_printf printf
void onEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  if(type == WS_EVT_CONNECT){
    //client connected
    os_printf("ws[%s][%u] connect\n", server->url(), client->id());

    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    root["msg"] = "Hello Client :)";
    root["id"] = client->id();
    size_t len = root.measureLength();
    AsyncWebSocketMessageBuffer * buffer = ws.makeBuffer(len);
    root.printTo((char *)buffer->get(), len + 1);
    ws.textAll(buffer);

    client->ping();
  } else if(type == WS_EVT_DISCONNECT){
    //client disconnected
    os_printf("ws[%s][%u] disconnect: %u\n", server->url(), client->id(), client->id());
  } else if(type == WS_EVT_ERROR){
    //error was received from the other end
    os_printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if(type == WS_EVT_PONG){
    //pong message was received (in response to a ping request maybe)
    os_printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
  } else if(type == WS_EVT_DATA){
    //data packet
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    if(info->final && info->index == 0 && info->len == len){
      //the whole message is in a single frame and we got all of it's data
      os_printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", info->len);
      if(info->opcode == WS_TEXT){
        data[len] = 0; // IS IT SAFE??
        os_printf("%s\n", (char*)data);

        DynamicJsonBuffer reqBuffer;
        JsonObject& req = reqBuffer.parseObject(data);
        DynamicJsonBuffer respBuffer;
        JsonObject& resp = respBuffer.createObject();
        handleJsonRequest(req, resp);
        size_t len = resp.measureLength();
        AsyncWebSocketMessageBuffer * buffer = ws.makeBuffer(len);
        resp.printTo((char *)buffer->get(), len + 1);
        ws.textAll(buffer);
      }
    } else {
      //message is comprised of multiple frames or the frame is split into multiple packets
      os_printf("framed message: info->final=%d info->index=%lld info->len=%llu",
        info->final, info->index, info->len);
    }
  }
}

#include "index_html.h"

void setup() {
  M5.begin();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    printf("WiFi Failed!\n");
    return;
  }

  printf("IP Address: %s\n", WiFi.localIP().toString().c_str());

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
}

void loop() {
}
