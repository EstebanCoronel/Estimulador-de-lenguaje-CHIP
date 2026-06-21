#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>                
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_ImageReader.h>

// ==========================================
// PINES DE HARDWARE (Bus SPI Único)
// ==========================================
#define SPI_MOSI 11
#define SPI_SCLK 12
#define SPI_MISO 13

#define TFT_CS   10
#define TFT_DC   9
#define TFT_RST  14  
#define SD_CS    4   

#define BOTON_PIN 5  // Pin asignado para el botón (Conectado a GND)

// ==========================================
// CONFIGURACIÓN DE CATEGORÍAS DE IMÁGENES
// ==========================================
const char* listaImagenesAnimales[] = {
  "/animales/leon.bmp",
  "/animales/gato.bmp",
  "/animales/pato.bmp",
  "/animales/perro.bmp",
  "/animales/pollo.bmp",
  "/animales/rana.bmp",
  "/animales/vaca.bmp"
};

const char* listaImagenesFamilia[] = {
  "/familia/papa.bmp",
  "/familia/mama.bmp",
  "/familia/hermano.bmp", // Revisa si en tu SD está escrito así o como "hermano.bmp"
  "/familia/hermana.bmp",
  "/familia/abuelo.bmp",
  "/familia/abuela.bmp",
  "/familia/primo.bmp"
};

// Control del modo de visualización
// true = Recorre animales | false = Recorre familia
bool modoAnimales = false; 

int indiceActual = 0; 
int totalImagenes = 0;

// Instanciar el sistema de archivos de SdFat
SdFat SD_Card;

// Instanciar la pantalla
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// Pasar el objeto SdFat al lector de imágenes
Adafruit_ImageReader reader(SD_Card);

// Control de rebote (Debounce) del botón
unsigned long ultimoTiempoRebote = 0;
const unsigned long tiempoDebounce = 350; // Milisegundos de espera entre pulsaciones

// Función para cargar y dibujar imágenes de forma segura
void mostrarImagen(const char* ruta) {
  Serial.print("Cargando: ");
  Serial.println(ruta);
  
  tft.fillScreen(ILI9341_BLACK); // Limpiar pantalla antes de dibujar
  delay(30);                     // Pequeña pausa de estabilización física del bus
  
  ImageReturnCode stat = reader.drawBMP((char*)ruta, tft, 0, 0);

  // Forzar que la pantalla desactive su línea CS tras terminar de dibujar
  digitalWrite(TFT_CS, HIGH);

  if (stat == IMAGE_SUCCESS) {
    Serial.println("¡Imagen cargada con éxito!");
  } else {
    Serial.print("Error al cargar imagen. Código de error: ");
    reader.printStatus(stat);
    
    tft.setTextColor(ILI9341_RED);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.printf("Error BMP: %d", stat);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- Sistema Visor B.E.M.O. Unificado con SdFat ---");

  // Configurar el botón con la resistencia pull-up interna activa
  pinMode(BOTON_PIN, INPUT_PULLUP);

  // Forzar apagado de selectores SPI al inicio para evitar estados flotantes
  pinMode(TFT_CS, OUTPUT);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH); 
  digitalWrite(SD_CS, HIGH);  
  delay(50);

  // 1. Inicializar bus SPI de hardware
  SPI.begin(SPI_SCLK, SPI_MISO, SPI_MOSI);

  // 2. Inicializar Pantalla
  tft.begin();
  tft.setRotation(3); 
  tft.fillScreen(ILI9341_BLUE); // Pantalla azul de espera

  // 3. Inicializar Tarjeta SD usando tu nueva sintaxis directa de SdFat
  Serial.print("Inicializando tarjeta SD... ");
  if (!SD_Card.begin(SD_CS, SD_SCK_MHZ(16))) {
    Serial.println("¡Fallo al detectar la SD!");
    tft.fillScreen(ILI9341_RED);
    return;
  }
  Serial.println("¡SD OK!");

  // 4. Determinar el tamaño del set y cargar la primera imagen al arrancar
  if (modoAnimales) {
    totalImagenes = sizeof(listaImagenesAnimales) / sizeof(listaImagenesAnimales[0]);
    mostrarImagen(listaImagenesAnimales[indiceActual]);
  } else {
    totalImagenes = sizeof(listaImagenesFamilia) / sizeof(listaImagenesFamilia[0]);
    mostrarImagen(listaImagenesFamilia[indiceActual]);
  }
}

void loop() {
  // Leer el estado del botón (LOW significa presionado)
  if (digitalRead(BOTON_PIN) == LOW) {
    
    // Verificar filtro antirrebote
    if ((millis() - ultimoTiempoRebote) > tiempoDebounce) {
      
      indiceActual++; // Avanzar al siguiente archivo
      
      // Si llegamos al final del set, regresar al inicio (ciclo infinito)
      if (indiceActual >= totalImagenes) {
        indiceActual = 0;
      }
      
      // Proyectar la imagen según el arreglo activo
      if (modoAnimales) {
        mostrarImagen(listaImagenesAnimales[indiceActual]);
      } else {
        mostrarImagen(listaImagenesFamilia[indiceActual]);
      }
      
      ultimoTiempoRebote = millis(); // Actualizar la marca de tiempo
    }
  }
}
