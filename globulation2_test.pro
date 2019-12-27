include(globulation2.pri)
include(globulation2_test.pri)
include(cppunit.pri)
SOURCES += glob2/test/TestsRunner.cpp

TEMPLATE = app
TARGET = globulation2_test

# Unique to tests
LIBS += -ldl

INCLUDEPATH += $$PWD/glob2
INCLUDEPATH += $$PWD/glob2/libgag/include
INCLUDEPATH += $$PWD/glob2/libusl/src
#INCLUDEPATH += $$PWD/glob2/libwee/include

INCLUDEPATH += /usr/include/SDL
LIBS += -lSDL -lz -lboost_system -lboost_thread
LIBS += -lSDL_net -lvorbisfile -lvorbis -logg -lspeex
LIBS += -lSDL_ttf -lSDL_image
#INCLUDEPATH += /usr/include/ImageMagick-6
#LIBS += -lImageMagick++

# The following define makes your compiler warn you if you use any
# feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

