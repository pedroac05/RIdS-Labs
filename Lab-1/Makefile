
CONTIKI = CONTIKI = ../..

all: example-abc example-mesh example-collect example-trickle example-polite \
     example-rudolph1 example-rudolph2 example-rucb \
     example-runicast example-unicast example-neighbors \
     tree-architecture

// MY_TX_POWER_DBM = 7

// $(info MY_TX_POWER_DBM is ${MY_TX_POWER_DBM})

// CFLAGS+=-DPROJECT_CONF_H=\"project-conf.h\"
// CFLAGS += -DMY_TX_POWER_DBM=${MY_TX_POWER_DBM}

PROJECT_SOURCEFILES += tree_lib.c

APPS += powertrace

CONTIKI_WITH_RIME = 1

include $(CONTIKI)/Makefile.include
