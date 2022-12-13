# ===================================================================================
# Project:  PocketRadio
# Author:   Stefan Wagner
# Year:     2022
# URL:      https://github.com/wagiminator    
# ===================================================================================
# Type "make help" in the command line.
# ===================================================================================

# Input and Output File Names
SKETCH   = PocketRadio.ino
TARGET   = pocketradio

# Microcontroller Settings
DEVICE  ?= attiny412
CLOCK    = 1000000
FUSE0    = 0x00
FUSE1    = 0x00
FUSE2    = 0x01
FUSE4    = 0x00
FUSE5    = 0xC5
FUSE6    = 0x04
FUSE7    = 0x00
FUSE8    = 0x00

# Programmer Settings
PROGRMR ?= serialupdi
PORT    ?= /dev/ttyUSB0

# Toolchain
CC       = avr-gcc
OBJCOPY  = avr-objcopy
OBJDUMP  = avr-objdump
AVRSIZE  = avr-size
PYPROG   = python3 -u ./tools/pymcuprog/prog.py -t uart -u $(PORT) -b 230400 -d $(DEVICE)
AVRDUDE  = avrdude -C ./tools/avrdude/avrdude.conf -c $(PROGRMR) -P $(PORT) -p $(DEVICE)
CLEAN    = rm -f *.lst *.obj *.cof *.list *.map *.eep.hex *.o *.s *.d

# Compiler Flags
CFLAGS   = -w -flto -Os -mmcu=$(DEVICE) -DF_CPU=$(CLOCK) -x c++
CFLAGS  += -B include/dev/$(DEVICE) -I include

# Symbolic Targets
help:
	@echo "Use the following commands:"
	@echo "make all       compile and build $(TARGET).elf/.bin/.hex/.asm for $(DEVICE)"
	@echo "make hex       compile and build $(TARGET).hex for $(DEVICE)"
	@echo "make asm       compile and disassemble to $(TARGET).asm for $(DEVICE)"
	@echo "make bin       compile and build $(TARGET).bin for $(DEVICE)"
	@echo "make upload    compile and upload to $(DEVICE) using $(PROGRMR)"
	@echo "make fuses     burn fuses of $(DEVICE) using $(PROGRMR) programmer"
	@echo "make install   compile, upload and burn fuses for $(DEVICE)"
	@echo "make clean     remove all build files"

all:	buildelf buildbin buildhex buildasm removetemp size

elf:	buildelf removetemp size

bin:	buildelf buildbin removetemp size removeelf

hex:	buildelf buildhex removetemp size removeelf

asm:	buildelf buildasm removetemp size removeelf

flash:	install

install: hex
	@echo "Installing $(TARGET).hex to $(DEVICE) using $(PROGRMR) ..."
ifeq ($(PROGRMR),serialupdi)
	@$(PYPROG) --fuses 0:$(FUSE0) 1:$(FUSE1) 2:$(FUSE2) 4:$(FUSE4) 5:$(FUSE5) 6:$(FUSE6) 7:$(FUSE7) 8:$(FUSE8) -f $(TARGET).hex -a write
else
	@$(AVRDUDE) -Ufuse0:w:$(FUSE0):m -Ufuse1:w:$(FUSE1):m -Ufuse2:w:$(FUSE2):m -Ufuse4:w:$(FUSE4):m -Ufuse5:w:$(FUSE5):m -Ufuse6:w:$(FUSE6):m -Ufuse7:w:$(FUSE7):m -Ufuse8:w:$(FUSE8):m -U flash:w:$(TARGET).hex:i
endif

upload: hex
	@echo "Uploading $(TARGET).hex to $(DEVICE) using $(PROGRMR) ..."
ifeq ($(PROGRMR),serialupdi)
	@$(PYPROG) -f $(TARGET).hex -a write
else
	@$(AVRDUDE) -U flash:w:$(TARGET).hex:i
endif

fuses:
	@echo "Burning fuses of $(DEVICE) ..."
ifeq ($(PROGRMR),serialupdi)
	@$(PYPROG) --fuses 0:$(FUSE0) 1:$(FUSE1) 2:$(FUSE2) 4:$(FUSE4) 5:$(FUSE5) 6:$(FUSE6) 7:$(FUSE7) 8:$(FUSE8) -a erase
else
	@$(AVRDUDE) -e -Ufuse0:w:$(FUSE0):m -Ufuse1:w:$(FUSE1):m -Ufuse2:w:$(FUSE2):m -Ufuse4:w:$(FUSE4):m -Ufuse5:w:$(FUSE5):m -Ufuse6:w:$(FUSE6):m -Ufuse7:w:$(FUSE7):m -Ufuse8:w:$(FUSE8):m
endif

clean:
	@echo "Cleaning all up ..."
	@$(CLEAN)
	@rm -f $(TARGET).elf $(TARGET).bin $(TARGET).hex $(TARGET).asm

buildelf:
	@echo "Compiling $(SKETCH) for $(DEVICE) @ $(CLOCK)Hz ..."
	@$(CC) $(CFLAGS) $(SKETCH) -o $(TARGET).elf

buildbin:
	@echo "Building $(TARGET).bin ..."
	@$(OBJCOPY) -O binary -R .eeprom $(TARGET).elf $(TARGET).bin

buildhex:
	@echo "Building $(TARGET).hex ..."
	@$(OBJCOPY) -j .text -j .data -O ihex $(TARGET).elf $(TARGET).hex

buildasm:
	@echo "Disassembling to $(TARGET).asm ..."
	@$(OBJDUMP) -d $(TARGET).elf > $(TARGET).asm

size:
	@echo "------------------"
	@echo "FLASH: $(shell $(AVRSIZE) -d $(TARGET).elf | awk '/[0-9]/ {print $$1 + $$2}') bytes"
	@echo "SRAM:  $(shell $(AVRSIZE) -d $(TARGET).elf | awk '/[0-9]/ {print $$2 + $$3}') bytes"
	@echo "------------------"

removetemp:
	@echo "Removing temporary files ..."
	@$(CLEAN)

removeelf:
	@echo "Removing $(TARGET).elf ..."
	@rm -f $(TARGET).elf
