all : blink.bin

TARGET:=blink
TARGET_MCU?=CH32V003
include ../../ch32v003fun/ch32fun/ch32fun.mk

flash : cv_flash
clean : cv_clean

.PHONY : all clean flash