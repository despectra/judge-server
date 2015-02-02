TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += thread_pool.c \
    server.c \
    main.c \
    utils.c \
    solutions_db.c

HEADERS += \
    thread_pool.h \
    server.h \
    utils.h \
    solutions_db.h \
    uthash.h \
    utlist.h

QMAKE_CFLAGS += -std=c99 -pthread $(mysql_config --cflags)
QMAKE_LFLAGS += $(mysql_config --libs)
LIBS += -pthread -lmysqlclient

