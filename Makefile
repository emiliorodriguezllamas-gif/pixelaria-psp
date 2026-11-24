TARGET = chordbreaker_tower
OBJS = src/main.o

BUILD_PRX = 1
PSP_FW_VERSION = 500

CFLAGS = -O2 -G0 -Wall -std=gnu99
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBS = -lpspgu -lpspgum -lpspaudio -lpspdisplay -lpspge -lpspctrl -lpsppower \
       -lpspnet -lpspnet_adhoc -lpspnet_adhocctl -lpspnet_adhocmatching -lm

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Chordbreaker Tower Ad-Hoc
PSP_EBOOT_ICON = ICON0.PNG
PSP_EBOOT_PIC1 = PIC1.PNG
PSP_EBOOT_SND0 = SND0.AT3

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
