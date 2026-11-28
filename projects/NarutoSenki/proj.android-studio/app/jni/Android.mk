LOCAL_PATH := $(call my-dir)
COCOS_ROOT := $(LOCAL_PATH)/../../../../../
PHOTON_DIR := $(COCOS_ROOT)/external/photon

# --- Prebuilt Photon Realtime C++ SDK ---
include $(CLEAR_VARS)
LOCAL_MODULE := common_cpp_static
LOCAL_SRC_FILES := $(PHOTON_DIR)/libs/libcommon.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := photon_cpp_static
LOCAL_SRC_FILES := $(PHOTON_DIR)/libs/libphoton.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := loadbalancing_cpp_static
LOCAL_SRC_FILES := $(PHOTON_DIR)/libs/libloadbalancing.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := egcrypto_static
LOCAL_SRC_FILES := $(PHOTON_DIR)/libs/libegcrypto.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := egssl_static
LOCAL_SRC_FILES := $(PHOTON_DIR)/libs/libegssl.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := websockets_static
LOCAL_SRC_FILES := $(PHOTON_DIR)/libs/libwebsockets.a
include $(PREBUILT_STATIC_LIBRARY)

# --- Modul Utama Game ---
include $(CLEAR_VARS)

LOCAL_MODULE := cocos2dcpp_shared

LOCAL_MODULE_FILENAME := libcocos2dcpp

LOCAL_SRC_FILES := narutosenki/main.cpp \
	    ../../../Classes/AppDelegate.cpp \
		../../../Classes/ActionButton.cpp \
		../../../Classes/CharacterBase.cpp \
		../../../Classes/BGLayer.cpp \
		../../../Classes/CreditsLayer.cpp \
		../../../Classes/Effect.cpp \
		../../../Classes/Element.cpp \
		../../../Classes/GameLayer.cpp \
		../../../Classes/GameOver.cpp \
		../../../Classes/GameScene.cpp \
		../../../Classes/GearLayer.cpp \
		../../../Classes/HPBar.cpp \
		../../../Classes/HudLayer.cpp \
		../../../Classes/JoyStick.cpp \
		../../../Classes/LoadLayer.cpp \
		../../../Classes/PauseLayer.cpp \
		../../../Classes/SelectLayer.cpp \
		../../../Classes/StartMenu.cpp \
		../../../Classes/Network/LanManager.cpp \
		../../../Classes/MyUtils/CCScrewLayer.cpp \
		../../../Classes/MyUtils/CCShake.cpp \
		../../../Classes/MyUtils/CCStrokeLabel.cpp \
		../../../Classes/MyUtils/KTools.cpp \
		../../../Classes/MyUtils/MD5ChecksumDefines.cpp \
		../../../Classes/UI/GameModeLayer.cpp \
        ../../../../../external/sqlite3/src/sqlite3.c \

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../Classes \
					$(LOCAL_PATH)/../../../Classes/Core \
					$(COCOS_ROOT)/external \
					$(PHOTON_DIR)/inc \
					$(COCOS_ROOT)/external/sqlite3/src \
					$(COCOS_ROOT)/extensions \
					$(COCOS_ROOT)/cocos2dx/platform/third_party/android/prebuilt/libcurl/include \
    
LOCAL_WHOLE_STATIC_LIBRARIES += cocos2dx_static
LOCAL_WHOLE_STATIC_LIBRARIES += cocosdenshion_static
LOCAL_WHOLE_STATIC_LIBRARIES += cocos_lua_static
LOCAL_WHOLE_STATIC_LIBRARIES += cocos_extension_static
LOCAL_WHOLE_STATIC_LIBRARIES += cocos_curl_static

LOCAL_STATIC_LIBRARIES += loadbalancing_cpp_static
LOCAL_STATIC_LIBRARIES += photon_cpp_static
LOCAL_STATIC_LIBRARIES += common_cpp_static
LOCAL_STATIC_LIBRARIES += websockets_static
LOCAL_STATIC_LIBRARIES += egssl_static
LOCAL_STATIC_LIBRARIES += egcrypto_static

include $(BUILD_SHARED_LIBRARY)

$(call import-module,cocos2dx)
$(call import-module,CocosDenshion/android)
$(call import-module,extensions)
$(call import-module,scripting/lua/proj.android)
$(call import-module,cocos2dx/platform/third_party/android/prebuilt/libcurl)
