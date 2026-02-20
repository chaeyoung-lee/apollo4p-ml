include make/helpers.mk
include make/neuralspot_config.mk
include make/neuralspot_toolchain.mk
include make/jlink.mk

# Simple configuration
# Set MLDEBUG=1 to enable detailed error messages
# Set PROFILING=1 (or make CFLAGS+=-DPROFILING) to disable per-query prints and report IVF/TFLite cycle counts
MLDEBUG ?= 1
PROFILING ?= 0
ENERGY_MODE := 0

DEFINES += EE_CFG_ENERGY_MODE=$(ENERGY_MODE)

ifeq ($(MLDEBUG),1)
DEFINES += TF_LITE_STRIP_ERROR_STRINGS=0
else
DEFINES += TF_LITE_STRIP_ERROR_STRINGS=1
endif
ifeq ($(PROFILING),1)
DEFINES += PROFILING
endif

# Use CMSIS-NN optimized kernels for int8 Conv2D, DepthwiseConv2D, FullyConnected
DEFINES += CMSIS_NN

# Application name
local_app_name := main
TARGET := main

# Source files
sources := $(wildcard src/*.c)
sources += $(wildcard src/*.cc)
sources += $(wildcard src/*.cpp)
sources += $(wildcard src/*.s)
sources += $(wildcard src/model/*.c)
sources += $(wildcard src/model/*.cc)
sources += $(wildcard src/ivf/*.cc)
sources += $(wildcard src/util/*.c)
sources += $(wildcard src/utils/*.c)
sources += $(wildcard src/utils/*.cc)
sources += $(wildcard src/sd_card/*.c)
sources += $(wildcard src/peripherals/*.c)
# FatFs (ff16)
sources += ff16/source/ff.c ff16/source/diskio.c ff16/source/ffsystem.c ff16/source/ffunicode.c

VPATH += $(dir $(sources))
VPATH += ff16/source

targets  := $(BINDIR)/$(local_app_name).axf
targets  += $(BINDIR)/$(local_app_name).bin
mains    += $(BINDIR)/$(local_app_name).o

objs      = $(call source-to-object2,$(sources))
objects   = $(objs:%=$(BINDIR)/%)
dependencies = $(subst .o,.d,$(objects))

# TensorFlow Lite version
ifeq ($(TF_VERSION),b04cd98)
	INCLUDES += extern/AmbiqSuite/R4.3.0/boards/apollo4p_evb/bsp extern/AmbiqSuite/R4.3.0/CMSIS/ARM/Include extern/AmbiqSuite/R4.3.0/CMSIS/AmbiqMicro/Include extern/AmbiqSuite/R4.3.0/devices extern/AmbiqSuite/R4.1.0/mcu/apollo4p extern/AmbiqSuite/R4.1.0/mcu/apollo4p/hal/mcu extern/AmbiqSuite/R4.1.0/utils  extern/tensorflow/b04cd98/. extern/tensorflow/b04cd98/third_party extern/tensorflow/b04cd98/tensorflow/lite/micro/tools/make/downloads/flatbuffers/include neuralspot/ns-harness/includes-api neuralspot/ns-peripherals/includes-api
	libraries += libs/ambiqsuite.a libs/ns-peripherals.a libs/libam_hal.a libs/libam_bsp.a libs/libtensorflow-microlite-optimizednew.a
else
	INCLUDES += extern/AmbiqSuite/R4.1.0/boards/apollo4p_blue_evb/bsp extern/AmbiqSuite/R4.1.0/CMSIS/ARM/Include extern/AmbiqSuite/R4.1.0/CMSIS/AmbiqMicro/Include extern/AmbiqSuite/R4.1.0/devices extern/AmbiqSuite/R4.1.0/mcu/apollo4p extern/AmbiqSuite/R4.1.0/mcu/apollo4p/hal/mcu extern/AmbiqSuite/R4.1.0/utils  extern/tensorflow/R2.3.1/tensorflow extern/tensorflow/R2.3.1/third_party extern/tensorflow/R2.3.1/third_party/flatbuffers/include neuralspot/ns-harness/includes-api neuralspot/ns-peripherals/includes-api
	libraries += libs/ambiqsuite.a libs/ns-peripherals.a libs/libam_hal.a libs/libam_bsp.a libs/libtensorflow-microlite-oldopt.a
endif

# Local includes
LOCAL_INCLUDES = src
LOCAL_INCLUDES += src/util
LOCAL_INCLUDES += src/utils
LOCAL_INCLUDES += src/model
LOCAL_INCLUDES += src/ivf
LOCAL_INCLUDES += src/sd_card
LOCAL_INCLUDES += src/peripherals
LOCAL_INCLUDES += ff16/source

CFLAGS     += $(addprefix -D,$(DEFINES))
CFLAGS     += $(addprefix -I includes/,$(INCLUDES))
CFLAGS     += $(addprefix -I , $(LOCAL_INCLUDES))
LINKER_FILE := libs/linker_script.ld

all: $(BINDIR) $(objects) $(targets)

.PHONY: clean
clean:
ifeq ($(OS),Windows_NT)
	@echo "Windows_NT"
	@echo $(Q) $(RM) -rf $(BINDIR)/*
	$(Q) $(RM) -rf $(BINDIR)/*
else
	$(Q) $(RM) -rf $(BINDIR) $(JLINK_CF)
endif

ifneq "$(MAKECMDGOALS)" "clean"
  include $(dependencies)
endif

$(BINDIR):
	@mkdir -p $@

$(BINDIR)/%.o: %.cc
	@echo " ********CC Compiling $(COMPILERNAME) $< to make $@"
	@mkdir -p $(@D)
	$(Q) $(CC) -c $(CFLAGS) $(CCFLAGS) $< -o $@

$(BINDIR)/%.o: %.cpp $(BINDIR)/%.d
	@echo " ********CPP Compiling $(COMPILERNAME) $< to make $@"
	@mkdir -p $(@D)
	$(Q) $(CC) -c $(CFLAGS) $(CCFLAGS) $< -o $@

$(BINDIR)/%.o: %.c
	@echo " ********C Compiling $(COMPILERNAME) $< to make $@"
	@mkdir -p $(@D)
	$(Q) $(CC) -c $(CFLAGS) $(CONLY_FLAGS) $< -o $@

$(BINDIR)/%.o: %.s $(BINDIR)/%.d
	@echo " Assembling $(COMPILERNAME) $<"
	@mkdir -p $(@D)
	$(Q) $(CC) -c $(CFLAGS) $< -o $@

$(BINDIR)/$(local_app_name).axf: $(objects)
	@echo " Linking $(COMPILERNAME) $@"
	@mkdir -p $(@D)
	$(Q) $(CC) -Wl,-T,$(LINKER_FILE) -o $@ $(objects) $(LFLAGS)

$(BINDIR)/$(local_app_name).bin: $(BINDIR)/$(local_app_name).axf 
	@echo " Copying $(COMPILERNAME) $@..."
	@mkdir -p $(@D)
	$(Q) $(CP) $(CPFLAGS) $< $@
	$(Q) $(OD) $(ODFLAGS) $< > $*.lst
	$(Q) $(SIZE) $(objects) $(lib_prebuilt) $< > $*.size

$(JLINK_CF):
	@echo " Creating JLink command sequence input file..."
	$(Q) echo "ExitOnError 1" > $@
	$(Q) echo "Reset" >> $@
	$(Q) echo "LoadFile $(BINDIR)/$(TARGET).bin, $(JLINK_PF_ADDR)" >> $@
	$(Q) echo "Exit" >> $@

.PHONY: deploy
deploy: $(JLINK_CF)
	@echo " Deploying $< to device (ensure JLink USB connected and powered on)..."
	$(Q) $(JLINK) $(JLINK_CMD)

.PHONY: view
view:
	@echo " Printing SWO output (ensure JLink USB connected and powered on)..."
	$(Q) $(JLINK_SWO) $(JLINK_SWO_CMD)

%.d: ;
