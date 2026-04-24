/**
 * \file
 *         Implementación de la librería auxiliar para la
 *         arquitectura de árbol basada en RSSI.
 *
 *         Incluye:
 *           - llenar_beacon()   : llena el struct beacon para broadcast.
 *           - printf_hello()    : saludo de depuración.
 *           - Serialize()       : serializa el árbol n-ario a string.
 *           - Deserialize()     : reconstruye el árbol desde un string.
 *           - Add_child()       : agrega un hijo al árbol n-ario.
 *           - Search_forwarder(): decide upstream (0) o downstream (ID).
 */

#include "tree_lib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
/*  Serialize()                                                              */
/*  Formato del string: "id:h1,h2,...;id:h1,h2,...;"                        */
/*  Cada nodo ocupa un segmento terminado en ';'.                           */
/*  Si no tiene hijos, el segmento es "id:;"                                */
/*---------------------------------------------------------------------------*/
uint8_t
Serialize(struct tree_node *tree, uint8_t n_nodes,
          char *buf, uint8_t buf_len)
{
  uint8_t i, j, pos = 0;
  int written;

  buf[0] = '\0';

  for(i = 0; i < n_nodes && pos < buf_len - 1; i++) {
    /* Escribe "id:" */
    written = snprintf(buf + pos, buf_len - pos, "%u:", tree[i].id);
    if(written < 0 || pos + written >= buf_len) break;
    pos += written;

    /* Escribe los hijos separados por ',' */
    for(j = 0; j < tree[i].num_children && pos < buf_len - 1; j++) {
      if(j > 0) {
        buf[pos++] = ',';
        buf[pos]   = '\0';
      }
      written = snprintf(buf + pos, buf_len - pos, "%u", tree[i].children[j]);
      if(written < 0 || pos + written >= buf_len) break;
      pos += written;
    }

    /* Termina el segmento con ';' */
    if(pos < buf_len - 1) {
      buf[pos++] = ';';
      buf[pos]   = '\0';
    }
  }

  return pos;
}

/*---------------------------------------------------------------------------*/
/*  Deserialize()                                                            */
/*  Parsea el string "id:h1,h2,...;id:...;" y rellena el arreglo tree[].   */
/*---------------------------------------------------------------------------*/
uint8_t
Deserialize(const char *buf, struct tree_node *tree, uint8_t max_n)
{
  uint8_t n = 0;
  const char *p = buf;

  while(*p != '\0' && n < max_n) {
    /* Lee el ID del nodo */
    uint8_t node_id = (uint8_t)atoi(p);

    /* Avanza hasta ':' */
    while(*p != ':' && *p != '\0') p++;
    if(*p == '\0') break;
    p++; /* salta ':' */

    /* Inicializa la entrada */
    tree[n].id           = node_id;
    tree[n].num_children = 0;

    /* Lee los hijos separados por ',' hasta encontrar ';' */
    while(*p != ';' && *p != '\0') {
      if(*p >= '0' && *p <= '9') {
        uint8_t child_id = (uint8_t)atoi(p);
        if(tree[n].num_children < MAX_CHILDREN) {
          tree[n].children[tree[n].num_children++] = child_id;
        }
        /* Avanza hasta ',' o ';' */
        while(*p != ',' && *p != ';' && *p != '\0') p++;
        if(*p == ',') p++; /* salta ',' */
      } else {
        p++;
      }
    }
    if(*p == ';') p++; /* salta ';' */

    n++;
  }

  return n;
}

/*---------------------------------------------------------------------------*/
/*  Add_child()                                                              */
/*  Busca parent_id en el árbol. Si no existe, lo crea.                    */
/*  Agrega child_id a su lista de hijos (sin duplicados).                  */
/*---------------------------------------------------------------------------*/
void
Add_child(struct tree_node *tree, uint8_t *n_nodes, uint8_t max_n,
          uint8_t parent_id, uint8_t child_id)
{
  uint8_t i, j;
  struct tree_node *node = NULL;

  /* Buscar el nodo padre */
  for(i = 0; i < *n_nodes; i++) {
    if(tree[i].id == parent_id) {
      node = &tree[i];
      break;
    }
  }

  /* Si no existe el padre, crearlo */
  if(node == NULL) {
    if(*n_nodes >= max_n) return; /* Sin espacio */
    node = &tree[*n_nodes];
    node->id           = parent_id;
    node->num_children = 0;
    (*n_nodes)++;
  }

  /* Verificar que el hijo no esté ya registrado */
  for(j = 0; j < node->num_children; j++) {
    if(node->children[j] == child_id) return; /* Ya existe */
  }

  /* Agregar el hijo */
  if(node->num_children < MAX_CHILDREN) {
    node->children[node->num_children++] = child_id;
  }
}

/*---------------------------------------------------------------------------*/
/*  Search_forwarder()                                                       */
/*  Hace una búsqueda BFS/DFS en el subárbol de cada hijo directo de my_id.*/
/*  Si dst_id está en el subárbol del hijo X, retorna X → downstream.      */
/*  Si no lo encuentra, retorna 0 → upstream.                              */
/*---------------------------------------------------------------------------*/

/* Función auxiliar: busca dst en el subárbol con raíz 'root_id'.
 * Retorna 1 si lo encuentra, 0 si no. */
static uint8_t
find_in_subtree(struct tree_node *tree, uint8_t n_nodes,
                uint8_t root_id, uint8_t dst_id)
{
  uint8_t i, j;

  if(root_id == dst_id) return 1;

  /* Buscar el nodo root_id en el árbol */
  for(i = 0; i < n_nodes; i++) {
    if(tree[i].id == root_id) {
      /* Buscar dst_id recursivamente en sus hijos */
      for(j = 0; j < tree[i].num_children; j++) {
        if(find_in_subtree(tree, n_nodes, tree[i].children[j], dst_id)) {
          return 1;
        }
      }
      break;
    }
  }
  return 0;
}

uint8_t
Search_forwarder(struct tree_node *tree, uint8_t n_nodes,
                 uint8_t my_id, uint8_t dst_id)
{
  uint8_t i, j;

  /* Buscar la entrada de my_id en el árbol */
  for(i = 0; i < n_nodes; i++) {
    if(tree[i].id == my_id) {
      /* Para cada hijo directo, verificar si dst_id está en su subárbol */
      for(j = 0; j < tree[i].num_children; j++) {
        uint8_t child = tree[i].children[j];
        if(find_in_subtree(tree, n_nodes, child, dst_id)) {
          return child; /* Downstream: reenviar hacia este hijo */
        }
      }
      break;
    }
  }

  /* No está en ningún subárbol propio → Upstream */
  return 0;
}
/*---------------------------------------------------------------------------*/




