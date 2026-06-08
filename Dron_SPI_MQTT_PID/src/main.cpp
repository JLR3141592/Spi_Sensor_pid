#include <Arduino.h>     // Librería base obligatoria para el ecosistema PlatformIO
#include <WiFi.h>        // Librería nativa para la gestión de redes inalámbricas en el ESP32
#include <MPU6500_WE.h>  // Librería específica para controlar y adquirir datos del sensor MPU6500
#include <SPI.h>         // Librería para manejar la interfaz de comunicación SPI con el sensor
#include <Wire.h>        // Requerida como dependencia interna por varias librerías de sensores
#include <PicoMQTT.h>    // Librería ligera para levantar un Broker/Servidor MQTT dentro del ESP32
#include <WebServer.h>   // Servidor Web nativo del ESP32 para servir la interfaz gráfica
#include <ArduinoJson.h> // Para empaquetar de forma eficiente la ruta JSON (/datos)

//////////////////// MQTT ////////////////////

PicoMQTT::Server mqtt;   // Instancia del servidor MQTT local que gestionará las conexiones
String IP;    
WebServer server(80);             // Variable global para almacenar la IP en formato de texto legible

//////////////////// SPI ////////////////////

const int csPin   = 5;   // Chip Select: Pin de control para activar el sensor en el bus
const int mosiPin = 23;  // Master Out Slave In: Línea de salida de datos del ESP32 al sensor
const int misoPin = 19;  // Master In Slave Out: Línea de entrada de datos del sensor al ESP32
const int sckPin  = 18;  // Serial Clock: Línea generadora del pulso de reloj de sincronía

//////////////////// MOTOR ////////////////////

const int mot = 22;      // Pin GPIO asignado para enviar la señal de potencia al controlador del motor

// Configuración de la señal PWM (Modulación por Ancho de Pulsos)
const int pwmFreq = 1000;     // Frecuencia de operación fijada a 1 kHz para el motor
const int pwmResolution = 8;  // Resolución de 8 bits, permitiendo escalados de velocidad de 0 a 255

//////////////////// PID ////////////////////

float ref = 0;   // Consigna u objetivo dinámico del sistema (Ángulo ideal que se desea alcanzar)

float kp = 0.1;  // Ganancia Proporcional: Fuerza de corrección basada en el error actual
float ki = 0.08; // Ganancia Integral: Elimina el error en estado estacionario
float kd = 12.0;  // Ganancia Derivativa: Anticipa la tendencia amortiguando oscilaciones

float e  = 0;    // Error instantáneo calculado (Referencia - Real)
float ei = 0;    // Acumulador integral del error
float ea = 0;    // Memoria del error de la iteración anterior
float ed = 0;    // Velocidad de cambio del error (Término derivativo)

float u  = 0;    // Magnitud de salida final calculada por el PID

//////////////////// MPU6500 ////////////////////

bool useSPI = true; // Flag configurativo para forzar el uso del protocolo SPI

MPU6500_WE myMPU6500(&SPI, csPin, mosiPin, misoPin, sckPin, true); // Constructor con pines manuales

//////////////////// ÁNGULOS Y TIEMPO ////////////////////

float ang_x = 0;       // Ángulo definitivo estabilizado en el eje X (Roll)
float ang_y = 0;       // Ángulo definitivo estabilizado en el eje Y (Pitch)
float ang_yy = 0;      // Variable de control que procesa el eje Y invertido mecánicamente

float ang_x_prev = 0;  // Registro histórico del ángulo X del ciclo anterior
float ang_y_prev = 0;  // Registro histórico del ángulo Y del ciclo anterior

float dt;                  // Delta de tiempo real en segundos invertido en cada lazo PID
unsigned long tiempo_prev = 0; // Almacén del tiempo en milisegundos de la última iteración

///////////////////////////////////////////////////////
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
    ref = textoWeb.toFloat(); // Actualiza de inmediato la variable en memoria
    
    mqtt.publish("lectura", textoWeb.c_str()); // Dispara el mensaje al ecosistema MQTT
    Serial.print("-> [BOTÓN CHICK] MQTT Publicado en 'lectura': ");
    Serial.println(ref);
  }

  // Despacho de JSON estándar hacia el navegador
  JsonDocument doc; 
  doc["y"] = String(ang_y, 2);
  doc["num"] = String(ref, 2); 

  String respuestaJSON;
  serializeJson(doc, respuestaJSON);
  server.send(200, "application/json", respuestaJSON);
}

///////////////////////////////////////////////////////


void setup() {

  Serial.begin(115200); // Inicializa el canal serial para depuración

  ///////////////// SPI /////////////////

  SPI.begin(sckPin, misoPin, mosiPin, csPin); // Arranca el bus SPI con el mapeo de pines

  ///////////////// MOTOR PWM /////////////////

  pinMode(mot, OUTPUT); // Configura el pin físico del motor como salida

  ///////////////// WIFI AP /////////////////

  WiFi.mode(WIFI_AP); // Establece el hardware en modo Punto de Acceso
  
  WiFi.softAP(
    "Jhojan_Broker", // Nombre público de la red (SSID)
    "123456789",     // Contraseña de seguridad
    1,               // Canal de transmisión
    false,           // Visibilidad de la red (visible)
    4                // Máximo de 4 clientes conectados
  );

  IPAddress ip = WiFi.softAPIP(); 
  IP = ip.toString();            
  
  Serial.print("IP AP: ");
  Serial.println(IP);

  ///////////////// MPU6500 /////////////////

  if (!myMPU6500.init()) { 
    Serial.println("MPU6500 no responde");
    while (1); // Bloqueo de seguridad si el sensor falla
  }

  Serial.println("MPU6500 conectado");
  delay(1000); // Espera de estabilidad mecánica antes de calibrar

  Serial.println("Calibrando... no mover");
  myMPU6500.autoOffsets(); // Descuenta ruidos estáticos de fábrica
  Serial.println("Calibracion terminada");

  ///////////////// CONFIGURACIÓN FILTROS MPU /////////////////

  myMPU6500.enableGyrDLPF();           
  myMPU6500.setGyrDLPF(MPU6500_DLPF_6); // Suavizado de señal ruidosa en giroscopio
  myMPU6500.enableAccDLPF(true);       
  myMPU6500.setAccDLPF(MPU6500_DLPF_6); // Máxima filtración en lecturas de aceleración
  myMPU6500.setSampleRateDivider(5);   
  myMPU6500.setGyrRange(MPU6500_GYRO_RANGE_250); 
  myMPU6500.setAccRange(MPU6500_ACC_RANGE_2G);   

  ///////////////// MQTT /////////////////

  mqtt.subscribe("lectura", [](const char* topic, const char* payload) {
    ref = atof(payload); // Parsea el string recibido a float inmediatamente
  });

  mqtt.begin(); // Enciende el servidor MQTT local

  ///////////////// SERVIDOR WEB HTTP /////////////////
  server.on("/", handleRoot);
  server.on("/datos", handleDatos);
  server.begin();
  Serial.println("Servidor Web HTTP iniciado");
}

///////////////////////////////////////////////////////

void loop() {

  server.handleClient(); // Procesa las peticiones HTTP entrantes del navegador
  mqtt.loop(); // Procesa paquetes de red MQTT de forma asíncrona

    ///////////////// LECTURA MPU /////////////////

    xyzFloat acc = myMPU6500.getGValues();   
    xyzFloat gyr = myMPU6500.getGyrValues(); 

    ////////////////////////////////////////////////

    dt = (millis() - tiempo_prev) / 1000.0;

    tiempo_prev = millis();

    ///////////////// ACELERÓMETRO /////////////////

    // Trigonometría espacial para deducir ángulos estáticos
    float accel_ang_x = atan(-acc.x / sqrt(acc.y * acc.y + acc.z * acc.z)) * 180.0 / PI;
    float accel_ang_y = atan(acc.y / sqrt(acc.x * acc.x + acc.z * acc.z)) * 180.0 / PI;

    ///////////////// FILTRO COMPLEMENTARIO CORREGIDO /////////////////

    // Fusión de variables: La velocidad angular del eje Y altera el ángulo X, y viceversa.
    ang_x = 0.98 * (ang_x_prev + gyr.y * dt) + 0.02 * accel_ang_x;
    ang_y = 0.98 * (ang_y_prev + gyr.x * dt) + 0.02 * accel_ang_y;

    ang_x_prev = ang_x; 
    ang_y_prev = ang_y; 

    ///////////////// PID INTEGRADO Y CORREGIDO /////////////////

    ang_yy = -1.0 * ang_y; // Inversión mecánica adaptada a la cinemática de tu planta
    e = ref - ang_yy;      

    // Término Derivativo exacto
    ed = e - ea;

    u=(kp * e)+(ei*ki)+(ed*kd);//se integran el control proporcional, integral y diferencial para la variable de control

    // Término Integral acumulativo
    ei += e * ki;

    ///////////////// LIMITADOR DE ACCIÓN PWM /////////////////

    if (u > 255.0) u = 255.0;
    if (u < 0.0)   u = 0.0; 

    ///////////////// ENVÍO DE POTENCIA /////////////////

    analogWrite(mot,floor(u)); 

    ea = e; // Actualiza la memoria para el próximo ciclo

    ///////////////// TELEMETRÍA DETALLADA /////////////////

    Serial.print(">angulo_medido:");
    Serial.println(ang_yy);
    Serial.print(">referencia:");
    Serial.println(ref);
    Serial.print(">accion_pwm:");
    Serial.println(u);
}