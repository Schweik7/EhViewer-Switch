.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error Please set DEVKITPRO, for example /opt/devkitpro)
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

APP_TITLE   := EhViewer Switch
APP_AUTHOR  := EhViewer contributors / Switch port
APP_VERSION := 0.5.3

TARGET      := EhViewerSwitch
BUILD       := build
SOURCES     := source source/core source/net source/ui source/reader source/download
DATA        := data
INCLUDES    := source

ARCH       := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS     := -g -Wall -Wextra -O2 -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS     += $(INCLUDE) -D__SWITCH__ -DEHV_SWITCH_VERSION=\"$(APP_VERSION)\"
CXXFLAGS   := $(CFLAGS) -std=gnu++17
ASFLAGS    := -g $(ARCH)
LDFLAGS     = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS := -lSDL2_ttf -lSDL2_image -lSDL2 \
        -lwebp -ljpeg -lharfbuzz -lfreetype -lpng16 -lbz2 \
        -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz \
        -lEGL -lglapi -ldrm_nouveau \
        -lnx -lm -lstdc++ -lpthread
LIBDIRS := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.bin)))

export LD := $(CXX)
export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))
export INCLUDE    := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                     $(foreach dir,$(LIBDIRS),-isystem $(dir)/include) \
                     -I$(CURDIR)/$(BUILD)
export LIBPATHS   := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

# Embed the NACP (title/author/version) and an icon in the NRO so launchers
# such as Sphaira and hbmenu can show the version.
ifneq (,$(wildcard $(CURDIR)/icon.jpg))
export APP_ICON := $(CURDIR)/icon.jpg
else
export APP_ICON := $(LIBNX)/default_icon.jpg
endif
export NROFLAGS += --icon=$(APP_ICON) --nacp=$(CURDIR)/$(TARGET).nacp

.PHONY: $(BUILD) clean all
all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

else

.PHONY: all
DEPENDS := $(OFILES:.o=.d)
all : $(OUTPUT).nro
$(OUTPUT).nro : $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf : $(OFILES)
$(OFILES_SRC) : $(HFILES_BIN)
%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)
-include $(DEPENDS)

endif
