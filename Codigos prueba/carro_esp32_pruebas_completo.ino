/*
=====================================================================
 CARRO RC - ESP32 + BTS7960 + RS550 + SERVO MG995
 Archivo de referencia: esquema de conexiones, pines, lecciones de
 las pruebas y código de prueba (servo y motor) en un solo archivo.
=====================================================================

---------------------------------------------------------------------
 1. MATERIALES
---------------------------------------------------------------------
 - ESP32 con placa de expansión (puerto DC 6.5 a 16 V, USB-C, micro-USB)
 - Driver BTS7960 (puente H)
 - Motor RS550 de 6 V (cables rojo y negro)
 - Servo MG995 (cables café, rojo y naranja)
 - Pack de 3 baterías 18650 -> alimenta la ESP32 por el puerto DC
 - Pack de 2 baterías 18650 en serie (7.4 V) -> alimenta el MOTOR
 - LiPo 11.1 V 460 mAh 35C -> alimentará el motor en la versión final
 - LM2596 (regulador) -> NO se usa en el camino del motor
 - Interruptores y fusible de 10 a 15 A (para la LiPo)

---------------------------------------------------------------------
 2. ALIMENTACIÓN (lo más importante)
---------------------------------------------------------------------
 ESP32 + placa de expansión:
   Pack de 3 baterías 18650 -> conector al puerto DC de la placa.
   (Mide antes: debe dar entre 9 y 12.6 V. Centro del plug = positivo.)
   El USB-C solo se usa para cargar el código. No tener USB y pack
   conectados al mismo tiempo.

 Jumper amarillo de la placa (junto al USB-C):
   Debe cubrir los pines 5V y V (los dos de ARRIBA).
   Así la fila V de la placa da 5 V. En la parte baja da 3.3 V.

 Motor:
   Batería directo a los bornes del BTS7960, SIN regulador.
     Negativo (-) -> borne 1 (B-)
     Positivo (+) -> borne 2 (B+)
   * Pruebas: 2 celdas 18650 en serie = 7.4 V
   * Versión final: LiPo 11.1 V (12.6 V cargada) con interruptor y
     fusible de 10 a 15 A en el positivo. Cambiar VOLTAJE_BATERIA
     a 12.6 en este código.
   El BTS7960 necesita MÍNIMO 5.5 V en B+ y B-. Con una sola celda
   (3.7 V) el chip se bloquea y el motor no gira.

---------------------------------------------------------------------
 3. CONEXIONES DEL BTS7960 A LA PLACA DE EXPANSIÓN
---------------------------------------------------------------------
 Bloque de 8 pines (filas):  A-B  /  C-D  /  E-F  /  G-H

   Pin BTS7960         Va a (fila S de la columna indicada)
   ------------------  -------------------------------------
   A (GND)             Un pin G (GND) de la placa
   B (VCC)             Un pin V (5 V) de la placa
   E y F (unidos)      D22   (un puente corto E-F, un solo cable)
   G (LPWM, reversa)   D18
   H (RPWM, adelante)  D5
   C y D               Sin conectar

   Bornes verdes:
     1 (B-)  Negativo de la batería del motor
     2 (B+)  Positivo de la batería del motor
     3 (M+)  Cable rojo del motor
     4 (M-)  Cable negro del motor

 Tierra común: el pin A del BTS7960 está unido internamente al
 borne 1. Al conectar A a un pin G de la placa, la ESP32 y la batería
 del motor comparten GND.

---------------------------------------------------------------------
 4. SERVO MG995 (dirección)
---------------------------------------------------------------------
   Cable café    -> fila G de la columna D21
   Cable rojo    -> fila V de la columna D21 (5 V)
   Cable naranja -> fila S de la columna D21 (señal)
   Posición recta de las ruedas = 90 grados en la librería.
   Si la ESP32 se reinicia cuando el servo se esfuerza, alimentar el
   rojo con un regulador aparte a 5 V (GND unido).

---------------------------------------------------------------------
 5. LECCIONES DE LAS PRUEBAS (por qué quedó así)
---------------------------------------------------------------------
 - El LM2596 solo entrega unos 3 A y su voltaje cae al arrancar el
   RS550. Se retiró del camino del motor.
 - El BTS7960 no funciona por debajo de 5.5 V (B+ a B-).
 - El jumper de la placa estaba mal: debe dar 5 V en la fila V.
 - Con señal fija el motor giró en D25/D33/D26/D27, pero con PWM no.
   Con los pines del código original (D22, D18, D5) SÍ funcionó.
   Se queda con esos pines.
 - El servo se inicia ANTES de configurar el motor, como en el código
   original que funcionaba (ESP32Servo reparte los canales PWM).
 - Pin 12 de la ESP32 es de arranque: evitarlo.
 - Librería necesaria: ESP32Servo (Kevin Harrington).
 - Después de un humo en el BTS7960 las pruebas de continuidad no
   sonaron (sin cortos) y el módulo resultó sano.

---------------------------------------------------------------------
 6. PROCEDIMIENTO
---------------------------------------------------------------------
 1. Todas las baterías desconectadas. Sube el código por USB.
 2. Desconecta el USB.
 3. Conecta la batería del motor a los bornes 1 y 2 (primero el
    negativo, al final el positivo).
 4. Conecta el pack de 3 baterías al puerto DC de la placa.
 5. El LED parpadea 10 veces y empieza la prueba.
 Ruedas en el aire. Si algo huele a quemado o se calienta, desconecta
 la batería del motor de inmediato.

---------------------------------------------------------------------
 7. USO
---------------------------------------------------------------------
 Con USB y Serial Monitor a 115200 ("Nueva línea"), o solo con el pack:
   Se repite sola: servo + motor (AUTO_REPETIR = true).
   Comandos por Serial:  1 = servo   2 = motor   3 = ambos
=====================================================================
*/

#include <ESP32Servo.h>

// ===== PINES =====
const int LED = 2;            // LED integrado de la ESP32

const int PIN_SERVO = 21;     // Servo (columna D21)

const int EN   = 22;          // E y F del BTS7960, unidos
const int LPWM = 18;          // G (reversa)
const int RPWM = 5;           // H (adelante)

// ===== SERVO =====
const int CENTRO = 90;                       // ruedas rectas
const int AMPLITUDES[] = {18, 30, 46};       // grados a cada lado
const int NUM_AMPLITUDES = 3;
const int PASO_GRADOS = 1;
const int RETARDO_SERVO = 25;                // ms entre pasos
const int PAUSA_EXTREMO = 600;               // ms

Servo direccion;
int posicionActual = CENTRO;

// ===== MOTOR =====
// 2 celdas en serie = 7.4. Con la LiPo de 11.1 V, poner 12.6.
const float VOLTAJE_BATERIA = 7.4;
const float VOLTAJE_MOTOR = 6.0;             // RS550 de 6 V
const int VEL_MAX = (VOLTAJE_BATERIA <= VOLTAJE_MOTOR)
                      ? 255
                      : (int)(255.0 * VOLTAJE_MOTOR / VOLTAJE_BATERIA);

const int PASO_PWM = 25;                     // igual que el código original
const int RETARDO_PASO = 300;                // ms entre pasos
const int TIEMPO_MAXIMO = 2500;              // ms a velocidad tope

// ===== GENERAL =====
const bool AUTO_REPETIR = true;
const int PAUSA_CICLO = 5000;
int ciclo = 0;

// ---------------------------------------------------------------------
// SERVO
// ---------------------------------------------------------------------
void moverSuave(int destino) {
  while (posicionActual != destino) {
    if (posicionActual < destino) {
      posicionActual = min(posicionActual + PASO_GRADOS, destino);
    } else {
      posicionActual = max(posicionActual - PASO_GRADOS, destino);
    }
    direccion.write(posicionActual);
    delay(RETARDO_SERVO);
  }
}

void pruebaServo() {
  Serial.println("=== PRUEBA DE SERVO ===");
  for (int i = 0; i < NUM_AMPLITUDES; i++) {
    int a = AMPLITUDES[i];
    Serial.print("Ronda ");
    Serial.print(i + 1);
    Serial.print(": +-");
    Serial.print(a);
    Serial.println(" grados");

    Serial.println("DERECHA");
    moverSuave(CENTRO + a);
    delay(PAUSA_EXTREMO);

    Serial.println("CENTRO");
    moverSuave(CENTRO);
    delay(PAUSA_EXTREMO);

    Serial.println("IZQUIERDA");
    moverSuave(CENTRO - a);
    delay(PAUSA_EXTREMO);

    Serial.println("CENTRO");
    moverSuave(CENTRO);
    delay(PAUSA_EXTREMO);
  }
  direccion.write(CENTRO);
  Serial.println("Servo en CENTRO");
}

// ---------------------------------------------------------------------
// MOTOR
// ---------------------------------------------------------------------
void aplicar(int dir, int pwm) {
  pwm = constrain(pwm, 0, VEL_MAX);
  if (dir == 1) {
    analogWrite(RPWM, pwm);
    analogWrite(LPWM, 0);
  } else {
    analogWrite(LPWM, pwm);
    analogWrite(RPWM, 0);
  }
  Serial.print(dir == 1 ? "ADELANTE" : "ATRAS");
  Serial.print(" PWM: ");
  Serial.print(pwm);
  Serial.print(" (");
  Serial.print((pwm * 100) / 255);
  Serial.println("%)");
}

void detener() {
  analogWrite(LPWM, 0);
  analogWrite(RPWM, 0);
  digitalWrite(LED, LOW);
  Serial.println("MOTOR DETENIDO");
}

void subirYBajar(int dir) {
  digitalWrite(LED, HIGH);
  for (int v = PASO_PWM; v < VEL_MAX; v += PASO_PWM) {
    aplicar(dir, v);
    delay(RETARDO_PASO);
  }
  aplicar(dir, VEL_MAX);
  delay(TIEMPO_MAXIMO);
  for (int v = VEL_MAX - PASO_PWM; v > 0; v -= PASO_PWM) {
    aplicar(dir, v);
    delay(RETARDO_PASO);
  }
  detener();
}

void pruebaMotor() {
  Serial.println("=== PRUEBA DE MOTOR ===");
  Serial.println("##### ADELANTE #####");
  subirYBajar(1);
  delay(2000);
  Serial.println("##### REVERSA #####");
  subirYBajar(-1);
  delay(2000);
}

// ---------------------------------------------------------------------
// GENERAL
// ---------------------------------------------------------------------
void pruebaCompleta() {
  ciclo++;
  Serial.print("##### CICLO ");
  Serial.print(ciclo);
  Serial.println(" #####");
  pruebaServo();
  delay(1000);
  pruebaMotor();
}

void menu() {
  Serial.println("Comandos: 1 = servo | 2 = motor | 3 = ambos");
}

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);

  // Servo primero (como en el código original que funcionaba)
  direccion.setPeriodHertz(50);
  direccion.attach(PIN_SERVO, 500, 2400);
  direccion.write(CENTRO);
  posicionActual = CENTRO;

  // Motor
  pinMode(EN, OUTPUT);
  pinMode(LPWM, OUTPUT);
  pinMode(RPWM, OUTPUT);
  digitalWrite(EN, HIGH);
  analogWrite(LPWM, 0);
  analogWrite(RPWM, 0);

  Serial.println("=== CARRO RC: PRUEBA COMPLETA ===");
  Serial.print("Tope PWM: ");
  Serial.println(VEL_MAX);
  menu();

  for (int i = 1; i <= 10; i++) {
    digitalWrite(LED, HIGH); delay(500);
    digitalWrite(LED, LOW);  delay(500);
    Serial.print("Parpadeo ");
    Serial.print(i);
    Serial.println(" de 10");
  }
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == '1') pruebaServo();
    else if (c == '2') pruebaMotor();
    else if (c == '3') pruebaCompleta();
    else if (c != '\n' && c != '\r') menu();
    return;
  }

  if (AUTO_REPETIR) {
    pruebaCompleta();
    Serial.println("Ciclo terminado. Repite en 5 segundos...");
    delay(PAUSA_CICLO);
  }
}
