# Carro RC con ESP32 (tracción BTS7960 + dirección por servo)

Proyecto personal de electrónica y firmware, en desarrollo desde 2023. Es un carro a control remoto con tracción por motor DC de escobillas y dirección por servomotor, controlado por una ESP32. El objetivo final es manejarlo con un control físico para competir en pruebas de carrera, donde la precisión de la dirección es el requisito principal.

> **Estado actual (octubre de 2026):** el hardware de potencia y dirección está validado en banco. El motor ya gira con el firmware de prueba. Falta validar el regulador LM2596 bajo carga y conectar un control físico compatible.

---

## Contenido

1. [Resumen técnico](#1-resumen-técnico)
2. [Hardware](#2-hardware)
3. [Arquitectura de alimentación](#3-arquitectura-de-alimentación)
4. [Conexiones](#4-conexiones)
5. [Firmware](#5-firmware)
6. [Procedimiento de puesta en marcha](#6-procedimiento-de-puesta-en-marcha)
7. [Lecciones aprendidas y diagnóstico](#7-lecciones-aprendidas-y-diagnóstico)
8. [Seguridad](#8-seguridad)
9. [Pendientes](#9-pendientes)

---

## 1. Resumen técnico

| Subsistema | Solución |
|---|---|
| Controlador | ESP32 (dual core, 240 MHz, 4 MB flash) sobre placa de expansión de pines |
| Tracción | Motor RS550 de 6 V, controlado por puente H BTS7960 con PWM |
| Dirección | Servo MG995, con recorrido calibrado alrededor de 90° (ruedas rectas) |
| Comunicación | BLE (perfil tipo Nordic UART) en la versión original; control físico en desarrollo |
| Alimentación | Dos fuentes separadas con tierra común: lógica por un lado, potencia por otro |

Principio de diseño: la energía del motor y la de la electrónica de control están separadas, para que los picos de corriente del motor no reinicien la ESP32.

![Diagrama de alimentación](../img/diagrama_alimentacion.png)

---

## 2. Hardware

| Componente | Función | Notas |
|---|---|---|
| ESP32 + placa de expansión | Control | Puerto DC de 6.5 a 16 V, USB-C y micro-USB. Jumper de 5V/3.3V para la fila V |
| BTS7960 | Puente H del motor | Lógica a 5 V. B+/B− de 5.5 a 27 V |
| Motor RS550 6 V | Tracción | Alto consumo de arranque |
| Servo MG995 | Dirección | Pulso de 500 a 2400 µs |
| LM2596 (ajustable) | Regulador a 6 V para el motor | Máximo aproximado de 3 A |
| LiPo 11.1 V, 460 mAh, 35C | Fuente del motor | Capacidad de descarga de unos 16 A |
| Pack de 3 celdas 18650 | Fuente de la ESP32 y el servo | Entra por el puerto DC de la placa |
| Pack de 2 celdas 18650 en serie (7.4 V) | Fuente del motor en pruebas | Alternativa a la LiPo |
| Interruptores, fusible de 10 a 15 A | Protección | El fusible va en el positivo de la LiPo |



**Descartado en esta revisión:** módulo HC-06 y ESP8266 (el control ahora va directo por la ESP32).

---

## 3. Arquitectura de alimentación

| Fuente | Alimenta | Pasa por |
|---|---|---|
| Pack de 3 × 18650 (11.1 V) | ESP32, placa de expansión y servo | SW1 y el puerto DC de la placa |
| LiPo 11.1 V | Motor | SW2 con fusible, LM2596 a 6 V y bornes B+/B− del BTS7960 |

Reglas que se deducen de las pruebas:

- **Tierra común obligatoria.** Se resuelve con un cable del pin A (GND) del BTS7960 a un GND de la placa. El pin A está unido internamente al borne 1 (B−) y los reguladores tipo LM2596 comparten IN− y OUT−, así que todos los negativos quedan en el mismo punto. No hace falta un segundo cable.
- **Voltaje mínimo del driver.** El BTS7960 se bloquea por debajo de 5.5 V en B+/B−. Con una sola celda de 3.7 V el motor no gira.
- **No alimentar por USB y por el puerto DC a la vez.** No se verificó si la placa aísla ambas fuentes.

---

## 4. Conexiones

### 4.1 BTS7960 a la placa de expansión

| Pin del BTS7960 | Pin de la placa |
|---|---|
| A (GND) | GND |
| B (VCC) | 5V |
| E (L_EN) y F (R_EN), unidos con un puente | D22 |
| G (LPWM, reversa) | D18 |
| H (RPWM, adelante) | D5 |
| C y D | Sin conectar |

### 4.2 Bornes de potencia del BTS7960

| Borne | Conexión |
|---|---|
| 1 (B−) | OUT− del LM2596 (negativo) |
| 2 (B+) | OUT+ del LM2596 (6 V) |
| 3 (M+) | Cable rojo del motor |
| 4 (M−) | Cable negro del motor |

### 4.3 Servo MG995

| Cable | Pin de la placa |
|---|---|
| Naranja (señal) | D21 |
| Rojo | 5V |
| Café | GND |

### 4.4 Regulador y fuentes

| Desde | Hacia |
|---|---|
| LiPo (+) | SW2 con fusible y IN+ del LM2596 |
| LiPo (−) | IN− del LM2596 |
| Pack de 3 celdas | Puerto DC de la placa, con SW1 en el positivo |

El LM2596 se ajusta a **6.0 V** con un multímetro **antes** de conectar el BTS7960.

![Diagrama de señales, colores y pines](../img/diagrama_senales_colores_pines.png)

---

## 5. Firmware

| Archivo | Descripción |
|---|---|
| `firmware/carro_esp32_pruebas_completo.ino` | Banco de pruebas actual: servo y motor en bucle, con 10 parpadeos de espera y comandos por Serial |
| `firmware/codigo_unificado_ble.ino` | Versión original por BLE: comandos `w`, `s`, `r` y `l`, con inercia del motor y retorno automático del servo |

**Dependencia:** librería `ESP32Servo`. Placa seleccionada en el IDE: ESP32 Dev Module.

### Parámetros relevantes

| Parámetro | Valor |
|---|---|
| Centro del servo (ruedas rectas) | 90° |
| Amplitudes de prueba del servo | ±18°, ±30° y ±46° |
| Tope de PWM del motor | `255 × 6 / V_alimentación` |
| Con 7.4 V (2 celdas) | Tope de 206 |
| Con regulador a 6 V | Tope de 255 |
| Con LiPo directa de 12.6 V | Tope de aproximadamente 121 |

Al cambiar la fuente del motor, hay que actualizar `VOLTAJE_BATERIA` en el firmware.

---

## 6. Procedimiento de puesta en marcha

1. Dejar todas las baterías desconectadas y cargar el firmware por USB.
2. Desconectar el USB.
3. Conectar la fuente del motor, primero el negativo y al final el positivo.
4. Conectar el pack de 3 celdas al puerto DC de la placa.
5. El LED integrado parpadea 10 veces y comienza la prueba automática.
6. Con el cable USB y el Serial Monitor a 115200 se pueden usar los comandos `1` (servo), `2` (motor) y `3` (ambos).

Probar siempre con las ruedas en el aire.

---

## 7. Lecciones aprendidas y diagnóstico

| Síntoma | Causa encontrada |
|---|---|
| El motor no giraba con 3.7 V | El BTS7960 necesita al menos 5.5 V en B+/B− |
| VCC del driver sin 5 V | El jumper de la placa estaba en 3.3V; debe cubrir 5V y V |
| El motor no giraba con el firmware de prueba en D25, D26, D27 y D33 | Con esos pines y PWM no giró, aunque con señal digital fija sí. No se determinó la causa. Con D22, D18 y D5 funciona |
| La batería caía de 3.7 V a 1.7 V al activar el driver | Sobrecarga de una sola celda, no un fallo del módulo |
| Humo al conectar la batería | Evento aislado en el primer montaje. El módulo siguió funcionando |

### Rutina de diagnóstico del BTS7960

1. Medir 5 V entre A y B.
2. Medir 5 V en E, F y H cuando se activan por cable.
3. Con la batería conectada, medir B+ contra B−.
4. Con H a 5 V, medir M+ contra B−: debe dar aproximadamente el voltaje de la batería.
5. Probar el motor aparte conectándolo directo a una batería.

---

## 8. Seguridad

- Verificar la polaridad con multímetro antes de conectar cualquier batería. No guiarse solo por el color de los cables.
- Usar un fusible de 10 a 15 A en el positivo de la LiPo.
- No cortocircuitar celdas 18650 sin protección.
- Evitar pasar de 6 V efectivos en el RS550: limitar el PWM según el voltaje de la fuente.
- No cargar ni dejar la LiPo desatendida. No descargarla por debajo de 9 V.
- Desconectar la batería del motor antes de mover cables.

---

## 9. Pendientes

- [ ] Validar el LM2596 con el motor bajo carga. Su límite es de unos 3 A y podría caer al arrancar.
- [ ] Conectar un control físico compatible (la placa seleccionada actualmente es una variante Bluepad32).
- [ ] Calibrar la dirección con el control: precisión y límites de recorrido.
- [ ] Definir si el servo se alimenta desde la placa o desde un regulador de 5 V aparte.
- [ ] Documentar tiempos de vuelta y consumo en pista.

---

## Estructura del repositorio

```
.
├── README.md
├── firmware/
│   ├── carro_esp32_pruebas_completo.ino
│   └── codigo_unificado_ble.ino
└── img/
    ├── diagrama_alimentacion.png
    ├── diagrama_senales.png
    ├── bts7960_pinout.png
    ├── placa_expansion.png
    └── lm2596.png
```