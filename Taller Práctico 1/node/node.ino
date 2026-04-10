#include "painlessMesh.h"  // Biblioteca para crear redes mesh WiFi entre nodos ESP

// ─── Configuración de la red mesh ────────────────────────────────────────────
#define MESH_PREFIX     "RIds"        // Nombre (SSID) de la red mesh compartida
#define MESH_PASSWORD   "RIds2026"    // Contraseña de la red mesh
#define MESH_PORT       5555          // Puerto TCP usado internamente por painlessMesh

// ─── Identificación del nodo  ──────────────────────────────────────────────────
const int LOGICAL_ID = 4;            // ID representativo asignado al código
uint32_t ROOT_ID = 549173993;        // ID real (meshId) del nodo raíz, Este valor lo provee painlessMesh al nodo root

// ─── Objetos principales ──────────────────────────────────────────────────────
Scheduler userScheduler;             // Scheduler de TaskScheduler para tareas periódicas
painlessMesh mesh;                   // Objeto principal de la red mesh

// ─── Prototipo de función enviar temperatura─────────────────────────────────────────────────────
void sendTemperature();

// Tarea periódica: llama a sendTemperature() cada 4 segundos indefinidamente
Task taskSendTemperature(TASK_SECOND * 4, TASK_FOREVER, &sendTemperature);


// ─── Construcción del mensaje JSON ───────────────────────────────────────────
// Arma un string JSON con los datos del nodo y una temperatura simulada
String buildTempMessage() {
  String msg = "{";
  msg += "\"logicalId\":" + String(LOGICAL_ID) + ",";    // ID lógico del nodo
  msg += "\"meshId\":" + String(mesh.getNodeId()) + ","; // ID real asignado por la mesh
  msg += "\"type\":\"temperature\",";                     // Tipo de mensaje 
  msg += "\"temp\":" + String(random(18, 27));            // Temperatura aleatoria simulada (18–26 °C)
  msg += "}";
  return msg;
}

// ─── Envío de temperatura al root ────────────────────────────────────────────
void sendTemperature() {
  if (!mesh.isConnected(ROOT_ID)) {
    // Si el root no es alcanzable en este momento, solo imprime advertencia
    Serial.printf("No hay ruta al root %u\n", ROOT_ID);
  } else {
    String msg = buildTempMessage();

    // Envía el mensaje directamente al nodo root (unicast)
    mesh.sendSingle(ROOT_ID, msg);

    Serial.printf("Mensaje enviado al root (%u): %s\n", ROOT_ID, msg.c_str());

    // Aleatoriza el próximo intervalo de envío (3–8 segundos)
    // Esto evita colisiones si varios nodos transmiten al mismo tiempo
    taskSendTemperature.setInterval(random(TASK_SECOND * 3, TASK_SECOND * 8));
  }
}

// ─── Callbacks de la mesh ─────────────────────────────────────────────────────

// Se llama cuando este nodo recibe un mensaje de cualquier otro nodo
void receivedCallback(uint32_t from, String &msg) {
  Serial.printf("Recibido de %u: %s\n", from, msg.c_str());
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

  // Solo muestra errores y mensajes de inicio 
  mesh.setDebugMsgTypes(ERROR | STARTUP);

  // Inicializa la mesh con SSID, contraseña, scheduler y puerto
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);

  // Indica que en la red existe un nodo root, para que la mesh ajuste su comportamiento
  // (routing hacia el root, sincronización de tiempo)
  mesh.setContainsRoot(true);

  // Registro de callbacks
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

  // Agrega y habilita la tarea periódica de envío
  userScheduler.addTask(taskSendTemperature);
  taskSendTemperature.enable();

  // Información de arranque
  Serial.printf("Nodo iniciado\n");
  Serial.printf("logicalId = %d\n", LOGICAL_ID);
  Serial.printf("meshId    = %u\n", mesh.getNodeId());  // ID real asignado por la mesh
  Serial.printf("ROOT_ID   = %u\n", ROOT_ID);
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
  // painlessMesh requiere ser llamado continuamente para procesar
  // mensajes, mantener conexiones y ejecutar las tareas del scheduler
  mesh.update();
}