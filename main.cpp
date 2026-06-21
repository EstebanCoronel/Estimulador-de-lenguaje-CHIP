#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_ImageReader.h>
#include "driver/i2s.h" 

// Pines de la pantalla
#define SPI_MOSI 11
#define SPI_SCLK 12
#define SPI_MISO 13

#define TFT_CS   10
#define TFT_DC   9
#define TFT_RST  14  
#define SD_CS    4 

// Pines de Audio I2S (MAX98357A)
#define I2S_BCLK_PIN  47
#define I2S_LRC_PIN   45
#define I2S_DIN_PIN   21

// Botones del sistema
#define PIN_BTN_1   6
#define PIN_BTN_2   5
#define PIN_BTN_3   16
#define PIN_BTN_4   17
#define PIN_BTN_5   8
#define PIN_BTN_6   20
#define PIN_BTN_7   38

#define DEBOUNCE_MS        150
#define TIMEOUT_REPOSO_MS  18000

// Estructura de cabecera estándar WAV para saltar metadatos
struct WavHeader {
    char chunkID[4]; uint32_t chunkSize; char format[4];
    char subchunk1ID[4]; uint32_t subchunk1Size; uint16_t audioFormat;
    uint16_t numChannels; uint32_t sampleRate; uint32_t byteRate;
    uint16_t blockAlign; uint16_t bitsPerSample; char subchunk2ID[4];
    uint32_t subchunk2Size;
};

// Enum para los modos
typedef enum {
    MODO_FONEMAS  = 0,
    MODO_ANIMALES = 1,
    MODO_COLORES  = 2,
    MODO_FAMILIA  = 3,
    MODO_CUERPO   = 4,
    MODO_CANTIDAD = 5
} Modo;

// Estructura para incluir la ruta del audio WAV
typedef struct {
    const char *etiqueta;
    const char *imagen_bmp;   
    const char *audio_wav;  
} ItemContenido;

const ItemContenido FONEMAS[7] = {
    { "Ma", 0, 0 }, { "Pa", 0, 0 }, { "Ba", 0, 0 }, { "Ta", 0, 0 },
    { "La", 0, 0 }, { "Sa", 0, 0 }, { "Na", 0, 0 }
};

const ItemContenido ANIMALES[7] = {
    { "Gato",  "/animales/gato.bmp",  "/audio/animales/gato.wav"  }, 
    { "Leon",  "/animales/leon.bmp",  "/audio/animales/leon.wav"  },
    { "Pato",  "/animales/pato.bmp",  "/audio/animales/pato.wav"  }, 
    { "Perro", "/animales/perro.bmp", "/audio/animales/perro.wav" }, 
    { "Pollo", "/animales/pollo.bmp", "/audio/animales/pollo.wav" }, 
    { "Rana",  "/animales/rana.bmp",  "/audio/animales/rana.wav"  }, 
    { "Vaca",  "/animales/vaca.bmp",  "/audio/animales/vaca.wav"  }
};

const ItemContenido COLORES[7] = {
    { "Rojo",     0, "/audio/colores/rojo.wav"     }, 
    { "Azul",     0, "/audio/colores/azul.wav"     }, 
    { "Verde",    0, "/audio/colores/verde.wav"    }, 
    { "Amarillo", 0, "/audio/colores/amarillo.wav" },
    { "Naranja",  0, "/audio/colores/naranja.wav"  }, 
    { "Morado",   0, "/audio/colores/morado.wav"   }, 
    { "Rosa",     0, "/audio/colores/rosa.wav"     }
};

const ItemContenido FAMILIA[7] = {
    { "Mama",    "/familia/mama.bmp",    "/audio/familia/mama.wav"    }, 
    { "Papa",    "/familia/papa.bmp",    "/audio/familia/papa.wav"    }, 
    { "Hermano", "/familia/hermano.bmp", "/audio/familia/hermano.wav" }, 
    { "Hermana", "/familia/hermana.bmp", "/audio/familia/hermana.wav" },
    { "Abuelo",  "/familia/abuelo.bmp",  "/audio/familia/abuelo.wav"  }, 
    { "Abuela",  "/familia/abuela.bmp",  "/audio/familia/abuela.wav"  }, 
    { "Primo",   "/familia/primo.bmp",   "/audio/familia/primo.wav"   }
};

const ItemContenido CUERPO[7] = {
    { "Cabeza", "/cuerpo/cabeza.bmp", "/audio/cuerpo/cabeza.wav" }, 
    { "Ojo",    "/cuerpo/ojo.bmp",    "/audio/cuerpo/ojo.wav"    }, 
    { "Nariz",  "/cuerpo/nariz.bmp",  "/audio/cuerpo/nariz.wav"  }, 
    { "Boca",   "/cuerpo/boca.bmp",   "/audio/cuerpo/boca.wav"   },
    { "Mano",   "/cuerpo/mano.bmp",   "/audio/cuerpo/mano.wav"   }, 
    { "Pie",    "/cuerpo/pie.bmp",    "/audio/cuerpo/pie.wav"    }, 
    { "Oreja",  "/cuerpo/oreja.bmp",  "/audio/cuerpo/oreja.wav"  }
};

const ItemContenido *obtener_contenido(Modo m) {
    if (m == MODO_FONEMAS)  return FONEMAS;
    if (m == MODO_ANIMALES) return ANIMALES;
    if (m == MODO_COLORES)  return COLORES;
    if (m == MODO_FAMILIA)  return FAMILIA;
    if (m == MODO_CUERPO)   return CUERPO;
    return 0;
}

const char *obtener_nombre_modo(Modo m) {
    if (m == MODO_FONEMAS)  return "Fonemas";
    if (m == MODO_ANIMALES) return "Animales";
    if (m == MODO_COLORES)  return "Colores";
    if (m == MODO_FAMILIA)  return "Familia";
    if (m == MODO_CUERPO)   return "Cuerpo";
    return "?";
}

typedef enum {
    ESTADO_REPOSO,
    ESTADO_MODO_ACTIVO
} Estado;

Estado        estado_actual = ESTADO_REPOSO;
Modo          modo_actual   = MODO_FONEMAS;
unsigned long ultima_actividad = 0;

const uint8_t PINES_BTN[7] = { PIN_BTN_1, PIN_BTN_2, PIN_BTN_3, PIN_BTN_4, PIN_BTN_5, PIN_BTN_6, PIN_BTN_7 };
volatile bool flag_btn[7] = { false, false, false, false, false, false, false };
volatile unsigned long ultimo_tiempo[7] = {0};

void IRAM_ATTR isr_btn0() { flag_btn[0]=true; }
void IRAM_ATTR isr_btn1() { flag_btn[1]=true; }
void IRAM_ATTR isr_btn2() { flag_btn[2]=true; }
void IRAM_ATTR isr_btn3() { flag_btn[3]=true; }
void IRAM_ATTR isr_btn4() { flag_btn[4]=true; }
void IRAM_ATTR isr_btn5() { flag_btn[5]=true; }
void IRAM_ATTR isr_btn6() { flag_btn[6]=true; }

void (*ISRS[7])() = { isr_btn0, isr_btn1, isr_btn2, isr_btn3, isr_btn4, isr_btn5, isr_btn6 };

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
SdFat             SD_Card; 
Adafruit_ImageReader reader{SD_Card};

void initI2SClassic(uint32_t sample_rate) {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = sample_rate,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, 
        .communication_format = I2S_COMM_FORMAT_STAND_MSB,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 16,        // <-- Subimos de 8 a 16 buffers
        .dma_buf_len = 512,         // <-- Aumentamos el tamaño de 256 a 512
        .use_apll = false,
        .tx_desc_auto_clear = true
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK_PIN,
        .ws_io_num = I2S_LRC_PIN,
        .data_out_num = I2S_DIN_PIN,
        .data_in_num = -1 
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_zero_dma_buffer(I2S_NUM_0);
}

void playWavFromSD(const char* path) {
    if (path == 0) return;

    File wavFile = SD_Card.open(path, FILE_READ);
    if (!wavFile) {
        Serial.printf("[-] Error abriendo audio WAV: %s\n", path);
        return;
    }

    WavHeader header;
    if (wavFile.read((uint8_t*)&header, sizeof(WavHeader)) != sizeof(WavHeader)) {
        wavFile.close();
        return;
    }

    if (memcmp(header.format, "WAVE", 4) != 0) {
        wavFile.close();
        return;
    }

    i2s_set_sample_rates(I2S_NUM_0, header.sampleRate);

    const size_t bufferSize = 512;
    uint8_t buffer[bufferSize];
    size_t bytesWritten = 0;

    while (wavFile.available()) {
        int bytesRead = wavFile.read(buffer, bufferSize);
        if (bytesRead > 0) {
            i2s_write(I2S_NUM_0, buffer, bytesRead, &bytesWritten, portMAX_DELAY);
        }
    }
    wavFile.close();
    Serial.println("[AUDIO] Sonido reproducido exitosamente");
}

void pantalla_mostrar_reposo() {
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 130);
    tft.println("Elige un modo!");
}

void pantalla_mostrar_modo(Modo m) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_CYAN);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.print("Modo: ");
    tft.println(obtener_nombre_modo(m));
}

void pantalla_mostrar_item(const ItemContenido &item) {
    // 1. CASO MODO COLORES (Cambia el fondo de la pantalla y reproduce sonido)
    if (modo_actual == MODO_COLORES) {
        uint16_t color_fondo = ILI9341_BLACK;
        if (strcmp(item.etiqueta, "Rojo") == 0)          color_fondo = ILI9341_RED;
        else if (strcmp(item.etiqueta, "Azul") == 0)     color_fondo = ILI9341_BLUE;
        else if (strcmp(item.etiqueta, "Verde") == 0)    color_fondo = ILI9341_GREEN;
        else if (strcmp(item.etiqueta, "Amarillo") == 0) color_fondo = ILI9341_YELLOW;
        else if (strcmp(item.etiqueta, "Naranja") == 0)  color_fondo = 0xFD20; 
        else if (strcmp(item.etiqueta, "Morado") == 0)   color_fondo = 0x780F; 
        else if (strcmp(item.etiqueta, "Rosa") == 0)     color_fondo = 0xFE19; 

        tft.fillScreen(color_fondo);
        tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
        tft.setTextSize(4);
        tft.setCursor(40, 110);
        tft.println(item.etiqueta);

        // --- SOLUCIÓN: Disparar el audio antes de salir de la función ---
        if (item.audio_wav != 0) {
            playWavFromSD(item.audio_wav);
        }
        return; // Ahora sí puede salir de forma segura
    }

    tft.fillScreen(ILI9341_BLACK);

    // 2. CASO MODO FONEMAS
    if (modo_actual == MODO_FONEMAS) {
        tft.setTextColor(ILI9341_GREEN);
        tft.setTextSize(3);
        tft.setCursor(20, 20);
        tft.printf("Familia de la %c:", item.etiqueta[0]); 
        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(5); 
        char letra = item.etiqueta[0]; 
        tft.setCursor(40, 110);
        tft.printf("%ca   %ce   %ci   %co   %cu", letra, letra, letra, letra, letra);
        return; 
    }

    // 3. CASO ANIMALES, FAMILIA, CUERPO (Imágenes desde la SD)
    if (item.imagen_bmp != 0) {
        Serial.print("Cargando desde SD: ");
        Serial.println(item.imagen_bmp);

        digitalWrite(TFT_CS, HIGH);
        digitalWrite(SD_CS, HIGH);

        ImageReturnCode stat = reader.drawBMP((char *) item.imagen_bmp, tft, 0, 0);
        
        if (stat != IMAGE_SUCCESS) {
            Serial.print("Error SD. Codigo: ");
            reader.printStatus(stat);
            tft.setTextColor(ILI9341_RED);
            tft.setTextSize(2);
            tft.setCursor(10, 50);
            tft.printf("Err BMP: %d", (int)stat);
        }
    }

    tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
    tft.setTextSize(4);
    tft.setCursor(30, 250);
    tft.println(item.etiqueta);

    // Reproducción normal para los modos que cargan imágenes
    if (item.audio_wav != 0) {
        playWavFromSD(item.audio_wav);
    }
}

void entrar_a_modo(Modo m) {
    modo_actual = m;
    pantalla_mostrar_modo(modo_actual);
    estado_actual = ESTADO_MODO_ACTIVO;
}

void mostrar_item_de_modo(uint8_t boton_idx) {
    const ItemContenido *items = obtener_contenido(modo_actual);
    if (!items) return;
    pantalla_mostrar_item(items[boton_idx]);
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- INICIALIZANDO ESP32-S3 ---");

    for (uint8_t i = 0; i < 7; i++) {
        pinMode(PINES_BTN[i], INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(PINES_BTN[i]), ISRS[i], FALLING);
    }
    Serial.println("[OK] Botones configurados");

    pinMode(TFT_CS, OUTPUT);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    digitalWrite(SD_CS, HIGH);
    delay(100);

    SPI.begin(SPI_SCLK, SPI_MISO, SPI_MOSI);
    
    tft.begin();
    tft.setRotation(3);
    tft.fillScreen(ILI9341_BLUE);

    if (!SD_Card.begin(SdSpiConfig(SD_CS, SHARED_SPI, SD_SCK_MHZ(16), &SPI))) {
        Serial.println("¡Fallo en la SD!");
        tft.fillScreen(ILI9341_RED);
    } else {
        Serial.println("¡SD OK!");
    }

    initI2SClassic(44100);

    digitalWrite(SD_CS, HIGH);
    digitalWrite(TFT_CS, HIGH);
    pantalla_mostrar_reposo();
    ultima_actividad = millis();

    Serial.println("=== Todo configurado sin conflictos ===");
}

void loop() {
    for (uint8_t i = 0; i < 7; i++) {
        if (!flag_btn[i]) continue;
        
        if (millis() - ultimo_tiempo[i] > DEBOUNCE_MS) {
            ultimo_tiempo[i] = millis();
            ultima_actividad = millis();

            if (estado_actual == ESTADO_REPOSO) {
                // --- SEGURIDAD ANTI-DESBORDE: Solo entra si el botón mapea un Modo válido (0 a 4) ---
                if (i <= 4) { 
                    entrar_a_modo((Modo) i);
                    Serial.printf("[MODO] -> %s\n", obtener_nombre_modo(modo_actual));
                } else {
                    Serial.printf("[INFO] Boton %d presionado en reposo pero no tiene modo asignado\n", i);
                }
            }
            else if (estado_actual == ESTADO_MODO_ACTIVO) {
                mostrar_item_de_modo(i);
                Serial.printf("[ITEM] Modo=%s Boton=%d\n", obtener_nombre_modo(modo_actual), i);
            }
            
            delay(50); 
            flag_btn[i] = false;
        } else {
            flag_btn[i] = false;
        }
    }

    if (estado_actual != ESTADO_REPOSO && millis() - ultima_actividad > TIMEOUT_REPOSO_MS) {
        pantalla_mostrar_reposo();
        estado_actual = ESTADO_REPOSO;
        Serial.println("[TIMEOUT] Volviendo a reposo");
    }

    delay(10);
}
