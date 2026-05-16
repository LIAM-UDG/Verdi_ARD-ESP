#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

Preferences preferences;

// Conexion al hotspot
const char* ssid = "ModemLG";
const char* password = "7x8K225&";

//  URLs de comunicacion al servidor
const char* urlCodVal       = "http://lothgarder.atwebpages.com/?accion=generarCodV";
const char* urlObtenerIds   = "http://lothgarder.atwebpages.com/?accion=obtenerIDsPU";

const char* urlSistema      = "http://lothgarder.atwebpages.com/?accion=actualizarSis";
const char* urlPlanta       = "http://lothgarder.atwebpages.com/?accion=actualizarPs";

// Pines de comunicacion serial
#define RXD2 16
#define TXD2 17

// VARIABLES GLOBALES
String sistemaID = "2";
String deviceKey = "123";
//Tiempo de vinculacion
const unsigned long tiempoVin = 15000; 

// Variables de vinculación
String correoSistema = "";
String claveAct = "";
bool vinculado = false;

// Control de intervalos
unsigned long ultimoCheckIds = 0;
unsigned long ultimoIntento = 0;

// Usar Preferences para guardar datos
bool usarPreferences = false;

// Setup del ESP32
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  // Concexion a la red
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[ESP32] WiFi Conectado");

  // Cargar datos guardados si se usa preferencias
  if (usarPreferences) {
    preferences.begin("sistema", false);
    claveAct = preferences.getString("clave", "");
    correoSistema = preferences.getString("correo", "");
  }

  // Verificar vinculación
  if (claveAct != "") {
    vinculado = true;
    Serial.println("Sistema ya vinculado");
  } else {
    vinculado = false;
    Serial.println("Sistema en modo vinculación");
  }
}

// Loop del ESP32
void loop() {

  // Reconectar WiFi si se pierde
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ESP32] WiFi perdido, reconectando...");
    WiFi.begin(ssid, password);
    delay(2000);
    return;
  }

  // Modo vinculación
  if (claveAct == "") {
    if (ultimoIntento == 0 || millis() - ultimoIntento > tiempoVin) {
      //Llamado a la funcion para generar el codigo de vinculacion 
      generarCodigo();
      ultimoIntento = millis();
    }
    return;
  }

  // Obtener IDs y configuración de plantas 
  if (millis() - ultimoCheckIds > 1800000 || ultimoCheckIds == 0) {
    //Funcino para obetener IDs y humedad de plantas
    obtenerPlantasYConfigurar();
    ultimoCheckIds = millis();
  }

  // Validacion de la comunicaicon de arduino y ESP32
  if (Serial2.available()) {
    String mensaje = Serial2.readStringUntil('\n');
    mensaje.trim();

    //Extraccion de datos de sistema
    if (mensaje.startsWith("SISTEMA")) {
      //Llamado a la funcion para enviar datos del firware
      enviarMetricasSistema(mensaje);
    }//Extracccion de datos de planta
    else if (mensaje.startsWith("PLANTA")) {
      //Llamado a la funcion para enviar datos de la planta
      enviarMetricasPlanta(mensaje);
    }//Extraccion de modulos
    else if (mensaje.startsWith("MODULOS")) {
      // Arduino notifica cuántos módulos detectó
      int numModulos = mensaje.substring(8).toInt();
      Serial.print("[ESP32] Arduino detectó ");
      Serial.print(numModulos);
      Serial.println(" módulos");
    }
  }
}

// Funcion para generar codigo de vinculacion 
void generarCodigo() {
  HTTPClient http;
  http.begin(urlCodVal);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String datos = "idS=" + sistemaID + "&llave=" + deviceKey;
  int httpResponseCode = http.POST(datos);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("[ESP32] Respuesta vinculación: " + response);

    // Sistema vinculado
    if (response.indexOf("linked") != -1) {

      // Extraer clave
      int inicioClave = response.indexOf("clave\":\"") + 8;
      int finClave = response.indexOf("\"", inicioClave);
      String claveNueva = response.substring(inicioClave, finClave);
      claveAct = claveNueva;

      // Extraer correo
      int inicioCorreo = response.indexOf("correo\":\"") + 9;
      int finCorreo = response.indexOf("\"", inicioCorreo);
      correoSistema = response.substring(inicioCorreo, finCorreo);

      vinculado = true;

      // Guardar en Preferences solo si se usan
      if (usarPreferences) {
        preferences.putString("clave", claveNueva);
        preferences.putString("correo", correoSistema);
      }

      Serial.println("[ESP32] ¡SISTEMA VINCULADO!");
      Serial.println("[ESP32] Clave: " + claveAct);
      Serial.println("[ESP32] Correo: " + correoSistema);

      // Obtener plantas inmediatamente
      obtenerPlantasYConfigurar();

      http.end();
      return;
    }

    // Mostrar código de vinculación
    Serial.println("ID Sistema: " + sistemaID);
    int pos = response.indexOf("codigo");
    if (pos != -1) {
      String codigo = response.substring(pos + 9, pos + 15);
      Serial.println("CÓDIGO VINCULACIÓN: " + codigo);
    }

  } else {
    Serial.println("Error HTTP al generar código");
  }

  http.end();
}

// Obtener IDs y humedad ideal de las plantas para enviarlas al ARDUINO
void obtenerPlantasYConfigurar() {
  HTTPClient http;
  http.begin(urlObtenerIds);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String datos = "correo=" + correoSistema + "&clave=" + claveAct;
  Serial.println("POST DATA:");
  Serial.println(datos);
  int httpCode = http.POST(datos);

  if (httpCode == 200) {
    String payload = http.getString();
    Serial.println("JSON recibido:");
    Serial.println(payload);

    // Parsear JSON
    StaticJsonDocument<2048> doc; //Tamaño del JSON
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("Error parseando JSON: ");
      Serial.println(error.c_str());
      http.end();
      return;
    }

    // Verificar status
    const char* status = doc["status"];
    if (strcmp(status, "success") != 0) {
      Serial.println("Servidor no retornó que la peticion fue correcta");
      http.end();
      return;
    }

    // Obtener array de plantas
    JsonArray plantas = doc["plantas"];
    int numPlantas = plantas.size();

    if (numPlantas == 0) {
      Serial.println("No hay plantas automáticas configuradas");
      http.end();
      return;
    }

    Serial.print("Plantas encontradas: ");
    Serial.println(numPlantas);

    // Primero se realiza el envio de IDs de plantas al arduino
    String idsString = "";
    for (int i = 0; i < numPlantas; i++) {
      int id = plantas[i]["ID_InfoP"];
      if (i > 0) idsString += ",";
      idsString += String(id);
    }

    Serial2.println("CONFIG_IDS:" + idsString);
    Serial.println("ESP32 a Arduino: CONFIG_IDS:" + idsString);

    delay(100);  // Dar tiempo al Arduino para procesar

    // Despues se realiza el envio de humbrales de humedad al arduino
    for (int i = 0; i < numPlantas; i++) {
      int id = plantas[i]["ID_InfoP"];
      int humIdealPct = plantas[i]["HumIdeal"];
      int humMinimaPct = plantas[i]["HuMinima"];

      // CONVERSIÓN: % → ADC invertido
      // HL-69: 1023=seco, 0=húmedo
      // Por lo tanto:
      //   HuMinima (20%) → valor ADC alto (seco)
      //   HumIdeal (40%) → valor ADC bajo (húmedo)
      int umbralMinimo = map(humMinimaPct, 0, 100, 1023, 0);  // 20% → ~818
      int umbralIdeal  = map(humIdealPct, 0, 100, 1023, 0);   // 40% → ~614

      // Enviar al Arduino
      String umbralMsg = "CONFIG_UMBRALES:" + String(id) + "," +
                         String(umbralMinimo) + "," + String(umbralIdeal);

      Serial2.println(umbralMsg);
      Serial.println("ESP32 a Arduino: " + umbralMsg);

      // Debug info
      Serial.print("    Planta ID ");
      Serial.print(id);
      Serial.print(" | Mínima: ");
      Serial.print(humMinimaPct);
      Serial.print("% → ADC ");
      Serial.print(umbralMinimo);
      Serial.print(" | Ideal: ");
      Serial.print(humIdealPct);
      Serial.print("% → ADC ");
      Serial.println(umbralIdeal);

      delay(50);  // Pequeña pausa entre mensajes
    }

    Serial.println("Configuración enviada al Arduino");

  } else {
    Serial.print("Error HTTP al obtener plantas: ");
    Serial.println(httpCode);
  }

  http.end();
}

// Funcion para enviar datos del firmware al servidor
void enviarMetricasSistema(String mensaje) {
  HTTPClient http;
  http.begin(urlSistema);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // Parsear mensaje del Arduino
  // Formato: SISTEMA,temp,hum,caudal,gasto,nivelAgua
  char buffer[150];
  mensaje.toCharArray(buffer, 150);

  strtok(buffer, ",");                    // "SISTEMA"
  float temp      = atof(strtok(NULL, ",")); // Temperatura
  float hum       = atof(strtok(NULL, ",")); // Humedad ambiente
  float caudal    = atof(strtok(NULL, ",")); // Caudal L/min
  float aguaS     = atof(strtok(NULL, ",")); // Gasto total L
  int nivelAgua   = atoi(strtok(NULL, ",")); // Nivel: 1=OK, 0=bajo

  // Debug
  Serial.println("\n── SISTEMA recibido ──");
  Serial.printf("  Temp: %.2f°C | Hum: %.2f%% | Caudal: %.2f L/min\n", temp, hum, caudal);
  Serial.printf("  Gasto total: %.2f L | Nivel agua: %s\n", aguaS, nivelAgua ? "OK" : "BAJO");

  // Preparar POST
  String datosPost = "idS=" + sistemaID +
                     "&clave=" + claveAct +
                     "&tempS=" + String(temp, 2) +
                     "&humS=" + String(hum, 2) +
                     "&aguaS=" + String(aguaS, 2) +
                     "&correo=" + correoSistema;

  // Enviar
  int httpCode = http.POST(datosPost);

  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("Respuesta de servidor: " + response);

    // Clave inválida → resetear sistema
    if (response.indexOf("CLAVE_INVALIDA") != -1) {
      resetSistema();
    }

  } else {
    Serial.print("Error enviando sistema: ");
    Serial.println(httpCode);
  }

  http.end();
}

// Funcion para enviar datos de plantas al servidor
void enviarMetricasPlanta(String mensaje) {
  HTTPClient http;
  http.begin(urlPlanta);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // Parsear mensaje del Arduino
  // Formato: PLANTA,id,humedad%,temp,estado
  char buffer[150];
  mensaje.toCharArray(buffer, 150);

  strtok(buffer, ",");                    // "PLANTA"
  int id        = atoi(strtok(NULL, ",")); // ID planta
  int humPct    = atoi(strtok(NULL, ",")); // Humedad %
  float tempAmb = atof(strtok(NULL, ",")); // Temp ambiente
  int estado    = atoi(strtok(NULL, ",")); // 0=inactivo, 1=regando, 2=filtrando

  // Debug
  Serial.println("\n── PLANTA recibida ──");
  Serial.printf("  ID: %d | Humedad: %d%% | Temp: %.2f°C | Estado: %d\n",
                id, humPct, tempAmb, estado);

  // Preparar POST
  String datosPost = "idPu=" + String(id) +
                     "&humP=" + String(humPct) +
                     "&correo=" + correoSistema +
                     "&clave=" + claveAct;

  // Enviar
  int httpCode = http.POST(datosPost);

  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("Servidor planta: " + response);
  } else {
    Serial.print("Error enviando planta: ");
    Serial.println(httpCode);
  }

  http.end();
}

// Funcion para resetear el sistema
void resetSistema() {
  Serial.println("\nCLAVE INVÁLIDA → Volviendo a modo vinculación");

  claveAct = "";
  vinculado = false;

  if (usarPreferences) {
    preferences.remove("clave");
    preferences.remove("correo");
  }

  correoSistema = "";
}
