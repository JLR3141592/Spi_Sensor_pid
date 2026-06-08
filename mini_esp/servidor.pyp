# =============================================================================
# BLOQUE 1: IMPORTACIÓN DE LIBRERÍAS Y CONFIGURACIÓN
# =============================================================================
# Importamos 'jsonify' para poder enviar los datos en un formato que JavaScript entienda
from flask import Flask, jsonify
import paho.mqtt.client as mqtt

app = Flask(__name__)

# Diccionario global para almacenar los últimos datos recibidos del ESP32
datos_sensores = {
    "x": "Esperando datos...",
    "y": "Esperando datos..."
}

# =============================================================================
# BLOQUE 2: CALLBACKS DE MQTT (Se mantienen igual)
# =============================================================================
def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print("¡Conectado al Broker del ESP32!")
        client.subscribe("datox")
        client.subscribe("datoy")
        print("Escuchando canales 'datox' y 'datoy'...")
    else:
        print(f"Error al conectar. Código: {reason_code}")

def on_message(client, userdata, msg):
    global datos_sensores
    texto_recibido = msg.payload.decode()
    
    if msg.topic == "datox":
        print(f"[EJE X] -> {texto_recibido}")
        datos_sensores["x"] = texto_recibido
    elif msg.topic == "datoy":
        print(f"[EJE Y] -> {texto_recibido}")
        datos_sensores["y"] = texto_recibido
    else:
        print(f"[{msg.topic}] -> {texto_recibido}")

# =============================================================================
# BLOQUE 3: RUTAS DE FLASK
# =============================================================================

# 1. Ruta principal: Entrega la estructura de la página web al navegador
@app.route('/')
def principal():
    html = f"""
    <!DOCTYPE html>
    <html>
    <head>
        <title>Monitor ESP32 en Tiempo Real</title>
        <meta charset="utf-8">
        <style>
            body {{ font-family: Arial, sans-serif; text-align: center; margin-top: 50px; background-color: #f4f4f9; }}
            .container {{ display: inline-block; background: white; padding: 30px; border-radius: 10px; box-shadow: 0px 4px 10px rgba(0,0,0,0.1); }}
            .dato {{ font-size: 24px; font-weight: bold; color: #333; margin: 15px 0; }}
            .eje-x {{ color: #e74c3c; }}
            .eje-y {{ color: #3498db; }}
        </style>
    </head>
    <body>
        <div class="container">
            <h2>Telemetría del ESP32</h2>
            <hr>
            <div class="dato">Valor Eje X: <span class="eje-x" id="valor-x">{datos_sensores['x']}</span></div>
            <div class="dato">Valor Eje Y: <span class="eje-y" id="valor-y">{datos_sensores['y']}</span></div>
            <p><small style="color: #777;">Los datos se actualizan automáticamente cada 100ms.</small></p>
        </div>

        <script>
            function traerNuevosDatos() {{
                // Hacemos una petición en segundo plano a la ruta '/datos'
                fetch('/datos')
                    .then(response => response.json()) // Convertimos la respuesta a formato JSON
                    .then(data => {{
                        // Reemplazamos el texto de los contenedores HTML con los valores nuevos
                        document.getElementById('valor-x').innerText = data.x;
                        document.getElementById('valor-y').innerText = data.y;
                    }})
                    .catch(error => console.error('Error al obtener datos:', error));
            }}

            // Configuramos un temporizador para que ejecute la función cada 100 milisegundos
            setInterval(traerNuevosDatos, 100);
        </script>
    </body>
    </html>
    """
    return html

# 2. NUEVA RUTA: Actúa como una API que solo entrega los valores puros en formato JSON
@app.route('/datos')
def obtener_datos():
    return jsonify(datos_sensores)

# =============================================================================
# BLOQUE 4: EJECUCIÓN DEL SISTEMA
# =============================================================================
if __name__ == '__main__':
    cliente = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    cliente.on_connect = on_connect
    cliente.on_message = on_message

    print("Conectando al broker MQTT...")
    cliente.connect("192.168.4.1", 1883, keepalive=60)
    cliente.loop_start()

    # Ejecutamos Flask
    app.run(debug=True, host='0.0.0.0', port=5000)