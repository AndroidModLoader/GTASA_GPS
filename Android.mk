LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_CPP_EXTENSION := .cpp .cc
LOCAL_MODULE    := GPS
LOCAL_SRC_FILES := main.cpp mod/logger.cpp mod/config.cpp
LOCAL_CFLAGS += -O2 -mfloat-abi=softfp -DNDEBUG
LOCAL_C_INCLUDES += ./include
LOCAL_LDLIBS += -llog
include $(BUILD_SHARED_LIBRARY)