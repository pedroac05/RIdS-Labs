#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include <stdio.h>
#include "tree_lib.h"
#include "powertrace.h"

/* --- Memory & List --- */
MEMB(preferred_parent_mem, struct preferred_parent, 10); // Reserva memoria estática para hasta 10 estructuras del tipo preferred_parent.
LIST(preferred_parent_list); // Declara una lista enlazada donde se guardarán los vecinos/padres detectados.

/* --- Global Variables --- */
static linkaddr_t best_parent_id; // Guarda la dirección del mejor padre seleccionado.
static int16_t my_path_rssi = -1000;// Guarda la métrica acumulada de este nodo hacia el nodo root, se inicia en -1000 para representar "no tengo ruta válida todavía".

/* --- Rime Setup --- */
static struct broadcast_conn broadcast;// Conexión broadcast para enviar y recibir beacons.
static struct unicast_conn uc;// Conexión unicast para enviar datos al padre seleccionado.
static void register_parent(struct broadcast_conn *c, const linkaddr_t *from);// Prototipo del callback que se ejecuta cuando llega un broadcast.
static void recv_uc(struct unicast_conn *c, const linkaddr_t *from);// Prototipo del callback que se ejecuta cuando llega un unicast.
static void sent_uc(struct unicast_conn *c, int status, int num_tx);// Prototipo del callback que se ejecuta cuando termina un envío unicast.
static const struct broadcast_callbacks broadcast_call = {register_parent};// Asocia la recepción de broadcast con la función register_parent().
static const struct unicast_callbacks unicast_callbacks = {recv_uc, sent_uc};// Asocia la recepción y confirmación de unicast con recv_uc() y sent_uc().

/* --- Processes --- */
PROCESS(broadcast_rssi, "Beaconing");// Proceso encargado de enviar beacons periódicamente.
PROCESS(select_prefered_parent, "RSSI Sum Selection");// Proceso encargado de seleccionar el mejor padre según la suma de RSSI.
PROCESS(print_parent_list, "Debug Table");// Proceso encargado de imprimir una tabla de vecinos y decisiones.
PROCESS(example_unicast_process, "Data Flow");// Proceso encargado de enviar datos al padre seleccionado.
AUTOSTART_PROCESSES(&broadcast_rssi, &select_prefered_parent, &print_parent_list, &example_unicast_process);// Hace que esos 4 procesos arranquen automáticamente cuando inicia el nodo.

/* -------------------------------------------------------------------------- */
/* CALLBACK: Register Neighbor & Calculate Summed Path RSSI                   */
/* -------------------------------------------------------------------------- */
static void
register_parent(struct broadcast_conn *c, const linkaddr_t *from)
{
  int8_t last_rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI); // Obtiene el RSSI local del paquete recibido.

  struct beacon *b_recv = (struct beacon *)packetbuf_dataptr(); // Obtiene el contenido del paquete recibido y lo interpreta como un beacon.

  struct preferred_parent *p; // Puntero auxiliar para recorrer o crear nodos en la lista.
  for(p = list_head(preferred_parent_list); p != NULL; p = list_item_next(p)) { // Recorre toda la lista de vecinos registrados.
    if(linkaddr_cmp(&p->id, from)) break;
  }

  if(p == NULL) {
    // Si no encontró ese vecino, hay que crearlo en la lista.
    p = memb_alloc(&preferred_parent_mem);
    // Reserva memoria para un nuevo elemento preferred_parent.
    if(p == NULL) return;
    // Si no hay memoria disponible, sale sin hacer nada.
    linkaddr_copy(&p->id, from);
    // Copia la dirección del vecino en la estructura.
    list_push(preferred_parent_list, p);
    // Agrega el nuevo vecino a la lista.
  }

  p->rssi_p = (int16_t)b_recv->rssi_p + (int16_t)last_rssi;
  // Calcula la suma de RSSI del camino por ese vecino:
  // ruta anunciada por el vecino + RSSI local del enlace hacia ese vecino.

  p->rssi_a = last_rssi;
  // Guarda el RSSI local directo.

/* -------------------------------------------------------------------------- */
/* THREAD 1: Broadcast our Accumulated Path RSSI                              */
/* -------------------------------------------------------------------------- */

PROCESS_THREAD(broadcast_rssi, ev, data)
{
  static struct etimer et;// Timer de evento para definir cuándo enviar el siguiente beacon.
  static struct beacon b;// Estructura beacon que se llenará y enviará
  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)// Si el proceso termina, se cierra la conexión broadcast.
  PROCESS_BEGIN();
  // Inicio obligatorio del proceso en Contiki.
  broadcast_open(&broadcast, 129, &broadcast_call);
  // Abre el canal broadcast 129 y registra el callback asociado.
  while(1) {
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));
    // Programa el envío del siguiente beacon entre 4 y 8 segundos aprox.

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    // Espera hasta que el timer expire.
    if(linkaddr_node_addr.u8[0] == 1) {
      // Si el nodo actual es el nodo 1, se considera el nodo root
      llenar_beacon(&b, linkaddr_node_addr, 0);
      // Lel nodo root se anuncia como 0 que es el valor ideal.

      printf("#A color=red\n");
      // Cambia el color del nodo a rojo en la visualización.
    } else {
      // Si no es raíz...

      llenar_beacon(&b, linkaddr_node_addr, (int16_t)my_path_rssi);
      // Anuncia su métrica acumulada actual.

      printf("#A color=%s\n", my_path_rssi > -1000 ? "green" : "white");
      // Si ya tiene ruta válida lo pinta verde.
      // Si no tiene ruta válida todavía lo pinta blanco.
    }

    packetbuf_copyfrom(&b, sizeof(struct beacon));
    // Copia la estructura beacon al buffer del paquete.

    broadcast_send(&broadcast);
    // Envía el beacon a todos los vecinos por broadcast.
  }

  PROCESS_END();
}

/* -------------------------------------------------------------------------- */
/* THREAD 2: Select Parent with the "Highest" Sum (Closest to 0)              */
/* -------------------------------------------------------------------------- */
PROCESS_THREAD(select_prefered_parent, ev, data)
{
  static struct etimer et;// Timer para decidir cada cuánto reevaluar el mejor padre.
  struct preferred_parent *p;// Puntero para recorrer la lista de vecinos.

  PROCESS_BEGIN();

  if(linkaddr_node_addr.u8[0] == 1) {
    // Si este nodo es la raíz...
    my_path_rssi = 0;
    // La raíz tiene métrica 0.
    PROCESS_EXIT();
    // Sale del proceso porque la raíz no necesita elegir padre.
  }

  while(1) {
    etimer_set(&et, CLOCK_SECOND * 5);
    // Reevalúa el mejor padre cada 5 segundos.
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    // Espera hasta que expire el timer.
    int16_t best_path = -1000;
    // Inicializa el mejor valor como inválido.
    struct preferred_parent *winner = NULL;
    // Inicialmente no hay ningún ganador.
    for(p = list_head(preferred_parent_list); p != NULL; p = list_item_next(p)) {
      // Recorre todos los vecinos registrados.

      if(p->rssi_p > best_path && p->rssi_p < 0) {
        // Como el RSSI es negativo, un valor "más alto" es mejor.
        // Ejemplo: -150 es mejor que -250.
        // Además se ignoran valores inválidos o no útiles.

        best_path = p->rssi_p;
        // Actualiza el mejor camino encontrado.
        winner = p;
        // Guarda el vecino que ofrece ese mejor camino.
      }
    }

    if(winner != NULL) {
      // Si encontró un mejor vecino válido...
      linkaddr_copy(&best_parent_id, &winner->id);
      // Guarda la dirección de ese vecino como mejor padre.
      my_path_rssi = best_path;
      // Actualiza la métrica propia con la mejor ruta encontrada.
    }
  }

  PROCESS_END();
}

/* -------------------------------------------------------------------------- */
/* THREAD 3: Print Debug Table & Decision                                     */
/* -------------------------------------------------------------------------- */
PROCESS_THREAD(print_parent_list, ev, data)
{
  static struct etimer et;// Timer para imprimir la tabla periódicamente.
  struct preferred_parent *p;// Puntero para recorrer la lista.
  PROCESS_BEGIN();
  while(1) {
    etimer_set(&et, CLOCK_SECOND * 10);
    // Imprime la tabla cada 10 segundos.

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    // Espera al siguiente evento del timer.

    printf("\n--- TABLE FOR %d.%d (My Path Sum: %d) ---\n",
           linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1], (int)my_path_rssi);
    // Imprime el encabezado de la tabla con la dirección del nodo actual
    // y su métrica acumulada.

    for(p = list_head(preferred_parent_list); p != NULL; p = list_item_next(p)) {
      // Recorre todos los vecinos guardados.

      printf("Neighbor %d.%d | Path Sum: %d | Local Link: %d\n",
             p->id.u8[0], p->id.u8[1], (int)p->rssi_p, (int)p->rssi_a);
      // Imprime cada vecino:
      // - su dirección
      // - la suma de ruta por ese vecino
      // - el RSSI del enlace local
    }

    printf("------------------------------------\n");

    if(!linkaddr_cmp(&best_parent_id, &linkaddr_null)) {
      // Si best_parent_id NO es la dirección nula, sí hay padre válido.

      printf(">>> DECISION: Node %d.%d selected %d.%d as parent\n",
             linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
             best_parent_id.u8[0], best_parent_id.u8[1]);
      // Imprime cuál vecino fue seleccionado como padre.
    } else {
      // Si best_parent_id sí es nulo...

      printf(">>> DECISION: Node %d.%d has NO valid parent\n",
             linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
      // Indica que todavía no tiene padre.
    }

    printf("====================================\n\n");
  }

  PROCESS_END();
}

/* -------------------------------------------------------------------------- */
/* THREAD 4: Data Transmission                                                */
/* -------------------------------------------------------------------------- */
PROCESS_THREAD(example_unicast_process, ev, data)
{
  static struct etimer et;// Timer para definir cada cuánto enviar datos.

  PROCESS_EXITHANDLER(unicast_close(&uc);)
  // Si el proceso termina, cierra la conexión unicast.

  PROCESS_BEGIN();
  // Inicio del proceso.

  unicast_open(&uc, 146, &unicast_callbacks);
  // Abre el canal unicast 146 con sus callbacks.

  while(1) {
    etimer_set(&et, CLOCK_SECOND * 10);
    // Intenta enviar datos cada 10 segundos.

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    // Espera hasta que expire el timer.

    if(!linkaddr_cmp(&best_parent_id, &linkaddr_null)) {
      // Solo envía si ya tiene un padre válido.

      packetbuf_copyfrom("Data", 4);
      // Copia la palabra "Data" al buffer de salida.
      // Son 4 bytes: D a t a

      unicast_send(&uc, &best_parent_id);
      // Envía el paquete al padre seleccionado.

      printf("#L %d 1\n", best_parent_id.u8[0]);
      // Dibuja una línea hacia el padre en la visualización.
    }
  }

  PROCESS_END();
}

/* Unicast Callbacks */
static void recv_uc(struct unicast_conn *c, const linkaddr_t *from) {
  printf("DATA RECV from %d.%d\n", from->u8[0], from->u8[1]);
  // Se ejecuta cuando este nodo recibe un paquete unicast.
  // Solo imprime desde qué nodo llegó el dato.
}

static void sent_uc(struct unicast_conn *c, int status, int num_tx) {
  const linkaddr_t *dest = packetbuf_addr(PACKETBUF_ADDR_RECEIVER);
  // Obtiene la dirección del destinatario del paquete recién enviado.

  if(dest) printf("DATA SENT to %d.%d status %d\n", dest->u8[0], dest->u8[1], status);
  // Si existe destino, imprime a quién se envió y el estado del envío.
}
