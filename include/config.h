#ifndef CONFIG_H
#define CONFIG_H

// =======================
// Pines HX711
// =======================
#define HX711_DT_PIN   4
#define HX711_SCK_PIN  5

// =======================
// Pines OLED I2C
// =======================
#define OLED_SDA_PIN   21
#define OLED_SCL_PIN   22

// =======================
// Configuración báscula 5 kg
// =======================
#define MIN_VALID_WEIGHT_KG  0.005   // 5 gramos
#define MAX_VALID_WEIGHT_KG  5.0     // 5 kg máximo

// Factor provisional. Luego lo calibraremos.
#define CALIBRATION_FACTOR   6.5

// Cambia a true si el peso sale negativo
#define INVERT_WEIGHT_SIGN   false

#endif