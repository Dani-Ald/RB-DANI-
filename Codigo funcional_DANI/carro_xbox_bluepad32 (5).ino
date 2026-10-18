// ===== CARRO RC CON CONTROL DE XBOX (Bluepad32) - v2 =====
// Joystick DERECHO (horizontal) -> dirección (servo en D21)
// Joystick IZQUIERDO (vertical) -> potencia del motor, proporcional
//     arriba = adelante | abajo = reversa | más lejos del centro = más potencia
//
// Pines motor: E y F unidos -> D22, G (LPWM) -> D18, H (RPWM) -> D5
// Pines servo: naranja -> D21, rojo -> 5V, café -> GND
// Placa en el IDE: ESP32 Dev Module (esp32_bluepad32). Librería: ESP32Servo.
//
// LED: parpadea mientras no hay control, fijo cuando está conectado.
// Monitor serial: muestra errores y datos del control al conectar.
// Los movimientos solo se imprimen cuando hay movimiento.

#include <Bluepad32.h>
#include <ESP32Servo.h>
#include <esp_system.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

const int LED = 2;

// ===== SERVO - DIRECCIÓN =====
const int PIN_SERVO = 21;
const int CENTRO = 90;                 // ruedas rectas
const int LIMITE_GIRO = 46;            // grados máximos a cada lado (calibrado)
const bool INVERTIR_DIRECCION = false; // true si gira al revés
const float SERVO_PASO_MAX = 1.5;      // grados máximos por ciclo de 10 ms (1.5 = 150 grados/s)
const float SERVO_SUAVIZADO = 0.2;     // 0.05 = muy suave | 1.0 = sin suavizado
// SOLO PARA DIAGNOSTICO: si al desactivarlo ya no se desconecta, el problema es voltaje.
const bool DESACTIVAR_BROWNOUT = false;

Servo direccion;
int anguloActual = CENTRO;
float anguloF = CENTRO;                // posición suavizada (decimal)

// ===== MOTOR - TRACCIÓN =====
const int EN   = 22;
const int LPWM = 18;
const int RPWM = 5;

// Voltaje en los bornes B+ y B-: LM2596 a 6 V = 6.0 | LiPo directa = 12.6
const float VOLTAJE_BATERIA = 6.0;
const float VOLTAJE_MOTOR = 6.0;       // RS550 de 6 V
const int VEL_MAX = (VOLTAJE_BATERIA <= VOLTAJE_MOTOR)
                      ? 255
                      : (int)(255.0 * VOLTAJE_MOTOR / VOLTAJE_BATERIA);

const int PWM_MINIMO = 0;              // sube si el motor no arranca con poco joystick
const int RAMPA_SUBIDA = 6;            // cambio máximo de PWM por ciclo al acelerar
const int RAMPA_BAJADA = 12;           // cambio máximo de PWM por ciclo al frenar

int velocidadActual = 0;               // de -VEL_MAX (reversa) a +VEL_MAX (adelante)

// ===== JOYSTICK =====
const int EJE_MAX = 512;               // Bluepad32 entrega de -511 a 512
const int ZONA_MUERTA = 60;            // ignora movimiento pequeño cerca del centro

// ===== CONEXIÓN DEL CONTROL =====
const bool OLVIDAR_PAREOS = false;     // true una vez si no logra emparejar
const bool FILTRAR_POR_ID = false;     // true para aceptar solo tu control
const uint16_t VID_PERMITIDO = 0x0000; // pon aquí el VID que muestra el monitor
const uint16_t PID_PERMITIDO = 0x0000; // pon aquí el PID que muestra el monitor

ControllerPtr control = nullptr;
bool armado = false;
bool avisoArmado = false;
bool avisoNoGamepad = false;
bool avisoSinClase = false;

unsigned long ultimoCiclo = 0;
unsigned long ultimoMensaje = 0;
unsigned long ultimoParpadeo = 0;
int ultimoAngulo = CENTRO;
int ultimoPwm = 0;

// ---------------------------------------------------------------------
// CALLBACKS DE BLUEPAD32
// ---------------------------------------------------------------------
void onConnectedController(ControllerPtr ctl) {
  ControllerProperties p = ctl->getProperties();

  if (control != nullptr) {
    Serial.println("ERROR: ya hay un control conectado. Se ignora el nuevo.");
    return;
  }
  if (FILTRAR_POR_ID && (p.vendor_id != VID_PERMITIDO || p.product_id != PID_PERMITIDO)) {
    Serial.printf("ERROR: control rechazado por ID (VID=0x%04X, PID=0x%04X).\n",
                  p.vendor_id, p.product_id);
    return;
  }

  control = ctl;
  armado = false;
  avisoArmado = false;
  avisoNoGamepad = false;
  avisoSinClase = false;
  Serial.printf("Control conectado: %s | VID=0x%04X | PID=0x%04X | gamepad: %s\n",
                ctl->getModelName().c_str(), p.vendor_id, p.product_id,
                ctl->isGamepad() ? "si" : "aun no");
}

void onDisconnectedController(ControllerPtr ctl) {
  if (control == ctl) {
    control = nullptr;
    Serial.println("AVISO: control desconectado. Motor detenido y direccion al centro.");
  }
}

// ---------------------------------------------------------------------
// FUNCIONES
// ---------------------------------------------------------------------
// Convierte el valor del eje en un número de -1.0 a 1.0, con zona muerta
float normalizar(int valor) {
  valor = constrain(valor, -EJE_MAX, EJE_MAX);
  int magnitud = abs(valor);
  if (magnitud <= ZONA_MUERTA) return 0.0;
  float n = (float)(magnitud - ZONA_MUERTA) / (EJE_MAX - ZONA_MUERTA);
  return (valor < 0) ? -n : n;
}

void aplicarMotor(int v) {
  if (v > 0) {
    analogWrite(RPWM, v);
    analogWrite(LPWM, 0);
  } else if (v < 0) {
    analogWrite(LPWM, -v);
    analogWrite(RPWM, 0);
  } else {
    analogWrite(LPWM, 0);
    analogWrite(RPWM, 0);
  }
}

// Informa por qué arrancó la ESP32 (sirve para detectar caídas de voltaje)
void imprimirMotivoReinicio() {
  esp_reset_reason_t r = esp_reset_reason();
  switch (r) {
    case ESP_RST_POWERON:
      Serial.println("Arranque: encendido normal.");
      break;
    case ESP_RST_BROWNOUT:
      Serial.println("ERROR: reinicio por BROWNOUT (caida de voltaje en la alimentacion).");
      break;
    case ESP_RST_PANIC:
      Serial.println("ERROR: reinicio por fallo del programa (panic).");
      break;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      Serial.println("ERROR: reinicio por watchdog (el programa se bloqueo).");
      break;
    case ESP_RST_SW:
      Serial.println("Arranque: reinicio por software.");
      break;
    case ESP_RST_EXT:
      Serial.println("Arranque: reinicio por boton EN.");
      break;
    default:
      Serial.printf("Arranque: motivo de reinicio %d\n", (int)r);
  }
}

// ---------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------
void setup() {
  if (DESACTIVAR_BROWNOUT) {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);   // desactiva el detector de caída de voltaje
  }
  Serial.begin(115200);
  delay(500);
  if (DESACTIVAR_BROWNOUT) {
    Serial.println("AVISO: detector de brownout DESACTIVADO (solo para diagnostico).");
  }
  imprimirMotivoReinicio();
  pinMode(LED, OUTPUT);

  // Servo primero (como en el código que ya funcionaba)
  direccion.setPeriodHertz(50);
  direccion.attach(PIN_SERVO, 500, 2400);
  if (!direccion.attached()) {
    Serial.println("ERROR: no se pudo iniciar el servo en D21.");
  }
  direccion.write(CENTRO);
  anguloActual = CENTRO;
  anguloF = CENTRO;

  // Motor
  pinMode(EN, OUTPUT);
  pinMode(LPWM, OUTPUT);
  pinMode(RPWM, OUTPUT);
  digitalWrite(EN, HIGH);
  aplicarMotor(0);

  // Bluepad32
  BP32.setup(&onConnectedController, &onDisconnectedController);
  if (OLVIDAR_PAREOS) {
    BP32.forgetBluetoothKeys();
    Serial.println("AVISO: se borraron los emparejamientos guardados.");
  }

  Serial.printf("Listo. Tope PWM: %d | Giro maximo: +-%d grados\n", VEL_MAX, LIMITE_GIRO);
  Serial.println("Esperando control...");
}

// ---------------------------------------------------------------------
// LOOP
// ---------------------------------------------------------------------
void loop() {
  BP32.update();

  unsigned long ahora = millis();
  if (ahora - ultimoCiclo < 10) {         // un ciclo cada 10 ms
    delay(1);                             // cede tiempo al Bluetooth
    return;
  }
  ultimoCiclo = ahora;

  int objetivoVelocidad = 0;
  int objetivoAngulo = CENTRO;

  bool conectado = (control != nullptr && control->isConnected());
  bool esGamepad = conectado && control->isGamepad();
  bool listo = conectado && (esGamepad || control->hasData());

  if (conectado && !listo && !avisoNoGamepad) {
    avisoNoGamepad = true;
    Serial.println("ERROR: control conectado pero sin datos. Bluepad32 no lo reconoce.");
  }
  if (listo && !esGamepad && !avisoSinClase) {
    avisoSinClase = true;
    Serial.println("AVISO: Bluepad32 no lo clasifica como gamepad, pero envia datos. Se usan los ejes.");
  }

  if (listo) {
    digitalWrite(LED, HIGH);              // fijo: control conectado y con datos
    if (avisoNoGamepad) {
      avisoNoGamepad = false;
      Serial.println("Control listo.");
    }

    // --- Dirección (joystick derecho) ---
    float giro = normalizar(control->axisRX());
    if (INVERTIR_DIRECCION) giro = -giro;
    objetivoAngulo = CENTRO + (int)round(giro * LIMITE_GIRO);

    // --- Motor (joystick izquierdo, arriba = adelante) ---
    float potencia = -normalizar(control->axisY());

    if (!armado) {
      if (potencia == 0.0) {
        armado = true;
        Serial.println("Motor armado.");
      } else if (!avisoArmado) {
        avisoArmado = true;
        Serial.println("AVISO: lleva el joystick izquierdo al centro para armar el motor.");
      }
    } else if (potencia != 0.0) {
      int magnitud = PWM_MINIMO + (int)(fabs(potencia) * (VEL_MAX - PWM_MINIMO));
      magnitud = constrain(magnitud, 0, VEL_MAX);
      objetivoVelocidad = (potencia > 0) ? magnitud : -magnitud;
    }
  } else {
    // Sin control: parar el motor, centrar la dirección y parpadear el LED
    velocidadActual = 0;
    armado = false;
    if (ahora - ultimoParpadeo >= 500) {
      ultimoParpadeo = ahora;
      digitalWrite(LED, !digitalRead(LED));
    }
  }

  // --- Rampa de velocidad (protege el regulador y suaviza el arranque) ---
  bool acelerando = (velocidadActual == 0) ||
                    ((long)objetivoVelocidad * velocidadActual > 0 &&
                     abs(objetivoVelocidad) > abs(velocidadActual));
  int paso = acelerando ? RAMPA_SUBIDA : RAMPA_BAJADA;

  if (velocidadActual < objetivoVelocidad) {
    velocidadActual = min(velocidadActual + paso, objetivoVelocidad);
  } else if (velocidadActual > objetivoVelocidad) {
    velocidadActual = max(velocidadActual - paso, objetivoVelocidad);
  }
  aplicarMotor(velocidadActual);

  // --- Servo: suavizado exponencial + límite de velocidad ---
  // Evita arranques y cambios de sentido bruscos, que son los que piden más corriente.
  float diferencia = (objetivoAngulo - anguloF) * SERVO_SUAVIZADO;
  diferencia = constrain(diferencia, -SERVO_PASO_MAX, SERVO_PASO_MAX);
  anguloF += diferencia;
  anguloF = constrain(anguloF, CENTRO - LIMITE_GIRO, CENTRO + LIMITE_GIRO);

  int nuevoAngulo = (int)round(anguloF);
  if (nuevoAngulo != anguloActual) {       // solo se manda al servo si cambió
    anguloActual = nuevoAngulo;
    direccion.write(anguloActual);
  }

  // --- Monitor: solo si hay movimiento y cambió algo ---
  bool enMovimiento = (anguloActual != CENTRO) || (velocidadActual != 0);
  if (enMovimiento) {
    if ((anguloActual != ultimoAngulo || velocidadActual != ultimoPwm) &&
        ahora - ultimoMensaje >= 200) {
      ultimoMensaje = ahora;
      ultimoAngulo = anguloActual;
      ultimoPwm = velocidadActual;
      Serial.printf("Dir: %d | PWM: %d (%d%%)\n",
                    anguloActual, velocidadActual, (abs(velocidadActual) * 100) / 255);
    }
  } else {
    ultimoAngulo = CENTRO;
    ultimoPwm = 0;
  }
}
