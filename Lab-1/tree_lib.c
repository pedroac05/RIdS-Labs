/**
 * \file
 *         Implementación de la librería auxiliar para la
 *         arquitectura de árbol basada en RSSI.
 */

#include "tree_lib.h"
#include <stdio.h>

/*---------------------------------------------------------------------------*/
void
llenar_beacon(struct beacon *b, linkaddr_t id, int16_t rssi_p)
{
  b->id     = id;
  b->rssi_p = rssi_p;
}
/*---------------------------------------------------------------------------*/
void
printf_hello(void)
{
  printf("tree_lib: hello from node %d.%d\n",
         linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
}
/*---------------------------------------------------------------------------*/
