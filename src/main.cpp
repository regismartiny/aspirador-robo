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
#include "enums.h"

#define HOSTNAME        "aspirador-robo"

#define ELEGANTOTA_HTML "<html><head><script>setTimeout(function(){window.location.replace('/update');}, 3000)</script></head>You will be redirected to OTA Update interface in 3s.</html>"

#define MQTT_HOST       IPAddress(192, 168, 0, 200)
#define MQTT_PORT       1883
#define TOPIC_SEND      "aspirador-robo/out"
#define TOPIC_CMD       "aspirador-robo/in/cmd"

#define PORT_ASPIRADOR  D7 //digital port connected do relay that cycles the power state

#define ID_ALEXA        "ASPIRADOR ROBO"

#define CMD_TURN_ON     "TURN_ON"
#define CMD_TURN_OFF    "TURN_OFF"

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

AsyncWebServer server(80);

Espalexa espalexa;

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

unsigned long ota_progress_millis = 0;
boolean flagTurnDeviceOn = false;
boolean flagTurnDeviceOff = false;

void initElegantOTA();
void initAlexa();
void onAlexaAspiradorRoboChange(uint8_t brightness);


char* concat(const char *s1, const char *s2)
{
    const size_t len1 = strlen(s1);
    const size_t len2 = strlen(s2);
    char *result = (char*)malloc(len1 + len2 + 1); // +1 for the null-terminator
    memcpy(result, s1, len1);
    memcpy(result + len1, s2, len2 + 1); // +1 to copy the null-terminator
    return result;
}

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

  uint16_t packetIdPub0 = mqttClient.publish(TOPIC_SEND, 0, true, "Test Publishing at QoS 0");
  Serial.print("Publishing at QoS 0, packetId: ");
  Serial.println(packetIdPub0);
  uint16_t packetIdPub1 = mqttClient.publish(TOPIC_SEND, 1, true, "Test Publishing at QoS 1");
  Serial.print("Publishing at QoS 1, packetId: ");
  Serial.println(packetIdPub1);
  uint16_t packetIdPub2 = mqttClient.publish(TOPIC_SEND, 2, true, "Test Publishing at QoS 2");
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

void executeCommand(COMMAND_ENUM command) {
  Serial.print("Executing command: ");
  Serial.println(COMMAND_STRING[command]);

  if (command == TURN_ON) {
    Serial.println("Turning on...");
    flagTurnDeviceOn = true;
  }
  else if (command == TURN_OFF) {
    Serial.println("Turning off...");
    flagTurnDeviceOff = true;
  }
}

void interpretMqttCommand(char *commandStr) {
  Serial.print("Interpreting command string: ");
  Serial.println(commandStr);

  for (int cmd = TURN_ON; cmd <= END; ++cmd)
  {
    if (strcmp(commandStr, COMMAND_STRING[cmd]) == 0) {
      executeCommand((COMMAND_ENUM)cmd);
      break;
    }
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
    char cmdStr[len + 1] = { 0 };
    strncpy(cmdStr, payload, len);

    char *message = concat("Command message received from Queue: ", cmdStr);

    mqttClient.publish(TOPIC_SEND, 0, true, message);

    free(message);

    interpretMqttCommand(cmdStr);
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
    request->send(200, "text/html", ELEGANTOTA_HTML);
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
  espalexa.addDevice(ID_ALEXA, onAlexaAspiradorRoboChange); //simplest definition, default state off

  espalexa.begin(&server); //give espalexa a pointer to your server object so it can use your server instead of creating its own
}

void onAlexaAspiradorRoboChange(uint8_t brightness) {

    COMMAND_ENUM command;
    
    if (brightness == 255) {
      command = TURN_ON;
    }
    else if (brightness == 0) {
      command = TURN_OFF;
    } else {
      mqttClient.publish(TOPIC_SEND, 0, true, "Invalid command received from Alexa");
      return;
    }

    char *message = concat("Command received from Alexa: ", COMMAND_STRING[command]);

    mqttClient.publish(TOPIC_SEND, 0, true, message);

    free(message);

    executeCommand(command);
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

void switchPower() {
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

  if (flagTurnDeviceOn) {
    switchPower();
    flagTurnDeviceOn = false;
  }
  if (flagTurnDeviceOff) {
    switchPower();
    flagTurnDeviceOff = false;
  }
}