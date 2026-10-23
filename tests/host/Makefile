# Host-side unit tests for the pure status-LED renderer. No Zephyr toolchain needed.
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O1 -g
INC := -I../../include
SRC := ../../src/adv360_status_render.c

BASELINE_OBJS := build/baseline_left.o build/baseline_right.o build/baseline_other.o

.PHONY: all test clean FORCE
FORCE:
all: test

build:
	mkdir -p build

build/baseline_left.o: ref/proffan_baseline.c ref/zephyr_stubs.h | build
	$(CC) $(CFLAGS) -Wno-sign-compare -Iref -DTEST_SIDE_LEFT -DBASELINE_ENTRY=baseline_left -c $< -o $@
build/baseline_right.o: ref/proffan_baseline.c ref/zephyr_stubs.h | build
	$(CC) $(CFLAGS) -Wno-sign-compare -Iref -DTEST_SIDE_RIGHT -DBASELINE_ENTRY=baseline_right -c $< -o $@
build/baseline_other.o: ref/proffan_baseline.c ref/zephyr_stubs.h | build
	$(CC) $(CFLAGS) -Wno-sign-compare -Iref -DBASELINE_ENTRY=baseline_other -c $< -o $@

HDRS := $(wildcard ../../include/zmk_keyboard_adv360/*.h)

FORK_OBJS := build/fork_left.o build/fork_right.o build/fork_cfgb.o
ENGINE_SRC := ../../src/adv360_underglow_engine.c
FORK_CFLAGS := -std=c11 -Wall -Wno-gnu-designator -Wno-sign-compare -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -O1 -g

# Oracle objects: {central, peripheral} x {config A (official), config B (non-default)}.
build/fork_left.o: ref/fork_underglow.c ref/fork_stubs.h | build
	$(CC) $(FORK_CFLAGS) -Iref -DZMK_BLE_IS_CENTRAL=1 -DFORK_PFX=fork_left_ -c $< -o $@
build/fork_right.o: ref/fork_underglow.c ref/fork_stubs.h | build
	$(CC) $(FORK_CFLAGS) -Iref -DZMK_BLE_IS_CENTRAL=0 -DFORK_PFX=fork_right_ -c $< -o $@
build/fork_cfgb.o: ref/fork_underglow.c ref/fork_stubs.h | build
	$(CC) $(FORK_CFLAGS) -Iref -DZMK_BLE_IS_CENTRAL=1 -DUG_CONFIG_B -DFORK_PFX=fork_cfgb_ -c $< -o $@

build/test_fork_differential: FORCE test_fork_differential.c $(SRC) $(HDRS) $(FORK_OBJS)
	$(CC) $(CFLAGS) $(INC) test_fork_differential.c $(SRC) $(FORK_OBJS) -o $@

build/test_underglow_differential: FORCE test_underglow_differential.c $(ENGINE_SRC) $(SRC) $(HDRS) $(FORK_OBJS)
	$(CC) $(CFLAGS) $(INC) test_underglow_differential.c $(ENGINE_SRC) $(SRC) $(FORK_OBJS) -o $@

build/test_underglow_plan: FORCE test_underglow_plan.c $(ENGINE_SRC) $(SRC) $(HDRS)
	$(CC) $(CFLAGS) $(INC) test_underglow_plan.c $(ENGINE_SRC) $(SRC) -o $@

build/test_blink_schedule: FORCE test_blink_schedule.c $(SRC) $(HDRS)
	$(CC) $(CFLAGS) $(INC) test_blink_schedule.c $(SRC) -o $@

build/test_baseline_equiv: FORCE test_baseline_equiv.c $(SRC) $(HDRS) $(BASELINE_OBJS)
	$(CC) $(CFLAGS) $(INC) test_baseline_equiv.c $(SRC) $(BASELINE_OBJS) -o $@

test: build/test_baseline_equiv build/test_fork_differential build/test_underglow_differential build/test_blink_schedule build/test_underglow_plan
	./build/test_baseline_equiv
	./build/test_fork_differential
	./build/test_underglow_differential
	./build/test_blink_schedule
	./build/test_underglow_plan

clean:
	rm -rf build
