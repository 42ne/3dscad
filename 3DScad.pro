
QT += core gui widgets opengl concurrent

CONFIG += c++17

win32:LIBS += -lopengl32

contains(CONFIG, opengl_renderer) {
    DEFINES += ENABLE_OPENGL_RENDER_BACKEND
}


# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    animatedtitlebar.cpp \
    appearancethemes.cpp \
    codeeditorpanel.cpp \
    csgevaluator.cpp \
    examplebrowsermenu.cpp \
    theme.cpp \
    themeeditordialog.cpp \
    examplepreviewpopup.cpp \
    groupthumbnailcache.cpp \
    main.cpp \
    nodethumbnailcache.cpp \
    mainwindow.cpp \
    scenecontroller.cpp \
    manifoldcsg.cpp \
    openscadgenerator.cpp \
    openscadparser.cpp \
    scenecommands.cpp \
    scenetreegraphicshelpers.cpp \
    scenetreegraphicswidget.cpp \
    scenetreegraphicswidget_canvasdrag.cpp \
    scenetreeinlinetextinput.cpp \
    scenetreelayout.cpp \
    scenetreenoderenderer.cpp \
    scenetreepalette.cpp \
    scenetreepreviewrenderer.cpp \
    scenetreetoolbarrenderer.cpp \
    scenetree.cpp \
    scenemesh.cpp \
    scenedocument.cpp \
    viewportwidget.cpp \
    viewporthelpers.cpp \
    viewportsoftwarerenderer.cpp \
    viewportglrenderer.cpp \
    viewportaxisgizmo.cpp

HEADERS += \
    animatedtitlebar.h \
    appearancethemes.h \
    codeeditorpanel.h \
    csgevaluator.h \
    examplebrowsermenu.h \
    theme.h \
    themeeditordialog.h \
    examplepreviewpopup.h \
    groupthumbnailcache.h \
    scenecontroller.h \
    nodethumbnailcache.h \
    expression.h \
    mainwindow.h \
    manifoldcsg.h \
    openscadgenerator.h \
    openscadparser.h \
    scenecommands.h \
    scenetreegraphicshelpers.h \
    scenetreegraphicswidget.h \
    scenetreeinlinetextinput.h \
    scenetreelayout.h \
    scenetreenoderenderer.h \
    scenetreepalette.h \
    scenetreepreviewrenderer.h \
    scenetreetoolbarrenderer.h \
    scenetree.h \
    scenemesh.h \
    scenedocument.h \
    shapenode.h \
    viewportwidget.h \
    viewporthelpers.h \
    viewportsoftwarerenderer.h \
    viewportglrenderer.h \
    viewportaxisgizmo.h

FORMS += \
    mainwindow.ui

win32:contains(QT_ARCH, x86_64) {
    MANIFOLD_BUILD_DIR = $$PWD/build/manifold-build-64
} else:win32 {
    MANIFOLD_BUILD_DIR = $$PWD/build/manifold-build-32
} else {
    MANIFOLD_BUILD_DIR = $$PWD/build/manifold-build
}

exists($$MANIFOLD_BUILD_DIR/src/libmanifold.a) {
    DEFINES += HAVE_MANIFOLD_CSG
    INCLUDEPATH += $$PWD/build/manifold-src/include
    LIBS += $$MANIFOLD_BUILD_DIR/src/libmanifold.a
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
