EE_BIN = GBA_PS2.ELF
EE_OBJS = main.o gba_core.o usbd_irx.o bdm_irx.o bdmfs_fatfs_irx.o usbmass_bd_irx.o

EE_INCS += -I$(PS2SDK)/ports/include -I$(GSKIT)/include -I.
EE_CFLAGS += -O2 -G0 -Wall -Wno-unused-variable -Wno-unused-function
EE_LDFLAGS += -L$(PS2SDK)/ports/lib -L$(GSKIT)/lib
EE_LIBS += -lgskit -ldmakit -lpad -lpatches -lm

all: $(EE_BIN)

usbd_irx.c:
	$(PS2SDK)/bin/bin2c $(PS2SDK)/iop/irx/usbd.irx usbd_irx.c usbd_irx

bdm_irx.c:
	$(PS2SDK)/bin/bin2c $(PS2SDK)/iop/irx/bdm.irx bdm_irx.c bdm_irx

bdmfs_fatfs_irx.c:
	$(PS2SDK)/bin/bin2c $(PS2SDK)/iop/irx/bdmfs_fatfs.irx bdmfs_fatfs_irx.c bdmfs_fatfs_irx

usbmass_bd_irx.c:
	$(PS2SDK)/bin/bin2c $(PS2SDK)/iop/irx/usbmass_bd.irx usbmass_bd_irx.c usbmass_bd_irx

clean:
	rm -f $(EE_BIN) $(EE_OBJS) usbd_irx.c bdm_irx.c bdmfs_fatfs_irx.c usbmass_bd_irx.c

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
