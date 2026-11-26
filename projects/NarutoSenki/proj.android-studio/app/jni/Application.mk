APP_STL := c++_shared
APP_ABI := arm64-v8a

APP_CPPFLAGS := -std=c++2a -frtti -fsigned-char -fexceptions -D_EG_ANDROID_PLATFORM
APP_LDFLAGS := -latomic

APP_SHORT_COMMANDS := true

ifeq ($(NDK_DEBUG),1)
  APP_CPPFLAGS += -DNDEBUG -DCOCOS2D_DEBUG=1
else
  APP_CPPFLAGS += -DCOCOS2D_DEBUG=0
endif
