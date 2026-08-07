# Сборка самой игры под Android. Исходники лежат в корне репозитория, но
# LOCAL_PATH НАРОЧНО оставлен здесь, в jni/src, а к исходникам ведёт ../../../../:
# ndk-build молча добавляет LOCAL_PATH в пути поиска заголовков, а в корне репы
# лежит игровой features.h — он перекрыл бы системный, который libc++ включает
# внутри <__config>, и стандартная библиотека перестала бы собираться.
# Заголовки игры при этом находятся: include "game.h" ищется рядом с самим
# исходником, то есть в корне репы, и путь поиска для этого не нужен.
STARCLUSTER_JNI := $(call my-dir)
GAME := ../../../..

LOCAL_PATH := $(STARCLUSTER_JNI)

include $(CLEAR_VARS)

LOCAL_MODULE := main

LOCAL_C_INCLUDES := \
    $(STARCLUSTER_JNI)/../SDL/include \
    $(STARCLUSTER_JNI)/../SDL2_mixer/include \
    $(STARCLUSTER_JNI)/$(GAME)/android/support

# Тот же список, что и в Makefile (цель `game`), плюс android-слой.
LOCAL_SRC_FILES := \
    $(GAME)/drive.cpp $(GAME)/main.cpp $(GAME)/shell.cpp $(GAME)/game.cpp \
    $(GAME)/cluster.cpp $(GAME)/resource.cpp $(GAME)/market.cpp $(GAME)/econ.cpp \
    $(GAME)/ship.cpp $(GAME)/agent.cpp $(GAME)/colony.cpp $(GAME)/faction.cpp \
    $(GAME)/ui.cpp $(GAME)/mining.cpp $(GAME)/combat.cpp $(GAME)/spaceevents.cpp \
    $(GAME)/anomaly.cpp $(GAME)/modules.cpp $(GAME)/chromo.cpp $(GAME)/render2d.cpp \
    $(GAME)/localgen.cpp $(GAME)/localsim.cpp $(GAME)/localdraw.cpp \
    $(GAME)/stb_image.cpp $(GAME)/i18n.cpp \
    $(GAME)/android/support/android_support.cpp

LOCAL_CPPFLAGS := -std=c++11 -O3 -Wall -Wextra -Wno-unused-parameter

LOCAL_SHARED_LIBRARIES := SDL2 SDL2_mixer

LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -lOpenSLES -llog -landroid

include $(BUILD_SHARED_LIBRARY)
