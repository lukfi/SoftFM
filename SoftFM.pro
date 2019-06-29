TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt
#QMAKE_CXXFLAGS += -ffast-math -O3

INCLUDEPATH += ../Common/System ../Common/Multimedia

SOURCES += \
        AudioOutput.cpp \
        Filter.cpp \
        FmDecode.cpp \
        RtlSdrSource.cpp \
        mian.cpp \
        oldmain.cpp

HEADERS += \
    AudioOutput.h \
    Filter.h \
    FmDecode.h \
    RtlSdrSource.h \
    SoftFM.h \
    fastatan2.h

win32 {
    INCLUDEPATH += $$PWD/../LFFM/_win/include
    LIBS += -L$$PWD/../LFFM/_win/lib
}

LIBS += -L../CommonLibs/debug
LIBS += -lrtlsdr -lpthread -lMultimedia -lSystem

