#drivers/input/touchscreen/focaltech_touch/focaltech_test
#
#Makefile for test module
#



obj-$(CONFIG_TOUCHSCREEN_FTS) += focaltech_test.o
obj-$(CONFIG_TOUCHSCREEN_FTS) += focaltech_test_ini.o
obj-$(CONFIG_TOUCHSCREEN_FTS) += focaltech_test_detail_threshold.o
obj-$(CONFIG_TOUCHSCREEN_FTS) += supported_ic/

subdir-ccflags-y := $(call cc-disable-warning, strlcpy-strlcat-size)
