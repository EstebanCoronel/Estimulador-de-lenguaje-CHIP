#ifndef AP_WIFI_EEPROM_H
#define AP_WIFI_EEPROM_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <DNSServer.h> // <-- Librería clave para la redirección automática del portal

extern WebServer server;

// Estas dos funciones viven en main.cpp y construyen el HTML/JSON del dashboard
// del juego (modo actual, aciertos, errores, progreso). Las declaramos aquí
// para poder registrarlas como rutas también desde este header.
void handleDashboard();
void handleApiEstado();

// --- ESTADO GLOBAL DEL MODO DE RED ---
// true  = portal cautivo / modo AP activo (sin red de casa todavía)
// false = ya conectado a la red de casa, AP y DNS apagados
inline bool& modoPortalActivo() {
    static bool activo = false;
    return activo;
}

// Instanciamos el servidor DNS global en este header (solo se usa si el portal está activo)
inline DNSServer& getDNSServer() {
    static DNSServer dns;
    return dns;
}

// --- DIRECCIONES FIJAS EN EEPROM ---
// 0   / 50   -> SSID   slot A / slot B (100 bytes cada uno, sobra de margen)
// 150 / 200  -> PASS   slot A / slot B
// 300        -> marcador de slot activo ("a" / "b")
// 400        -> marcador de inicialización EEPROM ("OK")
#define EEPROM_MARCA_INIT   400
#define EEPROM_SLOT_MARCA   300

// --- FUNCIONES INTERNAS DE MEMORIA ---
inline String leerStringDeEEPROM(int direccion) {
    String cadena = "";
    char caracter = EEPROM.read(direccion);
    // 0xFF es el valor de una EEPROM virgen (nunca escrita). Si lo vemos,
    // tratamos esa posición como vacía para no leer basura.
    if ((uint8_t)caracter == 0xFF) return "";

    int i = 0;
    while (caracter != '\0' && i < 100) {
        cadena += caracter;
        i++;
        caracter = EEPROM.read(direccion + i);
        if ((uint8_t)caracter == 0xFF) return ""; // corte de seguridad ante datos corruptos
    }
    return cadena;
}

inline void escribirStringEnEEPROM(int direccion, String cadena) {
    int longitudCadena = cadena.length();
    for (int i = 0; i < longitudCadena; i++) {
        EEPROM.write(direccion + i, cadena[i]);
    }
    EEPROM.write(direccion + longitudCadena, '\0');
    EEPROM.commit();
}

// Marca la EEPROM como inicializada y limpia los slots de red la primera vez
// que corre el firmware en un chip nuevo (evita leer 0xFF como credenciales).
inline void asegurarEEPROMInicializada() {
    String marca = leerStringDeEEPROM(EEPROM_MARCA_INIT);
    if (marca != "OK") {
        Serial.println("[EEPROM] Primera vez: limpiando slots de red...");
        escribirStringEnEEPROM(0, "");
        escribirStringEnEEPROM(50, "");
        escribirStringEnEEPROM(150, "");
        escribirStringEnEEPROM(200, "");
        escribirStringEnEEPROM(EEPROM_SLOT_MARCA, "a");
        escribirStringEnEEPROM(EEPROM_MARCA_INIT, "OK");
    }
}

// --- INFO DE RED PARA EL DASHBOARD ---
inline String ssidConectado() {
    if (WiFi.status() == WL_CONNECTED) return WiFi.SSID();
    return "";
}

// --- BORRAR CREDENCIALES GUARDADAS (botón "Olvidar red" del dashboard) ---
inline void olvidarRedesGuardadas() {
    escribirStringEnEEPROM(0, "");
    escribirStringEnEEPROM(50, "");
    escribirStringEnEEPROM(150, "");
    escribirStringEnEEPROM(200, "");
    escribirStringEnEEPROM(EEPROM_SLOT_MARCA, "a");
}

// --- APAGADO DEL PORTAL (AP + DNS) CUANDO YA HAY RED DE CASA ---
inline void apagarPortalCautivo() {
    if (!modoPortalActivo()) return; // ya estaba apagado

    Serial.println("[Portal] Apagando AP y DNS (ya conectado a red de casa).");
    getDNSServer().stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA); // dejamos solo modo estación, sin AP
    modoPortalActivo() = false;
}

// --- PÁGINA DE CONFIGURACIÓN WIFI (escaneo + formulario) ---
inline void handleSetupWifi() {
    Serial.println("[WiFi] Escaneando redes cercanas...");

    WiFi.scanDelete();
    delay(100);

    int n = WiFi.scanNetworks(false, false, false, 150);
    Serial.printf("[WiFi] %d redes encontradas.\n", n);

    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>";
    html += "body{font-family:Arial,sans-serif; padding:15px; background:#f4f4f9; text-align:center; color:#333;}";
    html += ".container{max-width:360px; background:white; padding:20px; border-radius:12px; box-shadow:0px 4px 15px rgba(0,0,0,0.1); margin:10px auto;}";
    html += "select, input{width:100%; padding:12px; margin:10px 0; border:1px solid #ccc; border-radius:6px; box-sizing:border-box; font-size:16px;}";
    html += "input[type='submit']{background:#4a148c; color:white; font-weight:bold; cursor:pointer; border:none; transition:0.2s;}";
    html += "input[type='submit']:active{background:#300a5a;}";
    html += "</style></head><body>";

    html += "<div class='container'>";
    html += "<h2>🤖 CHIP WiFi Setup</h2>";
    html += "<p style='font-size:14px; color:#666;'>Selecciona la red de tu hogar:</p>";
    html += "<form method='POST' action='/wifi'>";

    html += "<select name='ssid' required>";
    html += "<option value='' disabled selected>-- Elige una red local --</option>";

    if (n <= 0) {
        html += "<option value=''>No se detectaron redes (Recarga la página)</option>";
    } else {
        String redesAgregadas = "";
        for (int i = 0; i < n; ++i) {
            String currentSSID = WiFi.SSID(i);
            if (currentSSID == "" || redesAgregadas.indexOf(currentSSID) != -1) continue;

            redesAgregadas += currentSSID + "|";
            html += "<option value='" + currentSSID + "'>" + currentSSID + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
        }
    }
    html += "</select><br>";

    html += "<input type='password' name='password' placeholder='Contraseña de tu red' required><br>";
    html += "<input type='submit' value='Guardar y Conectar CHIP'>";
    html += "</form>";
    html += "<button onclick='window.location.reload()' style='width:100%; padding:10px; background:#e0e0e0; color:#333; border:none; border-radius:6px; cursor:pointer; font-size:14px;'>🔄 Buscar redes otra vez</button>";
    html += "</div></body></html>";

    server.send(200, "text/html", html);
}

inline void handleWifi() {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    int posW = 50;

    if (ssid == "" || ssid.startsWith("No se")) {
        server.send(200, "text/html", "<h2>Error: Red inválida.</h2><br><a href='/'>Volver</a>");
        return;
    }

    Serial.print("[WiFi] Intentando conexión hacia: "); Serial.println(ssid);

    WiFi.disconnect();
    delay(100);
    WiFi.begin(ssid.c_str(), password.c_str());

    int cnt = 0;
    while (WiFi.status() != WL_CONNECTED && cnt < 15) {
        delay(1000);
        Serial.print(".");
        cnt++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WiFi] ¡Enlazado Exitoso!");
        Serial.print("[WiFi] IP asignada por el router: ");
        Serial.println(WiFi.localIP());

        String varsave = leerStringDeEEPROM(EEPROM_SLOT_MARCA);
        if (varsave == "a") {
            posW = 0;
            escribirStringEnEEPROM(EEPROM_SLOT_MARCA, "b");
        } else {
            posW = 50;
            escribirStringEnEEPROM(EEPROM_SLOT_MARCA, "a");
        }

        escribirStringEnEEPROM(0 + posW, ssid);
        escribirStringEnEEPROM(150 + posW, password);

        String ipTexto = WiFi.localIP().toString();
        String html = "<div style='text-align:center; font-family:Arial;'><h2>¡CHIP Conectado con éxito!</h2>";
        html += "<p>El dashboard estará disponible en:</p>";
        html += "<p style='font-size:20px; font-weight:bold;'>http://" + ipTexto + "</p>";
        html += "<p>Reiniciando el robot...</p></div>";
        server.send(200, "text/html", html);

        delay(2500);
        ESP.restart();
    } else {
        Serial.println("\n[WiFi] Error: Timeout de credenciales.");
        server.send(200, "text/html", "<div style='text-align:center; font-family:Arial;'><h2>Contraseña incorrecta</h2><p>No se logró la conexión.</p><br><a href='/'>Reintentar</a></div>");
    }
}

// Endpoint para "Olvidar red" desde el dashboard: borra credenciales y reinicia
// directo en modo AP, sin tener que desenchufar nada.
inline void handleOlvidarRed() {
    Serial.println("[WiFi] Olvidando red guardada por orden del dashboard...");
    olvidarRedesGuardadas();
    server.send(200, "text/html", "<div style='text-align:center; font-family:Arial;'><h2>Red olvidada</h2><p>Reiniciando en modo configuración...</p></div>");
    delay(1500);
    ESP.restart();
}

// Intenta conectarse con la última red guardada en EEPROM (slot A o B).
// IMPORTANTE: se fuerza WIFI_STA puro (sin AP) ANTES de intentar, porque
// dejar el radio en modo dual (WIFI_AP_STA) retrasa y a veces impide que el
// ESP32 se asocie rápido a la red guardada.
inline bool lastRed() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true); // limpia configuración previa de STA, sin tocar AP
    delay(100);

    for (int psW = 0; psW <= 50; psW += 50) {
        String usu = leerStringDeEEPROM(0 + psW);
        String cla = leerStringDeEEPROM(150 + psW);

        if (usu == "" || usu.length() < 2) continue;

        Serial.print("[Memoria] Auto-conectando a: "); Serial.println(usu);
        WiFi.begin(usu.c_str(), cla.c_str());

        int cnt = 0;
        while (WiFi.status() != WL_CONNECTED && cnt < 15) {
            delay(500);
            Serial.print(".");
            cnt++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n[WiFi] Conectado desde EEPROM.");
            Serial.print("[WiFi] IP del dashboard: "); Serial.println(WiFi.localIP());
            return true;
        }
        Serial.println("\n[WiFi] Ese slot no conectó, probando el otro si existe...");
        WiFi.disconnect();
        delay(200);
    }
    return false;
}

inline void initAP(const char *apSsid) {
    Serial.println("[Portal] Levantando modo AP + portal cautivo...");

    // Modo dual para poder escanear el entorno mientras se mantiene la red propia
    WiFi.mode(WIFI_AP_STA);

    IPAddress apIP(192, 168, 4, 1);
    IPAddress netMsk(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, apIP, netMsk);

    WiFi.softAP(apSsid, NULL); // Red ABIERTA

    getDNSServer().start(53, "*", apIP);

    // En modo AP, la raíz "/" SIEMPRE es el formulario de configuración.
    server.on("/", handleSetupWifi);
    server.on("/wifi", handleWifi);
    server.on("/generate_204", handleSetupWifi);  // Android (chequeo de portal cautivo)
    server.on("/fwlink", handleSetupWifi);         // Windows (chequeo de portal cautivo)
    server.onNotFound(handleSetupWifi);            // Cualquier otra URL -> formulario

    server.begin();
    modoPortalActivo() = true;
    Serial.println("[OK] DNS e Interfaz de Red listas en 192.168.4.1");
}

// Página simple para rutas desconocidas en modo normal (favicon.ico, etc.)
// Evita el error "request handler not found" en el log y simplemente redirige
// al dashboard, sin gastar tiempo escaneando wifi como hacía antes.
inline void handleNotFoundNormal() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

// Registra las rutas para cuando YA estamos conectados a la red de casa.
// Aquí la raíz "/" es el DASHBOARD directo, no el formulario de wifi.
inline void initServidorNormal() {
    server.on("/", handleDashboard);
    server.on("/api/estado", handleApiEstado);
    server.on("/setup-wifi", handleSetupWifi); // reconfigurar manualmente si se quiere, sin tener que apagar el AP
    server.on("/wifi", handleWifi);
    server.on("/olvidar-red", handleOlvidarRed);
    server.onNotFound(handleNotFoundNormal);
    server.begin();
}

inline void configurarWiFi() {
    EEPROM.begin(512);
    asegurarEEPROMInicializada();
    Serial.println("[CHIP-WiFi] Buscando credenciales...");

    if (lastRed()) {
        // Ya hay red de casa: NO levantamos AP ni DNS, solo el WebServer normal.
        initServidorNormal();
        Serial.println("[OK] Conectado a red de casa. Dashboard en la raíz http://<IP>/");
    } else {
        Serial.println("[ALERTA] Requiere configuración manual. Levantando portal AP...");
        initAP("CHIP_Robot_Educativo");
    }
}

#endif // AP_WIFI_EEPROM_H