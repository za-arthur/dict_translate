# contrib/dict_translate/Makefile

MODULE_big = dict_translate
OBJS = dict_translate.o $(WIN32RES)

EXTENSION = dict_translate
DATA = dict_translate--1.0.sql
# DATA_TSEARCH = dict_translate_sample.trn
PGFILEDESC = "dict_translate - text search dictionary template for translation"

REGRESS = dict_translate

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/dict_translate
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
