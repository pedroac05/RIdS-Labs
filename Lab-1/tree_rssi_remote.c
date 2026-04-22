#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "tree_lib.h"
#include "powertrace.h"
#include "net/netstack.h"

/* -------------------------------------------------------------------------- */
/*                         Memory & Lists                                     */
/* -------------------------------------------------------------------------- */

/* --- Tabla de vecinos / padres preferidos --- */
MEMB(preferred_parent_mem, struct preferred_parent, 10);
LIST(preferred_parent_list);

/* --- Árbol n-ario de enrutamiento (tabla de rutas) --- */
#define MAX_TREE_NODES 20
static struct tree_node routing_tree[MAX_TREE_NODES];
static uint8_t tree_n_nodes = 0;

/* --- Lista ligada de paquetes pendientes de enrutar (Figura 4) --- */
MEMB(pkt_mem, struct pkt_entry, 10);
LIST(pkt_list);

/* -------------------------------------------------------------------------- */
/*                         Global Variables                                   */
/* -------------------------------------------------------------------------- */
static linkaddr_t best_parent_id;       /* Mejor padre seleccionado          */
static int16_t    my_path_rssi = -1000; /* Métrica acumulada hacia la raíz   */

/* Eventos de proceso para los process_post() */
static process_event_t ev_update_routing; /*    actualizar tabla       */
static process_event_t ev_route_pkt;      /*    enrutar paquete        */

/* -------------------------------------------------------------------------- */
/*                         Rime Setup                                         */
/* -------------------------------------------------------------------------- */
static struct broadcast_conn broadcast;
static struct unicast_conn    uc;

static void register_parent(struct broadcast_conn *c, const linkaddr_t *from);
static void recv_uc(struct unicast_conn *c, const linkaddr_t *from);
static void sent_uc(struct unicast_conn *c, int status, int num_tx);

static const struct broadcast_callbacks broadcast_call   = {register_parent};
static const struct unicast_callbacks   unicast_callbacks = {recv_uc, sent_uc};

/* -------------------------------------------------------------------------- */
/*                         Process Declarations                               */
/* -------------------------------------------------------------------------- */
PROCESS(broadcast_rssi,            "Beaconing");
PROCESS(select_prefered_parent,    "RSSI Sum Selection");
PROCESS(print_parent_list,         "Debug Table");
PROCESS(send_routing_table,        "Send Routing Table");   /* Timer  */
PROCESS(update_routing_table,      "Update Routing Table"); /* Post   */
PROCESS(generate_pkt_dst,          "Generate Pkt Dst");     /* Timer  */
PROCESS(routing_upstream_downstream,"Routing Up/Down");     /* Post   */

AUTOSTART_PROCESSES(
  &broadcast_rssi,
  &select_prefered_parent,
  &print_parent_list,
  &send_routing_table,
  &update_routing_table,
  &generate_pkt_dst,
  &routing_upstream_downstream
);

/* ========================================================================== */
/* CALLBACK: register_parent – Recibe beacon por broadcast                    */
/*           Actualiza la tabla de vecinos y selecciona el mejor padre.       */
/* ========================================================================== */
static void
register_parent(struct broadcast_conn *c, const linkaddr_t *from)
{
  int8_t last_rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
  struct beacon *b_recv = (struct beacon *)packetbuf_dataptr();

  /* --- Buscar o crear entrada en la lista de vecinos --- */
  struct preferred_parent *p;
  for(p = list_head(preferred_parent_list); p != NULL; p = list_item_next(p)) {
    if(linkaddr_cmp(&p->id, from)) break;
  }
  if(p == NULL) {
    p = memb_alloc(&preferred_parent_mem);
    if(p == NULL) return;
    linkaddr_copy(&p->id, from);
    list_push(preferred_parent_list, p);
  }

  p->rssi_p = (int16_t)b_recv->rssi_p + (int16_t)last_rssi;
  p->rssi_a = last_rssi;

  /* Al recibir un beacon, el emisor es un hijo en el árbol n-ario:
   * agrego "this_node → from" si este nodo es más cercano a la raíz.
   * (Solo la raíz y nodos con ruta válida registran relaciones padre-hijo.) */
  if(my_path_rssi > -1000 || linkaddr_node_addr.u8[0] == 1) {
    Add_child(routing_tree, &tree_n_nodes, MAX_TREE_NODES,
              linkaddr_node_addr.u8[0], from->u8[0]);
  }

  /* --- Selección del mejor padre (solo nodos no-raíz) --- */
  if(linkaddr_node_addr.u8[0] == 1) return;

  struct preferred_parent *q, *winner = NULL;
  int16_t best_path = -1000;
  for(q = list_head(preferred_parent_list); q != NULL; q = list_item_next(q)) {
    if(q->rssi_p > best_path && q->rssi_p < 0) {
      best_path = q->rssi_p;
      winner = q;
    }
  }
  if(winner != NULL) {
    printf("#L %d 0\n", best_parent_id.u8[0]);
    linkaddr_copy(&best_parent_id, &winner->id);
    my_path_rssi = best_path;
    printf("#L %d 1\n", best_parent_id.u8[0]);
  }
}

/* ========================================================================== */
/* CALLBACK: recv_uc – Recibe paquete unicast                                 */
/*                                                                            */
/*   si msg_type == U_CONTROL → process_post a update_routing_table           */
/*   si msg_type == U_DATA    → process_post a routing_up_downstream           */
/* ========================================================================== */
static void
recv_uc(struct unicast_conn *c, const linkaddr_t *from)
{
  uint8_t msg_type = packetbuf_attr(PACKETBUF_ATTR_UNICAST_TYPE);
  uint8_t len      = packetbuf_datalen();

  if(msg_type == U_CONTROL) {
    /* ---- tabla de enrutamiento recibida ---- */
    char serial_buf[TREE_SERIAL_BUF];
    if(len >= sizeof(serial_buf)) len = sizeof(serial_buf) - 1;
    memcpy(serial_buf, packetbuf_dataptr(), len);
    serial_buf[len] = '\0';

    printf("CTRL RECV from %d.%d: %s\n", from->u8[0], from->u8[1], serial_buf);

    /* Copia el string a memoria estática para pasarlo al proceso receptor */
    static char ctrl_payload[TREE_SERIAL_BUF];
    memcpy(ctrl_payload, serial_buf, len + 1);

    /* Dispara update_routing_table con el string serializado */
    process_post(&update_routing_table, ev_update_routing, ctrl_payload);

  } else {
    /* ---- paquete de datos a enrutar ---- */
    char msg[32];
    if(len >= sizeof(msg)) len = sizeof(msg) - 1;
    memcpy(msg, packetbuf_dataptr(), len);
    msg[len] = '\0';

    printf("DATA RECV from %d.%d: %s\n", from->u8[0], from->u8[1], msg);

    /* Agregar a la lista de paquetes pendientes */
    struct pkt_entry *pe = memb_alloc(&pkt_mem);
    if(pe != NULL) {
      /* El destino está codificado en el mensaje "DST:<id>:<payload>" */
      uint8_t dst_id = (uint8_t)from->u8[0]; /* por defecto: reenviar upstream */
      if(msg[0] == 'D' && msg[1] == 'S' && msg[2] == 'T' && msg[3] == ':') {
        dst_id = (uint8_t)atoi(msg + 4);
        /* Avanzar hasta el segundo ':' para obtener el payload */
        const char *p2 = msg + 4;
        while(*p2 != ':' && *p2 != '\0') p2++;
        if(*p2 == ':') p2++;
        strncpy(pe->msg, p2, sizeof(pe->msg) - 1);
        pe->msg[sizeof(pe->msg) - 1] = '\0';
      } else {
        strncpy(pe->msg, msg, sizeof(pe->msg) - 1);
        pe->msg[sizeof(pe->msg) - 1] = '\0';
      }
      pe->dst = dst_id;
      list_push(pkt_list, pe);
      process_post(&routing_upstream_downstream, ev_route_pkt, NULL);
    }
  }
}

/*---------------------------------------------------------------------------*/
static void
sent_uc(struct unicast_conn *c, int status, int num_tx)
{
  const linkaddr_t *dest = packetbuf_addr(PACKETBUF_ADDR_RECEIVER);
  if(dest) printf("DATA SENT to %d.%d status %d\n",
                  dest->u8[0], dest->u8[1], status);
}

/* ========================================================================== */
/* THREAD 1: broadcast_rssi – Anuncia la métrica acumulada por broadcast      */
/* ========================================================================== */
PROCESS_THREAD(broadcast_rssi, ev, data)
{
  static struct etimer et;
  static struct beacon b;
  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
  PROCESS_BEGIN();

  /* TX power config omitted: RADIO_PARAM_TXPOWER not available in Contiki classic.
   * To change TX power on Re-Mote, use cc2538_rf_set_tx_power() from
   * cpu/cc2538/dev/cc2538-rf.h if needed. */

  broadcast_open(&broadcast, 129, &broadcast_call);

  while(1) {
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    if(linkaddr_node_addr.u8[0] == 1) {
      llenar_beacon(&b, linkaddr_node_addr, 0);
      printf("#A color=red\n");
    } else {
      llenar_beacon(&b, linkaddr_node_addr, (int16_t)my_path_rssi);
      printf("#A color=%s\n", my_path_rssi > -1000 ? "green" : "white");
    }

    packetbuf_copyfrom(&b, sizeof(struct beacon));
    broadcast_send(&broadcast);
  }

  PROCESS_END();
}

/* ========================================================================== */
/* THREAD 2: select_prefered_parent – Inicializa la raíz                      */
/* ========================================================================== */
PROCESS_THREAD(select_prefered_parent, ev, data)
{
  PROCESS_BEGIN();

  memb_init(&preferred_parent_mem);
  list_init(preferred_parent_list);
  memb_init(&pkt_mem);
  list_init(pkt_list);

  /* Asignar eventos de proceso únicos */
  ev_update_routing = process_alloc_event();
  ev_route_pkt      = process_alloc_event();

  if(linkaddr_node_addr.u8[0] == 1) {
    my_path_rssi = 0;
    /* La raíz se inicializa en el árbol */
    routing_tree[0].id           = 1;
    routing_tree[0].num_children = 0;
    tree_n_nodes                 = 1;
  }

  PROCESS_END();
}

/* ========================================================================== */
/* THREAD 3: print_parent_list – Tabla de depuración                          */
/* ========================================================================== */
PROCESS_THREAD(print_parent_list, ev, data)
{
  static struct etimer et;
  struct preferred_parent *p;
  PROCESS_BEGIN();

  while(1) {
    etimer_set(&et, CLOCK_SECOND * 10);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    printf("\n--- TABLE FOR %d.%d (My Path Sum: %d) ---\n",
           linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1], (int)my_path_rssi);

    for(p = list_head(preferred_parent_list); p != NULL; p = list_item_next(p)) {
      printf("Neighbor %d.%d | Path Sum: %d | Local Link: %d\n",
             p->id.u8[0], p->id.u8[1], (int)p->rssi_p, (int)p->rssi_a);
    }
    printf("------------------------------------\n");

    if(!linkaddr_cmp(&best_parent_id, &linkaddr_null)) {
      printf(">>> DECISION: Node %d.%d selected %d.%d as parent\n",
             linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
             best_parent_id.u8[0], best_parent_id.u8[1]);
    } else {
      printf(">>> DECISION: Node %d.%d has NO valid parent\n",
             linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
    }
    printf("====================================\n\n");
  }

  PROCESS_END();
}

/* ========================================================================== */
/* THREAD 4: send_routing_table                                    */
/*   - Timer: cada 15 s serializa el árbol n-ario y lo envía por unicast      */
/*     al padre (y opcionalmente a los hijos conocidos).                      */
/*   - Usa Serialize() + unicast con PACKETBUF_ATTR_UNICAST_TYPE = U_CONTROL  */
/* ========================================================================== */
PROCESS_THREAD(send_routing_table, ev, data)
{
  static struct etimer et;
  PROCESS_EXITHANDLER(unicast_close(&uc);)
  PROCESS_BEGIN();

  unicast_open(&uc, 146, &unicast_callbacks);

  while(1) {
    etimer_set(&et, CLOCK_SECOND * 15);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    if(tree_n_nodes == 0) continue; /* Árbol vacío, nada que enviar */

    /* Serializar el árbol local */
    static char serial_buf[TREE_SERIAL_BUF];
    uint8_t slen = Serialize(routing_tree, tree_n_nodes,
                             serial_buf, sizeof(serial_buf));
    if(slen == 0) continue;

    /* Enviar la tabla al padre (upstream) */
    if(!linkaddr_cmp(&best_parent_id, &linkaddr_null)) {
      packetbuf_clear();
      packetbuf_copyfrom(serial_buf, slen + 1);
      packetbuf_set_attr(PACKETBUF_ATTR_UNICAST_TYPE, U_CONTROL);
      unicast_send(&uc, &best_parent_id);
      printf("CTRL SENT to parent %d.%d: %s\n",
             best_parent_id.u8[0], best_parent_id.u8[1], serial_buf);
    }
  }

  PROCESS_END();
}

/* ========================================================================== */
/* THREAD 5: update_routing_table                                  */
/*   - Se activa por process_post desde recv_uc cuando llega U_CONTROL.       */
/*   - Llama a Deserialize() + Add_child() para fusionar la tabla recibida.   */
/* ========================================================================== */
PROCESS_THREAD(update_routing_table, ev, data)
{
  PROCESS_BEGIN();

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(ev == ev_update_routing);

    const char *serial_str = (const char *)data;
    if(serial_str == NULL) continue;

    /* Reconstruir el árbol recibido en un buffer temporal */
    static struct tree_node recv_tree[MAX_TREE_NODES];
    uint8_t recv_n = Deserialize(serial_str, recv_tree, MAX_TREE_NODES);

    printf("ROUTING TABLE RECV: %u nodes\n", recv_n);

    /* Fusionar los nodos recibidos con el árbol local usando Add_child() */
    uint8_t i, j;
    for(i = 0; i < recv_n; i++) {
      for(j = 0; j < recv_tree[i].num_children; j++) {
        Add_child(routing_tree, &tree_n_nodes, MAX_TREE_NODES,
                  recv_tree[i].id, recv_tree[i].children[j]);
      }
      /* Si el nodo no tiene hijos pero no está en el árbol, agregarlo */
      if(recv_tree[i].num_children == 0) {
        uint8_t k, found = 0;
        for(k = 0; k < tree_n_nodes; k++) {
          if(routing_tree[k].id == recv_tree[i].id) { found = 1; break; }
        }
        if(!found && tree_n_nodes < MAX_TREE_NODES) {
          routing_tree[tree_n_nodes].id           = recv_tree[i].id;
          routing_tree[tree_n_nodes].num_children = 0;
          tree_n_nodes++;
        }
      }
    }

    /* Imprimir el árbol actualizado */
    printf("TREE (%u nodes): ", tree_n_nodes);
    for(i = 0; i < tree_n_nodes; i++) {
      printf("%u->[", routing_tree[i].id);
      for(j = 0; j < routing_tree[i].num_children; j++) {
        if(j) printf(",");
        printf("%u", routing_tree[i].children[j]);
      }
      printf("] ");
    }
    printf("\n");
  }

  PROCESS_END();
}

/* ========================================================================== */
/* THREAD 6: generate_pkt_dst                                      */
/*   - Timer: genera periódicamente un paquete con destino = DESTINO.         */
/*   - Solo lo hace el nodo ORIGEN.                                            */
/*   - Agrega el paquete a pkt_list y dispara routing_upstream_downstream.    */
/* ========================================================================== */
PROCESS_THREAD(generate_pkt_dst, ev, data)
{
  static struct etimer et;
  PROCESS_BEGIN();

  while(1) {
    etimer_set(&et, CLOCK_SECOND * 20 + random_rand() % (CLOCK_SECOND * 10));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    /* Solo el nodo ORIGEN genera paquetes con destino DESTINO */
    if(linkaddr_node_addr.u8[0] != ORIGEN) continue;

    printf("#A color=orange\n"); /* Identifica el nodo origen en Cooja */

    struct pkt_entry *pe = memb_alloc(&pkt_mem);
    if(pe == NULL) continue;

    pe->dst = DESTINO;
    snprintf(pe->msg, sizeof(pe->msg), "Hello from %u to %u",
             linkaddr_node_addr.u8[0], DESTINO);
    list_push(pkt_list, pe);

    printf("PKT GEN: dst=%u msg=%s\n", pe->dst, pe->msg);

    /* Disparar el proceso de enrutamiento */
    process_post(&routing_upstream_downstream, ev_route_pkt, NULL);
  }

  PROCESS_END();
}

/* ========================================================================== */
/* THREAD 7: routing_upstream_downstream                           */
/*   - Se activa por process_post desde generate_pkt_dst o recv_uc.          */
/*   - Llama a Search_forwarder() para decidir:                               */
/*       0   → Upstream: enviar al padre (best_parent_id)                     */
/*       int → Downstream: enviar al hijo directo con ese ID                  */
/* ========================================================================== */
PROCESS_THREAD(routing_upstream_downstream, ev, data)
{
  PROCESS_BEGIN();

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(ev == ev_route_pkt);

    /* Procesar todos los paquetes en la lista */
    struct pkt_entry *pe;
    while((pe = list_head(pkt_list)) != NULL) {
      list_remove(pkt_list, pe);

      uint8_t my_id  = linkaddr_node_addr.u8[0];
      uint8_t dst_id = pe->dst;

      /* Si ya llegamos al destino, entregar localmente */
      if(dst_id == my_id) {
        printf("PKT DELIVERED locally: %s\n", pe->msg);
        memb_free(&pkt_mem, pe);
        continue;
      }

      /* Llamar a Search_forwarder para decidir la dirección */
      uint8_t forwarder = Search_forwarder(routing_tree, tree_n_nodes,
                                           my_id, dst_id);

      linkaddr_t next_hop;
      linkaddr_copy(&next_hop, &linkaddr_null);

      if(forwarder == 0) {
        /* ---------- UPSTREAM ---------- */
        if(!linkaddr_cmp(&best_parent_id, &linkaddr_null)) {
          linkaddr_copy(&next_hop, &best_parent_id);
          printf("ROUTE UP: %u → parent %u (dst=%u)\n",
                 my_id, best_parent_id.u8[0], dst_id);
        } else {
          printf("ROUTE UP: No valid parent for dst=%u, DROP\n", dst_id);
          memb_free(&pkt_mem, pe);
          continue;
        }
      } else {
        /* ---------- DOWNSTREAM ---------- */
        next_hop.u8[0] = forwarder;
        next_hop.u8[1] = 0;
        printf("ROUTE DOWN: %u → child %u (dst=%u)\n",
               my_id, forwarder, dst_id);
      }

      /* Construir el mensaje con encabezado "DST:<id>:<payload>" */
      char fwd_msg[40];
      snprintf(fwd_msg, sizeof(fwd_msg), "DST:%u:%s", dst_id, pe->msg);

      packetbuf_clear();
      packetbuf_copyfrom(fwd_msg, strlen(fwd_msg) + 1);
      packetbuf_set_attr(PACKETBUF_ATTR_UNICAST_TYPE, U_DATA);
      unicast_send(&uc, &next_hop);

      memb_free(&pkt_mem, pe);
    }
  }

  PROCESS_END();
}