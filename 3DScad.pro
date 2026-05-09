
QT += core gui widgets opengl

CONFIG += c++17

win32:LIBS += -lopengl32


# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    openscadgenerator.cpp \
    scenedocument.cpp \
    viewportwidget.cpp

HEADERS += \
    mainwindow.h \
    openscadgenerator.h \
    scenedocument.h \
    shapenode.h \
    viewportwidget.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
