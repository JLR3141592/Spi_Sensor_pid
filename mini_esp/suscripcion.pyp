# =============================================================================
# BLOQUE 1: IMPORTACIÓN DE LIBRERÍAS
# =============================================================================
# Importamos el módulo de cliente MQTT de Paho y le asignamos el alias 'mqtt'
# para que el código sea más corto y limpio de escribir.
import paho.mqtt.client as mqtt


# =============================================================================
# BLOQUE 2: FUNCIÓN DE EVENTO "ON_CONNECT" (CALLBACK)
# =============================================================================
# Esta función NO se ejecuta sola. Es un "disparador" (callback) que el cliente 
# MQTT llamará automáticamente justo en el milisegundo en que reciba una 
# respuesta de conexión (CONNACK) desde el broker del ESP32.
def on_connect(client, userdata, flags, reason_code, properties):
    
    # 'reason_code' es un objeto que contiene el resultado de la conexión.
    # Si su valor numérico es 0, significa que la conexión fue exitosa.
    if reason_code == 0:
        print("¡Conectado al Broker del ESP32!")
        
        # client.subscribe() le dice al ESP32: "Por favor, envíame una copia de 
        # todo lo que se publique en este canal".
        # Lo hacemos AQUÍ adentro porque si el Wi-Fi se cae y se reconecta solo,
        # volverá a ejecutar esto y renovará la suscripción automáticamente.
        client.subscribe("datox")  # Suscripción al canal de datos X
        client.subscribe("datoy")  # Suscripción al canal de datos Y
        
        print("Escuchando canales 'datox' y 'datoy'...")
    
    # Si 'reason_code' es cualquier otra cosa (no es 0), la conexión falló.
    else:
        # Imprimimos el código de error específico para saber por qué nos rebotó.
        print(f"Error al conectar. Código: {reason_code}")


# =============================================================================
# BLOQUE 3: FUNCIÓN DE EVENTO "ON_MESSAGE" (CALLBACK)
# =============================================================================
# Este disparador se ejecuta automáticamente CADA VEZ que el ESP32 envía un
# mensaje a cualquiera de los canales a los que nos suscribimos arriba.
def on_message(client, userdata, msg):
    
    # 'msg.payload' contiene los datos puros enviados por el ESP32 (en bytes).
    # .decode() convierte esos bytes binarios en texto plano (un string de Python)
    # usando la codificación estándar UTF-8.
    texto_recibido = msg.payload.decode()
    
    # 'msg.topic' contiene el nombre exacto del canal de donde provino el mensaje.
    # Usamos condicionales (if/elif) para separar la lógica según el canal.
    if msg.topic == "datox":
        # Si el mensaje vino de "datox", lo imprimimos con la etiqueta [EJE X]
        print(f"[EJE X] -> {texto_recibido}")
        
    elif msg.topic == "datoy":
        # Si vino de "datoy", lo imprimimos con la etiqueta [EJE Y]
        print(f"[EJE Y] -> {texto_recibido}")
        
    else:
        # Por si en el futuro te suscribes a otro canal y olvidas agregarlo aquí,
        # este 'else' atrapará el mensaje y te dirá de qué topic desconocido vino.
        print(f"[{msg.topic}] -> {texto_recibido}")


# =============================================================================
# BLOQUE 4: CONFIGURACIÓN E INICIALIZACIÓN DEL CLIENTE
# =============================================================================

# Creamos el objeto cliente. Al pasarle 'mqtt.CallbackAPIVersion.VERSION2', le
# estamos diciendo explícitamente a Paho que use las reglas modernas de la versión
# 2.x de su librería (es obligatorio para evitar mensajes de advertencia o errores).
cliente = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

# Aquí enlazamos nuestras funciones lógicas con los eventos internos de la librería.
# Le decimos: "Cuando te conectes, usa MI función on_connect".
cliente.on_connect = on_connect

# Le decimos: "Cuando llegue un mensaje, usa MI función on_message".
cliente.on_message = on_message


# =============================================================================
# BLOQUE 5: CONEXIÓN Y BUCLE ACTIVO
# =============================================================================

# Intentamos establecer la conexión física y de red con el broker.
# Parámetros: IP del ESP32, Puerto MQTT estándar (1883), y Keepalive de 60 segundos
# (el keepalive es un "latido de corazón" que avisa al ESP32 que la PC sigue viva).
cliente.connect("192.168.4.1", 1883, keepalive=60)

# Esta línea es el motor del script. Congela el programa en un bucle infinito.
# Se encarga de procesar los datos que viajan por la red, disparar las funciones
# 'on_connect' y 'on_message' cuando corresponde, y lo más importante: si el
# ESP32 se reinicia o el Wi-Fi parpadea, este bucle reconecta todo automáticamente.
cliente.loop_forever()