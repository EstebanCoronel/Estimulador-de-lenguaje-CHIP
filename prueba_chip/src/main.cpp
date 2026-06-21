#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_ImageReader.h>
#include <math.h> // round() para el cálculo de % de precisión en el dashboard

// 1. Primero llamamos a las librerías nativas del sistema
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>

// 2. Definimos la instancia del servidor que requiere el .h
WebServer server(80);

// 3. Por último, incluimos el header local de la materia
#include "apwifieeprommode.h"

// --- CONFIGURACIÓN DE PINES (ESP32-S3) ---
#define SPI_MOSI 11
#define SPI_SCLK 12
#define SPI_MISO 13

#define TFT_CS   10
#define TFT_DC   9
#define TFT_RST  14  
#define SD_CS    4 

#define PIN_BTN_1   6
#define PIN_BTN_2   5
#define PIN_BTN_3   16
#define PIN_BTN_4   17
#define PIN_BTN_5   8
#define PIN_BTN_6   20
#define PIN_BTN_7   38

#define DEBOUNCE_MS        150
#define TIMEOUT_REPOSO_MS  18000

// --- ENUMS Y ESQUEMAS DE ESTADOS ---
typedef enum {
    MODO_FONEMAS  = 0,
    MODO_ANIMALES = 1,
    MODO_COLORES  = 2,
    MODO_FAMILIA  = 3,
    MODO_CUERPO   = 4,
    MODO_CANTIDAD = 5
} Modo;

typedef enum {
    ESTADO_REPOSO,
    ESTADO_MODO_ACTIVO,
    ESTADO_PREGUNTAS_JUGANDO
} Estado;

Estado estado_actual = ESTADO_REPOSO;
Modo modo_actual = MODO_FONEMAS;
unsigned long ultima_actividad = 0;

// --- ESTRUCTURAS DE DATOS PARA JUEGO ---
typedef struct {
    const char *etiqueta;
    const char *imagen_bmp;   
} ItemContenido;

typedef struct {
    const char* pregunta_texto;
    const char* ops[4];
    uint8_t correcta_idx; 
} EvaluacionItem;

// --- BANCO DE CONTENIDOS ESTÁTICOS ---
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
    { "Ma", 0 }, { "Pa", 0 }, { "Ba", 0 }, { "Ta", 0 }, { "La", 0 }, { "Sa", 0 }, { "Na", 0 },
};

const ItemContenido COLORES[7] = {
    { "Rojo", 0 }, { "Azul", 0 }, { "Verde", 0 }, { "Amarillo", 0 }, { "Naranja", 0 }, { "Morado", 0 }, { "Rosa", 0 },
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

// --- MÉTRICAS LÚDICAS ---
int aciertos_por_modo[5] = {0, 0, 0, 0, 0}; 
int errores_por_modo[5] = {0, 0, 0, 0, 0};
// El % de progreso ya no se guarda aparte: se calcula en vivo como
// aciertos / (aciertos+errores) en el endpoint del dashboard, así nunca
// se desincroniza de los contadores reales.

EvaluacionItem pregunta_activa;
int fallos_en_pregunta = 0;
unsigned long tiempo_limite_pregunta = 0;

// --- ASIGNACIÓN DE HARDWARE ---
const uint8_t PINES_BTN[7] = { PIN_BTN_1, PIN_BTN_2, PIN_BTN_3, PIN_BTN_4, PIN_BTN_5, PIN_BTN_6, PIN_BTN_7 };
volatile bool flag_btn[7] = { false, false, false, false, false, false, false };
volatile unsigned long ultimo_tiempo[7] = {0};

void IRAM_ATTR isr_btn0() { flag_btn[0] = true; }
void IRAM_ATTR isr_btn1() { flag_btn[1] = true; }
void IRAM_ATTR isr_btn2() { flag_btn[2] = true; }
void IRAM_ATTR isr_btn3() { flag_btn[3] = true; }
void IRAM_ATTR isr_btn4() { flag_btn[4] = true; }
void IRAM_ATTR isr_btn5() { flag_btn[5] = true; }
void IRAM_ATTR isr_btn6() { flag_btn[6] = true; }

void (*ISRS[7])() = { isr_btn0, isr_btn1, isr_btn2, isr_btn3, isr_btn4, isr_btn5, isr_btn6 };

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
SdFat            SD_Card; 
Adafruit_ImageReader reader{SD_Card};

// --- FUNCIONES AUXILIARES DE NAVEGACIÓN ---
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

// --- RENDERIZADO VISUAL DEL SISTEMA (PANTALLA TFT) ---
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
    if (modo_actual == MODO_COLORES) {
        uint16_t color_fondo = ILI9341_BLACK;
        if (strcmp(item.etiqueta, "Rojo") == 0)         color_fondo = ILI9341_RED;
        else if (strcmp(item.etiqueta, "Azul") == 0)    color_fondo = ILI9341_BLUE;
        else if (strcmp(item.etiqueta, "Verde") == 0)   color_fondo = ILI9341_GREEN;
        else if (strcmp(item.etiqueta, "Amarillo") == 0) color_fondo = ILI9341_YELLOW;
        else if (strcmp(item.etiqueta, "Naranja") == 0)  color_fondo = 0xFD20; 
        else if (strcmp(item.etiqueta, "Morado") == 0)   color_fondo = 0x780F; 
        else if (strcmp(item.etiqueta, "Rosa") == 0)     color_fondo = 0xFE19; 

        tft.fillScreen(color_fondo);
        tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
        tft.setTextSize(4);
        tft.setCursor(40, 110);
        tft.println(item.etiqueta);
        return; 
    }

    tft.fillScreen(ILI9341_BLACK);

    if (modo_actual == MODO_FONEMAS) {
        tft.setTextColor(ILI9341_GREEN);
        tft.setTextSize(3);
        tft.setCursor(20, 20);
        tft.printf("Familia de la %c:", item.etiqueta[0]); 

        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(5); 
        char letra = item.etiqueta[0]; 
        tft.setCursor(40, 110);
        tft.printf("%ca  %ce  %ci  %co  %cu", letra, letra, letra, letra, letra);
        return; 
    }

    if (item.imagen_bmp != 0) {
        Serial.print("Cargando desde SD: ");
        Serial.println(item.imagen_bmp);
        digitalWrite(TFT_CS, HIGH);
        digitalWrite(SD_CS, HIGH);

        ImageReturnCode stat = reader.drawBMP((char *) item.imagen_bmp, tft, 0, 0);
        if (stat != IMAGE_SUCCESS) {
            Serial.print("Error SD. Codigo: ");
            reader.printStatus(stat);
            tft.setTextColor(ILI9341_RED); tft.setTextSize(2); tft.setCursor(10, 50);
            tft.printf("Err BMP: %d", (int)stat);
        }
    }

    tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
    tft.setTextSize(4);
    tft.setCursor(30, 250);
    tft.println(item.etiqueta);
}

// --- MODO EVALUATIVO DE PREGUNTAS (CONTEXTUAL) ---
void generar_pregunta_contextual() {
    fallos_en_pregunta = 0;
    
    if (modo_actual == MODO_ANIMALES) {
        pregunta_activa = {"¿Que animal hace el sonido 'Muuu'?", {"Gato", "Vaca", "Perro", "Leon"}, 1};
    } else if (modo_actual == MODO_COLORES) {
        pregunta_activa = {"La Manzana, ¿de que color es?", {"Azul", "Verde", "Rojo", "Amarillo"}, 2};
    } else if (modo_actual == MODO_FONEMAS) {
        pregunta_activa = {"Busca la silaba 'Ma'", {"Pa", "Ba", "Ta", "Ma"}, 3};
    } else if (modo_actual == MODO_FAMILIA) {
        pregunta_activa = {"¿Quien es el papa de tu papa?", {"Abuelo", "Primo", "Hermano", "Tio"}, 0};
    } else { 
        pregunta_activa = {"¿Con que parte escuchas?", {"Ojo", "Nariz", "Oreja", "Boca"}, 2};
    }
    
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE); tft.setTextSize(2);
    tft.setCursor(10, 15); tft.println(pregunta_activa.pregunta_texto);
    
    uint16_t colores_bloques[4] = {ILI9341_RED, ILI9341_BLUE, ILI9341_GREEN, ILI9341_YELLOW};
    int posiciones_y[4] = {60, 100, 140, 180};
    
    for(int i=0; i<4; i++) {
        tft.fillRect(15, posiciones_y[i], 290, 32, colores_bloques[i]);
        tft.setTextColor(i >= 2 ? ILI9341_BLACK : ILI9341_WHITE);
        tft.setTextSize(2); tft.setCursor(25, posiciones_y[i] + 8);
        tft.printf("%d) %s", i+1, pregunta_activa.ops[i]);
    }
    tiempo_limite_pregunta = millis();
}

void evaluar_respuesta_boton(uint8_t btn_idx) {
    if (btn_idx > 3) return; 
    
    if (btn_idx == pregunta_activa.correcta_idx) {
        aciertos_por_modo[modo_actual]++;
        
        tft.fillScreen(ILI9341_GREEN);
        tft.setTextColor(ILI9341_BLACK); tft.setTextSize(3); tft.setCursor(60, 100);
        tft.println("¡CORRECTO!");
        delay(2000);
        
        pantalla_mostrar_reposo();
        estado_actual = ESTADO_REPOSO;
        tiempo_limite_pregunta = 0;
    } else {
        errores_por_modo[modo_actual]++;
        fallos_en_pregunta++;
        
        tft.fillScreen(ILI9341_RED);
        tft.setTextColor(ILI9341_WHITE); tft.setTextSize(3); tft.setCursor(30, 100);
        tft.println("INTENTA DE NUEVO");
        delay(1500);
        
        // Nivel 1 de Andamiaje: Reducción Dinámica a 50/50 al 2do fallo
        if (fallos_en_pregunta >= 2) {
            tft.fillScreen(ILI9341_BLACK);
            tft.setTextColor(ILI9341_MAGENTA); tft.setTextSize(2); tft.setCursor(10, 15);
            tft.println("¡Pista! Elige entre estas:");
            
            int posiciones_y[4] = {60, 100, 140, 180};
            uint16_t colores_bloques[4] = {ILI9341_RED, ILI9341_BLUE, ILI9341_GREEN, ILI9341_YELLOW};
            
            for(int i=0; i<4; i++) {
                if (i == pregunta_activa.correcta_idx || i == btn_idx) {
                    tft.fillRect(15, posiciones_y[i], 290, 32, colores_bloques[i]);
                    tft.setTextColor(i >= 2 ? ILI9341_BLACK : ILI9341_WHITE);
                    tft.setCursor(25, posiciones_y[i] + 8);
                    tft.printf("%d) %s", i+1, pregunta_activa.ops[i]);
                }
            }
        } else {
            generar_pregunta_contextual();
        }
        tiempo_limite_pregunta = millis(); 
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

// --- DASHBOARD WEB (Estado en vivo del CHIP) ---
const char *obtener_nombre_estado(Estado e) {
    if (e == ESTADO_REPOSO)            return "En reposo";
    if (e == ESTADO_MODO_ACTIVO)       return "Modo activo";
    if (e == ESTADO_PREGUNTAS_JUGANDO) return "Jugando preguntas";
    return "?";
}

// Color asociado a cada modo, reutilizado tanto en el TFT como en el dashboard
// para que el niño/familia asocie visualmente cada actividad.
const char *obtener_color_modo(Modo m) {
    if (m == MODO_FONEMAS)  return "#2e7d32"; // verde
    if (m == MODO_ANIMALES) return "#5d4037"; // marrón
    if (m == MODO_COLORES)  return "#ad1457"; // rosa fuerte
    if (m == MODO_FAMILIA)  return "#1565c0"; // azul
    if (m == MODO_CUERPO)   return "#ef6c00"; // naranja
    return "#4a148c";
}

// Devuelve el estado completo en JSON para que el dashboard se auto-actualice
// sin recargar toda la página (fetch periódico desde el navegador).
void handleApiEstado() {
    int totalAciertos = 0, totalErrores = 0;
    for (int i = 0; i < 5; i++) {
        totalAciertos += aciertos_por_modo[i];
        totalErrores  += errores_por_modo[i];
    }

    unsigned long segundosInactivo = (millis() - ultima_actividad) / 1000;

    String json = "{";
    json += "\"estado\":\"" + String(obtener_nombre_estado(estado_actual)) + "\",";
    json += "\"modo_actual\":\"" + String(obtener_nombre_modo(modo_actual)) + "\",";
    json += "\"modo_idx\":" + String((int)modo_actual) + ",";
    json += "\"segundos_inactivo\":" + String(segundosInactivo) + ",";
    json += "\"total_aciertos\":" + String(totalAciertos) + ",";
    json += "\"total_errores\":" + String(totalErrores) + ",";

    json += "\"wifi\":{";
    json += "\"ssid\":\"" + ssidConectado() + "\",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI());
    json += "},";

    json += "\"modos\":[";
    for (int i = 0; i < 5; i++) {
        if (i > 0) json += ",";
        int intentos = aciertos_por_modo[i] + errores_por_modo[i];
        // % de precisión: aciertos sobre el total de intentos. Sin intentos
        // todavía, la barra queda en 0 (no hay datos para mostrar progreso).
        int progreso = (intentos > 0) ? (int)round((aciertos_por_modo[i] * 100.0) / intentos) : 0;

        json += "{";
        json += "\"nombre\":\"" + String(obtener_nombre_modo((Modo)i)) + "\",";
        json += "\"color\":\"" + String(obtener_color_modo((Modo)i)) + "\",";
        json += "\"aciertos\":" + String(aciertos_por_modo[i]) + ",";
        json += "\"errores\":" + String(errores_por_modo[i]) + ",";
        json += "\"progreso\":" + String(progreso);
        json += "}";
    }
    json += "]}";

    server.send(200, "application/json", json);
}

// Página HTML del dashboard. Se auto-refresca vía JS llamando a /api/estado
// cada 1.5 segundos, sin recargar toda la página.
void handleDashboard() {
    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>CHIP - Dashboard</title>";
    html += "<style>";
    html += "*{box-sizing:border-box;}";
    html += "body{font-family:'Segoe UI',Arial,sans-serif; background:#f0f0f7; color:#2b2b2b; padding:14px; margin:0;}";
    html += ".container{max-width:520px; margin:0 auto;}";
    html += "h2{text-align:center; color:#4a148c; margin-bottom:4px;}";
    html += ".subtitulo{text-align:center; font-size:13px; color:#888; margin-bottom:16px;}";
    html += ".card{background:white; border-radius:14px; padding:18px; margin-bottom:14px; box-shadow:0px 4px 14px rgba(0,0,0,0.07);}";
    html += ".estado-actual{font-size:19px; font-weight:bold; text-align:center; color:#4a148c;}";
    html += ".sub-estado{text-align:center; font-size:13px; color:#777; margin-top:4px;}";
    html += ".resumen{display:flex; justify-content:space-around; margin-top:14px;}";
    html += ".resumen div{text-align:center;}";
    html += ".resumen .num{font-size:22px; font-weight:bold;}";
    html += ".resumen .label{font-size:11px; color:#888;}";
    html += ".ok{color:#2e7d32;} .err{color:#c62828;}";
    html += ".wifi-fila{display:flex; justify-content:space-between; font-size:13px; color:#555; margin-bottom:6px;}";
    html += ".wifi-fila b{color:#333;}";
    html += ".modo-fila{display:flex; align-items:center; margin-bottom:12px; padding:8px; border-radius:10px; transition:background 0.3s;}";
    html += ".modo-nombre{font-weight:bold; width:100px; font-size:14px;}";
    html += ".barra-bg{flex:1; background:#e8e8ef; border-radius:8px; height:16px; margin:0 10px; overflow:hidden;}";
    html += ".barra-fill{height:100%; border-radius:8px; transition:width 0.5s;}";
    html += ".stats{font-size:12px; color:#666; white-space:nowrap; width:70px; text-align:right;}";
    html += ".activo{background:#f3e5f5;}";
    html += "h3{margin:0 0 12px 0; font-size:15px; color:#444;}";
    html += "button.peligro{width:100%; padding:12px; background:#c62828; color:white; border:none; border-radius:8px; font-size:14px; font-weight:bold; cursor:pointer;}";
    html += "button.peligro:active{background:#8e0000;}";
    html += "a.link-config{display:block; text-align:center; margin-top:10px; font-size:13px; color:#4a148c;}";
    html += "</style></head><body>";

    html += "<div class='container'>";
    html += "<h2>🤖 CHIP - Dashboard</h2>";
    html += "<div class='subtitulo'>Robot educativo — estado en vivo</div>";

    html += "<div class='card'>";
    html += "<div class='estado-actual' id='estado-actual'>Cargando...</div>";
    html += "<div class='sub-estado' id='sub-estado'></div>";
    html += "<div class='resumen'>";
    html += "<div><div class='num ok' id='total-aciertos'>-</div><div class='label'>Aciertos totales</div></div>";
    html += "<div><div class='num err' id='total-errores'>-</div><div class='label'>Errores totales</div></div>";
    html += "</div>";
    html += "</div>";

    html += "<div class='card'>";
    html += "<h3>📶 Conexión WiFi</h3>";
    html += "<div class='wifi-fila'><span>Red</span><b id='wifi-ssid'>-</b></div>";
    html += "<div class='wifi-fila'><span>IP</span><b id='wifi-ip'>-</b></div>";
    html += "<div class='wifi-fila'><span>Señal</span><b id='wifi-rssi'>-</b></div>";
    html += "</div>";

    html += "<div class='card'>";
    html += "<h3>🎯 Progreso por modo</h3>";
    html += "<div id='lista-modos'>Cargando métricas...</div>";
    html += "</div>";

    html += "<div class='card'>";
    html += "<button class='peligro' onclick='olvidarRed()'>📡 Olvidar red y reconfigurar</button>";
    html += "<a class='link-config' href='/setup-wifi'>O cambiar de red manualmente sin borrar</a>";
    html += "</div>";

    html += "</div>";

    html += "<script>";
    html += "function fmtRSSI(r){ if(r >= -55) return r + ' dBm (excelente)'; if(r >= -70) return r + ' dBm (buena)'; return r + ' dBm (débil)'; }";
    html += "function fmtTiempo(s){ if(s < 60) return s + 's'; const m = Math.floor(s/60); return m + 'm ' + (s%60) + 's'; }";
    html += "async function actualizar(){";
    html += "  try{";
    html += "    const r = await fetch('/api/estado');";
    html += "    const d = await r.json();";
    html += "    document.getElementById('estado-actual').innerText = d.estado + ' — ' + d.modo_actual;";
    html += "    document.getElementById('sub-estado').innerText = 'Sin actividad hace ' + fmtTiempo(d.segundos_inactivo);";
    html += "    document.getElementById('total-aciertos').innerText = d.total_aciertos;";
    html += "    document.getElementById('total-errores').innerText = d.total_errores;";
    html += "    document.getElementById('wifi-ssid').innerText = d.wifi.ssid || '(desconectado)';";
    html += "    document.getElementById('wifi-ip').innerText = d.wifi.ip;";
    html += "    document.getElementById('wifi-rssi').innerText = fmtRSSI(d.wifi.rssi);";
    html += "    let h = '';";
    html += "    d.modos.forEach((m, i) => {";
    html += "      const activoClass = (i === d.modo_idx && d.estado !== 'En reposo') ? 'activo' : '';";
    html += "      h += '<div class=\"modo-fila ' + activoClass + '\">';";
    html += "      h += '<span class=\"modo-nombre\">' + m.nombre + '</span>';";
    html += "      h += '<span class=\"barra-bg\"><span class=\"barra-fill\" style=\"width:' + m.progreso + '%; background:' + m.color + '\"></span></span>';";
    html += "      h += '<span class=\"stats\">✅' + m.aciertos + ' ❌' + m.errores + '</span>';";
    html += "      h += '</div>';";
    html += "    });";
    html += "    document.getElementById('lista-modos').innerHTML = h;";
    html += "  } catch(e) { console.error(e); }";
    html += "}";
    html += "function olvidarRed(){";
    html += "  if(confirm('¿Seguro que quieres borrar la red guardada? El CHIP se reiniciará en modo configuración.')){";
    html += "    fetch('/olvidar-red').then(() => { alert('Listo. Busca la red CHIP_Robot_Educativo para reconfigurar.'); });";
    html += "  }";
    html += "}";
    html += "actualizar();";
    html += "setInterval(actualizar, 1500);";
    html += "</script>";

    html += "</body></html>";

    server.send(200, "text/html", html);
}

// --- SETUP PRINCIPAL ---
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- INICIALIZANDO ESP32-S3 CON PORTAL ABIERTO ---");

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

    if (!SD_Card.begin(SdSpiConfig(SD_CS, SHARED_SPI, SD_SCK_MHZ(10), &SPI))) {
        Serial.println("¡Fallo en la SD!");
        tft.fillScreen(ILI9341_RED);
    } else {
        Serial.println("¡SD OK!");
    }

    digitalWrite(SD_CS, HIGH);
    digitalWrite(TFT_CS, HIGH);

    // Llamado a la función de red adaptada sin clave.
    // Las rutas del dashboard ("/", "/api/estado", etc.) ya se registran
    // dentro de configurarWiFi() -> initServidorNormal(), solo cuando
    // estamos conectados a la red de casa (no en modo AP).
    configurarWiFi(); 

    pantalla_mostrar_reposo();
    ultima_actividad = millis();
    Serial.println("=== CHIP Listo para Jugar ===");
}

// --- BUCLE PRINCIPAL (MÁQUINA DE ESTADOS + REBOTE) ---
void loop() {
    // Escucha peticiones HTTP del portal cautivo SOLO si sigue activo.
    // Una vez conectado a la red de casa, el AP/DNS se apaga y dejamos
    // de llamar a esto (evita malgastar ciclos y posibles conflictos de DNS).
    if (modoPortalActivo()) {
        getDNSServer().processNextRequest();

        // Si en algún momento, estando en modo AP, logramos quedar conectados
        // a una red, apagamos el portal inmediatamente.
        if (WiFi.status() == WL_CONNECTED) {
            apagarPortalCautivo();
        }
    }

    // Escucha peticiones HTTP (dashboard / portal, según el modo activo)
    server.handleClient();

    for (uint8_t i = 0; i < 7; i++) {
        if (!flag_btn[i]) continue;
        
        // Antirrebote robusto por software
        if (millis() - ultimo_tiempo[i] > DEBOUNCE_MS) {
            ultimo_tiempo[i] = millis();
            ultima_actividad = millis();

            if (estado_actual == ESTADO_REPOSO) {
                if (i == 5) { // Botón 6 -> Modo Preguntas
                    if (estado_actual != ESTADO_PREGUNTAS_JUGANDO) {
                        Serial.println("[Evaluacion] Iniciando flujo evaluativo");
                        estado_actual = ESTADO_PREGUNTAS_JUGANDO;
                        generar_pregunta_contextual();
                    }
                } else {
                    entrar_a_modo((Modo) i);
                    Serial.printf("[MODO] -> %s\n", obtener_nombre_modo(modo_actual));
                }
            }
            else if (estado_actual == ESTADO_MODO_ACTIVO) {
                mostrar_item_de_modo(i);
                Serial.printf("[ITEM] Modo=%s Boton=%d\n", obtener_nombre_modo(modo_actual), i);
            }
            else if (estado_actual == ESTADO_PREGUNTAS_JUGANDO) {
                if (millis() - tiempo_limite_pregunta < 10000 && i <= 3) {
                    evaluar_respuesta_boton(i);
                }
            }
            
            delay(50); 
            flag_btn[i] = false;
        } else {
            flag_btn[i] = false;
        }
    }

    // Nivel 2 de Andamiaje: Timeout de asistencia tras 10s
    if (estado_actual == ESTADO_PREGUNTAS_JUGANDO && (millis() - tiempo_limite_pregunta > 10000) && tiempo_limite_pregunta != 0) {
        Serial.println("[Andamiaje] Timeout 10s alcanzado. Revelando de forma guiada.");
        tft.fillScreen(ILI9341_ORANGE);
        tft.setTextColor(ILI9341_BLACK); tft.setTextSize(2); tft.setCursor(15, 100);
        tft.printf("La respuesta correcta era:\n\n   -> %s", pregunta_activa.ops[pregunta_activa.correcta_idx]);
        delay(3500);
        
        tiempo_limite_pregunta = 0;
        pantalla_mostrar_reposo();
        estado_actual = ESTADO_REPOSO;
    }

    // Timeout por inactividad absoluta del sistema
    if (estado_actual != ESTADO_REPOSO && (millis() - ultima_actividad > TIMEOUT_REPOSO_MS) && estado_actual != ESTADO_PREGUNTAS_JUGANDO) {
        pantalla_mostrar_reposo();
        estado_actual = ESTADO_REPOSO;
        tiempo_limite_pregunta = 0;
        Serial.println("[TIMEOUT] Volviendo a reposo");
    }

    delay(10);
}