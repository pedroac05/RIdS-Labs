/*
 * Copyright (c) 2007, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

/**
 * \file
 *         Testing the broadcast layer in Rime
 * \author
 *         Adam Dunkels <adam@sics.se>
 */

#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"

#include "dev/button-sensor.h"

#include "dev/leds.h"

#include <stdio.h>

#include "tree_lib.h"

#include "powertrace.h"

//Tabla de padre preferido


MEMB(preferred_parent_mem, struct preferred_parent, 10);
LIST(preferred_parent_list);

/*---------------------------------------------------------------------------*/
PROCESS(broadcast_rssi, "Broadcast example");
PROCESS(select_prefered_parent , "Proceso_corre_timer" );
PROCESS(print_parent_list , "print_parent_list" );  // Imprime la lista de padres
PROCESS(example_unicast_process, "Example unicast");
AUTOSTART_PROCESSES(&broadcast_rssi, &select_prefered_parent, &print_parent_list, &example_unicast_process);

/*---------------------------------------------------------------------------*/
struct preferred_parent parent = { .id = linkaddr_node_addr, .rssi_a = 1 };
/*---------------------------------------------------------------------------*/
static void
register_parent(struct broadcast_conn *c, const linkaddr_t *from)
{
  uint16_t last_rssi;

  last_rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI); // registra el rssi del beacon que llego

  void *msg = packetbuf_dataptr(); //msg que llego

  // interpreta el puntero msg como un puntero a un struct beacon y
  // lo copia en b_recv con el asterisco del inicio
  struct beacon b_recv = *( (struct beacon*) msg );



  struct preferred_parent *p; // Para recorrer la lista de posibles padres
  struct preferred_parent *in_l; // in to list

  //Revisar si ya conozco este posible padre
  for(p = list_head(preferred_parent_list); p != NULL; p = list_item_next(p))
  {
    // Si el id del padre es igual al id del beacon que llego
    // rompo el bucle
    if(linkaddr_cmp(&p->id, &b_recv.id)) {
      break;
    }
  }


  // Si recorri toda la lista y no encontre el id del beacon, p es NULL 
  // y tengo que agregar el beacon a la lista
  if(p == NULL)
  {
    in_l = memb_alloc(&preferred_parent_mem); // entrega la dir de memoria para un nuevo padre o null si no hay espacio
    if(in_l == NULL) {            // Si preferred_parent_mem no tiene espacio para nuevos padres, imprime error
      printf("ERROR: we could not allocate a new entry for <<preferred_parent_list>> in tree_rssi\n");
    }else // Si preferred_parent_mem tiene espacio para nuevos padres
    {
      //Guardo los campos del mensaje
      in_l->id = b_recv.id; // Guardo el id del nodo en la posicion de memoria que dio memb_alloc
      
      //rssi_a es el rssi del padre + el rssi del enlace al padre
      in_l->rssi_a = b_recv.rssi_p + last_rssi; // Guardo del rssi acumulado. El rssi acumulado es el rssi divulgado por el nodo (rssi_path) + el rssi medido del beacon que acaba de llegar (rss)
      list_push(preferred_parent_list,in_l); // Add an item to the start of the list.
      printf("beacon added to list: id = %d rssi_a = %d\n", in_l->id.u8[0], in_l->rssi_a);

    }
  }else // Si p apunta a un padre en la lista preferred_parent_list
  {
    // Actualizo el rssi del padre
    p->rssi_a = b_recv.rssi_p + last_rssi;
    printf("beacon updated to list with RSSI_A = %d\n", p->rssi_a);

  }


  printf("BEACON_RECV NODE_ID = %d.%d RSSI_A = %d\n", b_recv.id.u8[0], b_recv.id.u8[1], b_recv.rssi_p);

  printf("A_NODE_ID = %d.%d  RSSI = %d \n", from->u8[0], from->u8[1], last_rssi);

  printf("broadcast message received from %d.%d: '%s'\n",
         from->u8[0], from->u8[1], (char *)packetbuf_dataptr());

  process_post(&print_parent_list, PROCESS_EVENT_CONTINUE, NULL);
}

/*---------------------------------------------------------------------------*/
static void
recv_uc(struct unicast_conn *c, const linkaddr_t *from)
{

  packetbuf_attr_t msg_type = packetbuf_attr(PACKETBUF_ATTR_UNICAST_TYPE);

  if(msg_type == U_DATA)  // Si el mensaje es de tipo data
  {

    printf("unicast U_DATA message received from %d.%d\n",
     from->u8[0], from->u8[1]);

  }else if(msg_type == U_CONTROL)  // Si el mensaje es de tipo control
  {

    printf("unicast U_CONTROL message received from %d.%d\n",
     from->u8[0], from->u8[1]);
  }

}
/*---------------------------------------------------------------------------*/
/** 
 * \brief Función que se ejecuta cuando se envía un mensaje unicast.
 * \param c: puntero a la conexión unicast que realizó el envío
 * \param status: estado del envío. 0 = éxito, 1 = fallo
 * \param num_tx: número de veces que se intentó transmitir el paquete (contando reintentos).
 */
static void
sent_uc(struct unicast_conn *c, int status, int num_tx)
{
  // Lee del buffer de paquetes la dirección del nodo destino
  const linkaddr_t *dest = packetbuf_addr(PACKETBUF_ADDR_RECEIVER);

  // Si dest es 0.0 no se hace nada. 0.0 significa que el mensaje fue
  // un broadcast, no un unicast.
  if(linkaddr_cmp(dest, &linkaddr_null)) {
    return;
  }
  printf("unicast message sent to %d.%d: status %d num_tx %d\n",
    dest->u8[0], dest->u8[1], status, num_tx);
}
/*---------------------------------------------------------------------------*/

static const struct broadcast_callbacks broadcast_call = {register_parent};
static struct broadcast_conn broadcast;


static const struct unicast_callbacks unicast_callbacks = {recv_uc, sent_uc};
static struct unicast_conn uc;

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(example_unicast_process, ev, data)
{
  // si el proceso termina, se cierra la conexión unicast
  PROCESS_EXITHANDLER(unicast_close(&uc);)

  PROCESS_BEGIN();

  powertrace_start(CLOCK_SECOND * 10);  // Cada 10 segundos se guarda el consumo de energia

  unicast_open(&uc, 146, &unicast_callbacks);  // Se abre la conexión unicast

  while(1) {
    static struct etimer et;
    linkaddr_t addr;  // Direccion del nodo destino

    etimer_set(&et, CLOCK_SECOND );

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));  // Espera un segundo

    printf("#L 3 1\n"); // 1 = Dibujar el enlace/link (exclusivo para Cooja)

    etimer_set(&et, CLOCK_SECOND );

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));  // Espera un segundo

    printf("#L 3 0\n"); // 0 = Borramos el enlace/link (exclusivo para Cooja)

    printf("Ticks per second: %u\n", RTIMER_SECOND);  // Imprime los ticks por segundo

    packetbuf_copyfrom("Hello", 5);  // Copia el mensaje "Hello" (5 bytes) en el buffer de paquetes
    
    // Se construye la direccion del nodo destino
    addr.u8[0] = parent.u8[0];
    addr.u8[1] = parent.u8[1];
    
    // Si la direccion del nodo destino no es la direccion del nodo actual
    if(!linkaddr_cmp(&addr, &linkaddr_node_addr)) {
      // Se establece el tipo de mensaje unicast como control
      packetbuf_set_attr(PACKETBUF_ATTR_UNICAST_TYPE, U_CONTROL);
      // Se envia el mensaje unicast
      unicast_send(&uc, &addr);
    }

  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/


/*---------------------------------------------------------------------------*/
/* Proceso que se encarga de imprimir la lista de padres */
PROCESS_THREAD(print_parent_list, ev, data)
{

  PROCESS_BEGIN();

  struct preferred_parent *p; // Para recorrer la lista de posibles padres

  while(1)
  {

    PROCESS_WAIT_EVENT(); // Espera el evento en la recepcion de un beacon

    printf("Corriendo print_parent_list\n" );


    // Imprime la lista de padres

    printf("-------\n");
    for(p = list_head(preferred_parent_list); p != NULL; p = list_item_next(p))
    {
      printf("LISTA ID=%d RSSI_A = %d \n", p->id.u8[0], p->rssi_a);

    }
    printf("-------\n");

  }

  PROCESS_END();


}

/*---------------------------------------------------------------------------*/

// Cada 2 segundos se imprime un "Corriendo select_prefered_parent"
PROCESS_THREAD(select_prefered_parent, ev, data)
{

  static struct etimer et;


  PROCESS_BEGIN();

  while(1)
  {

    etimer_set(&et, CLOCK_SECOND * 2);

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    printf("Seleccionando padre preferido \n" );

    // Seleccionar el padre preferido
    struct preferred_parent *p;
    struct preferred_parent *p_max = &parent;
    for(p = list_head(preferred_parent_list); p != NULL; p = list_item_next(p))
    {
      if(p->rssi_a > 0) continue; // Evitamos que los nodos que no conocen el camino hacia la raiz (RSSI_A > 0) sean seleccionados
      if(p->rssi_a > p_max->rssi_a) p_max = p; // Si el nodo actual tiene un mejor RSSI, se selecciona como padre preferido
    }

    printf("Padre preferido seleccionado: ID=%d RSSI_A = %d \n", parent.u8[0], parent.u8[1]);

  }


  PROCESS_END();


}

/*---------------------------------------------------------------------------*/

PROCESS_THREAD(broadcast_rssi, ev, data)
{
  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  struct beacon b;

  broadcast_open(&broadcast, 129, &broadcast_call);

  while(1) {

    /* Delay 2-4 seconds */
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    // Llenar el beacon con info
    //b.id.u8[0] = linkaddr_node_addr.u8[0];
    //b.id.u8[1] = linkaddr_node_addr.u8[1];
    //b.rssi_a   = -10;

    llenar_beacon(&b, linkaddr_node_addr, parent.rssi_a);


    packetbuf_copyfrom(&b, sizeof( struct beacon ));
    broadcast_send(&broadcast);
    printf("broadcast message sent\n");

    printf_hello();

    printf("NODE ID = %d.%d\n", linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);


    printf("#A color=orange\n"); //A = area

    /* Delay 2-4 seconds */
    etimer_set(&et, CLOCK_SECOND * 1);

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    printf("#A color=white\n"); //A = area

  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
