#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "sys/node-id.h"
#include "clock.h"
#include "dev/button-sensor.h"

#include "dev/leds.h"

#include <stdio.h>
/*---------------------------------------------------------------------------*/
PROCESS(example_broadcast_process, "Broadcast example");
AUTOSTART_PROCESSES(&example_broadcast_process);
/*---------------------------------------------------------------------------*/
#define CHECK_MESSAGE_RECEIVED_INTERVAL 6

static int message_received = 0; //Bandera, 1 cuando se recibe, 0 se inicia
static struct broadcast_conn broadcast; //llama la función de broadcast
/*---------------------------------------------------------------------------*/
static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from) // Función de callback, se define con *c la conexión, y con *from de dónde viene
{
  leds_on(LEDS_GREEN); // se inician los leds en verde cuando se recibe data
  uint8_t *recv_data = (uint8_t *)packetbuf_dataptr(); //untagged integer de 1 byte, se llena el buffer

  // printf("broadcast message received from %d.%d: %u\n",
  //        from->u8[0], from->u8[1], (uint8_t)packetbuf_dataptr());
  printf("Message received: {%d.%u}\n",from->u8[0], recv_data[0]); // identifica el id del nodo y el contador de mensajes
  
  uint8_t new_message_received = recv_data[0] + 1; //aumenta el contador del ping pong
  clock_wait(CLOCK_SECOND / 2); 
  packetbuf_copyfrom(&new_message_received, 1); // Reemplaza el valor del contador
  broadcast_send(&broadcast); // Vuelve a mandar el mensaje con los contadores actualizadas
  
  int node_direction = linkaddr_node_addr.u8[0]; //Vuelve a obtener el id del nodo

  printf("Message sent: {%d.%u}\n", node_direction, new_message_received); // Manda el nuevo mensaje con el id del nodo nuevo, es decir el que responde y con el nuevo mensaje (el contador iniciado)
  
  message_received = 1; //eleva la bandera, para salir del proceso
  leds_off(LEDS_GREEN); //apaga el led
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv}; // esta la necesito enntender
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(example_broadcast_process, ev, data) // Inicia el proceso de contiki
{
  static struct etimer et; //se crea el event timer

  PROCESS_EXITHANDLER(broadcast_close(&broadcast);) // para cerrar el broadcast

  PROCESS_BEGIN(); // Macro para inicial el proceso

  broadcast_open(&broadcast, 129, &broadcast_call); // abre el broadcast, usa el canal 129 y llama el callback

  int node_direction = linkaddr_node_addr.u8[0]; // guarda el id de los nodos que reciben el mensaje de los nodos 

  if(node_direction == 1){ //si el nodo es el ID 1 inicia la comunicación
    static uint8_t message = 1; // la comunicación inicia en 1

    while(1) {
      
      /* Timer set to CHECK_MESSAGE_RECEIVED_INTERVAL/2 */
      etimer_set(&et, CLOCK_SECOND * CHECK_MESSAGE_RECEIVED_INTERVAL/2); //el timer expira después de 6 segundos/2 es decir 3 segundos
      
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et)); //cuando se expira el timer, se libera el procesador
      
      if(message_received == 1){ // condicional de recepción de mensajes
        printf("Message received, waiting %d seconds to check again\n", CHECK_MESSAGE_RECEIVED_INTERVAL/2); //recibió algo, va a volver a mirar en 3 segundos
        message_received = 0; // resetea la bandera a 0
      }else{
        printf("Message not received within %d seconds\n", CHECK_MESSAGE_RECEIVED_INTERVAL/2); // si no recibe nada en 3 segundos, vuelve a mandar
        printf("Sending message: %u\n", message); // manda el mensaje
        packetbuf_copyfrom(&message, 1); //ahora sí manda el mensaje
        broadcast_send(&broadcast); //
        printf("broadcast message sent\n");
      }
    }
  }
    
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
