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
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/*                          Constantes                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief Tipo de mensaje unicast: datos (punto a punto).
 *
 * Se usa con PACKETBUF_ATTR_UNICAST_TYPE para indicar que
 * el paquete unicast transporta datos de la aplicación.
 */
#define U_DATA    0

/**
 * \brief Tipo de mensaje unicast: control (actualización de tabla de rutas).
 *
 * Se usa con PACKETBUF_ATTR_UNICAST_TYPE para indicar que
 * el paquete unicast transporta información de control del árbol
 * (tabla de enrutamiento serializada).
 */
#define U_CONTROL 1

/**
 * \brief Atributo custom de packetbuf para el tipo de unicast.
 *
 * Reutiliza PACKETBUF_ATTR_EPACKET_TYPE (disponible con Rime)
 * para almacenar U_DATA o U_CONTROL en la cabecera del paquete.
 */
#define PACKETBUF_ATTR_UNICAST_TYPE PACKETBUF_ATTR_EPACKET_TYPE

/**
 * \brief Número máximo de hijos en el árbol n-ario.
 */
#define MAX_CHILDREN 8

/**
 * \brief Tamaño máximo del buffer de serialización del árbol.
 */
#define TREE_SERIAL_BUF 128

/**
 * \brief Identificador del nodo origen para la generación de paquetes.
 */
#define ORIGEN  8

/**
 * \brief Identificador del nodo destino para la generación de paquetes.
 */
#define DESTINO 17

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
   struct preferred_parent *next;
   linkaddr_t id;
   int16_t rssi_p;     /* Path RSSI acumulado hacia la raíz por este vecino */
   int8_t rssi_a;      /* RSSI del enlace local directo hacia este vecino   */
 };

/**
 * \brief Nodo del árbol n-ario de enrutamiento.
 *
 * Cada nodo guarda el ID del mote que representa y un arreglo
 * de IDs de sus hijos directos. Se usa para construir la
 * tabla de enrutamiento downstream.
 */
struct tree_node {
  uint8_t id;                       /* ID (u8[0]) del mote representado     */
  uint8_t children[MAX_CHILDREN];   /* IDs de los hijos directos            */
  uint8_t num_children;             /* Número de hijos registrados          */
};

/**
 * \brief Entrada en la lista ligada de paquetes pendientes de enrutar.
 *
 * Contiene el nodo destino y el mensaje a entregar.
 * Se gestiona con las macros MEMB() y LIST() de Contiki.
 */
struct pkt_entry {
  struct pkt_entry *next;
  uint8_t dst;           /* ID del nodo destino                            */
  char    msg[32];       /* Payload del paquete                            */
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
 * \param id     Dirección Rime del nodo.
 * \param rssi_p RSSI del camino hacia la raíz.
 */
void llenar_beacon(struct beacon *b, linkaddr_t id, int16_t rssi_p);

/**
 * \brief Imprime un mensaje de depuración por consola serial.
 */
void printf_hello(void);

/**
 * \brief Serializa el árbol n-ario local en una cadena de caracteres.
 *
 * Convierte el arreglo de nodos del árbol en un string compacto
 * con formato "id:h1,h2,...;id:h1,...;" para poder transmitirlo
 * por unicast a los vecinos.
 *
 * \param tree     Arreglo de nodos del árbol.
 * \param n_nodes  Número de nodos en el arreglo.
 * \param buf      Buffer de salida.
 * \param buf_len  Longitud máxima del buffer.
 * \return Longitud del string resultante (sin '\0').
 */
uint8_t Serialize(struct tree_node *tree, uint8_t n_nodes,
                  char *buf, uint8_t buf_len);

/**
 * \brief Reconstruye el árbol n-ario desde una cadena serializada.
 *
 * Parsea el string recibido por unicast (con formato generado por
 * Serialize) y rellena el arreglo de nodos.
 *
 * \param buf      String serializado recibido.
 * \param tree     Arreglo de nodos donde se almacenará el resultado.
 * \param max_n    Capacidad máxima del arreglo.
 * \return Número de nodos reconstruidos.
 */
uint8_t Deserialize(const char *buf, struct tree_node *tree, uint8_t max_n);

/**
 * \brief Agrega un nodo hijo al árbol n-ario local.
 *
 * Busca el nodo padre en el arreglo y le añade el hijo indicado.
 * Si el padre no existe, crea un nodo nuevo para él.
 * Si el hijo ya está registrado, no hace nada.
 *
 * \param tree      Arreglo de nodos del árbol.
 * \param n_nodes   Puntero al contador de nodos (se incrementa si se crea uno).
 * \param max_n     Capacidad máxima del arreglo.
 * \param parent_id ID del nodo padre.
 * \param child_id  ID del nodo hijo a agregar.
 */
void Add_child(struct tree_node *tree, uint8_t *n_nodes, uint8_t max_n,
               uint8_t parent_id, uint8_t child_id);

/**
 * \brief Determina el siguiente salto para llegar al destino.
 *
 * Implementa la lógica de decisión upstream/downstream:
 * - Si el destino es un ancestro (o desconocido), retorna 0 → upstream.
 * - Si el destino está en algún subárbol hijo, retorna el ID
 *   del hijo directo que encamina hacia ese destino → downstream.
 *
 * \param tree      Arreglo de nodos del árbol.
 * \param n_nodes   Número de nodos en el arreglo.
 * \param my_id     ID del nodo actual.
 * \param dst_id    ID del nodo destino.
 * \return 0 si se debe enviar upstream, o el ID del hijo hacia el destino.
 */
uint8_t Search_forwarder(struct tree_node *tree, uint8_t n_nodes,
                         uint8_t my_id, uint8_t dst_id);

#endif /* TREE_LIB_H_ */
