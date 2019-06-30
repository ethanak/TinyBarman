#include "Barman.h"
#include <avr/pgmspace.h>

#define CHARGEPUMP 0x8D
#define COLUMNADDR 0x21
#define COMSCANDEC 0xC8
#define COMSCANINC 0xC0
#define DISPLAYALLON 0xA5
#define DISPLAYALLON_RESUME 0xA4
#define DISPLAYOFF 0xAE
#define DISPLAYON 0xAF
#define EXTERNALVCC 0x1
#define INVERTDISPLAY 0xA7
#define MEMORYMODE 0x20
#define NORMALDISPLAY 0xA6
#define PAGEADDR 0x22
#define PAGESTARTADDRESS 0xB0
#define SEGREMAP 0xA1
#define SETCOMPINS 0xDA
#define SETCONTRAST 0x81
#define SETDISPLAYCLOCKDIV 0xD5
#define SETDISPLAYOFFSET 0xD3
#define SETHIGHCOLUMN 0x10
#define SETLOWCOLUMN 0x00
#define SETMULTIPLEX 0xA8
#define SETPRECHARGE 0xD9
#define SETSEGMENTREMAP 0xA1
#define SETSTARTLINE 0x40
#define SETVCOMDETECT 0xDB
#define SWITCHCAPVCC 0x2
#define READMODIFYWRITE 0xe0
#define END 0xee
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

#define myI2cAddress 0x3c

static const uint8_t PROGMEM init_data[] = {
//    static const uint8_t init_data[]={
    DISPLAYOFF,
    NORMALDISPLAY,
    SETDISPLAYCLOCKDIV,
    0x80,
    SETMULTIPLEX,
    0x3F,
    SETDISPLAYOFFSET,
    0x00,
    SETSTARTLINE | 0x00,
    CHARGEPUMP,
    0x14,
    MEMORYMODE,
    0x00,
    SEGREMAP,
    COMSCANDEC,
    SETCOMPINS,
    0x12,
    SETCONTRAST,
    0xCF,
    SETPRECHARGE,
    0xF1,
    SETVCOMDETECT,
    0x40,
    DISPLAYALLON_RESUME,
    NORMALDISPLAY,
    0x2e,                       // stop scroll
    DISPLAYON
};

void display_init()
{
    uint8_t i;
    TWM_begin();
    for (i = 0; i < sizeof(init_data); i++)
        display_sendCommand(pgm_read_byte(init_data + i));
    display_resetDisplay();
}

void display_resetDisplay(void)
{
    display_displayOff();
    display_clear();
    display_displayOn();
}

void display_setContrast(char contrast)
{
    display_sendCommand(0x81);
    display_sendCommand(contrast);
}

void display_clear(void)
{
    uint8_t buf[72];
    for (uint8_t page = 0; page < 8; page++) {
        display_sendCommand(PAGESTARTADDRESS | page);
        display_sendCommand(SETLOWCOLUMN | 0);
        display_sendCommand(SETHIGHCOLUMN | 0);
        // send as 16 bunches of 8 bytes data in one xmission
        for (uint8_t y = 0; y < 2; y++) {
            TWM_beginTransmission(myI2cAddress, buf, sizeof(buf));
            TWM_write(0x40);
            for (uint8_t w = 0; w < 64; w++) {
                TWM_write(0x0);
            }
            TWM_endTransmission();
        }
    }
}

void display_sendCommand(unsigned char com)
{
    uint8_t buf[8];
    TWM_beginTransmission(myI2cAddress, buf, sizeof(buf));
    TWM_write(0x80);            //command mode
    TWM_write(com);
    TWM_endTransmission();     // stop transmitting

}

/*

void display_drawChar(uint8_t x,uint8_t y,uint8_t Char)
{
  
  uint8_t buf[18];
    display_sendCommand(PAGESTARTADDRESS | (y));
	display_sendCommand(SETLOWCOLUMN | ((x & 1) << 3));
	display_sendCommand(SETHIGHCOLUMN | x >> 1);

	// send 8 bytes of data in one go
	TWM_beginTransmission(myI2cAddress, buf, sizeof(buf));
	TWM_write(0x40);
  const uint8_t *bs;
  bs = (const uint8_t *)(font8x8[Char-32]);
	for (uint8_t w=0; w<8; w++) {
        TWM_write((Char >= 32)?pgm_read_byte(bs++):0);
	}
	TWM_endTransmission(1);
}
*/

void display_drawShape(uint8_t x, uint8_t y, const uint8_t * Char)
{

    uint8_t buf[18];
    display_sendCommand(PAGESTARTADDRESS | (y));
    display_sendCommand(SETLOWCOLUMN | (x & 15));
    display_sendCommand(SETHIGHCOLUMN | x >> 4);

    // send 8 bytes of data in one go
    TWM_beginTransmission(myI2cAddress, buf, sizeof(buf));
    TWM_write(0x40);
    for (uint8_t w = 0; w < 8; w++) {
        TWM_write(pgm_read_byte(Char++));
    }
    TWM_endTransmission();
}

void display_drawPartial(uint8_t x, uint8_t y, uint8_t * buf, uint8_t len,
                         uint8_t * workbuf)
{
    display_sendCommand(PAGESTARTADDRESS | (y));
    display_sendCommand(SETLOWCOLUMN | (x & 15));
    display_sendCommand(SETHIGHCOLUMN | x >> 4);
    TWM_beginTransmission(myI2cAddress, workbuf, len + 4);
    TWM_write(0x40);
    while (len--) {
        TWM_write(*buf++);
    }
    TWM_endTransmission();

}
