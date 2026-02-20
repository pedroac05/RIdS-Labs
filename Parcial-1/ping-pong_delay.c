#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "sys/node-id.h"
#include "sys/ctimer.h"

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
static struct ctimer rebroadcast_timer; // estructura para llamar el delay
static uint8_t pending_rebroadcast; //donde se guarda el mensaje mientras pasa el delay
static uint8_t rebroadcast_scheduled = 0; //bandera para controlar los eventos y mensajes en cola
/*---------------------------------------------------------------------------*/
static void
do_rebroadcast(void *ptr) // la función para enviar los mensajes con delay
{
  packetbuf_copyfrom(&pending_rebroadcast, 1); //guarda el mensaje a enviar en el buffer
  broadcast_send(&broadcast); //inicia el broadcast

  int node_direction = linkaddr_node_addr.u8[0];//Vuelve a obtener el id del nodo
  printf("Message sent: {%d.%u}\n", node_direction, pending_rebroadcast); // Manda el nuevo mensaje con el id del nodo nuevo, es decir el que responde y con el nuevo mensaje (el contador iniciado) con delay

  rebroadcast_scheduled = 0; //baja la bandera cuando se manda
}

static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from) // Función de callback, se define con *c la conexión, y con *from de dónde viene
{
  leds_on(LEDS_GREEN); // se inician los leds en verde cuando se recibe data
  uint8_t *recv_data = (uint8_t *)packetbuf_dataptr(); //untagged integer de 1 byte, se llena el buffer

  // printf("broadcast message received from %d.%d: %u\n",
  //        from->u8[0], from->u8[1], (uint8_t)packetbuf_dataptr());
  printf("Message received: {%d.%u}\n",from->u8[0], recv_data[0]); // identifica el id del nodo y el contador de mensajes
  
  uint8_t new_message_received = recv_data[0] + 1; //es la que recibe el mensaje y le aumenta uno al contador
  pending_rebroadcast = new_message_received; // iguala el mensaje con delay con el mensaje "recibido"

  if(!rebroadcast_scheduled) { //revisa si hay algún mensaje en cola con delay, si no hay ! se crea uno, si hay no se crea ninguno
    rebroadcast_scheduled = 1; //levanta la bandera

    clock_time_t delay = (CLOCK_SECOND / 5) + (random_rand() % (CLOCK_SECOND / 3)); //calcula el delay, se agrega el jitter "(random_rand() % (CLOCK_SECOND / 3)" para evitar que se sobrelape la transmición
    ctimer_set(&rebroadcast_timer, delay, do_rebroadcast, NULL); //detecta el timer, espera el delay, después del delay, vuelve a enviar el mensaje, devuelve un null al callback pq no usamos el ptr
  }

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
