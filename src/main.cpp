#include "HX711.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <time.h>
#include <sys/time.h>

// =====================================================
// PINES DEL PROYECTO
// =====================================================
// Aquí defino todos los pines que uso en el proyecto:
// el HX711, la pantalla TFT, la microSD y el botón de tara.

// HX711 - célula de carga
#define DOUT 4
#define CLK 5

// Pantalla TFT ILI9341 por SPI
#define TFT_SCLK 14
#define TFT_MOSI 13
#define TFT_MISO 19
#define TFT_RST  25
#define TFT_DC   26
#define TFT_CS   -1
#define TFT_BL   -1

// microSD por otro bus SPI
#define SD_SCK   22
#define SD_MISO  21
#define SD_MOSI  23
#define SD_CS    27

// Botón físico para hacer tara
#define BUTTON_PIN 18

// =====================================================
// WIFI Y SERVIDOR WEB
// =====================================================
// El ESP32 funciona como punto de acceso. Es decir, crea su propia red
// Wi-Fi y desde el móvil nos conectamos a la web de la báscula.

const char* AP_SSID = "SmartScale";
const char* AP_PASS = "12345678";

WebServer server(80);

// =====================================================
// OBJETOS PRINCIPALES
// =====================================================
// Aquí creo los objetos principales que uso durante todo el programa:
// sensor de peso, pantalla, memoria de preferencias y bus SPI de la SD.

HX711 scale;
Adafruit_ILI9341 display = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
Preferences prefs;

// Uso un bus SPI separado para la microSD para evitar conflictos con la pantalla.
SPIClass sdSPI(HSPI);

// Variable para saber si la microSD está funcionando.
bool sdDisponible = false;

// =====================================================
// VARIABLES DE CALIBRACIÓN Y PESO
// =====================================================
// Estas variables sirven para calcular el peso real en kg a partir
// de la lectura raw del HX711.

float calibration_factor = 206516.75;
long zero_offset = 0;
float ultimoPeso = 0.0;

// =====================================================
// TEMPORIZACIÓN Y ESTABILIDAD
// =====================================================
// Leo el peso cada cierto tiempo y detecto cuándo se queda estable.
// Cuando el peso está estable durante 2 segundos, lo considero peso final.

const unsigned long INTERVALO_LECTURA = 500;
unsigned long ultimaLectura = 0;

float pesoAnterior = 0.0;
unsigned long tiempoEstableInicio = 0;
bool pesoEstableDetectado = false;
bool mostrandoPesoFinal = false;
unsigned long tiempoPesoFinal = 0;
float pesoFinal = 0.0;

const float UMBRAL_ESTABILIDAD = 0.05;
const unsigned long TIEMPO_ESTABLE = 2000;
const unsigned long TIEMPO_FINAL = 5000;


// Margen para evitar falsas lecturas cuando la báscula está vacía.
// Si el peso calculado es muy pequeño, lo consideramos ruido y lo mostramos como 0.
const float ZONA_MUERTA_KG = 0.4;

// Como la báscula está pensada para personas, no permitimos detectar peso final
// con valores muy bajos. Así evitamos guardar falsas pesadas de 0.2 o 0.3 kg.
const float PESO_MINIMO_ESTABLE_KG = 5.0;
const float PESO_MINIMO_GUARDAR_KG = 5.0;


// =====================================================
// USUARIO ACTIVO
// =====================================================
// La web envía al ESP32 el usuario que está usando la báscula.
// Así el peso final se guarda asociado a ese usuario.

String usuarioActivo = "";
float alturaUsuarioActivo = 0.0;

bool pesoYaGuardadoEnEstaPesada = false;
String ultimoGuardadoFecha = "";

// =====================================================
// FILTRO DE PESO
// =====================================================
// Hago una media de varias lecturas para que el peso no vaya saltando
// tanto por pequeñas variaciones del sensor.

#define FILTER_SIZE 10

float weightBuffer[FILTER_SIZE];
int weightIndex = 0;
bool filterFilled = false;

float aplicarFiltro(float nuevoPeso) {
  weightBuffer[weightIndex] = nuevoPeso;
  weightIndex++;

  if (weightIndex >= FILTER_SIZE) {
    weightIndex = 0;
    filterFilled = true;
  }

  int muestras = filterFilled ? FILTER_SIZE : weightIndex;

  if (muestras == 0) {
    return nuevoPeso;
  }

  float suma = 0.0;

  for (int i = 0; i < muestras; i++) {
    suma += weightBuffer[i];
  }

  return suma / muestras;
}

void resetearFiltro() {
  for (int i = 0; i < FILTER_SIZE; i++) {
    weightBuffer[i] = 0.0;
  }

  weightIndex = 0;
  filterFilled = false;
  pesoAnterior = 0.0;
  ultimoPeso = 0.0;
}

// =====================================================
// PANTALLA TFT
// =====================================================
// Estas funciones solo sirven para mostrar información en la pantalla:
// mensajes, peso mientras mide y peso final cuando se estabiliza.

void mostrarMensaje(String linea1, String linea2) {
  display.fillScreen(ILI9341_BLACK);

  display.setTextColor(ILI9341_GREEN);
  display.setTextSize(3);
  display.setCursor(20, 35);
  display.println("SmartScale");

  display.drawLine(10, 75, 230, 75, ILI9341_GREEN);

  display.setTextColor(ILI9341_WHITE);
  display.setTextSize(2);
  display.setCursor(20, 115);
  display.println(linea1);

  display.setTextColor(ILI9341_CYAN);
  display.setTextSize(2);
  display.setCursor(20, 155);
  display.println(linea2);
}

void mostrarPeso(float peso, String estado) {
  display.fillScreen(ILI9341_BLACK);

  display.setTextColor(ILI9341_WHITE);
  display.setTextSize(2);
  display.setCursor(15, 15);
  display.println("SmartScale");

  display.drawLine(10, 45, 230, 45, ILI9341_GREEN);

  display.setTextColor(ILI9341_GREEN);
  display.setTextSize(5);
  display.setCursor(15, 85);
  display.print(peso, 1);

  display.setTextColor(ILI9341_GREEN);
  display.setTextSize(3);
  display.setCursor(165, 105);
  display.print("kg");

  display.setTextColor(ILI9341_WHITE);
  display.setTextSize(2);
  display.setCursor(15, 180);
  display.print("Estado:");

  display.setTextColor(ILI9341_YELLOW);
  display.setTextSize(2);
  display.setCursor(15, 210);
  display.print(estado);

  if (usuarioActivo.length() > 0) {
    display.setTextColor(ILI9341_CYAN);
    display.setTextSize(2);
    display.setCursor(15, 260);
    display.print(usuarioActivo);
  }
}

void mostrarPesoFinal(float peso) {
  display.fillScreen(ILI9341_BLACK);

  display.setTextColor(ILI9341_GREEN);
  display.setTextSize(3);
  display.setCursor(20, 30);
  display.println("Peso final");

  display.drawLine(10, 70, 230, 70, ILI9341_GREEN);

  display.setTextColor(ILI9341_WHITE);
  display.setTextSize(5);
  display.setCursor(15, 120);
  display.print(peso, 1);

  display.setTextColor(ILI9341_WHITE);
  display.setTextSize(3);
  display.setCursor(165, 140);
  display.print("kg");

  display.setTextColor(ILI9341_CYAN);
  display.setTextSize(2);
  display.setCursor(20, 220);

  if (sdDisponible) {
    display.println("Guardado en SD");
  } else {
    display.println("Guardado interno");
  }

  if (usuarioActivo.length() > 0) {
    display.setTextColor(ILI9341_YELLOW);
    display.setTextSize(2);
    display.setCursor(20, 255);
    display.print(usuarioActivo);
  }
}

// =====================================================
// LECTURA DEL HX711
// =====================================================
// Aquí leo el módulo HX711 de forma segura. Primero compruebo que esté
// listo y después hago una media de varias lecturas raw.

bool esperarHX711(unsigned long timeout_ms) {
  unsigned long inicio = millis();

  while (!scale.is_ready()) {
    if (millis() - inicio > timeout_ms) {
      return false;
    }

    delay(1);
  }

  return true;
}

bool leerRawSeguro(long &valor, int muestras = 5) {
  int64_t suma = 0;

  for (int i = 0; i < muestras; i++) {
    if (!esperarHX711(500)) {
      return false;
    }

    suma += scale.read();
  }

  valor = suma / muestras;
  return true;
}

// =====================================================
// FUNCIONES DE LA BÁSCULA
// =====================================================
// En esta parte están las funciones principales relacionadas con el peso:
// tara, calibración, guardado de parámetros y cálculo de kg.

void resetearEstadoEstabilidad() {
  pesoAnterior = 0.0;
  tiempoEstableInicio = 0;
  pesoEstableDetectado = false;
  mostrandoPesoFinal = false;
  tiempoPesoFinal = 0;
  pesoFinal = 0.0;
  pesoYaGuardadoEnEstaPesada = false;
}

void hacerTara() {
  Serial.println("Tara...");
  mostrarMensaje("Haciendo tara", "No pongas peso");

  delay(1000);

  long raw;

  if (leerRawSeguro(raw, 15)) {
    zero_offset = raw;

    resetearFiltro();
    resetearEstadoEstabilidad();

    Serial.print("Nuevo offset: ");
    Serial.println(zero_offset);

    mostrarMensaje("Tara OK", "Peso = 0.0 kg");
    delay(1000);
  } else {
    mostrarMensaje("ERROR HX711", "No responde");
    delay(1500);
  }
}

void calibrar(float kg) {
  Serial.print("Pon ");
  Serial.print(kg, 1);
  Serial.println(" kg...");

  // Para calibrar con mucho peso, como 72 kg, dejo más tiempo para subirse
  // y hago más muestras para que el factor salga más estable.
  if (kg >= 20.0) {
    mostrarMensaje("Calibrando", "Subete ahora");
    delay(8000);
  } else {
    mostrarMensaje("Calibrando", "Pon el peso");
    delay(5000);
  }

  long raw;
  int muestrasCalibracion = (kg >= 20.0) ? 30 : 15;

  if (leerRawSeguro(raw, muestrasCalibracion)) {
    calibration_factor = (float)(raw - zero_offset) / kg;

    Serial.print("Raw calibracion: ");
    Serial.println(raw);
    Serial.print("Offset: ");
    Serial.println(zero_offset);
    Serial.print("Peso usado: ");
    Serial.print(kg, 1);
    Serial.println(" kg");
    Serial.print("Nuevo factor: ");
    Serial.println(calibration_factor, 6);

    resetearFiltro();
    resetearEstadoEstabilidad();

    mostrarMensaje("Calibracion OK", "Factor actualizado");
    delay(1200);
  } else {
    mostrarMensaje("ERROR HX711", "No responde");
    delay(1500);
  }
}

void guardarParametros() {
  prefs.putFloat("cal", calibration_factor);
  prefs.putLong("offset", zero_offset);

  Serial.println("Guardado en memoria");

  mostrarMensaje("Guardado OK", "Parametros OK");
  delay(1000);
}

void resetearMemoria() {
  Serial.println("Reset memoria");

  prefs.clear();

  calibration_factor = 206516.75;
  zero_offset = 0;

  resetearFiltro();
  resetearEstadoEstabilidad();

  Serial.println("Memoria borrada");

  mostrarMensaje("RESET OK", "Memoria limpia");
  delay(1500);
}

float calcularPesoKg(long raw) {
  if (calibration_factor == 0) {
    return 0.0;
  }

  float peso = (raw - zero_offset) / calibration_factor;

  if (peso < 0.0) {
    peso = 0.0;
  }

  // Zona muerta: si el peso es muy pequeño, lo considero ruido.
  // Esto evita que estando vacía marque 0.1, 0.2 o 0.3 kg.
  if (peso < ZONA_MUERTA_KG) {
    peso = 0.0;
  }

  return peso;
}

// =====================================================
// MICRO SD
// =====================================================
// Inicio la microSD usando un SPI separado. Si no se detecta,
// el proyecto sigue funcionando con LittleFS como respaldo.

void iniciarSD() {
  Serial.println("Iniciando microSD...");

  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS, sdSPI)) {
    sdDisponible = false;
    Serial.println("microSD NO detectada. Se usara LittleFS como respaldo.");
    return;
  }

  sdDisponible = true;

  Serial.println("microSD detectada correctamente.");
  Serial.print("Tipo SD: ");

  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("Sin tarjeta");
    sdDisponible = false;
    return;
  } else if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("Desconocida");
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.print("Tamano SD: ");
  Serial.print(cardSize);
  Serial.println(" MB");
}

// =====================================================
// ARCHIVOS CSV
// =====================================================
// Los usuarios y los pesos se guardan en archivos CSV.
// La web también lee estos archivos para mostrar usuarios, historial y gráfico.

String limpiarCSV(String texto) {
  texto.replace(",", " ");
  texto.replace("\n", " ");
  texto.replace("\r", " ");
  texto.replace(";", " ");
  return texto;
}

void asegurarArchivoLittleFS(String path, String cabecera) {
  if (!LittleFS.exists(path)) {
    File file = LittleFS.open(path, "w");

    if (file) {
      file.println(cabecera);
      file.close();
    }
  }
}

void asegurarArchivoSD(String path, String cabecera) {
  if (!sdDisponible) {
    return;
  }

  if (!SD.exists(path)) {
    File file = SD.open(path, FILE_WRITE);

    if (file) {
      file.println(cabecera);
      file.close();
    }
  }
}

void asegurarArchivoCSV(String path, String cabecera) {
  asegurarArchivoLittleFS(path, cabecera);
  asegurarArchivoSD(path, cabecera);
}

void escribirLineaCSV(String path, String cabecera, String linea) {
  asegurarArchivoCSV(path, cabecera);

  File fileLittleFS = LittleFS.open(path, "a");

  if (fileLittleFS) {
    fileLittleFS.println(linea);
    fileLittleFS.close();
  } else {
    Serial.println("ERROR escribiendo en LittleFS");
  }

  if (sdDisponible) {
    File fileSD = SD.open(path, FILE_APPEND);

    if (fileSD) {
      fileSD.println(linea);
      fileSD.close();
      Serial.print("Linea guardada en microSD: ");
      Serial.println(path);
    } else {
      Serial.println("ERROR escribiendo en microSD");
    }
  }
}

String obtenerFechaHoraActual() {
  time_t now = time(nullptr);

  if (now < 1700000000) {
    return "sin_hora_" + String(millis());
  }

  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);

  return String(buffer);
}

String estadoIMC(float imc) {
  if (imc < 18.5) return "Bajo peso";
  if (imc < 25.0) return "Peso saludable";
  if (imc < 30.0) return "Sobrepeso";
  return "Obesidad";
}

void guardarPerfilCSV(String nombre, String altura, String edad, String sexo, String objetivo, String pesoObjetivo) {
  String cabecera = "nombre,altura,edad,sexo,objetivo,peso_objetivo";

  String linea = "";
  linea += limpiarCSV(nombre);
  linea += ",";
  linea += limpiarCSV(altura);
  linea += ",";
  linea += limpiarCSV(edad);
  linea += ",";
  linea += limpiarCSV(sexo);
  linea += ",";
  linea += limpiarCSV(objetivo);
  linea += ",";
  linea += limpiarCSV(pesoObjetivo);

  escribirLineaCSV("/perfiles.csv", cabecera, linea);

  Serial.println("Perfil guardado en CSV");
}

void guardarPesoCSV(String nombre, String altura, String peso, String imc, String estado) {
  String cabecera = "fecha_hora,nombre,altura,peso,imc,estado";

  String fechaHora = obtenerFechaHoraActual();

  String linea = "";
  linea += limpiarCSV(fechaHora);
  linea += ",";
  linea += limpiarCSV(nombre);
  linea += ",";
  linea += limpiarCSV(altura);
  linea += ",";
  linea += limpiarCSV(peso);
  linea += ",";
  linea += limpiarCSV(imc);
  linea += ",";
  linea += limpiarCSV(estado);

  escribirLineaCSV("/pesos.csv", cabecera, linea);

  ultimoGuardadoFecha = fechaHora;

  Serial.println("Peso guardado automaticamente");
  Serial.print("Fecha/hora: ");
  Serial.println(fechaHora);

  if (sdDisponible) {
    Serial.println("Guardado principal: microSD");
  } else {
    Serial.println("Guardado principal: LittleFS interno");
  }
}

void guardarPesoFinalAutomatico() {
  // No guardo pesos demasiado bajos porque la báscula está pensada para personas.
  // Así evito guardar falsas detecciones cuando la báscula está vacía.
  if (pesoFinal < PESO_MINIMO_GUARDAR_KG) {
    Serial.println("No se guarda: peso final demasiado bajo");
    return;
  }

  if (usuarioActivo.length() == 0 || alturaUsuarioActivo <= 0) {
    Serial.println("No se guarda: no hay usuario activo o altura");
    return;
  }

  float alturaM = alturaUsuarioActivo / 100.0;
  float imc = pesoFinal / (alturaM * alturaM);
  String estado = estadoIMC(imc);

  guardarPesoCSV(
    usuarioActivo,
    String(alturaUsuarioActivo, 0),
    String(pesoFinal, 1),
    String(imc, 1),
    estado
  );

  pesoYaGuardadoEnEstaPesada = true;
}

String leerArchivoComoTexto(String path) {
  if (sdDisponible && SD.exists(path)) {
    File fileSD = SD.open(path, FILE_READ);

    if (fileSD) {
      String contenido = fileSD.readString();
      fileSD.close();
      return contenido;
    }
  }

  File file = LittleFS.open(path, "r");

  if (!file) {
    return "";
  }

  String contenido = file.readString();
  file.close();

  return contenido;
}

// =====================================================
// SERVIDOR WEB
// =====================================================
// Aquí configuro todas las rutas de la web. Algunas sirven archivos,
// como index.html, y otras son rutas API para enviar datos a la página.

void enviarArchivo(String path, String contentType) {
  File file = LittleFS.open(path, "r");

  if (!file) {
    server.send(404, "text/plain", "Archivo no encontrado: " + path);
    return;
  }

  server.streamFile(file, contentType);
  file.close();
}

void iniciarWeb() {
  if (!LittleFS.begin(true)) {
    Serial.println("ERROR: No se pudo iniciar LittleFS");
    return;
  }

  asegurarArchivoCSV("/perfiles.csv", "nombre,altura,edad,sexo,objetivo,peso_objetivo");
  asegurarArchivoCSV("/pesos.csv", "fecha_hora,nombre,altura,peso,imc,estado");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  Serial.println();
  Serial.println("====================================");
  Serial.println(" Web SmartScale iniciada");
  Serial.print(" Red WiFi: ");
  Serial.println(AP_SSID);
  Serial.print(" Password: ");
  Serial.println(AP_PASS);
  Serial.print(" IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("====================================");

  server.on("/", HTTP_GET, []() {
    enviarArchivo("/index.html", "text/html");
  });

  server.on("/index.html", HTTP_GET, []() {
    enviarArchivo("/index.html", "text/html");
  });

  server.on("/style.css", HTTP_GET, []() {
    enviarArchivo("/style.css", "text/css");
  });

  server.on("/script.js", HTTP_GET, []() {
    enviarArchivo("/script.js", "application/javascript");
  });

  server.on("/api/set-time", HTTP_GET, []() {
    String epochStr = server.arg("epoch");

    if (epochStr.length() == 0) {
      server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Falta epoch\"}");
      return;
    }

    unsigned long epoch = epochStr.toInt();

    struct timeval tv;
    tv.tv_sec = epoch;
    tv.tv_usec = 0;

    settimeofday(&tv, nullptr);

    server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Hora actualizada\"}");
  });

  server.on("/api/usuario-activo", HTTP_GET, []() {
    usuarioActivo = server.arg("nombre");
    alturaUsuarioActivo = server.arg("altura").toFloat();

    if (usuarioActivo.length() == 0 || alturaUsuarioActivo <= 0) {
      server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Usuario o altura no validos\"}");
      return;
    }

    Serial.print("Usuario activo actualizado: ");
    Serial.print(usuarioActivo);
    Serial.print(" | Altura: ");
    Serial.println(alturaUsuarioActivo);

    server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Usuario activo actualizado\"}");
  });

  server.on("/api/peso", HTTP_GET, []() {
    String json = "{";
    json += "\"peso\":" + String(ultimoPeso, 1) + ",";
    json += "\"peso_final\":" + String(pesoFinal, 1) + ",";
    json += "\"estable\":";
    json += mostrandoPesoFinal ? "true" : "false";
    json += ",";
    json += "\"factor\":" + String(calibration_factor, 2) + ",";
    json += "\"offset\":" + String(zero_offset) + ",";
    json += "\"usuario_activo\":\"" + usuarioActivo + "\",";
    json += "\"altura_activa\":" + String(alturaUsuarioActivo, 0) + ",";
    json += "\"ultimo_guardado\":\"" + ultimoGuardadoFecha + "\",";
    json += "\"sd_disponible\":";
    json += sdDisponible ? "true" : "false";
    json += "}";

    server.send(200, "application/json", json);
  });

  server.on("/api/sd-status", HTTP_GET, []() {
    String json = "{";
    json += "\"sd_disponible\":";
    json += sdDisponible ? "true" : "false";

    if (sdDisponible) {
      uint64_t cardSize = SD.cardSize() / (1024 * 1024);
      json += ",\"tamano_mb\":" + String(cardSize);
    }

    json += "}";

    server.send(200, "application/json", json);
  });

  server.on("/api/tara", HTTP_GET, []() {
    hacerTara();
    server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Tara realizada\"}");
  });

  server.on("/api/guardar", HTTP_GET, []() {
    guardarParametros();
    server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Parametros guardados\"}");
  });

  server.on("/api/reset", HTTP_GET, []() {
    resetearMemoria();
    server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Memoria reseteada\"}");
  });

  server.on("/api/perfil", HTTP_GET, []() {
    String nombre = server.arg("nombre");
    String altura = server.arg("altura");
    String edad = server.arg("edad");
    String sexo = server.arg("sexo");
    String objetivo = server.arg("objetivo");
    String pesoObjetivo = server.arg("peso_objetivo");

    if (nombre.length() == 0 || altura.length() == 0) {
      server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Faltan nombre o altura\"}");
      return;
    }

    guardarPerfilCSV(nombre, altura, edad, sexo, objetivo, pesoObjetivo);

    usuarioActivo = nombre;
    alturaUsuarioActivo = altura.toFloat();

    server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Perfil guardado\"}");
  });

  server.on("/api/perfiles.csv", HTTP_GET, []() {
    String contenido = leerArchivoComoTexto("/perfiles.csv");

    if (contenido.length() == 0) {
      contenido = "nombre,altura,edad,sexo,objetivo,peso_objetivo\n";
    }

    server.send(200, "text/plain", contenido);
  });

  server.on("/api/pesos.csv", HTTP_GET, []() {
    String contenido = leerArchivoComoTexto("/pesos.csv");

    if (contenido.length() == 0) {
      contenido = "fecha_hora,nombre,altura,peso,imc,estado\n";
    }

    server.send(200, "text/plain", contenido);
  });

  server.onNotFound([]() {
    server.send(404, "text/plain", "Ruta no encontrada");
  });

  server.begin();

  Serial.println("Servidor web listo.");
}

// =====================================================
// SETUP
// =====================================================
// Esta parte se ejecuta solo una vez al arrancar el ESP32.
// Inicializo pantalla, sensor, memoria, microSD y web.

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);

  display.begin();
  display.setRotation(1);
  display.fillScreen(ILI9341_BLACK);

  display.setTextColor(ILI9341_GREEN);
  display.setTextSize(3);
  display.setCursor(20, 40);
  display.println("SmartScale");

  display.setTextColor(ILI9341_WHITE);
  display.setTextSize(2);
  display.setCursor(20, 90);
  display.println("Iniciando...");

  scale.begin(DOUT, CLK);
  scale.power_up();

  prefs.begin("balanza", false);

  calibration_factor = prefs.getFloat("cal", calibration_factor);
  zero_offset = prefs.getLong("offset", zero_offset);

  iniciarSD();

  Serial.println();
  Serial.println("====================================");
  Serial.println(" Sistema bascula listo");
  Serial.println("====================================");
  Serial.println("COMANDOS:");
  Serial.println("0 = Tara");
  Serial.println("1 = Calibrar con 1 kg");
  Serial.println("2 = Calibrar con 2 kg");
  Serial.println("3 = Calibrar con 3 kg");
  Serial.println("4 = Calibrar con 4 kg");
  Serial.println("7 = Calibrar con 72 kg");
  Serial.println("g = Guardar calibracion");
  Serial.println("r = Reset memoria");
  Serial.println("------------------------------------");
  Serial.print("Factor actual: ");
  Serial.println(calibration_factor, 6);
  Serial.print("Offset actual: ");
  Serial.println(zero_offset);
  Serial.print("microSD: ");
  Serial.println(sdDisponible ? "OK" : "NO DETECTADA");
  Serial.println("====================================");

  mostrarMensaje("Bascula lista", sdDisponible ? "SD detectada" : "Sin SD");

  delay(1200);

  mostrarMensaje("Bascula lista", "Iniciando tara");

  hacerTara();

  iniciarWeb();
}

// =====================================================
// LOOP PRINCIPAL
// =====================================================
// Esta es la parte que se repite continuamente.
// Atiende la web, lee comandos, revisa el botón, mide el peso
// y guarda automáticamente cuando detecta un peso estable.

void loop() {
  server.handleClient();

  if (Serial.available()) {
    char cmd = Serial.read();

    if (cmd == '0') {
      hacerTara();
    }

    if (cmd == '1') {
      calibrar(1.0);
    }

    if (cmd == '2') {
      calibrar(2.0);
    }

    if (cmd == '3') {
      calibrar(3.0);
    }

    if (cmd == '4') {
      calibrar(4.0);
    }

    if (cmd == '7') {
      calibrar(72.0);
    }

    if (cmd == 'g') {
      guardarParametros();
    }

    if (cmd == 'r') {
      resetearMemoria();
    }
  }

  if (digitalRead(BUTTON_PIN) == LOW) {
    Serial.println("Tara por boton");
    hacerTara();
    delay(300);
  }

  if (millis() - ultimaLectura < INTERVALO_LECTURA) {
    return;
  }

  ultimaLectura = millis();

  if (mostrandoPesoFinal) {
    mostrarPesoFinal(pesoFinal);

    if (millis() - tiempoPesoFinal >= TIEMPO_FINAL) {
      mostrandoPesoFinal = false;
      pesoEstableDetectado = false;
      tiempoEstableInicio = 0;
      pesoAnterior = 0.0;
    }

    return;
  }

  long raw;

  if (leerRawSeguro(raw, 5)) {
    float peso = calcularPesoKg(raw);

    peso = aplicarFiltro(peso);

    if (peso < 0.0) {
      peso = 0.0;
    }

    // Vuelvo a aplicar la zona muerta después del filtro.
    // El filtro puede dejar una media pequeña aunque la báscula esté vacía.
    if (peso < ZONA_MUERTA_KG) {
      peso = 0.0;
      tiempoEstableInicio = 0;
      pesoEstableDetectado = false;
    }

    ultimoPeso = peso;

    // Cuando vuelve a cero, permito guardar otra pesada más adelante.
    if (peso == 0.0) {
      pesoYaGuardadoEnEstaPesada = false;
    }

    Serial.print("Peso: ");
    Serial.print(peso, 1);
    Serial.print(" kg | Raw: ");
    Serial.print(raw);
    Serial.print(" | Offset: ");
    Serial.print(zero_offset);
    Serial.print(" | Factor: ");
    Serial.println(calibration_factor, 2);

    float diferencia = abs(peso - pesoAnterior);

    // Solo detecto peso final si supera el peso mínimo.
    // Así no se guarda una falsa pesada con pequeñas variaciones en vacío.
    if (diferencia <= UMBRAL_ESTABILIDAD && peso >= PESO_MINIMO_ESTABLE_KG) {
      if (tiempoEstableInicio == 0) {
        tiempoEstableInicio = millis();
      }

      if ((millis() - tiempoEstableInicio >= TIEMPO_ESTABLE) && !pesoEstableDetectado) {
        pesoEstableDetectado = true;
        mostrandoPesoFinal = true;
        tiempoPesoFinal = millis();
        pesoFinal = peso;

        Serial.print("PESO FINAL: ");
        Serial.print(pesoFinal, 1);
        Serial.println(" kg");

        if (!pesoYaGuardadoEnEstaPesada) {
          guardarPesoFinalAutomatico();
        }

        mostrarPesoFinal(pesoFinal);
        return;
      }
    } else {
      tiempoEstableInicio = 0;
      pesoEstableDetectado = false;
    }

    pesoAnterior = peso;

    mostrarPeso(peso, "Midiendo...");
  } else {
    mostrarMensaje("HX711 ERROR", "Revisa cables");
  }
}