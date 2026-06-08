#include <Arduino.h>     // Librería base obligatoria para el ecosistema PlatformIO
#include <WiFi.h>        // Librería nativa para la gestión de redes inalámbricas en el ESP32
#include <MPU6500_WE.h>  // Librería específica para controlar y adquirir datos del sensor MPU6500
#include <SPI.h>         // Librería para manejar la interfaz de comunicación SPI con el sensor
#include <Wire.h>        // Requerida como dependencia interna por varias librerías de sensores
#include <PicoMQTT.h>    // Librería ligera para levantar un Broker/Servidor MQTT dentro del ESP32


//////////////////// MQTT LOCAL BROKER ////////////////////

PicoMQTT::Server mqtt;   // Instancia del servidor MQTT local que gestionará las conexiones
String IP;               // Variable global para almacenar la IP en formato de texto legible

//////////////////// SPI ////////////////////

const int csPin   = 10;   // Chip Select: Pin de control para activar el sensor en el bus
const int mosiPin = 11;  // Master Out Slave In: Línea de salida de datos del ESP32 al sensor
const int misoPin = 13;  // Master In Slave Out: Línea de entrada de datos del sensor al ESP32
const int sckPin  = 12;  // Serial Clock: Línea generadora del pulso de reloj de sincronía

//////////////////// PID ////////////////////

float RefX = 0;           // Variable de referencia en X
float RefY = 0;           // Variable de referencia en Y
float num = 0;            // Variable de referencia para el valor numérico recibido (Altura)

float kp = 0.1;  // Ganancia Proporcional: Fuerza de corrección basada en el error actual
float ki = 0.08; // Ganancia Integral: Elimina el error en estado estacionario
float kd = 12.0; // Ganancia Derivativa: Anticipa la tendencia amortiguando oscilaciones

float eX  = 0;    // Error instantáneo calculado (Referencia - Real)
float eY  = 0;    // Error instantáneo calculado (Referencia - Real)
float edX = 0;    // Velocidad de cambio del error (Término derivativo en X)
float edY = 0;    // Velocidad de cambio del error (Término derivativo en Y)
float eiX = 0;    // Acumulador integral del error en X
float eiY = 0;    // Acumulador integral del error en Y
float eaX = 0;    // Memoria del error de la iteración anterior en X
float eaY = 0;    // Memoria del error de la iteración anterior en Y
float uX  = 0;    // Magnitud de salida final calculada por el PID en X
float uY  = 0;    // Magnitud de salida final calculada por el PID en Y

//////////////////// MOTOR ////////////////////

const int UL = 3;      // Pin GPIO controlador del motor (arriba izquierda)
const int UR = 4;      // Pin GPIO controlador del motor (arriba derecha)
const int DL = 5;      // Pin GPIO controlador del motor (abajo izquierda)
const int DR = 6;      // Pin GPIO controlador del motor (abajo derecha)

// Configuración de la señal PWM
const int pwmFreq = 1000;     // Frecuencia de operación fijada a 1 kHz para el motor
const int pwmResolution = 8;  // Resolución de 8 bits (0 a 255)

//////////////////// MPU6500 ////////////////////

bool useSPI = true; 
MPU6500_WE myMPU6500(&SPI, csPin, mosiPin, misoPin, sckPin, true); 

//////////////////// ÁNGULOS Y TIEMPO ////////////////////

float ang_x = 0;       // Almacena el ángulo definitivo estabilizado en el eje X
float ang_y = 0;       // Almacena el ángulo definitivo estabilizado en el eje Y

float ang_x_prev = 0;  // Registro histórico del ángulo X del ciclo anterior
float ang_y_prev = 0;  // Registro histórico del ángulo Y del ciclo anterior

float dt = 0;                  // Delta de tiempo real en segundos invertido en cada lazo PID
unsigned long tiempo_prev = 0; // Almacén del tiempo en milisegundos de la última iteración ejecutada

///////////////////////////////////////////////////////

void setup() {
  Serial.begin(115200); 
  
  // Configura los pines físicos de los motores como salidas
  pinMode(UL, OUTPUT); 
  pinMode(UR, OUTPUT); // Agregado para asegurar la inicialización correcta del pin UR
  pinMode(DL, OUTPUT); 
  pinMode(DR, OUTPUT); 

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

  ///////////////// MQTT SUBSCRIPTIONS /////////////////
  mqtt.subscribe("altura", [](const char* topic, const char* payload) {
    num = atof(payload); 
  });
  mqtt.subscribe("anguloX", [](const char* topic, const char* payload) {
    RefX = atof(payload); 
  });
  mqtt.subscribe("anguloY", [](const char* topic, const char* payload) {
    RefY = atof(payload); 
  });
  mqtt.begin(); 
  Serial.println("Broker MQTT local activo y escuchando topics.");

  ///////////////// TIEMPO /////////////////
  tiempo_prev = millis(); 
}

void loop() {
  mqtt.loop(); // Atiende broker y despacha subscripciones de forma asíncrona          

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

    eX = RefX - ang_x;
    eY = RefY - ang_y;

    // Término Derivativo exacto
    edX = eX - eaX;
    edY = eY - eaY;

    uX = (kp * eX) + (eiX * ki) + (edX * kd); // Integración PID en X
    uY = (kp * eY) + (eiY * ki) + (edY * kd); // Integración PID en Y

    // Término Integral acumulativo
    eiX += eX * ki;
    eiY += eY * ki;

    ///////////////// LIMITADOR DE ACCIÓN PWM /////////////////
    if (uX > 255.0) uX = 255.0;
    if (uY > 255.0) uY = 255.0;

    ///////////////// ENVÍO DE POTENCIA /////////////////
    analogWrite(UL, floor(uX-uY));
    analogWrite(UR, floor(uX+uY));
    analogWrite(DL, floor(-uX-uY));
    analogWrite(DR, floor(-uX+uY));

    eaX = eX; // Actualiza la memoria para el próximo ciclo
    eaY = eY; // Actualiza la memoria para el próximo ciclo

    ///////////////// TELEMETRÍA DETALLADA /////////////////
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