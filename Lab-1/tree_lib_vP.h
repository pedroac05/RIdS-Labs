/**
 * \file
 *         Librería auxiliar para la arquitectura de árbol basada en RSSI.
 *         Define las estructuras, constantes y funciones necesarias
 *         para el enrutamiento en árbol sobre Rime.
 */

#ifndef TREE_LIB_H_
#define TREE_LIB_H_

#include "contiki.h"
#include "net/rime/rime.h"

/*---------------------------------------------------------------------------*/
/*                          Constantes                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief Tipo de mensaje unicast: datos.
 *
 * Se usa con PACKETBUF_ATTR_UNICAST_TYPE para indicar que
 * el paquete unicast transporta datos de la aplicación.
 */
#define U_DATA    0

/**
 * \brief Tipo de mensaje unicast: control.
 *
 * Se usa con PACKETBUF_ATTR_UNICAST_TYPE para indicar que
 * el paquete unicast transporta información de control del árbol.
 */
#define U_CONTROL 1

/**
 * \brief Atributo custom de packetbuf para el tipo de unicast.
 *
 * Reutiliza PACKETBUF_ATTR_EPACKET_TYPE (disponible con Rime)
 * para almacenar U_DATA o U_CONTROL en la cabecera del paquete.
 */
#define PACKETBUF_ATTR_UNICAST_TYPE PACKETBUF_ATTR_EPACKET_TYPE

/*---------------------------------------------------------------------------*/
/*                          Estructuras                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief Mensaje beacon enviado por broadcast.
 *
 * Cada nodo envía periódicamente un beacon con su identidad y
 * la calidad acumulada de su enlace hacia la raíz del árbol.
 * Los vecinos usan esta información para elegir padre preferido.
 */
struct beacon {
  /** \brief Dirección Rime del nodo que originó el beacon. */
  linkaddr_t id;

  /**
   * \brief RSSI del camino (path RSSI).
   *
   * Representa la calidad acumulada del enlace desde este nodo
   * hasta la raíz. Los vecinos suman su propio RSSI medido
   * a este valor para obtener el RSSI acumulado total.
   */
  int16_t rssi_p;
};

/**
 * \brief Entrada en la tabla de padres preferidos.
 *
 * Cada entrada almacena la identidad de un vecino candidato
 * a padre y la calidad acumulada del enlace hacia la raíz
 * a través de ese vecino. Se gestiona con las macros
 * MEMB() y LIST() de Contiki.
 */
struct preferred_parent {
  /**
   * \brief Puntero al siguiente elemento en la lista enlazada.
   *
   * Este campo es requerido internamente por la macro LIST()
   * de Contiki. Debe ser el primer campo del struct.
   */
  struct preferred_parent *next;

  /** \brief Dirección Rime del vecino candidato a padre. */
  linkaddr_t id;

  /**
   * \brief RSSI acumulado a través de este padre.
   *
   * Se calcula como: rssi_a = beacon.rssi_p + RSSI_del_enlace_local.
   * Un valor más alto (menos negativo) indica un mejor camino
   * hacia la raíz.
   */
  int16_t rssi_a;
};

/*---------------------------------------------------------------------------*/
/*                          Funciones                                        */
/*---------------------------------------------------------------------------*/

/**
 * \brief Rellena un struct beacon con la información del nodo.
 *
 * Copia la dirección del nodo y el RSSI del camino en la
 * estructura beacon, dejándola lista para ser enviada
 * por broadcast con packetbuf_copyfrom().
 *
 * \param b      Puntero al beacon que se va a rellenar.
 *               No debe ser NULL.
 * \param id     Dirección Rime del nodo (normalmente
 *               linkaddr_node_addr).
 * \param rssi_p RSSI del camino hacia la raíz que este nodo
 *               quiere divulgar a sus vecinos.
 */
void llenar_beacon(struct beacon *b, linkaddr_t id, int16_t rssi_p);

/**
 * \brief Imprime un mensaje de depuración por consola serial.
 *
 * Función auxiliar que imprime un saludo para verificar
 * que la librería está correctamente enlazada y funcionando.
 */
void printf_hello(void);

#endif /* TREE_LIB_H_ */
