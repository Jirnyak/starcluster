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

# ⚠️ Список исходников НЕ ДУБЛИРУЕТСЯ РУКАМИ — он читается из корневого
# Makefile, единственного авторитетного места.
#
# Здесь стоял ручной список с комментарием «тот же, что и в Makefile», и он ровно
# так же и разошёлся: `exotic.cpp` добавили в Makefile, все настольные сборки и
# все харнесы собрались, а Android упал на CI связкой undefined symbol. Это тот
# же урок, что и с DLL Windows (§30): список, который надо помнить, рано или
# поздно забудут, поэтому гарантия обязана быть механической.
#
# ndk-build — это GNU make, так что `$(shell sed …)` здесь законен. Пустой
# результат обрывает сборку сразу и громко, а не даёт собрать половину.
STARCLUSTER_MAKEFILE := $(STARCLUSTER_JNI)/$(GAME)/Makefile
STARCLUSTER_SOURCES := $(shell sed -n 's/^SOURCES = //p' $(STARCLUSTER_MAKEFILE))
ifeq ($(strip $(STARCLUSTER_SOURCES)),)
$(error Starcluster: cannot read SOURCES from $(STARCLUSTER_MAKEFILE))
endif

LOCAL_SRC_FILES := \
    $(addprefix $(GAME)/,$(STARCLUSTER_SOURCES)) \
    $(GAME)/android/support/android_support.cpp

LOCAL_CPPFLAGS := -std=c++11 -O3 -Wall -Wextra -Wno-unused-parameter

LOCAL_SHARED_LIBRARIES := SDL2 SDL2_mixer

LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -lOpenSLES -llog -landroid

include $(BUILD_SHARED_LIBRARY)
