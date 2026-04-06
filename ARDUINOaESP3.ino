#include <DHT.h>
#include <SoftwareSerial.h>

//Definicion de la comunicacion serial mediante pines
SoftwareSerial serialESP(10, 11); 
//DEFINICIONES GLOBALES 
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);
//Pin para el sensore de caudal
#define pinFlujo 2
//Variable para contar los pulsos de riego
volatile int pulsosFlujo = 0;
//Variable para el conteo de gasto total de agua
float aguaS = 0; 

//Definicón de la estructura de la planta
struct Planta {
  String id; 
  int pinHumedad; 
  int pinRelay; 
  int umbralSeco;
  unsigned long tiempoTotalRiego; 
  bool estaRegando; 
  unsigned long inicioRiego; 
};

// Creación de la planta de prueba
Planta p1 = {"P1", A0, 7, 400, 0, false, 0}; 

//Contador de pulsos de riego para ir sumando el riego que va haceindolo de 1 segundo
void conteoPulsos() { pulsosFlujo++; }


void setup() {
  Serial.begin(9600);    
  serialESP.begin(9600); 
  dht.begin();  
  
  pinMode(p1.pinRelay, OUTPUT);
  //Apagado del relay en HIGH 
  digitalWrite(p1.pinRelay, HIGH);
  
  pinMode(pinFlujo, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pinFlujo), conteoPulsos, RISING);
  
  Serial.println("Arduino Listo...");
}

void loop() {
  // Llamado a la funcion de ID planta desde el ESP
  revisarID();

  //Funciones de calculo de información del sistema
  float caudalLPM = calcularLPMyTl();
  float tAmb = dht.readTemperature();
  float hAmb = dht.readHumidity();

  //Función para gestionar 
  gestionRiego(p1);

  // Enviamos datos al ESP32
  enviarDatosS(tAmb, hAmb, caudalLPM, aguaS);
  enviarDatosP(p1);
  
  delay(60000); 
}

//Funcion para calcular el consumo de litros por minuto y el total de litros gastados
float calcularLPMyTl() {
  float caudalLPM = (pulsosFlujo / 7.5);
  aguaS += (float)pulsosFlujo / 450.0;
  pulsosFlujo = 0;
  return caudalLPM;
}

//Función de gestion de riego
void gestionRiego(Planta &p) {
  int lecturaHumedad = analogRead(p.pinHumedad);
  unsigned long ahora = millis();

  if (!p.estaRegando && lecturaHumedad < p.umbralSeco) {
    digitalWrite(p.pinRelay, LOW); 
    p.inicioRiego = ahora;
    p.estaRegando = true;
  }

  if (p.estaRegando && (ahora - p.inicioRiego >= 2000)) {
    digitalWrite(p.pinRelay, HIGH);
    p.tiempoTotalRiego += 2000;      
    p.estaRegando = false;
  }
}

//Funcion de asignacion de id a planta
void revisarID() {
  if (serialESP.available()) {
    String mensaje = serialESP.readStringUntil('\n');
    if (mensaje.startsWith("SET_ID")) {
      int posComa = mensaje.indexOf(',');
      if (posComa != -1) {
        p1.id = mensaje.substring(posComa + 1);
        p1.id.trim();
        Serial.println("Nuevo ID recibido: " + p1.id);
      }
    }
  }
}

//Funcion para enviar datos de sistema
void enviarDatosS(float tAmb, float hAmb, float caudalLPM, float aguaS) {
  serialESP.print("SISTEMA,");
  serialESP.print(tAmb); serialESP.print(",");
  serialESP.print(hAmb); serialESP.print(",");
  serialESP.print(caudalLPM); serialESP.print(",");
  serialESP.println(aguaS);
}

//Funcion para enviar datos de planta
void enviarDatosP(Planta &p) {
  serialESP.print("PLANTA,");
  serialESP.print(p.id); serialESP.print(",");
  serialESP.print(analogRead(p.pinHumedad)); serialESP.print(",");
  serialESP.println(p.tiempoTotalRiego / 1000); 
}

