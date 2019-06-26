TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

INCLUDEPATH += ../Common/System ../Common/Multimedia

SOURCES += \
        AudioOutput.cc \
        Filter.cc \
        FmDecode.cc \
        RtlSdrSource.cc \
        main.cpp

HEADERS += \
    AudioOutput.h \
    Filter.h \
    FmDecode.h \
    RtlSdrSource.h \
    SoftFM.h

win32 {
    INCLUDEPATH += $$PWD/../LFFM/_win/include
    LIBS += -L$$PWD/../LFFM/_win/lib
}

LIBS += -L../CommonLibs/debug
LIBS += -lrtlsdr -lpthread -lMultimedia -lSystem

