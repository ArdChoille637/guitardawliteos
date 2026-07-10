#
# GuitarDAWLiteOS M3 audio-engine kernel — builds kernel_2712.img via Circle
#
# NOTE: needs the Circle tree built with -DARM_ALLOW_MULTI_CORE (the flag
# changes CSpinLock layout / atomic strength, so it must cover the LIBS
# below too, not just these objects). scripts/build.sh enforces this.
#
CIRCLEHOME = ../circle

OBJS	= main.o kernel.o audioengine.o cores.o

LIBS	= $(CIRCLEHOME)/lib/sound/libsound.a \
	  $(CIRCLEHOME)/lib/libcircle.a

include $(CIRCLEHOME)/Rules.mk

# Config.mk DEFINEs (ARM_ALLOW_MULTI_CORE, KERNEL_MAX_SIZE) change class
# layouts and memory-map constants baked into every object — force a
# recompile whenever the config changes, no matter how it changed.
$(OBJS): $(CIRCLEHOME)/Config.mk

-include $(DEPS)
