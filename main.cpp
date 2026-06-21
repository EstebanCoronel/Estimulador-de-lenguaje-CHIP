#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_ImageReader.h>

// ==========================================
// PINES DE HARDWARE
// ==========================================
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_MISO 13
#define TFT_CS   10
#define TFT_DC   9
#define TFT_RST  14  

#define SD_MOSI  18
#define SD_SCLK  7
#define SD_MISO  15
#define SD_CS    4   

#define BOTON_PIN 5  // Pin asignado para el botón (Conectado a GND)

// ==========================================
// CONFIGURACIÓN DE IMÁGENES
// ==========================================
// Lista de las imágenes que tienes en la carpeta /animales de tu SD
const char* listaImagenes[] = {
  //gato, leon, pato, perro, pollo, rana, vaca
  "/animales/leon.bmp",
  "/animales/gato.bmp",
  "/animales/pato.bmp",
  "/animales/perro.bmp",
  "/animales/pollo.bmp",
  "/animales/rana.bmp",
  "/animales/vaca.bmp"
};
// Calcula automáticamente cuántas imágenes hay en la lista
const int totalImagenes = sizeof(listaImagenes) / sizeof(listaImagenes[0]);
int indiceActual = 0; // Controla qué imagen se está mostrando

// Instanciar objetos
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
SPIClass SPI_SD(HSPI); 
SdFat SD_Card;
Adafruit_ImageReader reader(SD_Card);

// Variable para el control del rebote (Debounce) del botón
unsigned long ultimoTiempoRebote = 0;
const unsigned long tiempoDebounce = 250; // Milisegundos de espera entre pulsaciones

// Función para cargar una imagen de forma segura
void mostrarImagen(const char* ruta) {
  Serial.print("Cargando: ");
  Serial.println(ruta);
  
  tft.fillScreen(ILI9341_BLACK); // Limpiar pantalla antes de dibujar
  ImageReturnCode stat = reader.drawBMP((char*)ruta, tft, 0, 0);

  if (stat != IMAGE_SUCCESS) {
    Serial.print("Error al cargar. Código: ");
    reader.printStatus(stat);
    
    tft.setTextColor(ILI9341_RED);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.printf("Error: %d", stat);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Configurar el botón con la resistencia pull-up interna activa
  pinMode(BOTON_PIN, INPUT_PULLUP);

  // Apagar selectores SPI al inicio
  pinMode(TFT_CS, OUTPUT);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH); 
  digitalWrite(SD_CS, HIGH);  
  
  delay(100);
  Serial.println("\n--- Visor de Imágenes con Botón ---");

  // Inicializar Pantalla
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(3); 
  tft.fillScreen(ILI9341_BLUE);

  // Inicializar Tarjeta SD
  SPI_SD.begin(SD_SCLK, SD_MISO, SD_MOSI);
  if (!SD_Card.begin(SdSpiConfig(SD_CS, SHARED_SPI, SD_SCK_MHZ(4), &SPI_SD))) {
    Serial.println("¡Fallo en la SD!");
    tft.fillScreen(ILI9341_RED);
    return;
  }
  Serial.println("¡SD OK!");

  // Mostrar la primera imagen del arreglo al arrancar
  mostrarImagen(listaImagenes[indiceActual]);
}

void loop() {
  // Leer el estado del botón (LOW significa que fue presionado)
  if (digitalRead(BOTON_PIN) == LOW) {
    
    // Verificar si ya pasó suficiente tiempo desde el último clic (Antirrebote)
    if ((millis() - ultimoTiempoRebote) > tiempoDebounce) {
      
      // Avanzar al siguiente índice
      indiceActual++;
      
      // Si llegamos al final de la lista, regresar a la primera imagen (ciclo infinito)
      if (indiceActual >= totalImagenes) {
        indiceActual = 0;
      }
      
      // Cargar la nueva imagen en pantalla
      mostrarImagen(listaImagenes[indiceActual]);
      
      // Actualizar el tiempo del último clic válido
      ultimoTiempoRebote = millis();
    }
  }
}