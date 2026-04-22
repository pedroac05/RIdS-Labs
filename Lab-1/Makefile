CONTIKI = ../..

all: tree_rssi_remote

# MY_TX_POWER_DBM = 7
# $(info MY_TX_POWER_DBM is ${MY_TX_POWER_DBM})
# CFLAGS += -DMY_TX_POWER_DBM=${MY_TX_POWER_DBM}

CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

PROJECT_SOURCEFILES += tree_lib.c

APPS += powertrace

CONTIKI_WITH_RIME = 1

include $(CONTIKI)/Makefile.include