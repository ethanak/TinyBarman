
ISP = arduino
ISPEED = 19200
PORT = $(shell python ./ispport.py)

MCU = attiny85
AMCU = t85
CC = avr-gcc
OBJCOPY = avr-objcopy
CFLAGS += -Wall -Wextra -g -Os -mmcu=$(MCU) -D COMPILER_LTO -D F_CPU=8000000 -MMD -std=gnu11 -ffunction-sections -fdata-sections
LDFLAGS += -w -flto -g
OBJS = Barman.o TWireM.o Display.o

all: Barman.hex

Barman.elf: $(OBJS) 
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
	avr-size --format=avr --mcu=$(MCU) Barman.elf

Barman.hex: Barman.elf
	$(OBJCOPY) -O ihex $< $@

Display.o: Display.c Barman.h
	$(CC) $(CFLAGS) -o Display.o -c Display.c


Barman.o: Barman.c Barman.h chargen.h Common.c
	$(CC) $(CFLAGS) -c -o Barman.o Barman.c
	
%.o: %.c Barman.h
	$(CC) $(CFLAGS) -o $@ -c $<

flash: Barman.hex
	avrdude -P $(PORT) -b $(ISPEED) -c arduino -p $(AMCU) -U flash:w:Barman.hex

clean:
	rm -f *.d *.o *.elf *.hex *~
