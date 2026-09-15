CFLAGS = -Wall -MMD -MP
-include $(wildcard *.d)

/dev/loop9: blockdev
	sudo losetup /dev/loop9 blockdev

blockdev:
	dd if=/dev/zero of=blockdev bs=1024 count=1024

rwal_test: rwal_test.c unity/unity.c
	cc $(CFLAGS) $^ -o $@

.PHONY: test

test: rwal_test
	./rwal_test
