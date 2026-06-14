QT += core gui widgets opengl concurrent

TARGET   = treedebug
TEMPLATE = app
CONFIG  += c++17

# ── Sources from this sub-project ────────────────────────────────────────────
SOURCES += \
    main.cpp \
    treedebugwindow.cpp \
    nodethumbnailcache_stub.cpp \
    groupthumbnailcache_stub.cpp

HEADERS += \
    treedebugwindow.h

# ── Shared scene tree sources from parent project ────────────────────────────
PARENT = ../..

SOURCES += \
    $$PARENT/scenedocument.cpp \
    $$PARENT/scenemesh.cpp \
    $$PARENT/scenetree.cpp \
    $$PARENT/scenetreegraphicshelpers.cpp \
    $$PARENT/scenetreegraphicswidget.cpp \
    $$PARENT/scenetreegraphicswidget_canvasdrag.cpp \
    $$PARENT/scenetreeinlinetextinput.cpp \
    $$PARENT/scenetreelayout.cpp \
    $$PARENT/scenetreenoderenderer.cpp \
    $$PARENT/scenetreepalette.cpp \
    $$PARENT/scenetreepreviewrenderer.cpp \
    $$PARENT/scenetreetoolbarrenderer.cpp

HEADERS += \
    $$PARENT/expression.h \
    $$PARENT/groupthumbnailcache.h \
    $$PARENT/nodethumbnailcache.h \
    $$PARENT/scenedocument.h \
    $$PARENT/scenemesh.h \
    $$PARENT/scenetree.h \
    $$PARENT/scenetreegraphicshelpers.h \
    $$PARENT/scenetreegraphicswidget.h \
    $$PARENT/scenetreeinlinetextinput.h \
    $$PARENT/scenetreelayout.h \
    $$PARENT/scenetreenoderenderer.h \
    $$PARENT/scenetreepalette.h \
    $$PARENT/scenetreepreviewrenderer.h \
    $$PARENT/scenetreetoolbarrenderer.h \
    $$PARENT/shapenode.h

INCLUDEPATH += $$PARENT

# ── Platform ──────────────────────────────────────────────────────────────────
win32 {
    DEFINES += UNICODE _UNICODE WIN32 MINGW_HAS_SECURE_API=1
    LIBS += -lopengl32
}
