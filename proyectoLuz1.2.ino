
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// ========================================
// SMARTLIGHT - XIAO ESP32S3 SENSE
// CAJA DE 30 CM
// ========================================

// PINES
#define LDR_AO D0
#define LDR_DO D1

#define DIP1 D2
#define DIP2 D3

#define TRIG D4
#define ECHO D5

#define LED_LAMPARA D6
#define LED_ROJO D7
#define LED_VERDE D8

// BLUETOOTH BLE
#define SERVICE_UUID \
 "762433f8-4170-4b56-af6c-1a0d848b2296"

#define CHARACTERISTIC_UUID \
 "90a94e1b-e66c-48e6-aadc-d26a1f59f50b"

// DISTANCIAS AJUSTADAS
const float LIMITE_DISTANCIA = 15.0f;
const float DISTANCIA_SEGURA = 20.0f;

// INTERVALOS
const unsigned long SENSOR_MS = 300;
const unsigned long REPORTE_MS = 2000;
const unsigned long ANTIRREBOTE_MS = 50;

// En tu modulo: HIGH = oscuridad
const int LDR_OSCURO = HIGH;

// ESTADOS
bool lamparaEncendida = false;
bool rojoEncendido = false;
bool verdeEncendido = false;

bool modoAutomatico = false;
bool modoGuardian = false;
bool alertaGuardian = false;

bool estaOscuro = false;
bool luzDetectada = false;
bool luzAnterior = false;
bool primeraLecturaLuz = true;

int valorLDR = 0;
float distancia = -1;

unsigned long ultimoSensor = 0;
unsigned long ultimoReporte = 0;

// DIP SWITCH
const int pinesDip[2] = {DIP1, DIP2};

int lecturaAnteriorDip[2] = {HIGH, HIGH};
int estadoDip[2] = {HIGH, HIGH};

unsigned long ultimoCambioDip[2] = {0, 0};

// BLE
BLEServer* servidorBLE = nullptr;
BLECharacteristic* caracteristicaBLE = nullptr;

volatile bool bleConectado = false;
bool bleConectadoAntes = false;

struct MensajeBLE {
  char texto[64];
};

QueueHandle_t colaBLE = nullptr;

// ========================================
// ENVIAR MENSAJE
// ========================================
void enviarMensaje(const String& mensaje) {
  Serial.println(mensaje);

  if (bleConectado &&
      caracteristicaBLE != nullptr) {

    caracteristicaBLE->setValue(
      mensaje.c_str()
    );

    caracteristicaBLE->notify();

    // Pequena pausa entre notificaciones
    delay(35);
  }
}

// ========================================
// CONTROL LAMPARA
// ========================================
void cambiarLampara(bool encender) {
  if (lamparaEncendida == encender) return;

  lamparaEncendida = encender;

  digitalWrite(
    LED_LAMPARA,
    encender ? HIGH : LOW
  );

  enviarMensaje(
    encender ? "LAMPARA_ON" : "LAMPARA_OFF"
  );
}

// ========================================
// CONTROL LED ROJO
// ========================================
void cambiarRojo(bool encender) {
  if (rojoEncendido == encender) return;

  rojoEncendido = encender;

  digitalWrite(
    LED_ROJO,
    encender ? HIGH : LOW
  );

  enviarMensaje(
    encender ? "ROJO_ON" : "ROJO_OFF"
  );
}

// ========================================
// CONTROL LED VERDE
// En BLE mantiene el nombre AZUL
// ========================================
void cambiarVerde(bool encender) {
  if (verdeEncendido == encender) return;

  verdeEncendido = encender;

  digitalWrite(
    LED_VERDE,
    encender ? HIGH : LOW
  );

  enviarMensaje(
    encender ? "AZUL_ON" : "AZUL_OFF"
  );
}

// ========================================
// INDICADORES DEL GUARDIAN
// ========================================
void actualizarIndicadoresGuardian() {

  if (!modoGuardian) {
    cambiarRojo(false);
    cambiarVerde(false);
    return;
  }

  if (alertaGuardian) {
    // ALERTA: ROJO
    cambiarVerde(false);
    cambiarRojo(true);

  } else {
    // ZONA SEGURA: VERDE
    cambiarRojo(false);
    cambiarVerde(true);
  }
}

// ========================================
// MODO AUTOMATICO
// ========================================
void cambiarModoAutomatico(bool activar) {
  if (modoAutomatico == activar) return;

  modoAutomatico = activar;

  enviarMensaje(
    activar ? "AUTO_ON" : "AUTO_OFF"
  );

  if (activar) {
    cambiarLampara(estaOscuro);
  } else {
    cambiarLampara(false);
  }
}

// ========================================
// MODO GUARDIAN
// ========================================
void cambiarModoGuardian(bool activar) {
  if (modoGuardian == activar) return;

  modoGuardian = activar;

  if (alertaGuardian) {
    alertaGuardian = false;

    enviarMensaje(
      "ZONA_SEGURA_GUARDIAN"
    );
  }

  enviarMensaje(
    activar ? "MODO_GUARDIAN" : "MODO_NORMAL"
  );

  actualizarIndicadoresGuardian();
}

// ========================================
// MEDIR DISTANCIA HC-SR04
// ========================================
float medirDistancia() {

  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG, LOW);

  unsigned long duracion =
    pulseIn(ECHO, HIGH, 25000);

  if (duracion == 0) {
    return -1;
  }

  float cm =
    duracion * 0.0343f / 2.0f;

  if (cm < 2 || cm > 400) {
    return -1;
  }

  return cm;
}

// ========================================
// ESTADO EN MONITOR SERIE
// ========================================
void mostrarEstado() {

  Serial.println();
  Serial.println("====== SMARTLIGHT ======");

  Serial.print("DIP1: ");
  Serial.println(
    estadoDip[0] == LOW ? "ON" : "OFF"
  );

  Serial.print("DIP2: ");
  Serial.println(
    estadoDip[1] == LOW ? "ON" : "OFF"
  );

  Serial.print("Automatico: ");
  Serial.println(
    modoAutomatico ? "ON" : "OFF"
  );

  Serial.print("Guardian: ");
  Serial.println(
    modoGuardian ? "ON" : "OFF"
  );

  Serial.print("Lampara: ");
  Serial.println(
    lamparaEncendida ? "ON" : "OFF"
  );

  Serial.print("LED rojo: ");
  Serial.println(
    rojoEncendido ? "ON" : "OFF"
  );

  Serial.print("LED verde: ");
  Serial.println(
    verdeEncendido ? "ON" : "OFF"
  );

  Serial.print("LDR: ");
  Serial.println(valorLDR);

  Serial.print("Oscuridad: ");
  Serial.println(
    estaOscuro ? "SI" : "NO"
  );

  Serial.print("Distancia: ");

  if (distancia < 0) {
    Serial.println("SIN LECTURA");
  } else {
    Serial.print(distancia, 1);
    Serial.println(" cm");
  }

  Serial.print("Alerta: ");
  Serial.println(
    alertaGuardian ? "ACTIVA" : "INACTIVA"
  );

  Serial.print("BLE: ");
  Serial.println(
    bleConectado ? "CONECTADO" : "DESCONECTADO"
  );

  Serial.println("========================");
}

// ========================================
// ESTADOS PARA APP INVENTOR
// ========================================
void enviarEstadosBLE() {

  if (!bleConectado) return;

  enviarMensaje(
    "LAMPARA:" +
    String(lamparaEncendida ? 1 : 0)
  );

  enviarMensaje(
    "ROJO:" +
    String(rojoEncendido ? 1 : 0)
  );

  enviarMensaje(
    "AZUL:" +
    String(verdeEncendido ? 1 : 0)
  );

  enviarMensaje(
    "AUTO:" +
    String(modoAutomatico ? 1 : 0)
  );

  enviarMensaje(
    "GUARDIAN:" +
    String(modoGuardian ? 1 : 0)
  );

  enviarMensaje(
    "LUZ:" +
    String(luzDetectada ? 1 : 0)
  );

  enviarMensaje(
    "LDR:" +
    String(valorLDR)
  );

  enviarMensaje(
    "DIST:" +
    String(distancia, 1)
  );

  enviarMensaje(
    "ALERTA:" +
    String(alertaGuardian ? 1 : 0)
  );
}

// ========================================
// PROCESAR COMANDOS
// ========================================
void procesarComando(String comando) {

  comando.trim();
  comando.toUpperCase();

  if (comando.length() == 0) return;

  Serial.print("Comando: ");
  Serial.println(comando);

  if (comando == "LAMPARA_ON") {

    cambiarModoAutomatico(false);
    cambiarLampara(true);

  }
  else if (comando == "LAMPARA_OFF") {

    cambiarModoAutomatico(false);
    cambiarLampara(false);

  }
  else if (comando == "AUTO_ON") {

    cambiarModoAutomatico(true);

  }
  else if (comando == "AUTO_OFF") {

    cambiarModoAutomatico(false);

  }
  else if (comando == "MODO_GUARDIAN") {

    cambiarModoGuardian(true);

  }
  else if (comando == "MODO_NORMAL") {

    cambiarModoGuardian(false);

  }
  else if (comando == "ROJO_ON") {

    if (!modoGuardian) cambiarRojo(true);

  }
  else if (comando == "ROJO_OFF") {

    if (!modoGuardian) cambiarRojo(false);

  }
  else if (
    comando == "AZUL_ON" ||
    comando == "VERDE_ON"
  ) {

    if (!modoGuardian) cambiarVerde(true);

  }
  else if (
    comando == "AZUL_OFF" ||
    comando == "VERDE_OFF"
  ) {

    if (!modoGuardian) cambiarVerde(false);

  }
  else if (comando == "ESTADO") {

    mostrarEstado();
    enviarEstadosBLE();

  }
  else if (comando == "AYUDA") {

    Serial.println("LAMPARA_ON/OFF");
    Serial.println("ROJO_ON/OFF");
    Serial.println("AZUL_ON/OFF");
    Serial.println("AUTO_ON/OFF");
    Serial.println("MODO_GUARDIAN");
    Serial.println("MODO_NORMAL");
    Serial.println("ESTADO");

  }
  else {

    enviarMensaje("ERROR_COMANDO");

  }
}

// ========================================
// CALLBACKS BLE
// ========================================
class EventosServidor :
  public BLEServerCallbacks {

  void onConnect(
    BLEServer* servidor
  ) override {

    bleConectado = true;
  }

  void onDisconnect(
    BLEServer* servidor
  ) override {

    bleConectado = false;
  }
};

class EventosCaracteristica :
  public BLECharacteristicCallbacks {

  void onWrite(
    BLECharacteristic* caracteristica
  ) override {

    if (colaBLE == nullptr) return;

    String recibido =
      String(
        caracteristica->getValue().c_str()
      );

    MensajeBLE mensaje = {};

    recibido.toCharArray(
      mensaje.texto,
      sizeof(mensaje.texto)
    );

    xQueueSend(
      colaBLE,
      &mensaje,
      0
    );
  }
};

// ========================================
// INICIAR BLUETOOTH BLE
// ========================================
void iniciarBluetooth() {

  BLEDevice::init("SmartLight_ESP32");

  servidorBLE =
    BLEDevice::createServer();

  servidorBLE->setCallbacks(
    new EventosServidor()
  );

  BLEService* servicio =
    servidorBLE->createService(
      SERVICE_UUID
    );

  caracteristicaBLE =
    servicio->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_NOTIFY
    );

  caracteristicaBLE->setCallbacks(
    new EventosCaracteristica()
  );

  caracteristicaBLE->addDescriptor(
    new BLE2902()
  );

  caracteristicaBLE->setValue(
    "SMARTLIGHT_LISTO"
  );

  servicio->start();

  BLEAdvertising* publicidad =
    BLEDevice::getAdvertising();

  publicidad->addServiceUUID(
    SERVICE_UUID
  );

  publicidad->setScanResponse(true);
  publicidad->start();

  Serial.println("BLE INICIADO");
  Serial.println("SmartLight_ESP32");
}

// ========================================
// LEER DIP SWITCH
// ========================================
void revisarDip() {

  for (int i = 0; i < 2; i++) {

    int lectura =
      digitalRead(pinesDip[i]);

    if (
      lectura != lecturaAnteriorDip[i]
    ) {
      ultimoCambioDip[i] = millis();
    }

    if (
      millis() - ultimoCambioDip[i] >=
      ANTIRREBOTE_MS
    ) {

      if (lectura != estadoDip[i]) {

        estadoDip[i] = lectura;

        bool activado =
          estadoDip[i] == LOW;

        if (i == 0) {

          cambiarModoAutomatico(
            activado
          );

        } else {

          cambiarModoGuardian(
            activado
          );
        }
      }
    }

    lecturaAnteriorDip[i] = lectura;
  }
}

// ========================================
// MONITOREAR SENSORES
// ========================================
void monitorearSensores() {

  // LDR
  valorLDR = analogRead(LDR_AO);

  estaOscuro =
    digitalRead(LDR_DO) == LDR_OSCURO;

  luzDetectada = !estaOscuro;

  if (modoAutomatico) {
    cambiarLampara(estaOscuro);
  }

  if (
    primeraLecturaLuz ||
    luzDetectada != luzAnterior
  ) {

    primeraLecturaLuz = false;
    luzAnterior = luzDetectada;

    enviarMensaje(
      luzDetectada
      ? "LUZ_ON"
      : "LUZ_OFF"
    );
  }

  // SENSOR ULTRASONICO
  distancia = medirDistancia();

  if (!modoGuardian) {
    return;
  }

  // Ignorar lecturas invalidas
  if (distancia < 0) {
    return;
  }

  // ALERTA A 15 CM O MENOS
  if (
    distancia <= LIMITE_DISTANCIA
  ) {

    if (!alertaGuardian) {

      alertaGuardian = true;

      actualizarIndicadoresGuardian();

      enviarMensaje(
        "ALERTA_GUARDIAN"
      );
    }
  }

  // ZONA SEGURA A 20 CM O MAS
  else if (
    distancia >= DISTANCIA_SEGURA
  ) {

    if (alertaGuardian) {

      alertaGuardian = false;

      actualizarIndicadoresGuardian();

      enviarMensaje(
        "ZONA_SEGURA_GUARDIAN"
      );
    }
  }

  // Entre 15 y 20 cm mantiene
  // el estado anterior.
}

// ========================================
// SETUP
// ========================================
void setup() {

  Serial.begin(115200);
  Serial.setTimeout(30);

  pinMode(LDR_AO, INPUT);
  pinMode(LDR_DO, INPUT);

  // Pull-up externas de los DIP
  pinMode(DIP1, INPUT);
  pinMode(DIP2, INPUT);

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);

  pinMode(LED_LAMPARA, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);

  digitalWrite(TRIG, LOW);

  digitalWrite(
    LED_LAMPARA, LOW
  );

  digitalWrite(
    LED_ROJO, LOW
  );

  digitalWrite(
    LED_VERDE, LOW
  );

  analogReadResolution(12);

  colaBLE = xQueueCreate(
    10,
    sizeof(MensajeBLE)
  );

  if (colaBLE == nullptr) {
    Serial.println("ERROR COLA BLE");
  }

  valorLDR = analogRead(LDR_AO);

  estaOscuro =
    digitalRead(LDR_DO) == LDR_OSCURO;

  luzDetectada = !estaOscuro;
  luzAnterior = luzDetectada;

  // Estados iniciales de DIP
  for (int i = 0; i < 2; i++) {

    lecturaAnteriorDip[i] =
      digitalRead(pinesDip[i]);

    estadoDip[i] =
      lecturaAnteriorDip[i];
  }

  cambiarModoAutomatico(
    estadoDip[0] == LOW
  );

  cambiarModoGuardian(
    estadoDip[1] == LOW
  );

  iniciarBluetooth();

  Serial.println();
  Serial.println("SMARTLIGHT INICIADO");
  Serial.println("CAJA: 30 CM");
  Serial.println("ALERTA: <= 15 CM");
  Serial.println("SEGURO: >= 20 CM");
  Serial.println("SERIAL: 115200");
}

// ========================================
// LOOP PRINCIPAL
// ========================================
void loop() {

  // COMANDOS MONITOR SERIE
  if (Serial.available() > 0) {

    String comando =
      Serial.readStringUntil('\n');

    comando.trim();

    if (comando.length() > 0) {
      procesarComando(comando);
    }
  }

  // COMANDOS BLUETOOTH
  if (colaBLE != nullptr) {

    MensajeBLE mensaje;

    while (
      xQueueReceive(
        colaBLE,
        &mensaje,
        0
      ) == pdTRUE
    ) {

      procesarComando(
        String(mensaje.texto)
      );
    }
  }

  // DIP SWITCH
  revisarDip();

  // SENSORES
  if (
    millis() - ultimoSensor >=
    SENSOR_MS
  ) {

    ultimoSensor = millis();
    monitorearSensores();
  }

  // REPORTE
  if (
    millis() - ultimoReporte >=
    REPORTE_MS
  ) {

    ultimoReporte = millis();

    mostrarEstado();

    if (bleConectado) {
      enviarEstadosBLE();
    }
  }

  // REANUDAR PUBLICIDAD BLE
  if (
    !bleConectado &&
    bleConectadoAntes
  ) {

    delay(100);

    servidorBLE->startAdvertising();

    bleConectadoAntes = false;

    Serial.println(
      "BLE DESCONECTADO"
    );
  }

  if (
    bleConectado &&
    !bleConectadoAntes
  ) {

    bleConectadoAntes = true;

    Serial.println(
      "BLE CONECTADO"
    );

    ultimoReporte =
      millis() - REPORTE_MS;
  }

  delay(5);
}
