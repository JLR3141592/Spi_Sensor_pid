#include <Arduino.h>     // Librería base obligatoria para el ecosistema PlatformIO
#include <WiFi.h>        // Librería nativa para la gestión de redes inalámbricas en el ESP32
#include <MPU6500_WE.h>  // Librería específica para controlar y adquirir datos del sensor MPU6500
#include <SPI.h>         // Librería para manejar la interfaz de comunicación SPI con el sensor
#include <Wire.h>        // Requerida como dependencia interna por varias librerías de sensores
#include <PicoMQTT.h>    // Librería ligera para levantar un Broker/Servidor MQTT dentro del ESP32
#include <WebServer.h>   // Servidor Web nativo del ESP32 para servir la interfaz gráfica
#include <ArduinoJson.h> // Para empaquetar de forma eficiente la ruta JSON (/datos)

//////////////////// MQTT & WEB SERVER ////////////////////

PicoMQTT::Server mqtt;   // Instancia del servidor MQTT local que gestionará las conexiones
WebServer server(80);    // Instancia del servidor web en el puerto estándar 80
String IP;               // Variable global para almacenar la IP en formato de texto legible

//////////////////// SPI ////////////////////

const int csPin   = 10;   // Chip Select: Pin de control para activar el sensor en el bus
const int mosiPin = 11;  // Master Out Slave In: Línea de salida de datos del ESP32 al sensor
const int misoPin = 13;  // Master In Slave Out: Línea de entrada de datos del sensor al ESP32
const int sckPin  = 12;  // Serial Clock: Línea generadora del pulso de reloj de sincronía

float num = 0;           // Variable global compartida

//////////////////// MPU6500 ////////////////////

bool useSPI = true; // Flag configurativo para forzar el uso del protocolo SPI en el objeto
MPU6500_WE myMPU6500(&SPI, csPin, mosiPin, misoPin, sckPin, true); // Constructor con pines manuales

//////////////////// ÁNGULOS Y TIEMPO ////////////////////

float ang_x = 0;       // Almacena el ángulo definitivo estabilizado en el eje X
float ang_y = 0;       // Almacena el ángulo definitivo estabilizado en el eje Y

float ang_x_prev = 0;  // Registro histórico del ángulo X del ciclo anterior
float ang_y_prev = 0;  // Registro histórico del ángulo Y del ciclo anterior

float dt = 0;                  // Delta de tiempo real en segundos invertido en cada lazo PID
unsigned long tiempo_prev = 0; // Almacén del tiempo en milisegundos de la última iteración ejecutada

// =============================================================================
// RUTAS DEL SERVIDOR WEB
// =============================================================================

// 1. Ruta principal: HTML + CSS + JavaScript con Botón y envío síncronizado seguro
void handleRoot() {
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
      <title>Monitor ESP32 en Tiempo Real</title>
      <meta charset="utf-8">
      <style>
          body { font-family: Arial, sans-serif; text-align: center; margin-top: 40px; background-color: #f4f4f9; }
          .container { display: inline-block; background: white; padding: 30px; border-radius: 10px; box-shadow: 0px 4px 10px rgba(0,0,0,0.1); text-align: center; }
          .dato { font-size: 24px; font-weight: bold; color: #333; margin: 15px 0; }
          .eje-x { color: #e74c3c; }
          .eje-y { color: #3498db; }
          
          /* Estilos para el nuevo panel con botón */
          .control-panel { margin-top: 25px; padding-top: 20px; border-top: 2px dashed #ddd; }
          .control-panel input { padding: 10px; width: 110px; font-size: 18px; border: 2px solid #ccc; border-radius: 5px; text-align: center; }
          .control-panel button { padding: 10px 20px; font-size: 18px; background-color: #3498db; color: white; border: none; border-radius: 5px; cursor: pointer; margin-left: 8px; font-weight: bold; }
          .control-panel button:hover { background-color: #2980b9; }
      </style>
  </head>
  <body>
      <div class="container">
          <h2>Telemetría del ESP32</h2>
          <hr>
          <div class="dato">Valor Eje X: <span class="eje-x" id="valor-x">Cargando...</span></div>
          <div class="dato">Valor Eje Y: <span class="eje-y" id="valor-y">Cargando...</span></div>
          <div class="dato" style="font-size: 18px; color: #666;">Confirmación Broker ("num"): <span id="valor-num">0.00</span></div>
          
          <div class="control-panel">
              <label style="display:block; margin-bottom:8px; color:#555; font-size:15px;"><b>Enviar comando al topic 'lectura':</b></label>
              <input type="number" id="input-num" value="0" step="any">
              <button onclick="solicitarEnvio()">Publicar</button>
          </div>
          
          <p><small style="color: #777;">Actualización de pantalla: cada 100ms.</small></p>
      </div>

      <script>
          // Variable temporal que almacena el dato listo para irse
          let comandoPendiente = null;

          // Al presionar el botón, solo guardamos el valor completo aquí
          function solicitarEnvio() {
              let cajaTexto = document.getElementById('input-num');
              let valor = cajaTexto.value;
              
              if (valor !== "" && !isNaN(valor)) {
                  comandoPendiente = valor; 
                  console.log("Comando en cola para el próximo ciclo HTTP: " + valor);
              } else {
                  alert("Por favor ingresa un número válido.");
              }
          }

          function cicloBidireccional() {
              let url = '/datos';
              
              // Si el botón fue presionado, acoplamos el parámetro al viaje actual
              if (comandoPendiente !== null) {
                  url += '?val=' + comandoPendiente;
                  comandoPendiente = null; // Se limpia inmediatamente para que no se repita en el próximo bucle
              }

              fetch(url)
                  .then(response => response.json())
                  .then(data => {
                      document.getElementById('valor-x').innerText = data.x;
                      document.getElementById('valor-y').innerText = data.y;
                      document.getElementById('valor-num').innerText = data.num;
                  })
                  .catch(error => console.error('Error en enlace de datos:', error));
          }

          // Consulta constante cada 100 milisegundos
          setInterval(cicloBidireccional, 100);
      </script>
  </body>
  </html>
  )rawliteral";

  server.send(200, "text/html", html);
}

// 2. Ruta API Única: Procesa el envío eventual del botón y retorna estados
void handleDatos() {
  // Si la web anexó un valor (porque se clickeó el botón), se publica una única vez
  if (server.hasArg("val")) {
    String textoWeb = server.arg("val");
    num = textoWeb.toFloat(); // Actualiza de inmediato la variable en memoria
    
    mqtt.publish("lectura", textoWeb.c_str()); // Dispara el mensaje al ecosistema MQTT
    Serial.print("-> [BOTÓN CHICK] MQTT Publicado en 'lectura': ");
    Serial.println(num);
  }

  // Despacho de JSON estándar hacia el navegador
  JsonDocument doc; 
  doc["x"] = String(ang_x, 2);   
  doc["y"] = String(ang_y, 2);
  doc["num"] = String(num, 2);   

  String respuestaJSON;
  serializeJson(doc, respuestaJSON);
  server.send(200, "application/json", respuestaJSON);
}

///////////////////////////////////////////////////////

void setup() {
  Serial.begin(115200); 

  ///////////////// SPI /////////////////
  SPI.begin(sckPin, misoPin, mosiPin, csPin); 

  ///////////////// WIFI AP /////////////////
  WiFi.mode(WIFI_AP); 
  WiFi.softAP("Mini_esp_32", "987654321", 1, false, 4);

  IPAddress ip = WiFi.softAPIP(); 
  IP = ip.toString();            
  Serial.print("IP AP: ");
  Serial.println(IP);

  ///////////////// MPU6500 /////////////////
  if (!myMPU6500.init()) { 
    Serial.println("MPU6500 no responde");
    while (1); 
  }
  Serial.println("MPU6500 conectado");
  delay(1000); 
  Serial.println("Calibrando... no mover");
  myMPU6500.autoOffsets(); 
  Serial.println("Calibracion terminada");

  ///////////////// CONFIGURACIÓN FILTROS MPU /////////////////
  myMPU6500.enableGyrDLPF();           
  myMPU6500.setGyrDLPF(MPU6500_DLPF_6); 
  myMPU6500.enableAccDLPF(true);       
  myMPU6500.setAccDLPF(MPU6500_DLPF_6); 
  myMPU6500.setSampleRateDivider(5);   
  myMPU6500.setGyrRange(MPU6500_GYRO_RANGE_250); 
  myMPU6500.setAccRange(MPU6500_ACC_RANGE_2G);   

  ///////////////// MQTT /////////////////
  mqtt.subscribe("lectura", [](const char* topic, const char* payload) {
    num = atof(payload); 
  });
  mqtt.begin(); 

  ///////////////// CONFIGURACIÓN SERVIDOR WEB /////////////////
  server.on("/", handleRoot);       
  server.on("/datos", handleDatos); 
  server.begin();                   
  Serial.println("Servidor HTTP Activo en el puerto 80");

  ///////////////// TIEMPO /////////////////
  tiempo_prev = millis(); 
}

void loop() {
  mqtt.loop();           
  server.handleClient(); 

  unsigned long tiempo_actual = millis(); 

  // LAZO TEMPORAL ESTRICTO (10ms)
  if (tiempo_actual - tiempo_prev >= 10) {
    dt = (tiempo_actual - tiempo_prev) / 1000.0;
    tiempo_prev = tiempo_actual; 

    ///////////////// LECTURA MPU /////////////////
    xyzFloat acc = myMPU6500.getGValues();   
    xyzFloat gyr = myMPU6500.getGyrValues(); 

    ///////////////// ACELERÓMETRO /////////////////
    float accel_ang_y = atan2(acc.y, sqrt(acc.x * acc.x + acc.z * acc.z)) * 180.0 / PI;
    float accel_ang_x = atan2(-acc.x, sqrt(acc.y * acc.y + acc.z * acc.z)) * 180.0 / PI;

    ///////////////// FILTRO COMPLEMENTARIO /////////////////
    ang_x = 0.98 * (ang_x_prev + gyr.y * dt) + 0.02 * accel_ang_x;
    ang_y = 0.98 * (ang_y_prev + gyr.x * dt) + 0.02 * accel_ang_y;

    ang_x_prev = ang_x; 
    ang_y_prev = ang_y; 

    Serial.print(">angulo_en_Y:");
    Serial.println(ang_y);
    mqtt.publish("datoy", String(ang_y).c_str());
    Serial.print(">angulo_en_X:");
    Serial.println(ang_x);
    mqtt.publish("datox", String(ang_x).c_str());
    Serial.print(">numero:");
    Serial.println(num);
  }
}