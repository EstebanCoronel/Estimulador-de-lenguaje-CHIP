#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_ImageReader.h>

//pines de la pantalla
#define SPI_MOSI 11
#define SPI_SCLK 12
#define SPI_MISO 13

#define TFT_CS   10
#define TFT_DC   9
#define TFT_RST  14  
#define SD_CS    4 

//Botones del sistema
#define PIN_BTN_1   6
#define PIN_BTN_2   5
#define PIN_BTN_3   16
#define PIN_BTN_4   17
#define PIN_BTN_5   8
#define PIN_BTN_6   21
#define PIN_BTN_7   38

#define DEBOUNCE_MS        150
#define TIMEOUT_REPOSO_MS  18000

//enum para los modos
typedef enum {
    MODO_FONEMAS  = 0,
    MODO_ANIMALES = 1,
    MODO_COLORES  = 2,
    MODO_FAMILIA  = 3,
    MODO_CUERPO   = 4,
    MODO_CANTIDAD = 5
} Modo;

//struct para el contenido de cada modo (etiqueta + imagen BMP)
typedef struct {
    const char *etiqueta;
    const char *imagen_bmp;   
} ItemContenido;


const ItemContenido ANIMALES[7] = {
    { "Gato",  "/animales/gato.bmp"  },
    { "Leon",  "/animales/leon.bmp"  },
    { "Pato",  "/animales/pato.bmp"  },
    { "Perro", "/animales/perro.bmp" },
    { "Pollo", "/animales/pollo.bmp" },
    { "Rana",  "/animales/rana.bmp"  },
    { "Vaca",  "/animales/vaca.bmp"  },
};

const ItemContenido FONEMAS[7] = {
    { "Ma", 0 }, 
    { "Pa", 0 }, 
    { "Ba", 0 }, 
    { "Ta", 0 },
    { "La", 0 }, 
    { "Sa", 0 }, 
    { "Na", 0 },
};
const ItemContenido COLORES[7] = {
    { "Rojo", 0 }, 
    { "Azul", 0 }, 
    { "Verde", 0 }, 
    { "Amarillo", 0 },
    { "Naranja", 0 }, 
    { "Morado", 0 }, 
    { "Rosa", 0 },
};
const ItemContenido FAMILIA[7] = {
    { "Mama", "/familia/mama.bmp" }, 
    { "Papa", "/familia/papa.bmp" }, 
    { "Hermano", "/familia/hermano.bmp" }, 
    { "Hermana", "/familia/hermana.bmp" },
    { "Abuelo", "/familia/abuelo.bmp" }, 
    { "Abuela", "/familia/abuela.bmp" }, 
    { "Primo", "/familia/primo.bmp" },
};
const ItemContenido CUERPO[7] = {
    { "Cabeza", "/cuerpo/cabeza.bmp" }, 
    { "Ojo", "/cuerpo/ojo.bmp" }, 
    { "Nariz", "/cuerpo/nariz.bmp" }, 
    { "Boca", "/cuerpo/boca.bmp" },
    { "Mano", "/cuerpo/mano.bmp" }, 
    { "Pie", "/cuerpo/pie.bmp" }, 
    { "Oreja", "/cuerpo/oreja.bmp" },
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

//enum para las maquinas de estado
typedef enum {
    ESTADO_REPOSO,
    ESTADO_MODO_ACTIVO
} Estado;

Estado  estado_actual = ESTADO_REPOSO;
Modo    modo_actual   = MODO_FONEMAS;
unsigned long ultima_actividad = 0;

//area de las interrupciones
const uint8_t PINES_BTN[7] = {
    PIN_BTN_1, 
    PIN_BTN_2, 
    PIN_BTN_3, 
    PIN_BTN_4,
    PIN_BTN_5, 
    PIN_BTN_6, 
    PIN_BTN_7
};

volatile bool flag_btn[7] = { false, false, false, false, false, false, false };
volatile unsigned long ultimo_tiempo[7] = {0};

void IRAM_ATTR isr_btn0() { 
  flag_btn[0]=true; 
}
void IRAM_ATTR isr_btn1() { 
  flag_btn[1]=true;  
}
void IRAM_ATTR isr_btn2() { 
  flag_btn[2]=true;  
}
void IRAM_ATTR isr_btn3() { 
  flag_btn[3]=true; 
}
void IRAM_ATTR isr_btn4() { 
  flag_btn[4]=true; 
}
void IRAM_ATTR isr_btn5() { 
  flag_btn[5]=true; 
}
void IRAM_ATTR isr_btn6() { 
  flag_btn[6]=true; 
}
// Array de punteros a las funciones ISR de los botones
void (*ISRS[7])() = { 
  isr_btn0, 
  isr_btn1, 
  isr_btn2, 
  isr_btn3, 
  isr_btn4, 
  isr_btn5, 
  isr_btn6 
};

//Zona de la pantalla
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
SdFat            SD_Card; // Deja solo esto, borra SPI_SD(HSPI)
Adafruit_ImageReader reader{SD_Card};

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
    // 1. CASO ESPECIAL: MODO COLORES (Cambia todo el fondo de la pantalla)
    if (modo_actual == MODO_COLORES) {
        uint16_t color_fondo = ILI9341_BLACK;
        
        // Asignamos el color real de la pantalla según el nombre del item
        if (strcmp(item.etiqueta, "Rojo") == 0)     color_fondo = ILI9341_RED;
        else if (strcmp(item.etiqueta, "Azul") == 0)    color_fondo = ILI9341_BLUE;
        else if (strcmp(item.etiqueta, "Verde") == 0)   color_fondo = ILI9341_GREEN;
        else if (strcmp(item.etiqueta, "Amarillo") == 0) color_fondo = ILI9341_YELLOW;
        else if (strcmp(item.etiqueta, "Naranja") == 0)  color_fondo = 0xFD20; // Naranja en formato 565
        else if (strcmp(item.etiqueta, "Morado") == 0)   color_fondo = 0x780F; // Morado
        else if (strcmp(item.etiqueta, "Rosa") == 0)     color_fondo = 0xFE19; // Rosado

        tft.fillScreen(color_fondo);

        // Dibujamos el texto sobre el fondo (ponemos fondo negro detrás de las letras para leer bien)
        tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
        tft.setTextSize(4);
        tft.setCursor(40, 110);
        tft.println(item.etiqueta);
        return; // Salimos de la función, ya cumplió su objetivo
    }

    // Limpieza estándar para los demás modos
    tft.fillScreen(ILI9341_BLACK);

    // 2. CASO ESPECIAL: MODO FONEMAS (Dibuja las combinaciones Ma, Me, Mi, Mo, Mu)
    if (modo_actual == MODO_FONEMAS) {
        tft.setTextColor(ILI9341_GREEN);
        tft.setTextSize(3);
        tft.setCursor(20, 20);
        tft.printf("Familia de la %c:", item.etiqueta[0]); // Muestra "Familia de la M:" o "P:"

        // Dibujamos las sílabas grandes en el centro de la pantalla
        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(5); // Letras bien grandes para que se lean claro
        
        char letra = item.etiqueta[0]; // Guarda la 'M', 'P', 'B', etc.
        tft.setCursor(40, 110);
        // Imprime en horizontal: Ma  Me  Mi  Mo  Mu
        tft.printf("%ca  %ce  %ci  %co  %cu", letra, letra, letra, letra, letra);
        return; // Salimos de la función
    }

    // 3. CASO POR DEFECTO: ANIMALES, FAMILIA, CUERPO (Lectura normal de la SD)
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

    // Texto inferior para los modos de la SD
    tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
    tft.setTextSize(4);
    tft.setCursor(30, 250);
    tft.println(item.etiqueta);
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

    // Asegurar los estados del CS en HIGH antes de arrancar el bus
    pinMode(TFT_CS, OUTPUT);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    digitalWrite(SD_CS, HIGH);
    delay(100);

    // Inicializar el único bus SPI compartiendo pines
    SPI.begin(SPI_SCLK, SPI_MISO, SPI_MOSI);
    
    // Inicializar Pantalla
    tft.begin();
    tft.setRotation(3);
    tft.fillScreen(ILI9341_BLUE);

    // Inicializar SD usando el bus SPI global ya creado
    // Pasamos &SPI para decirle a SdFat que use ese bus exacto
    if (!SD_Card.begin(SdSpiConfig(SD_CS, SHARED_SPI, SD_SCK_MHZ(10), &SPI))) {
        Serial.println("¡Fallo en la SD!");
        tft.fillScreen(ILI9341_RED);
    } else {
        Serial.println("¡SD OK!");
    }

    // (Borra la línea de reader.begin(SD_Card) si la habías puesto, ya no hace falta)

    digitalWrite(SD_CS, HIGH);
    digitalWrite(TFT_CS, HIGH);
    pantalla_mostrar_reposo();
    ultima_actividad = millis();

    Serial.println("=== Todo configurado sin conflictos ===");
}

void loop() {
    for (uint8_t i = 0; i < 7; i++) {
        if (!flag_btn[i]) continue;
        
        // El antirrebote real se evalúa aquí de manera segura
        if (millis() - ultimo_tiempo[i] > DEBOUNCE_MS) {
            ultimo_tiempo[i] = millis();
            ultima_actividad = millis();

            if (estado_actual == ESTADO_REPOSO) {
                entrar_a_modo((Modo) i);
                Serial.printf("[MODO] -> %s\n", obtener_nombre_modo(modo_actual));
            }
            else if (estado_actual == ESTADO_MODO_ACTIVO) {
                mostrar_item_de_modo(i);
                Serial.printf("[ITEM] Modo=%s Boton=%d\n", obtener_nombre_modo(modo_actual), i);
            }
            
            // Truco clave: Limpiar rebotes que ocurrieron MIENTRAS se dibujaba la pantalla
            delay(50); 
            flag_btn[i] = false;
        } else {
            // Si fue un rebote falso, simplemente apagamos la bandera
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
