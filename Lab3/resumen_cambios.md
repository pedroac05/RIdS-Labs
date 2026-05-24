# Resumen de Cambios Realizados al Código

Este documento contiene un resumen y explicación técnica detallada de todos los cambios implementados el día de hoy en el proyecto del **Lab 3** (`tree_rssi_remote.c` y archivos auxiliares) para optimizar el enrutamiento, medir el consumo de recursos y calcular estadísticas de red.

---

## 1. Algoritmo Trickle para el Envío de Beacons
* **Ubicación**: Thread `broadcast_rssi` en [tree_rssi_remote.c](tree_rssi_remote.c#L289-L318)
* **Descripción**: Se implementó una lógica de intervalo dinámico para el envío de beacons. En lugar de transmitir con un período constante, el intervalo inicia en $125 \text{ ms}$ (`TRICKLE_MIN`) y se duplica tras cada envío exitoso hasta alcanzar un límite máximo de $4 \text{ s}$ (`TRICKLE_MAX`).
* **Justificación**: En redes estables, no es necesario enviar beacons frecuentemente, lo cual satura el canal y gasta batería. Duplicar el intervalo disminuye la sobrecarga de red exponencialmente cuando el árbol de enrutamiento se mantiene estable.

---

## 2. Temporizadores de Keepalive en Padres y Reseteo de Trickle
* **Ubicación**: 
  - Estructura `preferred_parent` en [tree_lib.h](tree_lib.h#L91-L97)
  - Callbacks `register_parent` y `keepalive_expired` en [tree_rssi_remote.c](tree_rssi_remote.c#L97-L133)
* **Descripción**: 
  - Se agregó un `struct ctimer keepalive` dentro de la estructura de cada padre.
  - Al recibir un beacon de un padre, se (re)inicia un temporizador de $5 \text{ segundos}$.
  - Si el temporizador expira sin recibir actualizaciones (lo que indica que el padre ya no está disponible o el enlace falló), se ejecuta la función de callback `keepalive_expired`, la cual baja inmediatamente el intervalo Trickle al mínimo (`TRICKLE_MIN`).
* **Justificación**: Permite una respuesta inmediata ante fallos de enlaces o caídas de nodos. Al bajar el intervalo Trickle al mínimo, el nodo y sus vecinos envían beacons de forma muy frecuente para reconfigurar rápidamente el árbol y encontrar un nuevo camino óptimo.

---

## 3. Numeración de Secuencia en Mensajes de Datos
* **Ubicación**: Thread `generate_pkt_dst` en [tree_rssi_remote.c](tree_rssi_remote.c#L502-L529)
* **Descripción**: Se implementó un contador estático `seq_num` en el nodo emisor (`ORIGEN = 8`). Cada paquete enviado incrementa este número de secuencia y se empaqueta con el formato de mensaje `"SEQ:<secuencia>:<origen>:Hi"`.
* **Justificación**: Proporciona un identificador único por mensaje de datos. Esto es fundamental para que el receptor o sumidero (Sink) conozca cuántos paquetes han sido enviados en total por cada nodo y pueda detectar pérdidas.

---

## 4. Tabla de Estadísticas de Pérdida de Paquetes (en el Sink)
* **Ubicación**:
  - Estructura y tabla `pkt_stats` en [tree_rssi_remote.c](tree_rssi_remote.c#L31-L38)
  - Callback `recv_uc` en [tree_rssi_remote.c](tree_rssi_remote.c#L198-L244)
  - Thread `print_parent_list` en [tree_rssi_remote.c](tree_rssi_remote.c#L383-L399)
* **Descripción**:
  - **Recepción**: Cuando el Sink (`id == 1`) recibe un mensaje unicast de datos, extrae el origen (`src`) y el número de secuencia (`seq`) del payload. Si es la primera vez que ve al nodo, lo registra; si no, incrementa el contador de recibidos y actualiza el número máximo de secuencia visto (`seq_max`), el cual representa el total de paquetes transmitidos por ese emisor.
  - **Impresión**: Cada 10 segundos, junto con la tabla de padres, el Sink calcula los paquetes perdidos (`seq_max - recibidos`) y muestra en pantalla la tabla en el siguiente formato:
    ```text
    --- PACKET LOSS TABLE (Sink) ---
    Source | Received | Sent(seq) | Loss %
      8    |     14   |     15    |    6%
    --------------------------------
    ```
* **Justificación**: Mide de manera cuantitativa el rendimiento de la red y el impacto de los cambios sobre la tasa de entrega (PDR) del árbol de enrutamiento.

---

## 5. Activación de Powertrace para Perfilado de Energía
* **Ubicación**: Thread `select_prefered_parent` en [tree_rssi_remote.c](tree_rssi_remote.c#L345-L346)
* **Descripción**: Se añadió la llamada a `powertrace_start(CLOCK_SECOND * 10)` al inicio del nodo. Esto activa el módulo Powertrace que imprime logs seriales periódicos con el número de ciclos de reloj que el microcontrolador pasa en estado activo, bajo consumo (LPM), transmisión (TX) y escucha (RX).
* **Justificación**: Permite recolectar métricas en tiempo real para calcular la potencia promedio de la red y el impacto energético de los mecanismos de optimización.

---

## 6. Corrección de Compilación para la plataforma `remote`
* **Ubicación**: Declaraciones en [tree_rssi_remote.c](tree_rssi_remote.c#L89-L95)
* **Descripción**: 
  - Se restauró la definición de la función auxiliar `blink_blue()` utilizando `clock_delay_usec(10000)`.
  - Se configuró la compilación usando `TARGET=remote` (ya que la plataforma `sky` no está configurada en esta instalación del contenedor).
* **Justificación**: Evita errores del enlazador (`undefined reference to blink_blue`) puesto que múltiples partes del hilo de envío de datos y recepción la utilizan para visualizar físicamente el flujo de los paquetes en simulación o hardware.
