QT += core gui widgets

TARGET   = subsettest
TEMPLATE = app
CONFIG  += c++17

SOURCES += \
    main.cpp \
    subsettestwindow.cpp

HEADERS += \
    subsettestwindow.h

PARENT = ../..

SOURCES += \
    $$PARENT/openscadparser.cpp \
    $$PARENT/scenedocument.cpp \
    $$PARENT/scenemesh.cpp \
    $$PARENT/scenetree.cpp

HEADERS += \
    $$PARENT/expression.h \
    $$PARENT/scenestringutils.h \
    $$PARENT/shapenode.h \
    $$PARENT/scenetree.h \
    $$PARENT/scenedocument.h \
    $$PARENT/scenemesh.h \
    $$PARENT/openscadparser.h

INCLUDEPATH += $$PARENT

# Embed path to test files at compile time
DEFINES += TESTCODES_DIR=\\\"$$PWD/testcodes/\\\"

win32 {
    DEFINES += UNICODE _UNICODE WIN32 MINGW_HAS_SECURE_API=1
}
