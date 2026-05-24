# Tutorial: Medición y Análisis del Consumo de Memoria en Contiki OS

En sistemas embebidos e Internet de las Cosas (IoT), los recursos de los nodos (microcontroladores) son extremadamente limitados. Este tutorial te explicará detalladamente cómo medir el consumo de memoria de tu firmware utilizando la herramienta de análisis de tamaño (`size`) y cómo interpretar sus resultados.

---

## 1. El comando `size` y sus variantes

El comando `size` es una utilidad que forma parte de la suite **GNU Binutils**. Analiza la cabecera del archivo ejecutable (formato ELF/objeto) y extrae el tamaño de cada sección de memoria.

Dependiendo de la arquitectura del nodo para el que estés compilando, utilizarás un comando diferente:

| Comando | Plataforma de Ejemplo | Arquitectura / Chip |
|---|---|---|
| **`arm-none-eabi-size`** | `remote` (Zolertia RE-Mote) | ARM Cortex-M3 (CC2538) |
| **`msp430-size`** | `sky` (TelosB / Tmote Sky) | MSP430 (MSP430F1611) |
| **`size`** | `native` / `cooja` | Nativo (x86 / x64 de la PC) |

Todos estos comandos funcionan exactamente igual y devuelven la misma estructura de información.

---

## 2. Cómo medir el tamaño en tu proyecto

Una vez que compiles tu programa para tu plataforma (por ejemplo, `remote`), se generará un archivo con la extensión de la plataforma o `.elf` en tu carpeta de origen.

Para medir su consumo de memoria, ejecuta el comando de tamaño apuntando a tu ejecutable. En tu entorno Docker de Contiki, puedes hacerlo de la siguiente manera:

```bash
arm-none-eabi-size src/Lab3/tree_rssi_remote.remote
```

### Salida Obtenida:
```text
   text	   data	    bss	    dec	    hex	filename
  26200	   1395	   7462	  35057	   88f1	src/Lab3/tree_rssi_remote.remote
```

---

## 3. ¿Qué significa cada columna?

El comando divide el software en tres regiones principales de memoria: **`text`**, **`data`** y **`bss`**.

### 💾 `text` (Código / Instrucciones)
* **¿Qué es?**: Contiene el código binario de las instrucciones de la CPU compiladas y las constantes de solo lectura (`const` en C, textos estáticos entre comillas como `"DATA RECV from..."`, etc.).
* **¿Dónde se almacena?**: En la memoria **Flash (ROM)** del microcontrolador. No es volátil y permanece tras apagar el nodo.

### 💾 `data` (Datos Inicializados)
* **¿Qué es?**: Contiene variables globales y estáticas que tienen un valor inicial asignado en el código (por ejemplo, `int x = 42;`).
* **¿Dónde se almacena?**: Ocupa **Flash (ROM)** (para almacenar el valor de inicio `42` cuando el nodo está apagado) **Y** ocupa **RAM (SRAM)** en tiempo de ejecución (para que el procesador pueda leer y reescribir su valor durante el funcionamiento). Al encender el nodo, la rutina de inicio copia estos valores desde Flash a RAM.

### 💾 `bss` (Block Started by Symbol / Datos no Inicializados)
* **¿Qué es?**: Contiene todas las variables globales y estáticas que **no** fueron inicializadas por ti, o que se inicializaron a cero (por ejemplo, `int y;` o `static char buffer[100];`).
* **¿Dónde se almacena?**: Ocupa **únicamente memoria RAM (SRAM)**. No consume espacio en Flash porque el microcontrolador simplemente limpia (pone a cero) esa región de la RAM durante la fase de arranque (bootloader).

### 🔢 `dec` y `hex`
* Representan la suma aritmética simple de las tres secciones (`text + data + bss`).
  * En decimal (`dec`): $26200 + 1395 + 7462 = 35057$ bytes.
  * En hexadecimal (`hex`): `0x88f1`.

---

## 4. Fórmulas para calcular el consumo de hardware

Para evaluar si tu programa cabe en el microcontrolador físico, debes realizar los siguientes cálculos básicos:

### ⚡ 1. Consumo total de memoria Flash (ROM)
La memoria Flash guarda todo el programa y los valores de inicialización de las variables.
$$\text{Flash Total} = \text{text} + \text{data}$$

* **Para tu ejecutable actual**:
  $$\text{Flash} = 26200\text{ B} + 1395\text{ B} = 27595\text{ B} \approx 27.6\text{ KB}$$

### ⚡ 2. Consumo total de memoria RAM (Estático)
Esta es la cantidad de memoria RAM reservada permanentemente desde el inicio de la ejecución.
$$\text{RAM Estática} = \text{data} + \text{bss}$$

* **Para tu ejecutable actual**:
  $$\text{RAM Estática} = 1395\text{ B} + 7462\text{ B} = 8857\text{ B} \approx 8.85\text{ KB}$$

---

## 5. El gran secreto: RAM Estática vs. RAM Dinámica (El Stack)

> [!WARNING]
> La memoria RAM total real utilizada en ejecución **es mayor** que `data + bss`.

La memoria RAM de un nodo se divide en tres partes:
1. **Región estática (`data` + `bss`)**: Crecimiento de abajo hacia arriba en la memoria.
2. **El Heap (Montículo)**: Para asignación dinámica con `malloc()`. *Nota: Contiki OS prácticamente prohíbe el uso de malloc por fragmentación, usando en su lugar gestores estáticos como `memb` y `list` que se sitúan en `.bss`.*
3. **El Stack (Pila)**: Crece de arriba hacia abajo en la RAM. Almacena:
   - Las variables locales declaradas dentro de funciones (ej. `int i;` dentro de un bucle `for`).
   - Las direcciones de retorno de funciones.
   - Parámetros de llamadas y registros guardados durante interrupciones.

Si tu **RAM Estática** es demasiado grande, quedará poco espacio para el Stack. Cuando el Stack crece tanto que llega a solaparse con la región `.bss` o `.data`, ocurre una **corrupción de memoria (Stack Overflow)**, lo que causa reinicios inesperados del nodo o comportamientos extraños. Por regla general de seguridad, se recomienda dejar al menos un **15% - 20%** de la RAM libre para el crecimiento dinámico del Stack.

---

## 6. Comparación con límites físicos de Hardware

Veamos cómo se comparan los requerimientos de tu código actual con las dos plataformas de hardware más comunes en Contiki:

### Caso A: Zolertia RE-Mote (`TARGET=remote` / CC2538)
* **Límites físicos**: **512 KB** de Flash / **32 KB** de RAM.
* **Tus consumos**: ~27.6 KB Flash / ~8.85 KB RAM.
* **Uso porcentual**:
  - **Flash**: $\frac{27.6}{512} \times 100 \approx 5.4\%$ (¡Sobradísimo espacio!).
  - **RAM Estática**: $\frac{8.85}{32} \times 100 \approx 27.6\%$ (Excelente, queda más del 70% libre para el Stack).

### Caso B: TelosB / Tmote Sky (`TARGET=sky` / MSP430)
* **Límites físicos**: **48 KB** de Flash / **10 KB** de RAM.
* **Tus consumos**: ~27.6 KB Flash / ~8.85 KB RAM.
* **Uso porcentual**:
  - **Flash**: $\frac{27.6}{48} \times 100 \approx 57.5\%$ (Entra sin problemas, pero ya ocupa más de la mitad).
  - **RAM Estática**: $\frac{8.85}{10} \times 100 \approx 88.5\%$ (¡Alerta Crítica! Solo queda $1.15\text{ KB}$ de RAM para el Stack, lo cual es muy arriesgado y podría causar Stack Overflow bajo carga de red).

---

## 7. Consejos prácticos para reducir el consumo de memoria
Si necesitas liberar espacio en tus nodos:
* **Para reducir Flash (`text`)**:
  - Evita incluir librerías de `printf` muy pesadas si no son necesarias.
  - Elimina funciones y código muerto no utilizado.
  - Compila con optimización de tamaño (el flag `-Os` de GCC, que ya está activado por defecto en Contiki).
* **Para reducir RAM (`data` + `bss`)**:
  - Reduce el tamaño de buffers globales temporales (ej. arrays de caracteres para enviar mensajes).
  - Configura tamaños máximos de colas menores en `project-conf.h` (ej. reducir `QUEUEBUF_CONF_NUM`).
  - Utiliza tipos de datos más pequeños cuando sea posible (ej. `uint8_t` en lugar de `int`).
