// Tortuga - receptor ESP-NOW: un motor N20 con TB6612FNG (canal A)
// Placa: Seeed Studio XIAO ESP32-C6 - Arduino core 3.x

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "motor_protocol.h"

#if !defined(CONFIG_IDF_TARGET_ESP32C6) || !defined(ARDUINO_XIAO_ESP32C6)
#error "Este firmware es solo para Seeed Studio XIAO ESP32-C6"
#endif
//Pines
constexpr uint8_t PIN_STBY = 0;   // D0 -> STBY
constexpr uint8_t PIN_AIN1 = 1;   // D1 -> AIN1
constexpr uint8_t PIN_AIN2 = 2;   // D2 -> AIN2
constexpr uint8_t PIN_PWMA = 21;  // D3 -> PWMA

//PWM (LEDC, API de Arduino core 3.x) 
constexpr uint32_t PWM_FREQ_HZ = 20000;  // 20 kHz: inaudible, < 100 kHz max del TB6612FNG
constexpr uint8_t PWM_RES_BITS = 8;      // duty 0..255
constexpr uint32_t PWM_DUTY_MAX = (1u << PWM_RES_BITS) - 1;

// ---- Tiempos -----------------------------------------------------------
constexpr uint32_t STARTUP_STOP_MS = 2000;

// Diagnostico
constexpr bool DIAG_HOLD_FORWARD = false;

// ---- Ultimo comando recibido (escrito en el callback de ESP-NOW) -------
portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint8_t rxCmd = MOTOR_CMD_STOP;
volatile uint8_t rxSpeed = 0;
volatile uint32_t rxLastMs = 0;
volatile bool rxAny = false;

void motorStop() {
  ledcWrite(PIN_PWMA, 0);
  digitalWrite(PIN_AIN1, LOW);
  digitalWrite(PIN_AIN2, LOW);
}

void motorForward(uint32_t duty) {
  // Primero PWM a 0 para no cambiar de direccion con el puente activo
  ledcWrite(PIN_PWMA, 0);
  digitalWrite(PIN_AIN1, HIGH);
  digitalWrite(PIN_AIN2, LOW);
  ledcWrite(PIN_PWMA, duty);
}

void motorReverse(uint32_t duty) {
  ledcWrite(PIN_PWMA, 0);
  digitalWrite(PIN_AIN1, LOW);
  digitalWrite(PIN_AIN2, HIGH);
  ledcWrite(PIN_PWMA, duty);
}

// Firma de Arduino core 3.x (ESP-IDF 5): recibe esp_now_recv_info_t, no la MAC
void onEspNowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(MotorMsg)) {
    return;
  }
  MotorMsg msg;
  memcpy(&msg, data, sizeof(msg));
  if (msg.magic != MOTOR_MSG_MAGIC || msg.cmd > MOTOR_CMD_REVERSE) {
    return;
  }
  portENTER_CRITICAL(&rxMux);
  rxCmd = msg.cmd;
  rxSpeed = msg.speed;
  rxLastMs = millis();
  rxAny = true;
  portEXIT_CRITICAL(&rxMux);
}

const char *cmdName(uint8_t cmd) {
  switch (cmd) {
    case MOTOR_CMD_FORWARD: return "Forward";
    case MOTOR_CMD_REVERSE: return "Reverse";
    default: return "Stop";
  }
}

void setup() {
  // 1) Estado seguro lo antes posible: driver en standby, sin direccion, sin PWM
  pinMode(PIN_STBY, OUTPUT);
  digitalWrite(PIN_STBY, LOW);
  pinMode(PIN_AIN1, OUTPUT);
  digitalWrite(PIN_AIN1, LOW);
  pinMode(PIN_AIN2, OUTPUT);
  digitalWrite(PIN_AIN2, LOW);

  Serial.begin(115200);
  // Con el USB conectado a la PC pero sin monitor abierto, cada print bloqueaba
  // hasta 100 ms (buffer lleno) y retrasaba los comandos del motor
  Serial.setTxTimeoutMs(0);
  delay(500);  // tiempo para que el USB-CDC enumere y no perder los primeros mensajes

  if (!ledcAttach(PIN_PWMA, PWM_FREQ_HZ, PWM_RES_BITS)) {
    Serial.println("ERROR: ledcAttach failed on PWMA, motor stays disabled");
    while (true) {
      delay(1000);
    }
  }
  ledcWrite(PIN_PWMA, 0);

  Serial.println();
  Serial.println("Tortuga motor receiver (ESP-NOW)");
  Serial.printf("STBY=GPIO%u AIN1=GPIO%u AIN2=GPIO%u PWMA=GPIO%u\n",
                PIN_STBY, PIN_AIN1, PIN_AIN2, PIN_PWMA);

  // 2) ESP-NOW: modo estacion sin conectarse a ninguna red, canal fijo
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: esp_now_init failed, motor stays disabled");
    while (true) {
      delay(1000);
    }
  }
  esp_now_register_recv_cb(onEspNowRecv);

  // Esta es la MAC que hay que poner en el control (WT32-SC01 Plus)
  Serial.printf("XIAO MAC: %s  channel %u\n", WiFi.macAddress().c_str(), ESPNOW_CHANNEL);

  // 3) Motor detenido 2 s con el driver aun en standby
  motorStop();
  delay(STARTUP_STOP_MS);

  // 4) Solo despues de inicializar todo se saca al driver de standby
  digitalWrite(PIN_STBY, HIGH);
  Serial.println("STBY enabled, waiting for commands");
}

void loop() {
  if (DIAG_HOLD_FORWARD) {
    motorForward(PWM_DUTY_MAX);
    Serial.println("DIAG: Forward 100% (STBY=H AIN1=H AIN2=L PWMA=H)");
    delay(1000);
    return;
  }

  static uint8_t appliedCmd = MOTOR_CMD_STOP;
  static uint8_t appliedSpeed = 0;
  static uint32_t lastStatusMs = 0;

  portENTER_CRITICAL(&rxMux);
  uint8_t cmd = rxCmd;
  uint8_t speed = rxSpeed;
  uint32_t lastMs = rxLastMs;
  bool any = rxAny;
  portEXIT_CRITICAL(&rxMux);

  // Failsafe: sin mensajes recientes (o ninguno aun) -> stop
  bool timedOut = !any || (millis() - lastMs > MOTOR_FAILSAFE_MS);
  if (timedOut) {
    cmd = MOTOR_CMD_STOP;
    speed = 0;
  }

  // Solo tocar el puente cuando cambia el comando, para no cortar el PWM en cada vuelta
  if (cmd != appliedCmd || speed != appliedSpeed) {
    switch (cmd) {
      case MOTOR_CMD_FORWARD: motorForward(speed); break;
      case MOTOR_CMD_REVERSE: motorReverse(speed); break;
      default: motorStop(); break;
    }
    Serial.printf("%s speed %u%s\n", cmdName(cmd), speed,
                  (timedOut && any && appliedCmd != MOTOR_CMD_STOP) ? " (failsafe: no signal)" : "");
    appliedCmd = cmd;
    appliedSpeed = speed;
  }

  // Recordatorio periodico de la MAC mientras no haya llegado ningun mensaje
  if (!any && millis() - lastStatusMs > 3000) {
    lastStatusMs = millis();
    Serial.printf("Waiting for WT32... XIAO MAC: %s\n", WiFi.macAddress().c_str());
  }

  delay(5);
}
