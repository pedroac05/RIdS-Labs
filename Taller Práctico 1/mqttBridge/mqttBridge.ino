#include <Arduino.h>
#include <painlessMesh.h>      // Biblioteca para crear redes mesh WiFi entre nodos ESP
#include <PubSubClient.h>      // Cliente MQTT para publicar mensajes al broker
#include <WiFiClient.h>        // Socket TCP necesario para la conexión MQTT
#include <Arduino_JSON.h>      // Parsing de strings JSON recibidos desde la mesh
#include <WiFi.h>              // Para verificar estado del uplink (WiFi.status())

// ─── Configuración de la red mesh ────────────────────────────────────────────
#define MESH_PREFIX     "RIds"        // Nombre (SSID) de la red mesh compartida
#define MESH_PASSWORD   "RIds2026"    // Contraseña de la red mesh
#define MESH_PORT       5555          // Puerto TCP usado internamente por painlessMesh

// ─── Credenciales del router externo (uplink a Internet/LAN) ─────────────────
#define STATION_SSID     "my_SSID"        // SSID del router al que se conecta el root
#define STATION_PASSWORD "My_Paswword"    // Contraseña del router

#define HOSTNAME "MQTT_Bridge"    // Nombre del nodo en la red local

// IP del broker MQTT (Mosquitto)
IPAddress mqttBroker(0,0,0,0);   

// ─── Objetos principales ──────────────────────────────────────────────────────
painlessMesh  mesh;                    // Objeto principal de la red mesh
WiFiClient    wifiClient;              // Socket TCP sobre el que corre MQTT
PubSubClient  mqttClient(wifiClient);  // Cliente MQTT usando el socket anterior

IPAddress myIP(0, 0, 0, 0);           // IP STA actual; se actualiza cuando el router asigna DHCP

// ─── Control de reconexión MQTT ──────────────────────────────────────────────
unsigned long lastMqttAttempt = 0;          // Timestamp del último intento de conexión
const unsigned long MQTT_RETRY_INTERVAL = 5000; // Tiempo mínimo entre reintentos (ms)

// ─── Prototipos ───────────────────────────────────────────────────────────────
IPAddress getLocalIP();
void reconnectMqtt();

// ─── Reconexión MQTT con retardo entre reintentos ──────────────────────────────────────────────
void reconnectMqtt() {
  // Salida de la función si ya está conectado 
  if (mqttClient.connected()) return;

  // No intenta si el uplink STA aún no tiene asociación con el router
  if (WiFi.status() != WL_CONNECTED) return;

  // No intenta si el router aún no asignó IP (DHCP pendiente)
  IPAddress ip = getLocalIP();
  if (ip == IPAddress(0, 0, 0, 0)) return;

  // Intervalo de retardo
  unsigned long now = millis();
  if (now - lastMqttAttempt < MQTT_RETRY_INTERVAL) return;
  lastMqttAttempt = now;

  // ClientId único por nodo (evita conflictos si hay otro cliente con el mismo ID)
  String clientId = "ESP32_ROOT" + String(mesh.getNodeId());
  Serial.printf("Intentando MQTT con clientId=%s\n", clientId.c_str());

  if (mqttClient.connect(clientId.c_str())) {
    Serial.println("Conectado al broker MQTT");
    // Publica mensaje de presencia para indicar que el gateway está operativo
    mqttClient.publish("painlessMesh/from/gateway", "Ready!");

  } else {
    // Imorime mensaje de fallo
    Serial.printf("Fallo MQTT, rc=%d. Reintentando en %lus...\n",
                  mqttClient.state(), MQTT_RETRY_INTERVAL / 1000);
  }
}

// ─── IP STA del root ──────────────────────────────────────────────────────────
// Retorna la IP asignada por el router al root (interfaz Station)
IPAddress getLocalIP() {
  return IPAddress(mesh.getStationIP());
}



// ─── Callbacks de la mesh ─────────────────────────────────────────────────────

// ─── Bridge mesh → MQTT ───────────────────────────────────────────────────────
// Se ejecuta cada vez que un nodo de la mesh envía un mensaje al root
void receivedCallback(uint32_t from, String &msg) {
  Serial.printf("Mesh -> Root | from=%u msg=%s\n", from, msg.c_str());

  // Parsea el JSON recibido (ej: {"logicalId":4,"meshId":...,"type":"temperature","temp":22})
  JSONVar data = JSON.parse(msg.c_str());
  if (JSON.typeof(data) == "undefined") {
    Serial.println("Error: JSON inválido");
    return;
  }

  // Extrae el ID lógico para construir el tópico MQTT dinámicamente
  int logicalId = (int)data["logicalId"];

  // Publica en el tópico "painlessMesh/from/<logicalId>"
  // Ejemplo: painlessMesh/from/4
  String topic = "painlessMesh/from/" + String(logicalId);

  if (mqttClient.connected()) {
    mqttClient.publish(topic.c_str(), msg.c_str());
  } else {
    Serial.println("MQTT no conectado, no se pudo publicar");
  }
}


// Se llama cuando un nuevo nodo se conecta a la mesh
void newConnectionCallback(uint32_t nodeId) {
  Serial.printf("--> Nueva conexión, nodeId = %u\n", nodeId);
}

// Se llama cuando cambia la topología de la red (nodo entra o sale)
void changedConnectionCallback() {
  Serial.println("Cambió la topología");
  Serial.println(mesh.subConnectionJson(true)); // Imprime el árbol de conexiones en JSON
}

// Se llama cuando el tiempo interno del nodo se sincroniza con la mesh
void nodeTimeAdjustedCallback(int32_t offset) {
  Serial.printf("Tiempo ajustado. nodeTime=%u offset=%d\n",
                mesh.getNodeTime(), offset);
}



// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Solo muestra errores, startup y eventos de conexión
  mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION);

  // Inicializa la mesh en modo AP+STA (canal 6):
  // AP  → permite que otros nodos se conecten a este root
  // STA → conecta al router externo para tener acceso al broker MQTT
  // El canal debe coincidir con el canal WiFi del router
  mesh.init(MESH_PREFIX, MESH_PASSWORD, MESH_PORT, WIFI_AP_STA, 6);

  // Indica al stack de painlessMesh las credenciales del router (uplink)
  mesh.stationManual(STATION_SSID, STATION_PASSWORD);
  mesh.setHostname(HOSTNAME);

  // Declara este nodo como root de la mesh
  mesh.setRoot(true);
  // Informa a los demás nodos que existe un root (para routing y sincronía de tiempo)
  mesh.setContainsRoot(true);

  // Registro de callbacks de la mesh
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

  // Configura la dirección del broker; la conexión real ocurre en loop()
  mqttClient.setServer(mqttBroker, 1883);
  //Información de arranque
  Serial.printf("Root iniciado. nodeId = %u\n", mesh.getNodeId());
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
  // painlessMesh requiere ser llamado continuamente para procesar
  // mensajes, mantener conexiones, enrutamiento
  mesh.update();

  if (mqttClient.connected()) {
    // Procesa keep-alive y mensajes entrantes del broker
    mqttClient.loop();
  } else {
    // Reconexión MQTT con retardo entre reintentos
    reconnectMqtt();
  }

  // Detecta cuando el router asigna IP (transición de 0.0.0.0 a IP real)
  IPAddress currentIP = getLocalIP();
  if (myIP != currentIP) {
    myIP = currentIP;
    Serial.println("Mi IP STA es: " + myIP.toString());
  }
}