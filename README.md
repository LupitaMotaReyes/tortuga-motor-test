# tortuga-motor-test

Prueba mínima y aislada del control de **un solo motor N20** del proyecto Tortuga
con un **Seeed Studio XIAO ESP32‑C6** y un driver **TB6612FNG** (canal A).

No incluye TCS34725, LEDs de batería, divisor de voltaje, Bluetooth ni ningún otro
componente: solo comprueba que el XIAO controla el motor en ambos sentidos.

## Hardware

| Componente | Notas |
|---|---|
| Seeed Studio XIAO ESP32‑C6 | GPIO a 3.3 V |
| Driver TB6612FNG | Solo canal A (AIN1, AIN2, PWMA, STBY → AO1, AO2) |
| Motorreductor N20 DC | Uno solo |
| Batería LiPo 3.7 V | Alimenta VM del TB6612FNG |

## Asignación de pines

Pinout tomado del variant oficial `variants/XIAO_ESP32C6/pins_arduino.h` (Arduino core 3.3.12).
El código usa **números de GPIO reales**, no constantes `Dx`.

| Señal TB6612FNG | GPIO (ESP32‑C6) | Pin físico XIAO | Función en el código |
|---|---|---|---|
| **STBY** | GPIO0 | **D0** | `HIGH` = driver activo, `LOW` = standby |
| **AIN1** | GPIO1 | **D1** | Dirección |
| **AIN2** | GPIO2 | **D2** | Dirección |
| **PWMA** | GPIO21 | **D3** | PWM LEDC 20 kHz, 8 bits |

> No se usan D6–D9: con D6 (GPIO16) y D7 (GPIO17), que son UART0 TX/RX, el motor no
> funcionó. D4/D5 quedan libres para el I²C del TCS34725.

> Para medir con multímetro, pon `DIAG_HOLD_FORWARD = true` en `src/main.cpp`: el motor
> queda fijo adelante al 100 % (STBY=H, AIN1=H, AIN2=L, PWMA=H).

> Recomendación: pon una resistencia de **10 kΩ de STBY a GND**. El TB6612FNG ya tiene
> pull‑downs internos, pero así el driver queda garantizado en standby mientras el XIAO
> arranca, se resetea o se está programando.

## Conexiones

### XIAO ESP32‑C6 → TB6612FNG

| XIAO | TB6612FNG |
|---|---|
| D0 (GPIO0) | STBY |
| D1 (GPIO1) | AIN1 |
| D2 (GPIO2) | AIN2 |
| D3 (GPIO21) | PWMA |
| 3V3 | VCC (lógica) |
| GND | GND |

### Motor

| TB6612FNG | Motor N20 |
|---|---|
| AO1 | Terminal 1 |
| AO2 | Terminal 2 |

Si el motor gira al revés de lo que consideras "adelante", intercambia AO1/AO2
(o intercambia `PIN_AIN1`/`PIN_AIN2` en el código).

### VM (potencia del motor)

- **VM ← LiPo + (3.7 V)**. El TB6612FNG admite VM de 2.5 V a 13.5 V.
- Pon un capacitor de **10–100 µF** entre VM y GND cerca del driver (muchos módulos ya lo traen).
- El motor **nunca** se alimenta desde un GPIO ni desde el pin 3V3 del XIAO.

### VCC (lógica)

- **VCC ← 3V3 del XIAO**. Así los niveles lógicos del driver coinciden con los GPIO de 3.3 V
  (el TB6612FNG admite VCC de 2.7 V a 5.5 V).

### GND común

**GND de la batería, GND del TB6612FNG y GND del XIAO deben estar unidos.**
Sin tierra común, las señales AIN1/AIN2/PWMA/STBY no tienen referencia: el motor no gira,
gira de forma errática o el driver se comporta aleatoriamente.

Si la LiPo está conectada a los pads BAT+/BAT− del XIAO, la tierra ya es común; toma VM
desde BAT+ y une el GND del driver al GND del XIAO.

## Entorno de compilación

- **PlatformIO** con la plataforma **pioarduino** (`platform-espressif32` stable) →
  Arduino core **3.3.12** (ESP‑IDF 5.5).
  La plataforma oficial `platformio/espressif32` solo trae Arduino core 2.x, que no soporta ESP32‑C6.
- Board: **`seeed_xiao_esp32c6`** (define `ARDUINO_XIAO_ESP32C6`, MCU `esp32c6`, 4 MB flash,
  USB‑CDC al arrancar).
- `main.cpp` tiene un `#error` si se compila para algo que no sea XIAO ESP32‑C6.
- PWM con la API LEDC actual del core 3.x: `ledcAttach(pin, freq, bits)` + `ledcWrite(pin, duty)`
  (las funciones antiguas `ledcSetup`/`ledcAttachPin` ya no existen en el core 3.x).

### Nota para Windows

`platformio.ini` guarda los paquetes en `C:/pio` porque, sin "long paths" habilitado en
Windows, el Arduino core 3.x no se puede descomprimir (rutas > 260 caracteres).
Si habilitas `LongPathsEnabled` en Windows puedes borrar las líneas `packages_dir` y `cache_dir`.

## Compilar

Desde la carpeta del repo, en PowerShell:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
```

(Si `pio` está en tu PATH, basta con `pio run`. En VS Code con la extensión PlatformIO:
botón ✓ *Build*.)

## Subir el firmware

1. Conecta el XIAO por USB‑C.
2. Busca el puerto:

   ```powershell
   & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device list
   ```

   El XIAO ESP32‑C6 aparece como "Dispositivo serie USB" con `VID:PID=303A:1001`.
3. Sube (cambia `COM11` por tu puerto):

   ```powershell
   & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload --upload-port COM11
   ```

Si no conecta: mantén presionado **BOOT**, presiona y suelta **RESET**, suelta **BOOT**
y vuelve a ejecutar el upload. Después de cargar, presiona **RESET** una vez.

> Por seguridad, haz la primera carga con la rueda del motor libre (sin tocar el suelo).

## Serial Monitor

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor --port COM11
```

(115200 baudios, ya configurado en `platformio.ini`; salir con `Ctrl+C`.)
Subir y abrir el monitor de una vez:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload -t monitor --upload-port COM11
```

## Comportamiento esperado

Al encender:

1. STBY `LOW`, AIN1 `LOW`, AIN2 `LOW`, PWM `0` → driver en standby, motor quieto.
2. `Motor test starting` + resumen de pines y PWM.
3. `Stop (startup)` → 2 s detenido.
4. `STBY enabled` → el driver sale de standby.

Luego, en ciclo:

| Mensaje | Motor | Duración |
|---|---|---|
| `Forward` | gira adelante a ~55 % (duty 140/255) | 3 s |
| `Stop` | detenido (AIN1=AIN2=LOW, PWM 0) | 2 s |
| `Reverse` | gira en sentido contrario a ~55 % | 3 s |
| `Stop` | detenido | 2 s |

La velocidad se cambia en `TEST_DUTY` (`src/main.cpp`).

## Solución de problemas

### El motor no gira

- ¿Aparece `STBY enabled` en el Serial Monitor? Si no, el programa no llegó a activar el driver.
- **GND común** entre batería, TB6612FNG y XIAO.
- Mide **VM** con multímetro: debe tener ~3.7 V (LiPo cargada).
- Mide **VCC** del driver: ~3.3 V.
- Mide **STBY**: debe estar en ~3.3 V después de `STBY enabled`.
- Mide AIN1/AIN2 durante `Forward`: uno en ~3.3 V y el otro en 0 V.
- Revisa que el motor esté en **AO1/AO2** (no en BO1/BO2) y que las señales vayan a
  AIN1/AIN2/PWMA (no a BIN1/BIN2/PWMB).
- Prueba subir `TEST_DUTY` (p. ej. 200): un N20 con mucha reducción a 3.7 V y 55 % puede no
  vencer la fricción inicial.
- Prueba el motor directamente con la batería para descartar un motor dañado.

### Gira solo en una dirección

- Una de las líneas AIN1 o AIN2 no llega: revisa continuidad de D1→AIN1 y D2→AIN2.
- Mide ambas durante `Forward` y `Reverse`: deben alternarse entre 3.3 V y 0 V.
- Verifica que no estén intercambiadas con BIN1/BIN2 ni en corto entre sí.
- Pin del módulo mal soldado (muy común en headers de TB6612FNG).

### El XIAO se reinicia cuando arranca el motor

Es una caída de voltaje (brown‑out) o ruido del motor:

- Asegura que el motor se alimenta de **VM**, nunca del 3V3 del XIAO.
- Agrega/verifica capacitor **10–100 µF** entre VM y GND cerca del driver.
- Suelda un capacitor cerámico de **100 nF** directamente entre los terminales del motor N20.
- LiPo descargada o con poca capacidad de corriente: cárgala o prueba otra.
- Cables delgados/largos o mala conexión de GND (punto de tierra común, cables cortos).
- Baja temporalmente `TEST_DUTY` para reducir la corriente de arranque.
- Si el XIAO también se alimenta de la LiPo, prueba alimentarlo por USB y solo el motor por
  batería (con GND común) para confirmar que es caída de voltaje.

## Estructura

```
tortuga-motor-test/
├── README.md
├── platformio.ini
└── src/
    └── main.cpp
```
