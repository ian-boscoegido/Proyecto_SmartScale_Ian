# SmartScale ESP32

SmartScale es una báscula inteligente desarrollada con ESP32. El sistema permite medir peso mediante una célula de carga y un módulo HX711, mostrar la información en una pantalla TFT, guardar datos en microSD y acceder a una web local para gestionar usuarios e historial.

## Descripción del proyecto

El objetivo del proyecto es crear una báscula más completa que una báscula convencional. Además de medir el peso, SmartScale puede detectar cuándo el peso está estable, asociar la medición a un usuario, calcular el IMC y guardar los datos en archivos CSV.

El ESP32 actúa como controlador principal del sistema. Se encarga de leer el sensor de peso, aplicar tara y calibración, filtrar las lecturas, mostrar información por pantalla, crear una red Wi-Fi propia y servir una página web local.

## Funcionalidades principales

* Medición de peso con célula de carga y módulo HX711.
* Calibración mediante pesos conocidos.
* Tara mediante botón físico, monitor serie o web.
* Filtro de media móvil para suavizar lecturas.
* Detección automática de peso estable.
* Pantalla TFT ILI9341 para mostrar peso y estado.
* Red Wi-Fi propia creada por el ESP32.
* Servidor web local accesible desde `192.168.4.1`.
* Gestión de usuarios desde la web.
* Cálculo de IMC.
* Guardado de perfiles y pesadas en archivos CSV.
* Uso de microSD como almacenamiento principal.
* Uso de LittleFS como respaldo interno y para alojar la web.

## Hardware utilizado

* ESP32 DevKit
* Módulo HX711
* Célula de carga
* Pantalla TFT ILI9341 2.4” 240x320
* Módulo microSD
* Tarjeta microSD
* Botón físico para tara
* Protoboard
* Cables Dupont
* Plataforma de madera para la báscula

## Conexiones principales

### HX711

| HX711     | ESP32  |
| --------- | ------ |
| DT / DOUT | GPIO 4 |
| SCK       | GPIO 5 |
| VCC       | 3V3    |
| GND       | GND    |

### Botón de tara

| Botón | ESP32   |
| ----- | ------- |
| Pin 1 | GPIO 18 |
| Pin 2 | GND     |

El botón usa `INPUT_PULLUP`, por lo que no necesita resistencia externa.

### Pantalla TFT ILI9341

| TFT  | ESP32   |
| ---- | ------- |
| GND  | GND     |
| VCC  | 3V3     |
| CLK  | GPIO 14 |
| MOSI | GPIO 13 |
| MISO | GPIO 19 |
| RES  | GPIO 25 |
| DC   | GPIO 26 |
| BLK  | 3V3     |

### MicroSD

| microSD | ESP32   |
| ------- | ------- |
| VCC     | 3V3     |
| GND     | GND     |
| CS      | GPIO 27 |
| SCK     | GPIO 22 |
| MOSI    | GPIO 23 |
| MISO    | GPIO 21 |

La microSD usa un bus SPI separado mediante `HSPI` para evitar conflictos con la pantalla TFT.

## Buses y comunicaciones utilizadas

* **SPI**: pantalla TFT y microSD.
* **HSPI**: bus SPI separado para la microSD.
* **UART**: monitor serie para depuración, tara, calibración y guardado.
* **Wi-Fi AP**: el ESP32 crea la red `SmartScale`.
* **HTTP**: comunicación entre la web y el ESP32.
* **DT/SCK del HX711**: interfaz digital propia del HX711, no es I2C ni SPI.

## Estructura del proyecto

```text
SMARTSCALE_TEST
├── data
│   └── index.html
├── include
├── lib
├── src
│   └── main.cpp
├── test
├── .gitignore
└── platformio.ini
```

## Web local

El ESP32 crea una red Wi-Fi propia:

```text
SSID: SmartScale
Password: 12345678
```

Una vez conectado a esa red, se puede acceder a la web desde:

```text
http://192.168.4.1
```

Desde la web se puede:

* Ver el peso actual.
* Seleccionar usuario activo.
* Crear perfiles de usuario.
* Hacer tara.
* Consultar historial.
* Ver el estado de la microSD.

## Archivos CSV

El proyecto utiliza dos archivos CSV:

### `perfiles.csv`

Guarda los usuarios registrados.

```csv
nombre,altura,edad,sexo,objetivo,peso_objetivo
```

### `pesos.csv`

Guarda las mediciones de peso.

```csv
fecha_hora,nombre,altura,peso,imc,estado
```

La microSD se usa como almacenamiento principal. LittleFS se utiliza como respaldo interno y para alojar la página web.

## Comandos por monitor serie

El proyecto permite controlar varias funciones desde el Monitor Serie de PlatformIO:

| Comando | Acción                     |
| ------- | -------------------------- |
| `0`     | Hacer tara                 |
| `1`     | Calibrar con 1 kg          |
| `2`     | Calibrar con 2 kg          |
| `3`     | Calibrar con 3 kg          |
| `4`     | Calibrar con 4 kg          |
| `7`     | Calibrar con 72 kg         |
| `g`     | Guardar calibración y tara |
| `r`     | Resetear memoria           |

## Calibración

Para calibrar correctamente:

1. Dejar la báscula vacía.
2. Enviar el comando `0` para hacer tara.
3. Colocar un peso conocido.
4. Enviar el comando correspondiente al peso usado.
5. Comprobar que la lectura sea correcta.
6. Guardar con el comando `g`.

Ejemplo para calibrar con 72 kg:

```text
0  → hacer tara con la báscula vacía
7  → calibrar con 72 kg
g  → guardar calibración
```

## Funcionamiento general

1. El HX711 lee la señal de la célula de carga.
2. El ESP32 convierte la lectura raw a kilogramos usando tara y factor de calibración.
3. Se aplica un filtro de media móvil para estabilizar la lectura.
4. Si el peso permanece estable durante un tiempo, se considera peso final.
5. El resultado se muestra en la pantalla TFT.
6. El peso final se guarda automáticamente en CSV.
7. La web local permite consultar y gestionar los datos.

## Tecnologías utilizadas

* ESP32
* PlatformIO
* Arduino Framework
* HX711
* Adafruit GFX
* Adafruit ILI9341
* LittleFS
* microSD
* Wi-Fi AP
* WebServer
* HTML, CSS y JavaScript

## Autor

Proyecto desarrollado como trabajo de Procesadores Digitales.

Autor: Ian Bosco
