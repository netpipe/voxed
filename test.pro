QT += widgets charts opengl

TARGET = ToneGenerator
TEMPLATE = app

SOURCES += main.cpp

HEADERS +=

INCLUDEPATH +=

#LIBS += -lglut -lGLU
#else:macos:
LIBS += -framework GLUT -framework OpenGL -framework Cocoa
