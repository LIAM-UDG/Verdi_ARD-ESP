#include "DHT.h"
#include <SoftwareSerial.h>

//Definicion del modo prueba, comentar para producción
#define MODO_PRUEBA

//Pine de sistema 
#define DHTPIN         2   // DHT11 
#define PIN_CAUDAL     3   // YF-S201
#define PIN_NIVEL      7   // Sensor flotador 
//Arduino Pin 10 (RX)
//Arduino Pin 11 (TX

//  Numero de modulos automatizados
#define MAX_MODULOS 3

const int PINES_HUMEDAD[MAX_MODULOS] = { A0, A1, A2 };  // Sensores tierra
const int PINES_RELE[MAX_MODULOS]    = {  4,  5,  6 };  // Relays

//Valor para deteccion de sensor activo
#define UMBRAL_DETECCION  1000   
#define MUESTRAS_DETECCION 5

//  Tiempos | MODO PRUEBA y PRODUCCIÓN
#ifdef MODO_PRUEBA
  #define T_ENVIO_SISTEMA_NORMAL   8000UL   //  8 s
  #define T_ENVIO_SISTEMA_RIEGO    4000UL   //  4 s
  #define T_ENVIO_PLANTAS_NORMAL   9000UL   //  9 s
  #define T_ENVIO_PLANTAS_RIEGO    5000UL   //  5 s
  #define T_DURACION_RIEGO         5000UL   //  5 s
  #define T_ESPERA_FILTRO         10000UL   // 10 s
  #define T_LECTURA_SUELO_BASE     8000UL   //  2 s 
  #define T_LECTURA_AMBIENTE       1000UL   //  1 s
#else
  #define T_ENVIO_SISTEMA_NORMAL  600000UL  // 10 min
  #define T_ENVIO_SISTEMA_RIEGO    20000UL  // 20 s
  #define T_ENVIO_PLANTAS_NORMAL 1200000UL  // 20 min
  #define T_ENVIO_PLANTAS_RIEGO    20000UL  // 20 s
  #define T_DURACION_RIEGO         30000UL  // 30 s
  #define T_ESPERA_FILTRO         900000UL  // 15 min
  #define T_LECTURA_SUELO_BASE  10800000UL  // 180 min (ajustable)
  #define T_LECTURA_AMBIENTE       1000UL   //  1 s
#endif

#define DHTTYPE DHT11

//  Estructura de planta
struct Planta {
  int  id;                   // ID desde ESP32
  int  pinHumedad;           // Pin humedad
  int  pinRele;              // Pin relay
  bool activo;               // Módulo detectado
  int  umbralMinimo;         // Umbral dinámico desde ESP32
  int  umbralIdeal;          // Umbral dinámico desde ESP32
  bool regando;
  bool esperandoFiltro;
  unsigned long ultimaAccion;
  float humedadActual;
};

//Creacion de plantas dependiendo de los modulos maximos
Planta misPlantas[MAX_MODULOS];
//Variable para detectar los modulos
int numModulosDetectados = 0;

//  VARIABLES GLOBALES
//Variables para el caudal
volatile double aguaS       = 0;
double          aguaAnterior = 0;
float           caudalLPM   = 0;

//Variables para el sensor de humedad y temperatura
float tempAmbiente = 0;
float humAmbiente  = 0;

//Variable para el flotador de agua
bool nivelAguaOK = true;  

//Variables para la medicion de intervalo de tiempo
unsigned long lastMillisCaudal   = 0;
unsigned long lastMillisAmbiente = 0;
unsigned long lastCheckSensores  = 0;
unsigned long lastEnvioSistema   = 0;
unsigned long lastEnvioPlantas   = 0;
unsigned long tiempoLecturaSuelo = T_LECTURA_SUELO_BASE;

//Creacion del sensor
DHT dht(DHTPIN, DHTTYPE);

//Creacion de la comunicacion serial
SoftwareSerial esp32Serial(10, 11);


// Funcion para la deteccion de modulos
bool sensorPresente(int pin) {
  for (int i = 0; i < MUESTRAS_DETECCION; i++) {
    if (analogRead(pin) >= UMBRAL_DETECCION) return false;
    delay(10);
  }
  return true;
}

// Funcion para la deteccion de modulos
int detectarModulos() {
  int count = 0;
  for (int i = 0; i < MAX_MODULOS; i++) {
    // Inicializar con valores por defecto
    misPlantas[i].id              = 0;
    misPlantas[i].pinHumedad      = PINES_HUMEDAD[i];
    misPlantas[i].pinRele         = PINES_RELE[i];
    misPlantas[i].activo          = false;
    misPlantas[i].umbralMinimo    = 600;  // Valor por defecto (bajo = seco)
    misPlantas[i].umbralIdeal     = 300;  // Valor por defecto (alto = húmedo)
    misPlantas[i].regando         = false;
    misPlantas[i].esperandoFiltro = false;
    misPlantas[i].ultimaAccion    = 0;
    misPlantas[i].humedadActual   = 0;

    if (sensorPresente(PINES_HUMEDAD[i])) {
      misPlantas[i].activo = true;
      pinMode(misPlantas[i].pinRele, OUTPUT);
      digitalWrite(misPlantas[i].pinRele, HIGH); // OFF (low-trigger)
      count++;
      Serial.print(F("Módulo "));
      Serial.print(i + 1);
      Serial.println(F(" detectado."));
      Serial.print("Valor de lector: ");
      Serial.println(analogRead(PINES_HUMEDAD[i]));
    } else {
      Serial.print(F("Módulo "));
      Serial.print(i + 1);
      Serial.println(F(" NO detectado."));
    }
  }
  return count;
}

// Setup del ARDUINO
void setup() {
  //Definicion de las comunicaciones
  Serial.begin(115200);
  esp32Serial.begin(9600);

  //Definicion de todos los sensores
  dht.begin();
  pinMode(PIN_CAUDAL, INPUT_PULLUP);
  pinMode(PIN_NIVEL, INPUT_PULLUP); 
  
  //Inicialilzacion del contador del caudal
  attachInterrupt(digitalPinToInterrupt(PIN_CAUDAL), pulso, RISING);

  //Deteccion de modulos activos - Llamada a la funcion
  numModulosDetectados = detectarModulos();

  
  Serial.println(F("\n\n------ ARDUINO INICIADO ------"));
  Serial.print(F("Módulos activos: "));
  Serial.println(numModulosDetectados);

  // Envio de módulos detectados al ESP32
  esp32Serial.print(F("MODULOS,"));
  esp32Serial.println(numModulosDetectados);

#ifdef MODO_PRUEBA
  Serial.println(F("*** MODO PRUEBA ***"));
#else
  Serial.println(F("*** MODO PRODUCCIÓN ***"));
#endif
}

// Loop del ARDUINO
void loop() {

  // Validacion de la comunicaicon de arduino y ESP32
  if (esp32Serial.available()) {
    String msg = esp32Serial.readStringUntil('\n');
    msg.trim();
    
    if (msg.startsWith("CONFIG_IDS:")) {
      //Llamado a la funcion para configurar los IDs
      asignarIdsMinimos(msg.substring(11));
    }
    else if (msg.startsWith("CONFIG_UMBRALES:")) {
      //Llamado a la funcion para configurar umbrales de humedad
      configurarUmbrales(msg.substring(16));
    }
  }

  // Conteo del caudal (simepre activo)
  if (millis() - lastMillisCaudal >= 1000) {
    caudalLPM    = (aguaS - aguaAnterior) * 60.0;
    aguaAnterior = aguaS;
    lastMillisCaudal = millis();
  }

  // Lectura de humedad y temperatura general (siempre activa)
  if (millis() - lastMillisAmbiente >= T_LECTURA_AMBIENTE) {
    tempAmbiente = dht.readTemperature();
    humAmbiente  = dht.readHumidity();
    lastMillisAmbiente = millis();
  }

  // Deteccion del niveo de agua (siempre activa)
  // LOW = agua OK, HIGH = sin agua (pull-up con flotador abierto)
  nivelAguaOK = (digitalRead(PIN_NIVEL) == LOW);

  // Ajuste dinamico del suelo sino esta en modo prueba
#ifndef MODO_PRUEBA
  ajustarIntervaloSuelo(tempAmbiente);
#endif

  // LOGICA DE RIEGO
  bool hayRiegoActivo = false;
  bool hayRelayActivo = false;

  // Deteccion de un relay activo
  for (int i = 0; i < MAX_MODULOS; i++) {
    if (misPlantas[i].activo && misPlantas[i].regando) {
      hayRelayActivo = true;
      break;
    }
  }

  // Lectura de sensores de tierra
  if (millis() - lastCheckSensores >= tiempoLecturaSuelo) {
    lastCheckSensores = millis();
    
    for (int i = 0; i < MAX_MODULOS; i++) {
      if (!misPlantas[i].activo)          continue;
      if ( misPlantas[i].regando)         continue;
      if ( misPlantas[i].esperandoFiltro) continue;

      misPlantas[i].humedadActual = analogRead(misPlantas[i].pinHumedad);

      // Calcular umbral crítico (mitad del mínimo)
      int umbralCritico = misPlantas[i].umbralMinimo + 100;

      // Evaluar si necesita riego
      bool necesitaRiego = (misPlantas[i].humedadActual > misPlantas[i].umbralMinimo);
      bool esCritico     = (misPlantas[i].humedadActual > umbralCritico);

      if (necesitaRiego) {
        // Validacion para solo regar sino hay otro relay activo y hay agua o es nivel crítico
        if (!hayRelayActivo && (nivelAguaOK || esCritico)) {
          //Llamado a la funcion para activar riego
          activarRiego(i);
          //Riego activo global
          hayRelayActivo = true;  
        } else if (!nivelAguaOK && !esCritico) {
          Serial.print(F("Planta de modulo"));
          Serial.print(i + 1);
          Serial.println(F(" necesita riego pero nivel bajo."));
        }
      }
    }
  }

  // Control de riegos activos
  for (int i = 0; i < MAX_MODULOS; i++) {
    if (!misPlantas[i].activo) continue;

    if (misPlantas[i].regando) {
      hayRiegoActivo = true;
      if (millis() - misPlantas[i].ultimaAccion >= T_DURACION_RIEGO) {
        //Llamado a la funcion para finalizar pulso y asignar filtrado
        finalizarPulsoRiego(i);
      }
    }

    if (misPlantas[i].esperandoFiltro) {
      //Riego activo global
      hayRiegoActivo = true; 
      if (millis() - misPlantas[i].ultimaAccion >= T_ESPERA_FILTRO) {
        //Llamado a la funcion para verficar la humedad al filtrar
        verificarHumedadPostFiltro(i);
      }
    }
  }

  // Configuracion de envios para el ESP32
  unsigned long tSistema = hayRiegoActivo ? T_ENVIO_SISTEMA_RIEGO : T_ENVIO_SISTEMA_NORMAL;
  unsigned long tPlantas = hayRiegoActivo ? T_ENVIO_PLANTAS_RIEGO : T_ENVIO_PLANTAS_NORMAL;

  if (millis() - lastEnvioSistema >= tSistema) {
    //Envio de datos del sistema al ESP32
    enviarDatosSistema();
    lastEnvioSistema = millis();
    if (!hayRiegoActivo) lastCheckSensores = millis();
  }

  if (millis() - lastEnvioPlantas >= tPlantas) {
    //Envio de datos de la planta al ESP32
    enviarDatosPlantas();
    lastEnvioPlantas = millis();
  }
}

// Funcion para asignar los IDs recibidos del ESP32
void asignarIdsMinimos(String listaIds) {
  int idsRecibidos[20];
  int totalIds = 0;

  char buffer[100];
  listaIds.toCharArray(buffer, 100);
  char* ptr = strtok(buffer, ",");

  while (ptr != NULL && totalIds < 20) {
    int val = atoi(ptr);
    if (val > 0) idsRecibidos[totalIds++] = val;
    ptr = strtok(NULL, ",");
  }

  // Ordenar ascendente
  for (int i = 0; i < totalIds - 1; i++) {
    for (int j = 0; j < totalIds - i - 1; j++) {
      if (idsRecibidos[j] > idsRecibidos[j + 1]) {
        int tmp = idsRecibidos[j];
        idsRecibidos[j] = idsRecibidos[j + 1];
        idsRecibidos[j + 1] = tmp;
      }
    }
  }

  // Asignar solo a módulos activos
  int asignados = 0;
  for (int i = 0; i < MAX_MODULOS && asignados < totalIds; i++) {
    if (!misPlantas[i].activo) continue;
    misPlantas[i].id = idsRecibidos[asignados];
    Serial.print(F("Módulo "));
    Serial.print(i + 1);
    Serial.print(F(" asigando con ID: "));
    Serial.println(misPlantas[i].id);
    asignados++;
  }
}

//  CONFIGURACIÓN DE UMBRALES DINÁMICOS
//  Formato: CONFIG_UMBRALES:id,minimo,ideal
void configurarUmbrales(String datos) {
  char buffer[50];
  datos.toCharArray(buffer, 50);
  
  int id     = atoi(strtok(buffer, ","));
  int minimo = atoi(strtok(NULL, ","));
  int ideal  = atoi(strtok(NULL, ","));

  // Buscar planta por ID y actualizar
  for (int i = 0; i < MAX_MODULOS; i++) {
    if (misPlantas[i].activo && misPlantas[i].id == id) {
      misPlantas[i].umbralMinimo = minimo;
      misPlantas[i].umbralIdeal  = ideal;
      
      Serial.print(F("Planta ID "));
      Serial.print(id);
      Serial.print(F(" umbrales: min="));
      Serial.print(minimo);
      Serial.print(F(" ideal="));
      Serial.println(ideal);
      break;
    }
  }
}

// Funcion para aumentar consumo de agua
void pulso() {
  aguaS += (1.0 / 450.0);
}

// Funcion para el ajuste de lectura de humedad en produccion
#ifndef MODO_PRUEBA
void ajustarIntervaloSuelo(float t) {
  if (isnan(t)) return;
  unsigned long nuevo;
  if      (t <  20) nuevo = 25200000UL;  // 420 min
  else if (t <= 30) nuevo = 10800000UL;  // 180 min
  else if (t <= 35) nuevo =  7200000UL;  // 120 min
  else              nuevo =  3600000UL;  //  60 min
  
  if (nuevo != tiempoLecturaSuelo) {
    tiempoLecturaSuelo = nuevo;
    Serial.print(F("Intervalo suelo: "));
    Serial.print(tiempoLecturaSuelo / 60000);
    Serial.println(F(" min"));
  }
}
#endif

// Funcion para enviar datos al ESP32
// Formato: SISTEMA,temp,hum,caudal,gasto,nivelAgua
void enviarDatosSistema() {
  esp32Serial.print(F("SISTEMA,"));
  esp32Serial.print(tempAmbiente);  esp32Serial.print(",");
  esp32Serial.print(humAmbiente);   esp32Serial.print(",");
  esp32Serial.print(caudalLPM);     esp32Serial.print(",");
  esp32Serial.print(aguaS);         esp32Serial.print(",");
  esp32Serial.println(nivelAguaOK ? 1 : 0);
  
  Serial.println(F("Envio de sensores generales de Arduino a ESP32"));
  Serial.print(F("ID Sistema: "));
    Serial.print(F("5"));
    Serial.print(F("  Temp ambiente: "));
    Serial.print(tempAmbiente);
    Serial.print(F("  Hum ambiente: "));
    Serial.print(humAmbiente);
    Serial.print(F("  Caudal: "));
    Serial.print(caudalLPM);
    Serial.print(F("  Gasto Agua: "));
    Serial.println(aguaS);
  Serial.println(F("----------------------------------------------"));
}

// Funcion para enviar datos de planta el ESP32
// Formato: PLANTA,id,humedad%,temp,estdo
// estado: 0=inactivo  1=regando  2=esperando_filtro
void enviarDatosPlantas() {
  for (int i = 0; i < MAX_MODULOS; i++) {
    if (!misPlantas[i].activo) continue;
    
    int estado = misPlantas[i].regando ? 1 
                  : misPlantas[i].esperandoFiltro ? 2 : 0;
    
    // Convertir a porcentaje (1023=0%, 0=100%)
    int pHum = map(misPlantas[i].humedadActual, 1023, 0, 0, 100);
    pHum = constrain(pHum, 0, 100);

    esp32Serial.print(F("PLANTA,"));
    esp32Serial.print(misPlantas[i].id);  esp32Serial.print(",");
    esp32Serial.print(pHum);              esp32Serial.print(",");
    esp32Serial.print(tempAmbiente);      esp32Serial.print(",");
    esp32Serial.println(estado);

    Serial.println(F("Envio de informacion de planta Arduino a ESP32"));
    Serial.print(F("ID Planta: "));
    Serial.print(misPlantas[i].id);
    Serial.print(F("  Humedad de sensor: "));
    Serial.print(pHum);
    Serial.print(F(" | "));
    Serial.print(misPlantas[i].humedadActual);
    Serial.print(F("  Estado: "));
    Serial.println(estado);
    Serial.println(F("----------------------------------------------"));
  }
}

// FUNCIONES DE RIEGO

//Funcion para activar el riego
void activarRiego(int idx) {
  misPlantas[idx].regando      = true;
  misPlantas[idx].ultimaAccion = millis();
  digitalWrite(misPlantas[idx].pinRele, LOW);
  
  Serial.print(F("Riego Activo en el Módulo "));
  Serial.println(idx + 1);
}

//Funcion para finalizar el riego y asignar filtrando
void finalizarPulsoRiego(int idx) {
  digitalWrite(misPlantas[idx].pinRele, HIGH);
  misPlantas[idx].regando         = false;
  misPlantas[idx].esperandoFiltro = true;
  misPlantas[idx].ultimaAccion    = millis();
  
  Serial.print(F("Pulso terminado en el Módulo "));
  Serial.print(idx + 1);
  Serial.println(F(", esperando filtrado."));
}

//Funcion para validar humedad tras filtrar
void verificarHumedadPostFiltro(int idx) {
  misPlantas[idx].humedadActual = analogRead(misPlantas[idx].pinHumedad);
  
  // Validacion de si ya llegó al ideal o lo superó terminar
  if (misPlantas[idx].humedadActual <= misPlantas[idx].umbralIdeal) {
    misPlantas[idx].esperandoFiltro = false;
    Serial.print(F("Humedad ideal en eñ Módulo "));
    Serial.println(idx + 1);
  }
  else {
    //Llamado a activar riego si no se llego a la humedad ideal
    activarRiego(idx);
  }
}


