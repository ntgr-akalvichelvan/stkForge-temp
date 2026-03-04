#
# (C) Copyright Broadcom Corporation 2007-2016
#

fitimage_dir  := fitimage_util
fitimage_exec := $(fitimage_dir)/fit_image

TGTS          := agent_stk extimage extfitimage mk_stk

override CFLAGS += -DSTANDALONE -DBRCM_FIXUP
override CFLAGS += -Werror -Wunused

ifneq (1,$(USE64))
  override CFLAGS  += -m32
endif

all: $(TGTS)
	@:

agent_stk: agent_stk.c stk.h build_fitimage
	gcc $(CFLAGS) $< $(fitimage_dir)/crc32.o -o $@

extimage: extimage.c stk.h image.h
	gcc $(CFLAGS) $< -o $@

extfitimage: $(fitimage_exec)
	cp -f $< $@

mk_stk: mk_stk.c stk.h
	gcc $(CFLAGS) $< -o $@

$(fitimage_exec): build_fitimage
	@:

.PHONY: build_fitimage
build_fitimage:
	@$(MAKE) $(MPARMS) -C $(fitimage_dir)

.PHONY: clean_fitimage
clean_fitimage:
	@$(MAKE) $(MPARMS) -C $(fitimage_dir) clean

clean: clean_fitimage
	rm -f $(TGTS)
	rm -f *.o

