.DEFAULT_GOAL := main

CC ?= cc
CFLAGS ?= -Wall -march=native

OBJECTS := main.o rwal.o rwal_test.o unity/unity.o
DEPS := $(OBJECTS:.o=.d)

.PHONY: test clean loopdev FORCE

main: main.o rwal.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

rwal_test: rwal_test.o rwal.o unity/unity.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(OBJECTS): %.o: %.c .build-config
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP \
		-MF $(@:.o=.d) -MT $@ -c $< -o $@

shquote = '$(subst ','"'"',$(1))'

.build-config: FORCE
	@printf '%s\n' $(call shquote,$(CC)) $(call shquote,$(CPPFLAGS)) \
		$(call shquote,$(CFLAGS)) $(call shquote,$(LDFLAGS)) \
		$(call shquote,$(LDLIBS)) > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

MISSING_DEPS := $(filter-out $(wildcard $(DEPS)),$(DEPS))
ifneq ($(strip $(MISSING_DEPS)),)
$(MISSING_DEPS:.d=.o): FORCE
endif

FORCE:

-include $(DEPS)

test: rwal_test
	./rwal_test

blockdev:
	dd if=/dev/zero of=$@ bs=1024 count=1024

loopdev: blockdev
	sudo losetup --find --show --nooverlap $<

clean:
	$(RM) main rwal rwal_test $(OBJECTS) $(DEPS) \
		.build-config .build-config.tmp
