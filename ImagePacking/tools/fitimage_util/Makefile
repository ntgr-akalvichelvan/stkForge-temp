#
# (C) Copyright Broadcom Corporation 2007-2016
#

FIT_EXEC      := fit_image

OBJS          := ext_fit_main.o image.o
FDTOBJS       := fdt.o fdt_ro.o fdt_rw.o fdt_strerror.o fdt_sw.o fdt_wip.o
UTILS         := libfdt.o crc32.o sha1.o md5.o

CC            := gcc
LD            := ld
VPATH         := .:./lib:./libfdt

override CFLAGS += -DUSE_HOSTCC -DBRCM_FIXUP
override CFLAGS += -I./include -I./lib -I ./libfdt
override CFLAGS += -Werror -Wunused

ifneq (1,$(USE64))
  override CFLAGS  += -m32
  override LDFLAGS += -melf_i386
endif


all: $(FIT_EXEC)
	@:

$(FIT_EXEC): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lrt -lpthread $(UTILS)
	$(CROSS_COMPILE)strip $@

.c.o:
	$(CC) $(CFLAGS) -c $<

libfdt.o: $(FDTOBJS)
	$(LD) $(LDFLAGS) -r -o $@ $^

image.o: $(UTILS)

clean:
	rm -f $(FIT_EXEC) $(OBJS) $(FDTOBJS) $(UTILS)

