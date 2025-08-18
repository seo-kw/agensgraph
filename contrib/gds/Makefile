MODULE_big = gds

OBJS = gds.o

EXTENSION = gds
DATA = gds--0.1.0.sql

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
