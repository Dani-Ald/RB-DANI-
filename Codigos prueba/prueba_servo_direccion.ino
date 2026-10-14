#include <ESP32Servo.h>

// ===== CONFIGURACIÓN =====
const int PIN_SERVO = 21;      // D21 en la placa de expansión
const int CENTRO = 90;         // Posición perfecta (ruedas rectas)

// Amplitudes de la prueba (grados a cada lado del centro)
// Progresivo: primero poco, luego más. Valores finales calibrados.
const int AMPLITUDES[] = {18, 30, 46};
const int NUM_AMPLITUDES = 3;

const int PASO_GRADOS = 1;     // Grados por movimiento (más bajo = más suave)
const int RETARDO_PASO = 25;   // ms entre pasos (más alto = más lento)
const int PAUSA_EXTREMO = 600; // ms de pausa al llegar a cada extremo

Servo direccion;
int posicionActual = CENTRO;

// Mueve el servo poco a poco hasta el ángulo destino
void moverSuave(int destino) {
  while (posicionActual != destino) {
    if (posicionActual < destino) {
      posicionActual = min(posicionActual + PASO_GRADOS, destino);
    } else {
      posicionActual = max(posicionActual - PASO_GRADOS, destino);
    }
    direccion.write(posicionActual);
    delay(RETARDO_PASO);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // MG995: pulsos de 500 a 2400 us
  direccion.setPeriodHertz(50);
  direccion.attach(PIN_SERVO, 500, 2400);

  direccion.write(CENTRO);
  posicionActual = CENTRO;
  Serial.println("=== PRUEBA DE DIRECCIÓN ===");
  Serial.println("Inicia en 3 segundos en posición CENTRO...");
  delay(3000);

  for (int i = 0; i < NUM_AMPLITUDES; i++) {
    int a = AMPLITUDES[i];
    Serial.print("--- Ronda ");
    Serial.print(i + 1);
    Serial.print(": +-");
    Serial.print(a);
    Serial.println(" grados ---");

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

  // Estado final = estado inicial
  direccion.write(CENTRO);
  Serial.println("=== PRUEBA TERMINADA: servo en CENTRO ===");
}

void loop() {
  // Nada: la prueba corre una sola vez. Presiona RESET para repetirla.
}
