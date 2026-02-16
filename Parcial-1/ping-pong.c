#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"

#include "dev/button-sensor.h"

#include "dev/leds.h"

#include <stdio.h>
/*---------------------------------------------------------------------------*/
PROCESS(example_broadcast_process, "Broadcast example");
AUTOSTART_PROCESSES(&example_broadcast_process);
/*---------------------------------------------------------------------------*/
#define CHECK_MESSAGE_RECEIVED_INTERVAL 6

static bool message_received = false;
/*---------------------------------------------------------------------------*/
static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
  leds_on(LEDS_GREEN);
  static uint8_t *message_received = (uint8_t *)packetbuf_dataptr();

  // printf("broadcast message received from %d.%d: %u\n",
  //        from->u8[0], from->u8[1], (uint8_t)packetbuf_dataptr());
  printf("Message received: {%d.%u}\n",from->u8[0], message_received[0]);
  
  static uint8_t new_message_received = message_received[0] + 1;
  packetbuf_copyfrom(&new_message_received, 1);
  broadcast_send(&broadcast);
  
  printf("Message sent: {%d.%u}\n",NODEID->u8[0], new_message_received);
  
  message_received = true;
  leds_off(LEDS_GREEN);
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(example_broadcast_process, ev, data)
{
  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  broadcast_open(&broadcast, 129, &broadcast_call);

  if(NODEID->u8[0] == 1){
    static uint8_t message = 1;

    while(1) {
      
      /* Timer set to CHECK_MESSAGE_RECEIVED_INTERVAL/2 */
      etimer_set(&et, CLOCK_SECOND * CHECK_MESSAGE_RECEIVED_INTERVAL/2);
      
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
      
      if(message_received){
        printf("Message received, waiting %d seconds to check again\n", CHECK_MESSAGE_RECEIVED_INTERVAL/2);
        message_received = false;
      }else{
        printf("Message not received within %d seconds\n", CHECK_MESSAGE_RECEIVED_INTERVAL);
        printf("Sending message: %s\n", message);
        packetbuf_copyfrom(&message, 1);
        broadcast_send(&broadcast);
        printf("broadcast message sent\n");
      }
    }
  }
    
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
