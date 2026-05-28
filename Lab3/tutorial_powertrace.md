# Tutorial: Calcular Potencia Promedio de Red con Powertrace

## 1. ¿Qué es Powertrace?

Powertrace es un módulo de Contiki que utiliza **Energest** para rastrear cuánto tiempo pasa cada nodo en cada estado de consumo de energía (CPU activo, bajo consumo, transmitiendo, escuchando). Imprime estos datos periódicamente por `printf()`.

## 2. Activación

Ya está activado en tu código con:

```c
powertrace_start(CLOCK_SECOND * 10);  // imprime cada 10 segundos
```

Requisitos (ya configurados):
- `APPS += powertrace` en el Makefile
- `#include "powertrace.h"` en el `.c`

## 3. Formato de Salida

Cada 10 segundos, **cada nodo** imprime una línea con este formato:

```
<str> <clock> P <node.0> <seqno> <all_cpu> <all_lpm> <all_tx> <all_rx> <all_idle_tx> <all_idle_rx> <cpu> <lpm> <tx> <rx> <idle_tx> <idle_rx> (radio X.XX% / X.XX% ...)
```

### Campos importantes

| # | Campo | Descripción |
|---|-------|-------------|
| 5 | `all_cpu` | Ticks **acumulados** en CPU activo (desde el inicio) |
| 6 | `all_lpm` | Ticks **acumulados** en Low Power Mode |
| 7 | `all_tx` | Ticks **acumulados** transmitiendo (radio TX) |
| 8 | `all_rx` | Ticks **acumulados** escuchando (radio RX/listen) |
| 12 | `cpu` | Ticks en CPU activo **en el último período** (10 s) |
| 13 | `lpm` | Ticks en LPM **en el último período** |
| 14 | `tx` | Ticks transmitiendo **en el último período** |
| 15 | `rx` | Ticks escuchando **en el último período** |

> [!NOTE]
> Los ticks están en unidades de **RTIMER_SECOND** (típicamente 32768 ticks/segundo).

## 4. Ejemplo de Línea Real

```
 327680 P 3.0 5 512803 14227588 153188 195436 0 0 18716 308591 5171 8411 0 0 (radio 2.36% / 4.15% ...)
```

Valores del **último período** (los que usaremos):
- `cpu` = 18716
- `lpm` = 308591
- `tx` = 5171
- `rx` = 8411

## 5. Fórmulas de Cálculo

### Paso 1: Obtener el tiempo total del período

```
tiempo_total = cpu + lpm
```

> [!IMPORTANT]
> `cpu + lpm` = tiempo total porque el procesador siempre está en uno de los dos estados.

### Paso 2: Corrientes por estado (dependen de la plataforma)

#### Plataformas comunes en Contiki

| Estado | CC2538 (Zoul/RE-Mote) | CC2420 (Sky/Z1) | Unidad |
|--------|----------------------|-----------------|--------|
| CPU activo (`I_cpu`) | 13.0 | 1.8 | mA |
| LPM (`I_lpm`) | 0.0006 | 0.0545 | mA |
| TX (`I_tx`) | 24.0 | 17.4 | mA |
| RX/Listen (`I_rx`) | 20.0 | 18.8 | mA |
| Voltaje (`V`) | 3.3 | 3.0 | V |

> [!TIP]
> Si estás usando **Cooja con motes Sky/Z1**, usa la columna CC2420.
> Si usas **Zoul/RE-Mote** real, usa la columna CC2538.

### Paso 3: Calcular corriente promedio del nodo

```
I_promedio = (cpu × I_cpu + lpm × I_lpm + tx × I_tx + rx × I_rx) / tiempo_total
```

### Paso 4: Calcular potencia del nodo

```
P_nodo = V × I_promedio    [en mW]
```

### Paso 5: Calcular potencia promedio de la red

```
P_red = (P_nodo1 + P_nodo2 + ... + P_nodoN) / N
```

## 6. Ejemplo Numérico Completo

### Datos del período (un nodo CC2538 Zoul):

```
cpu = 18716    lpm = 308591    tx = 5171    rx = 8411
```

### Cálculo:

```
tiempo_total = 18716 + 308591 = 327307

I_promedio = (18716 × 13.0 + 308591 × 0.0006 + 5171 × 24.0 + 8411 × 20.0) / 327307
           = (243308 + 185.15 + 124104 + 168220) / 327307
           = 535817.15 / 327307
           = 1.637 mA

P_nodo = 3.3 V × 1.637 mA = 5.40 mW
```

### Red de 5 nodos:

| Nodo | cpu | lpm | tx | rx | I_avg (mA) | P (mW) |
|------|-----|-----|-----|-----|-----------|---------|
| 1 (sink) | 18716 | 308591 | 5171 | 8411 | 1.637 | 5.40 |
| 2 | 15200 | 312107 | 3800 | 7100 | 1.342 | 4.43 |
| 3 | 16500 | 310807 | 4200 | 6500 | 1.387 | 4.58 |
| 4 | 14000 | 313307 | 3500 | 5800 | 1.229 | 4.05 |
| 5 | 17800 | 309507 | 4900 | 9200 | 1.617 | 5.34 |

```
P_red = (5.40 + 4.43 + 4.58 + 4.05 + 5.34) / 5 = 4.76 mW
```

## 7. Resumen Rápido (Fórmula Única)

Para cada nodo, en cada período de powertrace:

```
P_nodo (mW) = V × (cpu × I_cpu + lpm × I_lpm + tx × I_tx + rx × I_rx)
              ────────────────────────────────────────────────────────
                                  (cpu + lpm)
```

Luego promediar entre todos los nodos:

```
P_red_promedio = Σ P_nodo / N
```

> [!TIP]
> Si solo necesitas **comparar** configuraciones (ej. trickle vs. sin trickle), el **porcentaje de radio** que ya imprime powertrace al final de cada línea es suficiente. Solo necesitas la fórmula completa si te piden el consumo en **mW absolutos**.

## 8. Cómo Recoger los Datos

### En Cooja
1. Abrir **Tools → Mote output**
2. Ejecutar la simulación al menos 2-3 minutos
3. Filtrar las líneas que contienen `" P "` (esas son las de powertrace)
4. Copiar/exportar a un archivo de texto
5. Procesar con Excel, Python, o manualmente

### En hardware real
1. Conectar la tarjeta por USB
2. Abrir monitor serial (115200 baud)
3. Registrar las líneas `P` de cada nodo

> [!WARNING]
> Ignora la **primera línea** de powertrace de cada nodo — los valores del "período" serán iguales a los acumulados porque no hay período anterior con el que comparar.
