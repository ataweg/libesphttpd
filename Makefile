# Makefile, only used on esp8266 RTOS and non-RTOS SDK. Esp32 uses component.mk instead.

# Directory the Makefile is in. Please don't include other Makefiles before this.
THISDIR:=$(dir $(abspath $(lastword $(MAKEFILE_LIST))))/

#Include httpd config from lower level, if it exists
-include ../esphttpdconfig.mk

//USE_HEATSHRINK = NO


#Default options. If you want to change them, please create ../esphttpdconfig.mk with the options you want in it.
USE_GZIP_COMPRESSION ?= no
COMPRESS_W_YUI ?= no
YUI-COMPRESSOR ?= /usr/bin/yui-compressor
USE_UGLIFYJS ?= no
JS_MINIFY_TOOL ?= uglifyjs
USE_HEATSHRINK ?= yes
HTTPD_WEBSOCKETS ?= yes
USE_OPENSDK ?= no
#For FreeRTOS
HTTPD_STACKSIZE ?= 2048
ENABLE_SSL_SUPPORT ?= no
ENABLE_CORS_SUPPORT ?= no
#Auto-detect ESP32 build if not given.
ifneq (,$(wildcard $(SDK_PATH)include/esp32))
ESP32 ?= yes
FREERTOS ?= yes
else
ESP32 ?= no
FREERTOS ?= no
endif

# Output directors to store intermediate compiled files
# relative to the project directory
BUILD_BASE	?= build/

# Base directory for the compiler. Needs a / at the end; if not set it'll use the tools that are in
# the PATH.
XTENSA_TOOLS_ROOT ?=

# base directory of the ESP8266 SDK package, absolute
# Only used for the non-FreeRTOS build
SDK_BASE	?= c:/Espressif/ESP8266_NONOS_SDK/

# Base directory of the ESP8266 FreeRTOS SDK package, absolute
# Only used for the FreeRTOS build
SDK_PATH	?= c:/Espressif/ESP8266_RTOS_SDK/

# name for the target project
LIB		= $(BUILD_BASE)libesphttpd.a

# which modules (subdirectories) of the project to include in compiling
MODULES		= espfs core util
EXTRA_INCDIR  = $(EXT_INCDIR)
EXTRA_INCDIR += ./include \
		. \
		lib/heatshrink


# for non-os builds osapi.h includes "user_config.h" so we have to ensure that
# the include/libesphttpd folder is in the include path so this file can be found
ifeq ("$(FREERTOS)","no")
EXTRA_INCDIR	+= ./include/libesphttpd
endif

# compiler flags using during compilation of source files
CFLAGS		= -Os -ggdb -std=c99 -Werror -Wpointer-arith -Wundef -Wall -Wl,-EL -fno-inline-functions \
		-nostdlib -mlongcalls -mtext-section-literals  -D__ets__ -DICACHE_FLASH \
		-Wno-address -DHTTPD_STACKSIZE=$(HTTPD_STACKSIZE) \

# various paths from the SDK used in this project
SDK_LIBDIR	= lib/
SDK_LDDIR	= ld/

ifeq ("$(FREERTOS)","yes")
CFLAGS		+= -DFREERTOS -DLWIP_OPEN_SRC -ffunction-sections -fdata-sections
ifeq ("$(ESP32)","yes")
SDK_INCDIR	= include \
		include/esp32 \
		driver_lib/include \
		extra_include \
		third_party/include \
		third_party/include/cjson \
		third_party/include/freertos \
		third_party/include/lwip \
		third_party/include/lwip/ipv4 \
		third_party/include/lwip/ipv6 \
		third_party/include/ssl
CFLAGS		+= -DESP32 -DFREERTOS -DLWIP_OPEN_SRC -ffunction-sections -fdata-sections
else
SDK_INCDIR	= include \
		include/freertos \
		include/espressif/esp8266 \
		include/espressif \
		extra_include \
		include/lwip \
		include/lwip/lwip \
		include/lwip/ipv4 \
		include/lwip/ipv6
CFLAGS		+= -DFREERTOS -DLWIP_OPEN_SRC -ffunction-sections -fdata-sections
endif
SDK_INCDIR	:= $(addprefix -I$(SDK_PATH),$(SDK_INCDIR))
else
SDK_INCDIR	= include
SDK_INCDIR	:= $(addprefix -I$(SDK_BASE),$(SDK_INCDIR))
endif


ifeq ("$(ESP32)","yes")
TOOLPREFIX=xtensa-esp108-elf-
CFLAGS+=-DESP32
else
TOOLPREFIX=xtensa-lx106-elf-
endif

# select which tools to use as compiler, librarian and linker
CC		:= $(XTENSA_TOOLS_ROOT)$(TOOLPREFIX)gcc
AR		:= $(XTENSA_TOOLS_ROOT)$(TOOLPREFIX)ar
LD		:= $(XTENSA_TOOLS_ROOT)$(TOOLPREFIX)gcc
OBJCOPY	:= $(XTENSA_TOOLS_ROOT)$(TOOLPREFIX)objcopy

MKESPFSIMAGE=$(BUILD_BASE)mkespfsimage.exe

####
#### no user configurable options below here
####
SRC_DIR		:= $(MODULES)
BUILD_DIR	:= $(addprefix $(BUILD_BASE),$(MODULES))

SRC		:= $(foreach sdir,$(SRC_DIR),$(wildcard $(sdir)/*.c))
OBJ		:= $(patsubst %.c,$(BUILD_BASE)%.o,$(SRC))

INCDIR		+= $(addprefix -I,$(SRC_DIR))
EXTRA_INCDIR	:= $(addprefix -I,$(EXTRA_INCDIR))
MODULE_INCDIR	:= $(addsuffix /include,$(INCDIR))

V ?= $(VERBOSE)
ifeq ("$(V)","1")
Q :=
vecho := @true
else
Q := @
vecho := @echo
endif

ifneq ("$(FREERTOS)","yes")
ifeq ("$(USE_OPENSDK)","yes")
CFLAGS		+= -DUSE_OPENSDK
else
CFLAGS		+= -D_STDINT_H
endif
endif

ifeq ("$(USE_GZIP_COMPRESSION)","yes")
CFLAGS		+= -DUSE_GZIP_COMPRESSION
endif

ifeq ("$(USE_HEATSHRINK)","yes")
CFLAGS		+= -DESPFS_HEATSHRINK
endif

ifeq ("$(HTTPD_WEBSOCKETS)","yes")
CFLAGS		+= -DHTTPD_WEBSOCKETS
endif

ifeq ("$(ENABLE_SSL_SUPPORT)", "yes")
CFLAGS		+= -DCONFIG_ESPHTTPD_SSL_SUPPORT=1
endif

ifeq ("$(ENABLE_CORS_SUPPORT)", "yes")
CFLAGS		+= -DCONFIG_ESPHTTPD_CORS_SUPPORT=1
endif

ifeq ("$(ESP32)", "yes")
CFLAGS		+= -DESP32=1
endif

CFLAGS  += \
           -DICACHE_FLASH	\
           -DUSE_OPTIMIZE_PRINTF

vpath %.c $(SRC_DIR)

define compile-objects
$1/%.o: %.c
	$(vecho) "CC $$<"
	$(Q) $(CC) $(INCDIR) $(MODULE_INCDIR) $(EXTRA_INCDIR) $(SDK_INCDIR) $(CFLAGS)  -c $$< -o $$@
endef

#.PHONY: clean
.PHONY: all checkdirs clean webpages.espfs submodules

all: checkdirs submodules $(LIB) $(BUILD_BASE)webpages.espfs $(BUILD_BASE)libwebpages-espfs.a $(MKESPFSIMAGE)

$(LIB): $(BUILD_DIR)  $(OBJ)
	$(vecho) "AR $@"
	$(vecho)  $(OBJ)
	$(Q) $(AR) cru $@ $(OBJ)
#submodules

submodules: lib/heatshrink/Makefile

lib/heatshrink/Makefile:
	$(Q) echo "Heatshrink isn't found. Checking out submodules to fetch it."
	$(Q) git submodule init
	$(Q) git submodule update

checkdirs: $(BUILD_DIR)

$(BUILD_DIR):
	$(Q) mkdir -p $@

#ignore vim swap files
FIND_OPTIONS = -not -iname '*.swp'

$(BUILD_BASE)libwebpages-espfs.a: $(BUILD_BASE)webpages.espfs
	$(Q) $(OBJCOPY) -I binary -O elf32-xtensa-le -B xtensa --rename-section .data=.irom0.literal \
		$(BUILD_BASE)webpages.espfs $(BUILD_BASE)webpages.espfs.o.tmp
	$(Q) $(LD) -nostdlib -Wl,-r $(BUILD_BASE)webpages.espfs.o.tmp -o $(BUILD_BASE)webpages.espfs.o -Wl,-T webpages.espfs.ld
	$(Q) $(AR) cru $@ $(BUILD_BASE)webpages.espfs.o

$(BUILD_BASE)webpages.espfs: $(HTMLDIR) $(MKESPFSIMAGE)
ifeq ("$(COMPRESS_W_YUI)","yes")
	$(Q) rm -rf html_compressed;
	$(Q) cp -r ../html html_compressed;
	$(Q) echo "Compression assets with yui-compressor. This may take a while..."
	$(Q) for file in `find html_compressed -type f -name "*.js"`; do $(YUI-COMPRESSOR) --type js $$file -o $$file; done
	$(Q) for file in `find html_compressed -type f -name "*.css"`; do $(YUI-COMPRESSOR) --type css $$file -o $$file; done
	$(Q) awk "BEGIN {printf \"YUI compression ratio was: %.2f%%\\n\", (`du -b -s html_compressed/ | sed 's/\([0-9]*\).*/\1/'`/`du -b -s ../html/ | sed 's/\([0-9]*\).*/\1/'`)*100}"
# mkespfsimage will compress html, css, svg and js files with gzip by default if enabled
# override with -g cmdline parameter
	$(Q) cd html_compressed; find . $(FIND_OPTIONS) | $(MKESPFSIMAGE) > $(BUILD_BASE)webpages.espfs; cd ..;
else
  ifeq ("$(USE_UGLIFYJS)","yes")
	$(Q) echo "Using uglifyjs"
	$(Q) rm -rf html_compressed;
	$(Q) cp -r ../html html_compressed;
	$(Q) echo "Compressing javascript assets with uglifyjs"
	$(Q) for file in `find html_compressed -type f -name "*.js"`; do $(JS_MINIFY_TOOL) $$file -c -m -o $$file; done
	$(Q) awk "BEGIN {printf \" compression ratio was: %.2f%%\\n\", (`du -b -s html_compressed/ | sed 's/\([0-9]*\).*/\1/'`/`du -b -s $(PROJECT_PATH)/$(HTMLDIR) | sed 's/\([0-9]*\).*/\1/'`)*100}"
	$(Q) cd html_compressed; find . | $(COMPONENT_BUILD_DIR)/mkespfsimage/mkespfsimage > $(COMPONENT_BUILD_DIR)/webpages.espfs; cd ..;
  else
	$(Q) echo "Not using uglifyjs"
	$(Q) cd ../html; find . $(FIND_OPTIONS) | $(MKESPFSIMAGE) > $(BUILD_BASE)webpages.espfs; cd ..
  endif
endif

$(MKESPFSIMAGE): espfs/mkespfsimage/
	$(Q) echo "Build mkespfsimage"
	$(Q) $(MAKE) -C espfs/mkespfsimage BUILD_BASE="../../$(BUILD_BASE)" USE_HEATSHRINK="$(USE_HEATSHRINK)" USE_GZIP_COMPRESSION="$(USE_GZIP_COMPRESSION)"

clean:
	$(Q) rm -f $(LIB)
#	$(Q) find $(BUILD_BASE) -type f | xargs rm -f
	$(Q) rm -rf $(BUILD_DIR)
	$(Q) make -C espfs/mkespfsimage/ clean BUILD_BASE=../$(BUILD_BASE)
	$(Q) rm -rf $(FW_BASE)
	$(Q) rm -f $(BUILD_BASE)webpages.espfs* $(BUILD_BASE)libwebpages-espfs.a
ifeq ("$(COMPRESS_W_YUI)","yes")
	$(Q) rm -rf html_compressed
endif

$(foreach bdir,$(BUILD_DIR),$(eval $(call compile-objects,$(bdir))))
