# Funciones basicas contiki


## Inicio de procesos
```c
PROCESS_BEGIN()
```
**Que hace?**

Es el gestor de de hilos, 

---

## Terminacion de hilos
```c
PROCESS_END()
```
**Que hace?**

Mata cualquier hilo definido

---

## Suspender ejecucion actual
```c
PROCESS_YIELD()
```

**Que hace?**

Indica al S.O. que la ejecucion no esta haciendo nada, que aproveche el tiempo para hacer otras cosas


```c
PROCESS_EXIT()
```

```c
PROCESS_WAIT_EVENT_UNTIL()
```

```c
process.post()
```
---
# Identificadores de eventos (8 bits)
```c
if(ev=process_event_continue)
{
    algo...
} else if(ev = process_event_MSG)
{
    algo...
}else if(ev = process_event_timer)
{

}
```

`process_event_continue` = 133

`process_event_continue` = 134

`process_event_continue` = 136

