#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h> // <-- Biblioteca correta para o seu display atual

// Mapeamento de Pinos (Exatamente como está na sua montagem JSON)
#define PIN_SERVO      15
#define PIN_TRIG       13
#define PIN_ECHO       14
#define PIN_BOTAO      33
#define PIN_LED_ESQ    26
#define PIN_LED_DIR    27
#define PIN_LDR        34

// Pinos do Display I2C na sua montagem
#define I2C_SDA        23
#define I2C_SCL        22

// Configuração do Ecrã OLED Grove SH1107 (128x128)
Adafruit_SH1107 display = Adafruit_SH1107(128, 128, &Wire);

Servo motorServo;

// Variáveis de Estado do Carro
float velocidadeAtual = 0.0;
const float velMaxima = 120.0;
bool faroisLigados = false;
bool obstaculoProximo = false;
float distanciaObstaculo = 999.0;

// Temporizadores para não usar "delay()" (Multitarefa)
unsigned long ultimoTempoSensor = 0;
unsigned long ultimoTempoDisplay = 0;
unsigned long ultimoTempoMotor = 0;

void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_BOTAO, INPUT_PULLUP);
  pinMode(PIN_LED_ESQ, OUTPUT);
  pinMode(PIN_LED_DIR, OUTPUT);
  
  motorServo.attach(PIN_SERVO, 500, 2400); 
  motorServo.write(90); // Posição inicial (Parado)
  
  // 1º: Iniciar a comunicação I2C nos pinos exatos que você ligou (23 e 22)
  Wire.begin(I2C_SDA, I2C_SCL);
  
  // 2º: Iniciar o Ecrã SH1107 (Endereço I2C padrão é 0x3C)
  if(!display.begin(0x3C, true)) {
    Serial.println(F("Falha ao iniciar o Ecrã OLED SH1107"));
    for(;;); // Bloqueia se o ecrã falhar
  }
  
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE); // Constante de cor da nova biblioteca
}

void loop() {
  unsigned long tempoAtual = millis();

  // -----------------------------------------------------
  // 1. MOTOR & ACELERAÇÃO (A cada 50ms)
  // -----------------------------------------------------
  if (tempoAtual - ultimoTempoMotor >= 50) {
    bool botaoPressionado = (digitalRead(PIN_BOTAO) == LOW);
    
    if (obstaculoProximo) {
      velocidadeAtual = 0; // Travagem de emergência imediata
    } else {
      if (botaoPressionado) {
        velocidadeAtual += 2.0; // Acelera gradualmente
      } else {
        velocidadeAtual -= 1.5; // Desacelera (inércia do carro)
      }
    }
    
    if (velocidadeAtual > velMaxima) velocidadeAtual = velMaxima;
    if (velocidadeAtual < 0) velocidadeAtual = 0;
    
    int valorServo = map(velocidadeAtual, 0, 120, 90, 180);
    motorServo.write(valorServo);
    
    ultimoTempoMotor = tempoAtual;
  }

  // -----------------------------------------------------
  // 2. LEITURA DOS SENSORES LDR E ULTRASSOM (A cada 100ms)
  // -----------------------------------------------------
  if (tempoAtual - ultimoTempoSensor >= 100) {
    
    // --- Ultrassom HC-SR04 ---
    digitalWrite(PIN_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(PIN_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);
    
    long duracao = pulseIn(PIN_ECHO, HIGH, 30000); 
    
    if (duracao == 0) {
      distanciaObstaculo = 999.0;
    } else {
      distanciaObstaculo = (duracao * 0.0343) / 2.0; 
    }
    
    obstaculoProximo = (distanciaObstaculo > 0 && distanciaObstaculo <= 20.0);

    // --- LDR (Luz Automática) ---
    int valorLDR = analogRead(PIN_LDR);
    faroisLigados = (valorLDR > 2048); 
    
    digitalWrite(PIN_LED_ESQ, faroisLigados ? HIGH : LOW);
    digitalWrite(PIN_LED_DIR, faroisLigados ? HIGH : LOW);

    ultimoTempoSensor = tempoAtual;
  }

  // -----------------------------------------------------
  // 3. ATUALIZAÇÃO DO PAINEL / ECRÃ (A cada 200ms)
  // -----------------------------------------------------
  if (tempoAtual - ultimoTempoDisplay >= 200) {
    display.clearDisplay();
    
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("VELOCIDADE");
    
    display.setTextSize(2);
    display.setCursor(0, 12);
    display.print((int)velocidadeAtual);
    display.print(" km/h");
    
    int larguraBarra = map(velocidadeAtual, 0, 120, 0, 128);
    display.drawRect(0, 30, 128, 8, SH110X_WHITE);
    display.fillRect(0, 30, larguraBarra, 8, SH110X_WHITE);
    
    display.setTextSize(1);
    display.setCursor(0, 42);
    if (obstaculoProximo) {
      display.print("ALERTA: PARAGEM!");
    } else {
      display.print("CAMINHO LIVRE");
    }
    
    display.setCursor(0, 52);
    display.print("FAROIS: ");
    display.print(faroisLigados ? "ON" : "OFF");
    
    display.display();
    ultimoTempoDisplay = tempoAtual;
  }
}