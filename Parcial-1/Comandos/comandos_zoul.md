# Guía de Comandos para Zoul (Contiki)

A continuación se presentan los comandos necesarios para limpiar, compilar, identificar y cargar el firmware en los nodos Zoul, ordenados según su flujo de trabajo habitual.

## 1. Limpiar el entorno (Opcional pero recomendado)
```bash
make TARGET=zoul clean
```
**Descripción:**  
Elimina todos los archivos objeto (`.o`), binarios y ejecutables generados en compilaciones anteriores.  
**Función:**  
Garantiza que la próxima compilación se realice desde cero ("clean build"), evitando errores causados por archivos antiguos o corruptos. Es útil ejecutarlo antes de compilar si has hecho cambios importantes o si cambias de plataforma.

---

## 2. Compilar el proyecto
```bash
make TARGET=zoul example-broadcast
```
**Descripción:**  
Compila el código fuente `example-broadcast.c` (y sus dependencias) específicamente para la plataforma de hardware `zoul`.  
**Función:**  
Genera el archivo binario (firmware) listo para ser cargado en el microcontrolador. Verifica que no haya errores de sintaxis en tu código.

---

## 3. Identificar el puerto del dispositivo
```bash
ls /dev/ttyUSB*
```
**Descripción:**  
Lista todos los dispositivos conectados que se identifican como puertos serie USB en un sistema tipo Unix/Linux/WSL.  
**Función:**  
Te permite saber en qué puerto está conectado tu mote Zoul (por ejemplo, `/dev/ttyUSB0`, `/dev/ttyUSB1`, etc.) para poder especificarlo en el comando de carga.

---

## 4. Cargar (flasheár) el firmware al mote
```bash
make NODEID=0x0100 TARGET=zoul MOTES=/dev/ttyUSB0 example-broadcast.upload
```
**Descripción:**  
Sube el programa compilado al hardware físico conectado en el puerto especificado.
*   `NODEID=0x0100`: Asigna una dirección lógica o ID al nodo (opcional, depende de la aplicación, pero útil en Rime/IPv6 para identificar al nodo).
*   `TARGET=zoul`: Especifica la plataforma.
*   `MOTES=/dev/ttyUSB0`: Indica el puerto USB donde está conectado el dispositivo (debes cambiar `/dev/ttyUSB0` por el que hayas encontrado en el paso 3).
*   `.upload`: Es la regla del Makefile que inicia el proceso de flasheo.

**Función:**  
Transfiere tu programa desde el ordenador a la memoria del microcontrolador para que empiece a ejecutarse.
