#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/pgmspace.h>

extern void TWM_begin(void);
extern void TWM_beginTransmission(uint8_t slaveAddr, uint8_t * buf,
                                  uint8_t bufsize);
extern size_t TWM_write(uint8_t data);
extern void TWM_endTransmission(void);

extern void audioEffect(uint8_t);
extern uint8_t effectLocking(void);

extern void setup(void);
extern unsigned long millis(void);
extern void startTimer(void);
extern void delay(uint32_t msec);
extern void delayTo(uint32_t msec);
#define rand8() (ran() >> 8)
extern uint16_t ran(void);
extern uint16_t myrand(uint16_t n);

//extern void display_drawChar(uint8_t x,uint8_t y,uint8_t Char);
extern void display_sendCommand(unsigned char com);
extern void display_resetDisplay(void);
extern void display_setContrast(char contrast);
extern void display_clear(void);
extern void display_init(void);
extern void display_drawPartial(uint8_t x, uint8_t y, uint8_t * buf,
                                uint8_t len, uint8_t * workbuf);
extern void display_drawShape(uint8_t x, uint8_t y, const uint8_t * Char);

#define display_reconnect() TWM_begin()
#define display_displayOn() display_sendCommand(0xaf)
#define display_displayOff() display_sendCommand(0xae)

#define AUDIO_EFFECT_FILL 1
#define AUDIO_EFFECT_CRASH 2
#define AUDIO_EFFECT_PUSH 3
#define AUDIO_EFFECT_CLIENT 4

#define RETCODE_ANGRY_CLIENT 2
#define RETCODE_CRASH_FLOOR 1
#define RETCODE_CRASH_WALL 4
#define RETCODE_CRASH_MAN 8
