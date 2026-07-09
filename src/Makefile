#
# GuitarDAWLiteOS bring-up kernel — builds kernel_2712.img via Circle
#
CIRCLEHOME = ../circle

OBJS	= main.o kernel.o

LIBS	= $(CIRCLEHOME)/lib/libcircle.a

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)
