#define RXD2 16
#define TXD2 17

void setup() {
  Serial.begin(115200); 
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2); 
  
  Serial.println("ESP32 INICIADO");
  
  // Enviamos el ID inicial al Arduino
  delay(2000); 
  enviarNuevoID("Planta_Principal");
}

void loop() {
  if (Serial2.available()) {
    String mensaje = Serial2.readStringUntil('\n');
    char buffer[150];
    mensaje.toCharArray(buffer, 150);

    if (mensaje.startsWith("SISTEMA")) {
      strtok(buffer, ","); // Saltar etiqueta SISTEMA
      float t = atof(strtok(NULL, ","));
      float h = atof(strtok(NULL, ","));
      float c = atof(strtok(NULL, ","));
      float a = atof(strtok(NULL, ","));

      Serial.printf("[SISTEMA] Temp: %.1fC | Hum: %.1f%% | Caudal: %.1fL/m | Total: %.2fL\n------------------------------------------------------", t, h, c, a);
    } 
    else if (mensaje.startsWith("PLANTA")) {
      strtok(buffer, ","); // Saltar etiqueta PLANTA
      char* id = strtok(NULL, ",");
      int hum = atoi(strtok(NULL, ","));
      long seg = atol(strtok(NULL, ","));

      Serial.printf("\n[PLANTA ] ID: %s | Hum.Tierra: %d | Riego: %ld s \n\n\n", id, hum, seg);
    }
  }
}

void enviarNuevoID(String nuevoID) {
  Serial2.print("SET_ID,");
  Serial2.println(nuevoID);
  Serial.println("ID enviado al Arduino: " + nuevoID);
}
