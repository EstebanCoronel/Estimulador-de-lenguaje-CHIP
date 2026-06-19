#ifndef PROYECTO_H
#define PROYECTO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//Botones de los modos de interacción (7 en total)
#define PIN_MODO_FONEMAS    GPIO4
#define PIN_MODO_ANIMALES   GPIO5
#define PIN_MODO_COLORES    GPIO6
#define PIN_MODO_NUMEROS    GPIO7
#define PIN_MODO_CUERPO     GPIO15
#define PIN_MODO_PREGUNTAS  GPIO16
#define PIN_MODO_LIBRE      GPIO17   // pendiente de programar

//Botones de CONTENIDO (7 en total, uno por cada item del modo)
#define PIN_BTN_1   GPIO18
#define PIN_BTN_2   GPIO8
#define PIN_BTN_3   GPIO9
#define PIN_BTN_4   GPIO10
#define PIN_BTN_5   GPIO11
#define PIN_BTN_6   GPIO12
#define PIN_BTN_7   GPIO13

// DFPlayer Mini (modulo de audio)
#define PIN_DFPLAYER_RX   GPIO44
#define PIN_DFPLAYER_TX   GPIO43
#define PIN_DFPLAYER_BUSY  GPIO3

// Pantalla ILI9341 + SD compartida (CS independientes)
#define PIN_TFT_CS    GPIO21
#define PIN_TFT_DC    GPIO47
#define PIN_TFT_RST   GPIO48
#define PIN_SD_CS     GPIO38

//Tiempo anti-rebote de botones (en milisegundos)
#define DEBOUNCE_MS  150

//Enum para los modos de interacción
typedef enum {
    MODO_FONEMAS  = 0,
    MODO_ANIMALES = 1,
    MODO_COLORES  = 2,
    MODO_NUMEROS  = 3,
    MODO_CUERPO   = 4,
    MODO_CANTIDAD = 5
} Modo;

// Cada item de contenido: lo que se ve, se oye, y donde esta la imagen
typedef struct {
    const char *etiqueta;     // texto en pantalla, ej: "Perro"
    uint8_t     carpeta_sd;   // carpeta de audio en el DFPlayer
    uint8_t     track_sd;     // numero de pista dentro de la carpeta
} ItemContenido;

// Devuelve los 7 items de un modo. Implementado en modos.c
const ItemContenido *obtener_contenido(Modo modo);
const char           *obtener_nombre_modo(Modo modo);

//Estados del programa
typedef enum {
    ESTADO_REPOSO,         // pantalla de inicio, esperando
    ESTADO_MODO_ACTIVO,    // el nino esta en un modo, puede tocar botones
    ESTADO_REPRODUCIENDO,  // el audio esta sonando, esperar a que acabe
    ESTADO_PREGUNTA,       // esperando que el nino responda
    ESTADO_EVALUANDO       // mostrando si acerto o no, antes de la siguiente
} Estado;

#include <stdbool.h>

// Pantalla
void pantalla_iniciar();
void pantalla_mostrar_reposo();
void pantalla_mostrar_modo(Modo modo);
void pantalla_mostrar_item(const char *etiqueta);
void pantalla_mostrar_pregunta();
void pantalla_mostrar_resultado(bool acierto);

// Audio
void audio_iniciar();
void audio_reproducir(uint8_t carpeta, uint8_t track);
void audio_reproducir_resultado(bool acierto);
bool audio_esta_reproduciendo();

// Progreso
void progreso_cargar(int modo_idx, uint16_t *aciertos, uint16_t *errores);
void progreso_guardar(int modo_idx, uint16_t aciertos, uint16_t errores);

#ifdef __cplusplus
}
#endif

#endif // PROYECTO_H
