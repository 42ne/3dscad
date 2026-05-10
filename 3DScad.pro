
QT += core gui widgets opengl

CONFIG += c++17

win32:LIBS += -lopengl32


# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    csgevaluator.cpp \
    main.cpp \
    mainwindow.cpp \
    manifoldcsg.cpp \
    openscadgenerator.cpp \
    openscadparser.cpp \
    scenecommands.cpp \
    scenebooleantree.cpp \
    scenemesh.cpp \
    scenedocument.cpp \
    viewportwidget.cpp

HEADERS += \
    csgevaluator.h \
    mainwindow.h \
    manifoldcsg.h \
    openscadgenerator.h \
    openscadparser.h \
    scenecommands.h \
    scenebooleantree.h \
    scenemesh.h \
    scenedocument.h \
    shapenode.h \
    viewportwidget.h

FORMS += \
    mainwindow.ui

exists(build/manifold-build/src/libmanifold.a) {
    DEFINES += HAVE_MANIFOLD_CSG
    INCLUDEPATH += $$PWD/build/manifold-src/include
    LIBS += $$PWD/build/manifold-build/src/libmanifold.a
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
