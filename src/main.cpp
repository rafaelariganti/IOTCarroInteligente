#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// ---------- Pinos ----------
#define PIN_SERVO   15
#define PIN_TRIG    13
#define PIN_ECHO    14
#define PIN_BOTAO   33
#define PIN_LED_ESQ 26
#define PIN_LED_DIR 27
#define PIN_LDR     34
#define I2C_SDA     23
#define I2C_SCL     22

// ---------- WiFi / MQTT ----------
const char* WIFI_SSID = "Wokwi-GUEST";   // rede do simulador Wokwi
const char* WIFI_PASS = "";
const char* MQTT_HOST = "broker.hivemq.com";
const int   MQTT_PORT = 1883;

#define T_TELEMETRIA "fatec/carro01/telemetria"
#define T_STATUS     "fatec/carro01/status"
#define T_CMD_FREIO  "fatec/carro01/comando/freio"
#define T_CMD_FAROL  "fatec/carro01/comando/farol"

WiFiClient espClient;
PubSubClient mqtt(espClient);

Adafruit_SH1107 display = Adafruit_SH1107(128, 128, &Wire);
Servo motorServo;

// ---------- Estado do carro ----------
float velocidadeAtual = 0.0;
const float velMaxima = 120.0;
bool faroisLigados = false;
bool obstaculoProximo = false;
float distanciaObstaculo = 999.0;
int valorLDR = 0;
bool freioRemoto = false;   // comando vindo do Dashboard
int modoFarol = 0;          // 0 = automático (LDR), 1 = ligado, 2 = desligado

unsigned long ultimoTempoSensor = 0;
unsigned long ultimoTempoDisplay = 0;
unsigned long ultimoTempoMotor = 0;
unsigned long ultimoTempoMqtt = 0;
unsigned long ultimaTentativaMqtt = 0;

// ---------- Recebe comandos do Node-RED ----------
void callback(char* topic, byte* payload, unsigned int len) {
  String msg;
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  msg.trim();
  String t = String(topic);

  if (t == T_CMD_FREIO) {
    freioRemoto = (msg == "1" || msg == "true");
  } else if (t == T_CMD_FAROL) {
    if (msg == "on") modoFarol = 1;
    else if (msg == "off") modoFarol = 2;
    else modoFarol = 0;
  }
}

void conectaWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
    delay(250);
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? "WiFi OK" : "WiFi falhou");
}

void conectaMQTT() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;
  if (millis() - ultimaTentativaMqtt < 5000) return;
  ultimaTentativaMqtt = millis();

  String id = "carro-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  // Last Will: se o ESP32 cair, o broker publica "offline"
  if (mqtt.connect(id.c_str(), NULL, NULL, T_STATUS, 0, true, "offline")) {
    mqtt.publish(T_STATUS, "online", true);
    mqtt.subscribe(T_CMD_FREIO);
    mqtt.subscribe(T_CMD_FAROL);
    Serial.println("MQTT conectado");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_BOTAO, INPUT_PULLUP);
  pinMode(PIN_LED_ESQ, OUTPUT);
  pinMode(PIN_LED_DIR, OUTPUT);

  motorServo.attach(PIN_SERVO, 500, 2400);
  motorServo.write(90);

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!display.begin(0x3C, true)) {
    Serial.println(F("Falha ao iniciar o display SH1107"));
    for (;;);
  }
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  conectaWiFi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(callback);
}

void loop() {
  unsigned long tempoAtual = millis();

  conectaMQTT();
  mqtt.loop();

  // 1. MOTOR (50 ms)
  if (tempoAtual - ultimoTempoMotor >= 50) {
    bool botaoPressionado = (digitalRead(PIN_BOTAO) == LOW);

    if (obstaculoProximo || freioRemoto) {
      velocidadeAtual = 0;
    } else if (botaoPressionado) {
      velocidadeAtual += 2.0;
    } else {
      velocidadeAtual -= 1.5;
    }

    if (velocidadeAtual > velMaxima) velocidadeAtual = velMaxima;
    if (velocidadeAtual < 0) velocidadeAtual = 0;

    motorServo.write(map((int)velocidadeAtual, 0, 120, 90, 180));
    ultimoTempoMotor = tempoAtual;
  }

  // 2. SENSORES (100 ms)
  if (tempoAtual - ultimoTempoSensor >= 100) {
    digitalWrite(PIN_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(PIN_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);

    long duracao = pulseIn(PIN_ECHO, HIGH, 30000);
    distanciaObstaculo = (duracao == 0) ? 999.0 : (duracao * 0.0343) / 2.0;
    obstaculoProximo = (distanciaObstaculo > 0 && distanciaObstaculo <= 20.0);

    valorLDR = analogRead(PIN_LDR);
    if (modoFarol == 1) faroisLigados = true;
    else if (modoFarol == 2) faroisLigados = false;
    else faroisLigados = (valorLDR > 2048);

    digitalWrite(PIN_LED_ESQ, faroisLigados ? HIGH : LOW);
    digitalWrite(PIN_LED_DIR, faroisLigados ? HIGH : LOW);
    ultimoTempoSensor = tempoAtual;
  }

  // 3. DISPLAY (200 ms)
  if (tempoAtual - ultimoTempoDisplay >= 200) {
    display.clearDisplay();

    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("VELOCIDADE");

    display.setTextSize(2);
    display.setCursor(0, 12);
    display.print((int)velocidadeAtual);
    display.print(" km/h");

    int larguraBarra = map((int)velocidadeAtual, 0, 120, 0, 128);
    display.drawRect(0, 30, 128, 8, SH110X_WHITE);
    display.fillRect(0, 30, larguraBarra, 8, SH110X_WHITE);

    display.setTextSize(1);
    display.setCursor(0, 42);
    if (obstaculoProximo) display.print("ALERTA: PARAGEM!");
    else if (freioRemoto) display.print("FREIO REMOTO");
    else display.print("CAMINHO LIVRE");

    display.setCursor(0, 52);
    display.print("FAROIS: ");
    display.print(faroisLigados ? "ON" : "OFF");

    display.setCursor(0, 62);
    display.print("MQTT: ");
    display.print(mqtt.connected() ? "OK" : "OFF");

    display.display();
    ultimoTempoDisplay = tempoAtual;
  }

  // 4. PUBLICA TELEMETRIA (1 s)
  if (tempoAtual - ultimoTempoMqtt >= 1000) {
    if (mqtt.connected()) {
      char json[200];
      snprintf(json, sizeof(json),
        "{\"velocidade\":%d,\"distancia\":%.0f,\"ldr\":%d,\"farois\":%s,\"obstaculo\":%s,\"freio\":%s}",
        (int)velocidadeAtual, distanciaObstaculo, valorLDR,
        faroisLigados ? "true" : "false",
        obstaculoProximo ? "true" : "false",
        freioRemoto ? "true" : "false");
      mqtt.publish(T_TELEMETRIA, json);
    }
    ultimoTempoMqtt = tempoAtual;
  }
}