#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "sys/node-id.h"

#include "dev/button-sensor.h"

#include "dev/leds.h"

#include <stdio.h>
/*---------------------------------------------------------------------------*/
PROCESS(example_broadcast_process, "Broadcast example");
AUTOSTART_PROCESSES(&example_broadcast_process);
/*---------------------------------------------------------------------------*/
#define CHECK_MESSAGE_RECEIVED_INTERVAL 6

static int message_received = 0;
static struct broadcast_conn broadcast;
/*---------------------------------------------------------------------------*/
static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
  leds_on(LEDS_GREEN);
  uint8_t *recv_data = (uint8_t *)packetbuf_dataptr();

  // printf("broadcast message received from %d.%d: %u\n",
  //        from->u8[0], from->u8[1], (uint8_t)packetbuf_dataptr());
  printf("Message received: {%d.%u}\n",from->u8[0], recv_data[0]);
  
  uint8_t new_message_received = recv_data[0] + 1;
  packetbuf_copyfrom(&new_message_received, 1);
  broadcast_send(&broadcast);
  
  int node_direction = linkaddr_node_addr.u8[0];

  printf("Message sent: {%d.%u}\n", node_direction, new_message_received);
  
  message_received = 1;
  leds_off(LEDS_GREEN);
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(example_broadcast_process, ev, data)
{
  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  broadcast_open(&broadcast, 129, &broadcast_call);

  int node_direction = linkaddr_node_addr.u8[0];

  if(node_direction == 1){
    static uint8_t message = 1;

    while(1) {
      
      /* Timer set to CHECK_MESSAGE_RECEIVED_INTERVAL/2 */
      etimer_set(&et, CLOCK_SECOND * CHECK_MESSAGE_RECEIVED_INTERVAL/2);
      
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
      
      if(message_received == 1){
        printf("Message received, waiting %d seconds to check again\n", CHECK_MESSAGE_RECEIVED_INTERVAL/2);
        message_received = 0;
      }else{
        printf("Message not received within %d seconds\n", CHECK_MESSAGE_RECEIVED_INTERVAL);
        printf("Sending message: %u\n", message);
        packetbuf_copyfrom(&message, 1);
        broadcast_send(&broadcast);
        printf("broadcast message sent\n");
      }
    }
  }
    
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
