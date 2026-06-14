
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
    scenetreecanvasgraphics.cpp \
    scenetreeexpressionlayout.cpp \
    scenetreeglasspanelhelpers.cpp \
    scenetreegraphicsitems.cpp \
    scenetreeiconpainter.cpp \
    scenetreepreviewgeometry.cpp \
    scenetreetoolmetadata.cpp \
    scenetreecoloreditmode.cpp \
    scenetreehovermanager.cpp \
    scenetreeinlineeditor.cpp \
    scenecanvasdraghandler.cpp \
    scenetreecanvascontroller.cpp \
    scenetreewheelhandler.cpp \
    scenetreegraphicswidget.cpp \
    scenetreeinlinetextinput.cpp \
    scenetreelayout.cpp \
    scenetreenoderenderer.cpp \
    scenetreepalette.cpp \
    scenetreepreviewrenderer.cpp \
    scenetreetoolbarrenderer.cpp \
    scenetreeoverlaycontroller.cpp \
    scenetreedroppreviewcontroller.cpp \
    scenetreehittestmanager.cpp \
    scenetree.cpp \
    scenemesh.cpp \
    scenedocument.cpp \
    viewportwidget.cpp \
    viewporthelpers.cpp \
    viewportsoftwarerenderer.cpp \
    viewportglrenderer.cpp \
    viewportaxisgizmo.cpp \
    viewportoverlaypreview.cpp \
    viewportcamera.cpp

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
    scenetreecanvasgraphics.h \
    scenetreeexpressionlayout.h \
    scenetreeglasspanelhelpers.h \
    scenetreegraphicsconstants.h \
    scenetreegraphicshelpers.h \
    scenetreegraphicsitems.h \
    scenetreeiconpainter.h \
    scenetreepreviewgeometry.h \
    scenetreetoolmetadata.h \
    scenetreecoloreditmode.h \
    scenetreehovermanager.h \
    scenetreeinlineeditor.h \
    scenecanvasdraghandler.h \
    scenetreecanvascontroller.h \
    scenetreewheelhandler.h \
    scenetreegraphicswidget.h \
    scenetreeinlinetextinput.h \
    scenetreelayout.h \
    scenetreenoderenderer.h \
    scenetreepalette.h \
    scenetreepreviewrenderer.h \
    scenetreetoolbarrenderer.h \
    scenetreeoverlaycontroller.h \
    scenetreedroppreviewcontroller.h \
    scenetreehittestmanager.h \
    scenetreeoverlaygraphicsitems.h \
    scenetree.h \
    scenemesh.h \
    scenedocument.h \
    shapenode.h \
    viewportwidget.h \
    viewporthelpers.h \
    viewportsoftwarerenderer.h \
    viewportglrenderer.h \
    viewportaxisgizmo.h \
    viewportoverlaypreview.h \
    viewportcamera.h \
    viewportconstants.h

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
    # CrossSection (offset/2D ops) pulls in the bundled Clipper2 static lib.
    # Must come after libmanifold.a so the linker resolves its references.
    exists($$MANIFOLD_BUILD_DIR/_deps/clipper2-build/libClipper2.a) {
        LIBS += $$MANIFOLD_BUILD_DIR/_deps/clipper2-build/libClipper2.a
    }
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
