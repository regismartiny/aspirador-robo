#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESPAsyncTCP.h>
#elif defined(ESP32)
  #include <WiFi.h>
  #include <AsyncTCP.h>
#endif

#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>
#define ESPALEXA_ASYNC //it is important to define this before #include <Espalexa.h>!
#include "Espalexa.h"
#include "credentials.h"

#define MQTT_HOST IPAddress(192, 168, 0, 200)
#define MQTT_PORT 1883

#define PORT_ASPIRADOR D7

const char* HOSTNAME = "aspirador-robo";

const char* ID_ASPIRADOR = "ASPIRADOR ROBO";

const char* TOPIC_SEND   = "aspirador-robo/out";
const char* TOPIC_CMD    = "aspirador-robo/in/cmd";

const char* CMD_TURN_ON  = "TURN_ON";
const char* CMD_TURN_OFF = "TURN_OFF";


Espalexa espalexa;

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

AsyncWebServer server(80);

unsigned long ota_progress_millis = 0;
boolean ligarAspiradorRobo = false;
boolean desligarAspiradorRobo = false;

void initElegantOTA();
void initAlexa();
void onAspiradorRoboChange(uint8_t brightness);




void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Hostname: "); 
  Serial.println(WiFi.getHostname()); 
}

void connectToMqtt() {
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  Serial.println("Connected to Wi-Fi.");
  Serial.println("IP: " + WiFi.localIP().toString());
  WiFi.setHostname(HOSTNAME);
  connectToMqtt();
  initElegantOTA();
  initAlexa();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  Serial.println("Disconnected from Wi-Fi.");
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connectToWifi);
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);

  mqttClient.subscribe(TOPIC_CMD, 0);

  uint16_t packetIdSub = mqttClient.subscribe("test/lol", 2);
  Serial.print("Subscribing at QoS 2, packetId: ");
  Serial.println(packetIdSub);
  mqttClient.publish("test/lol", 0, true, "test 1");
  Serial.println("Publishing at QoS 0");
  uint16_t packetIdPub1 = mqttClient.publish("test/lol", 1, true, "test 2");
  Serial.print("Publishing at QoS 1, packetId: ");
  Serial.println(packetIdPub1);
  uint16_t packetIdPub2 = mqttClient.publish("test/lol", 2, true, "test 3");
  Serial.print("Publishing at QoS 2, packetId: ");
  Serial.println(packetIdPub2);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");

  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
  Serial.print("  qos: ");
  Serial.println(qos);
}

void onMqttUnsubscribe(uint16_t packetId) {
  Serial.println("Unsubscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void interpretCommand(const char* command) {
  Serial.println("command: ");
  Serial.println(command);

  if (strcmp(command, CMD_TURN_ON) == 0) {
    Serial.println("Turning on...");
    ligarAspiradorRobo = true;
  }
  else if (strcmp(command, CMD_TURN_OFF) == 0) {
    Serial.println("Turning off...");
    desligarAspiradorRobo = true;
  }
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  Serial.println("Publish received.");
  Serial.print("  topic: ");
  Serial.println(topic);
  Serial.print("  qos: ");
  Serial.println(properties.qos);
  Serial.print("  dup: ");
  Serial.println(properties.dup);
  Serial.print("  retain: ");
  Serial.println(properties.retain);
  Serial.print("  len: ");
  Serial.println(len);
  Serial.print("  index: ");
  Serial.println(index);
  Serial.print("  total: ");
  Serial.println(total);


  if (strcmp(topic, TOPIC_CMD) == 0) {
    Serial.println("Command message received");

    // convert raw payload into null terminated string
    char dest[len + 1] = { 0 };
    strncpy(dest, payload, len);

    interpretCommand(dest);
  }
}

void onMqttPublish(uint16_t packetId) {
  Serial.println("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void configureMqtt() {
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCredentials(MQTT_USER, MQTT_PASSWORD);
}

void onOTAStart() {
  Serial.println("OTA update started!");
}

void onOTAProgress(size_t current, size_t final) {
  // Log every 1 second
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
  }
}

void onOTAEnd(bool success) {
  if (success) {
    Serial.println("OTA update finished successfully!");
  } else {
    Serial.println("There was an error during OTA update!");
  }
}

void initElegantOTA() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", "<html><head><script>setTimeout(function(){window.location.replace('/update');}, 3000)</script></head>You will be redirected to OTA Update interface in 3s.</html>");
  });

  ElegantOTA.begin(&server);    // Start ElegantOTA
  // ElegantOTA callbacks
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);

  server.begin();
  Serial.println("HTTP server started");
}

void initAlexa() {
  server.onNotFound([](AsyncWebServerRequest *request){
    if (!espalexa.handleAlexaApiCall(request)) //if you don't know the URI, ask espalexa whether it is an Alexa control request
    {
      request->send(404, "text/plain", "Not found");
    }
  });

  // Define your devices here.
  espalexa.addDevice(ID_ASPIRADOR, onAspiradorRoboChange); //simplest definition, default state off

  espalexa.begin(&server); //give espalexa a pointer to your server object so it can use your server instead of creating its own
}

//our callback functions
void onAspiradorRoboChange(uint8_t brightness) {
    
    if (brightness == 255) {
      interpretCommand(CMD_TURN_ON);
    }
    else if (brightness == 0) {
      interpretCommand(CMD_TURN_OFF);
    }
}

void setup(void) {
  Serial.begin(9600);

  digitalWrite(PORT_ASPIRADOR, HIGH);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PORT_ASPIRADOR, OUTPUT);
  delay(100);

  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  configureMqtt();

  connectToWifi();
}

void ligarDesligar() {
  digitalWrite(PORT_ASPIRADOR, LOW);
  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
  digitalWrite(PORT_ASPIRADOR, HIGH);
  digitalWrite(LED_BUILTIN, HIGH);
}

void loop(void) {
  ElegantOTA.loop();
  espalexa.loop();
  delay(1);

  if (ligarAspiradorRobo) {
    ligarDesligar();
    ligarAspiradorRobo = false;
  }
  if (desligarAspiradorRobo) {
    ligarDesligar();
    desligarAspiradorRobo = false;
  }
}