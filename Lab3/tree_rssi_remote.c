#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include "dev/leds.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "tree_lib.h"
#include "powertrace.h"
#include "net/netstack.h"
#define REMOTE 1
/* -------------------------------------------------------------------------- */
/*                         Memory & Lists                                     */
/* -------------------------------------------------------------------------- */

MEMB(preferred_parent_mem, struct preferred_parent, 10);
LIST(preferred_parent_list);

#define MAX_TREE_NODES 20
static struct tree_node routing_tree[MAX_TREE_NODES];
static uint8_t tree_n_nodes = 0;

MEMB(pkt_mem, struct pkt_entry, 10);
LIST(pkt_list);

/* -------------------------------------------------------------------------- */
/*                  Packet Loss Stats (solo relevante en sink)                 */
/* -------------------------------------------------------------------------- */
#define MAX_PKT_SOURCES 20
struct pkt_stats {
  uint8_t  src_id;       /* ID del nodo origen                              */
  uint16_t seq_max;      /* Mayor seq recibido = total enviados por ese nodo */
  uint16_t received;     /* Cantidad de paquetes recibidos de ese nodo       */
};
static struct pkt_stats pkt_stats_table[MAX_PKT_SOURCES];
static uint8_t pkt_stats_count = 0;

/* -------------------------------------------------------------------------- */
/*                         Global Variables                                   */
/* -------------------------------------------------------------------------- */
static linkaddr_t best_parent_id;
static int16_t    my_path_rssi = -1000;

static process_event_t ev_update_routing;
static process_event_t ev_route_pkt;

static clock_time_t trickle_interval;

/* Constantes Trickle (globales para callback y thread) */
#define TRICKLE_MIN  (CLOCK_SECOND / 8)    /* 125 ms */
#define TRICKLE_MAX  (CLOCK_SECOND * 4)    /*   4 s  */
#define KEEPALIVE_TIMEOUT (CLOCK_SECOND * 5) /*  5 s  */

/* -------------------------------------------------------------------------- */
/*                         Rime Setup                                         */
/* -------------------------------------------------------------------------- */
static struct broadcast_conn broadcast;
static struct unicast_conn    uc;

static void register_parent(struct broadcast_conn *c, const linkaddr_t *from);
static void recv_uc(struct unicast_conn *c, const linkaddr_t *from);
static void sent_uc(struct unicast_conn *c, int status, int num_tx);

static const struct broadcast_callbacks broadcast_call    = {register_parent};
static const struct unicast_callbacks   unicast_callbacks = {recv_uc, sent_uc};

/* -------------------------------------------------------------------------- */
/*                         Process Declarations                               */
/* -------------------------------------------------------------------------- */
PROCESS(broadcast_rssi,             "Beaconing");
PROCESS(select_prefered_parent,     "RSSI Sum Selection");
PROCESS(print_parent_list,          "Debug Table");
PROCESS(send_routing_table,         "Send Routing Table");
PROCESS(update_routing_table,       "Update Routing Table");
PROCESS(generate_pkt_dst,           "Generate Pkt Dst");
PROCESS(routing_upstream_downstream,"Routing Up/Down");

AUTOSTART_PROCESSES(
  &broadcast_rssi,
  &select_prefered_parent,
  &print_parent_list,
  &send_routing_table,
  &update_routing_table,
  &generate_pkt_dst,
  &routing_upstream_downstream
);
static void
blink_blue(void)
{
  leds_on(LEDS_BLUE);
  clock_delay_usec(10000);
  leds_off(LEDS_BLUE);
}
/* -------------------------------------------------------------------------- */
/* Callback: keepalive expirado -> resetear trickle al minimo                 */
/* -------------------------------------------------------------------------- */
static void
keepalive_expired(void *ptr)
{
  struct preferred_parent *p = (struct preferred_parent *)ptr;
  printf("KEEPALIVE EXPIRED for %d.%d -> trickle reset\n",
         p->id.u8[0], p->id.u8[1]);
  trickle_interval = TRICKLE_MIN;
}

/* ========================================================================== */
/* CALLBACK: register_parent                                                  */
/* ========================================================================== */
static void
register_parent(struct broadcast_conn *c, const linkaddr_t *from)
{
  int8_t last_rssi = (int8_t)packetbuf_attr(PACKETBUF_ATTR_RSSI);
  struct beacon *b_recv = (struct beacon *)packetbuf_dataptr();

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

  /* Iniciar o reiniciar el ctimer keepalive de 5 s para este padre */
  ctimer_set(&p->keepalive, KEEPALIVE_TIMEOUT, keepalive_expired, p);

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
/* CALLBACK: recv_uc                                                          */
/* Primer byte del payload = tipo (U_CONTROL o U_DATA)                        */
/* ========================================================================== */
static void
recv_uc(struct unicast_conn *c, const linkaddr_t *from)
{
  uint16_t len = packetbuf_datalen();
  if(len == 0) return;

  uint8_t *raw = (uint8_t *)packetbuf_dataptr();
  uint8_t msg_type = raw[0];
  uint16_t payload_len = len - 1;

  printf("UC RX from %d.%d len=%u msg_type=%u\n",
         from->u8[0], from->u8[1], len, msg_type);

  if(msg_type == U_CONTROL) {
    /* ---- tabla de enrutamiento recibida ---- */
    char serial_buf[TREE_SERIAL_BUF];
    if(payload_len >= sizeof(serial_buf)) payload_len = sizeof(serial_buf) - 1;
    memcpy(serial_buf, raw + 1, payload_len);
    serial_buf[payload_len] = '\0';

    printf("CTRL RECV from %d.%d: %s\n", from->u8[0], from->u8[1], serial_buf);

    static char ctrl_payload[TREE_SERIAL_BUF];
    memcpy(ctrl_payload, serial_buf, payload_len + 1);

    process_post(&update_routing_table, ev_update_routing, ctrl_payload);

  } else if(msg_type == U_DATA) {
    /* ---- paquete de datos a enrutar ---- */
    char msg[32];
    if(payload_len >= sizeof(msg)) payload_len = sizeof(msg) - 1;
    memcpy(msg, raw + 1, payload_len);
    msg[payload_len] = '\0';

    printf("DATA RECV from %d.%d: %s\n", from->u8[0], from->u8[1], msg);
    /* Blink LED when a data message is received */
    /* This node currently has the message */
    printf("MESSAGE IS NOW AT NODE %d.%d\n",
       linkaddr_node_addr.u8[0],
       linkaddr_node_addr.u8[1]);
    blink_blue();

    /* ------ Actualizar stats de paquetes (solo en sink) ------ */
    if(linkaddr_node_addr.u8[0] == 1) {
      /* Formato esperado: DST:<dst>:SEQ:<seq>:<src>:<payload> */
      uint8_t  pkt_src = 0;
      uint16_t pkt_seq = 0;
      const char *sp = msg;
      /* Saltar DST:<dst>: */
      if(sp[0]=='D' && sp[1]=='S' && sp[2]=='T' && sp[3]==':') {
        sp += 4;
        while(*sp != ':' && *sp != '\0') sp++;
        if(*sp == ':') sp++;
      }
      /* Parsear SEQ:<seq>:<src>: */
      if(sp[0]=='S' && sp[1]=='E' && sp[2]=='Q' && sp[3]==':') {
        sp += 4;
        pkt_seq = (uint16_t)atoi(sp);
        while(*sp != ':' && *sp != '\0') sp++;
        if(*sp == ':') sp++;
        pkt_src = (uint8_t)atoi(sp);
      }
      if(pkt_src != 0) {
        /* Buscar o crear entrada en tabla de stats */
        /*NOTA: este no sé si funciona bien, probar en simulación*/
        uint8_t si;
        struct pkt_stats *ps = NULL;
        for(si = 0; si < pkt_stats_count; si++) {
          if(pkt_stats_table[si].src_id == pkt_src) {
            ps = &pkt_stats_table[si];
            break;
          }
        }
        if(ps == NULL && pkt_stats_count < MAX_PKT_SOURCES) {
          ps = &pkt_stats_table[pkt_stats_count++];
          ps->src_id  = pkt_src;
          ps->seq_max = 0;
          ps->received = 0;
        }
        if(ps != NULL) {
          ps->received++;
          if(pkt_seq > ps->seq_max) {
            ps->seq_max = pkt_seq;
          }
          printf("PKT STATS: src=%u seq=%u received=%u/%u\n",
                 pkt_src, pkt_seq, ps->received, ps->seq_max);
        }
      }
    }

    struct pkt_entry *pe = memb_alloc(&pkt_mem);
    if(pe != NULL) {
      uint8_t dst_id = (uint8_t)from->u8[0];
      if(msg[0] == 'D' && msg[1] == 'S' && msg[2] == 'T' && msg[3] == ':') {
        dst_id = (uint8_t)atoi(msg + 4);
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
  if(dest) printf("SENT to %d.%d status %d\n",
                  dest->u8[0], dest->u8[1], status);
}

/* ========================================================================== */
/* THREAD 1: broadcast_rssi                                                   */
/* ========================================================================== */
PROCESS_THREAD(broadcast_rssi, ev, data)
{
  static struct etimer et;
  static struct beacon b;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
  PROCESS_BEGIN();

  broadcast_open(&broadcast, 129, &broadcast_call);

  /* Inicializar el intervalo Trickle al minimo */
  trickle_interval = TRICKLE_MIN;

  while(1) {
    /* Esperar el intervalo Trickle actual */
    etimer_set(&et, trickle_interval);
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

    printf("TRICKLE: beacon sent, interval=%lu ms\n",
           (unsigned long)(trickle_interval * 1000 / CLOCK_SECOND));

    /* Duplicar el intervalo (x2), sin superar el maximo */
    if(trickle_interval < TRICKLE_MAX) {
      trickle_interval *= 2;
      if(trickle_interval > TRICKLE_MAX) {
        trickle_interval = TRICKLE_MAX;
      }
    }
  }

  PROCESS_END();
}

/* ========================================================================== */
/* THREAD 2: select_prefered_parent                                           */
/* ========================================================================== */
PROCESS_THREAD(select_prefered_parent, ev, data)
{
  PROCESS_BEGIN();

  memb_init(&preferred_parent_mem);
  list_init(preferred_parent_list);
  memb_init(&pkt_mem);
  list_init(pkt_list);

  ev_update_routing = process_alloc_event();
  ev_route_pkt      = process_alloc_event();

  if(linkaddr_node_addr.u8[0] == 1) {
    my_path_rssi = 0;
    routing_tree[0].id           = 1;
    routing_tree[0].num_children = 0;
    tree_n_nodes                 = 1;
  }

  /* Activar powertrace: imprime perfil de energia cada 10 s */
  powertrace_start(CLOCK_SECOND * 10);

  PROCESS_END();
}

/* ========================================================================== */
/* THREAD 3: print_parent_list                                                */
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

    /* ---------- Tabla de perdida de paquetes (solo sink) ---------- */
    if(linkaddr_node_addr.u8[0] == 1 && pkt_stats_count > 0) {
      uint8_t si;
      printf("\n--- PACKET LOSS TABLE (Sink) ---\n");
      printf("Source | Received | Sent(seq) | Loss %%\n");
      for(si = 0; si < pkt_stats_count; si++) {
        struct pkt_stats *ps = &pkt_stats_table[si];
        uint16_t lost = ps->seq_max - ps->received;
        uint16_t loss_pct = 0;
        if(ps->seq_max > 0) {
          loss_pct = (uint16_t)((lost * 100) / ps->seq_max);
        }
        printf("  %3u  |  %5u   |   %5u   |  %3u%%\n",
               ps->src_id, ps->received, ps->seq_max, loss_pct);
      }
      printf("--------------------------------\n\n");
    }
  }

  PROCESS_END();
}

/* ========================================================================== */
/* THREAD 4: send_routing_table                                               */
/* Formato: [U_CONTROL][serial_buf...]                                        */
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

    /* Solo enviar si tiene padre */
    if(linkaddr_cmp(&best_parent_id, &linkaddr_null)) continue;

    /* Serializar el árbol local */
    static char serial_buf[TREE_SERIAL_BUF];
    uint8_t slen;

    if(tree_n_nodes == 0) {
      /* Nodo hoja: notificar al padre que existe */
      slen = snprintf(serial_buf, sizeof(serial_buf), "%u:;",
                      linkaddr_node_addr.u8[0]);
    } else {
      slen = Serialize(routing_tree, tree_n_nodes,
                       serial_buf, sizeof(serial_buf));
    }

    if(slen == 0) continue;

    /* Construir payload: [U_CONTROL][serial_buf] */
    static char send_buf[TREE_SERIAL_BUF + 1];
    send_buf[0] = U_CONTROL;
    memcpy(send_buf + 1, serial_buf, slen + 1);

    packetbuf_clear();
    packetbuf_copyfrom(send_buf, slen + 2);
    unicast_send(&uc, &best_parent_id);

    printf("CTRL SENT to parent %d.%d: %s\n",
           best_parent_id.u8[0], best_parent_id.u8[1], serial_buf);
  }
  PROCESS_END();
}

/* ========================================================================== */
/* THREAD 5: update_routing_table                                             */
/* ========================================================================== */
PROCESS_THREAD(update_routing_table, ev, data)
{
  PROCESS_BEGIN();
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(ev == ev_update_routing);
    const char *serial_str = (const char *)data;
    if(serial_str == NULL) continue;

    static struct tree_node recv_tree[MAX_TREE_NODES];
    uint8_t recv_n = Deserialize(serial_str, recv_tree, MAX_TREE_NODES);
    printf("ROUTING TABLE RECV: %u nodes\n", recv_n);

    if(recv_n == 0) continue;

    uint8_t i, j;

    /* Registrar al emisor como hijo directo de este nodo */
    Add_child(routing_tree, &tree_n_nodes, MAX_TREE_NODES,
              linkaddr_node_addr.u8[0], recv_tree[0].id);

    /* Fusionar el subárbol recibido */
    for(i = 0; i < recv_n; i++) {
      for(j = 0; j < recv_tree[i].num_children; j++) {
        Add_child(routing_tree, &tree_n_nodes, MAX_TREE_NODES,
                  recv_tree[i].id, recv_tree[i].children[j]);
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
/* THREAD 6: generate_pkt_dst                                                 */
/* ========================================================================== */
PROCESS_THREAD(generate_pkt_dst, ev, data)
{
  static struct etimer et;
  static uint16_t seq_num = 0; /* Sequence number por nodo origen */
  PROCESS_BEGIN();

  while(1) {
    etimer_set(&et, CLOCK_SECOND * 20 + random_rand() % (CLOCK_SECOND * 10));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    if(linkaddr_node_addr.u8[0] != ORIGEN) continue;

    seq_num++;
    printf("#A color=orange\n");

    struct pkt_entry *pe = memb_alloc(&pkt_mem);
    if(pe == NULL) continue;

    pe->dst = DESTINO;
    /* Formato: SEQ:<seq>:<src>:<payload> */
    snprintf(pe->msg, sizeof(pe->msg), "SEQ:%u:%u:Hi",
             seq_num, linkaddr_node_addr.u8[0]);
    list_push(pkt_list, pe);

    printf("PKT GEN: seq=%u dst=%u msg=%s\n", seq_num, pe->dst, pe->msg);
    blink_blue();
    process_post(&routing_upstream_downstream, ev_route_pkt, NULL);
  }

  PROCESS_END();
}

/* ========================================================================== */
/* THREAD 7: routing_upstream_downstream                                      */
/* Formato: [U_DATA][DST:<id>:<payload>]                                      */
/* ========================================================================== */
PROCESS_THREAD(routing_upstream_downstream, ev, data)
{
  PROCESS_BEGIN();

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(ev == ev_route_pkt);

    struct pkt_entry *pe;
    while((pe = list_head(pkt_list)) != NULL) {
      list_remove(pkt_list, pe);

      uint8_t my_id  = linkaddr_node_addr.u8[0];
      uint8_t dst_id = pe->dst;

      if(dst_id == my_id) {
        printf("PKT DELIVERED locally: %s\n", pe->msg);
        /* Nodo destino: color verde */
        printf("#A color=blue\n");
        blink_blue();
        memb_free(&pkt_mem, pe);
        continue;
      }

      uint8_t forwarder = Search_forwarder(routing_tree, tree_n_nodes,
                                           my_id, dst_id);

      linkaddr_t next_hop;
      linkaddr_copy(&next_hop, &linkaddr_null);

      if(forwarder == 0) {
        if(!linkaddr_cmp(&best_parent_id, &linkaddr_null)) {
          linkaddr_copy(&next_hop, &best_parent_id);
          printf("ROUTE UP: %u -> parent %u (dst=%u)\n",
                 my_id, best_parent_id.u8[0], dst_id);
        } else {
          printf("ROUTE UP: No valid parent for dst=%u, DROP\n", dst_id);
          memb_free(&pkt_mem, pe);
          continue;
        }
      } else {
        next_hop.u8[0] = forwarder;
        next_hop.u8[1] = 0;
        printf("ROUTE DOWN: %u -> child %u (dst=%u)\n",
               my_id, forwarder, dst_id);
      }

      /* Colorear nodo actual (azul) y dibujar enlace hacia siguiente salto */
      printf("#A color=blue\n");
      printf("#L %d 1\n", next_hop.u8[0]);
      blink_blue();
      /* Construir payload: [U_DATA][DST:<id>:<msg>] */
      static char send_buf[42];
      uint8_t plen = snprintf(send_buf + 1, sizeof(send_buf) - 1,
                              "DST:%u:%s", dst_id, pe->msg);
      send_buf[0] = U_DATA;

      packetbuf_clear();
      packetbuf_copyfrom(send_buf, plen + 2);
      unicast_send(&uc, &next_hop);

      /* Borrar enlace después de enviar */
      printf("#L %d 0\n", next_hop.u8[0]);

      memb_free(&pkt_mem, pe);
    }
  }

  PROCESS_END();
}
