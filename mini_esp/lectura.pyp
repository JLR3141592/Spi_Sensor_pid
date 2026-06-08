# =============================================================================
# BLOQUE 1: IMPORTACIÓN DE LIBRERÍAS
# =============================================================================
# Importamos Paho para gestionar el protocolo MQTT.
import paho.mqtt.client as mqtt

# Importamos la librería nativa 'time' de Python. La necesitamos obligatoriamente
# para pausar el script unos milisegundos antes de que el programa se cierre.
import time


# =============================================================================
# BLOQUE 2: INICIALIZACIÓN DEL CLIENTE
# =============================================================================
# Creamos la instancia del cliente MQTT. Al igual que en el script anterior, 
# le indicamos que use la versión de API 2.0 (VERSION2) para estar al día con 
# las reglas de la librería.
cliente = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)


# =============================================================================
# BLOQUE 3: CONEXIÓN E INICIO DEL MOTOR DE RED
# =============================================================================
# Nos conectamos físicamente al Broker que vive dentro de tu ESP32 (192.168.4.1)
# usando el puerto MQTT común (1883).
cliente.connect("192.168.4.1", 1883, keepalive=60)

# AQUÍ ESTÁ EL TRUCO: Usamos 'loop_start()' en lugar de 'loop_forever()'.
# Esta función arranca el motor de red MQTT en un "hilo secundario" (en el fondo).
# Esto permite que el script envíe datos y continúe ejecutando las líneas de abajo,
# en lugar de quedarse congelado bloqueando la terminal.
cliente.loop_start()


# =============================================================================
# BLOQUE 4: DEFINICIÓN DE DATOS Y ENVÍO (PUBLISH)
# =============================================================================
# Declaramos en qué canal queremos escribir. Tu ESP32 está escuchando en "lectura".
topic_destino = "lectura"

# Definimos el texto plano que queremos que el ESP32 reciba y procese.
comando = "-50"

# client.publish() toma el texto plano, lo empaqueta con el protocolo MQTT 
# y lo dispara hacia el broker del ESP32 a través del hilo secundario.
cliente.publish(topic_destino, comando)

# Imprimimos un aviso en nuestra consola de Python para saber que la instrucción
# ya se mandó desde el código.
print(f"Comando '{comando}' enviado exitosamente al topic '{topic_destino}'")


# =============================================================================
# BLOQUE 5: TIEMPO DE ESPERA Y DESCONEXIÓN LIMPIA
# =============================================================================
# Pausamos el script por 1 segundo completo. 
# ¿Por qué? Porque la red tarda milisegundos en empujar el paquete hacia el ESP32.
# Si no ponemos esta pausa, el script pasaría directo a las líneas de abajo y 
# cerraría la conexión antes de que el mensaje logre salir de la tarjeta de red de la PC.
time.sleep(1)

# Apagamos el motor de red en segundo plano que iniciamos en el Bloque 3.
cliente.loop_stop()

# Cortamos formalmente la conexión Wi-Fi/MQTT con el ESP32 de manera educada.
cliente.disconnect()
