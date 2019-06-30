#include <avr/interrupt.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/atomic.h>

static volatile uint32_t milliseconds;
static unsigned long lastMillis;
static uint8_t effect, effectTime;
void noTone(void);
void tone(int);
extern uint16_t myrand(uint16_t);
static uint8_t milistep;

static uint16_t random_number = 1;

static uint16_t lfsr16_next(uint16_t n)
{

    return (n >> 0x01U) ^ (-(n & 0x01U) & 0xB400U);
}

uint16_t ran(void)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        random_number = lfsr16_next(random_number);
    }
    return random_number;
}

void miliTimer(void)
{
    if (effect) {
        if (effectTime) {
            effectTime--;
            if (!effectTime) {
                noTone();
                effect = 0;
                return;
            }
            if (effect == AUDIO_EFFECT_FILL) {
                tone(660 - 10 * effectTime + myrand(55));
            } else if (effect == AUDIO_EFFECT_CRASH) {
                tone(660 + myrand(440));
            } else if (effect == AUDIO_EFFECT_PUSH) {
                tone(60 + 16 * effectTime + myrand(30));
            } else {
                tone(660 + 5 * effectTime + myrand(10));
            }
        } else {
            noTone();
            effect = 0;
        }
    }
}

ISR(TIMER1_COMPA_vect)
{

    milliseconds++;
    if (milistep++ >= 20) {
        miliTimer();
        milistep = 0;
    }
}

unsigned long millis()
{
    uint32_t rc;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        rc = milliseconds;
    }
    return rc;
}

void startTimer(void)
{
    lastMillis = millis();
}

void delay(uint32_t msec)
{
    uint32_t m = millis();
    while (millis() - m < msec) ;
}

void delayTo(uint32_t msec)
{
    uint32_t m;
    while ((m = millis()) - lastMillis < msec) ;
    lastMillis = m;
}

void tone(int freq)
{

    uint16_t ocr = (F_CPU / 16) / freq;
    uint8_t prescal = 0x002;    // ck/1
    if (ocr > 256) {
        ocr >>= 3;
        prescal = 0x3;
    }

    ocr -= 1;
    TCCR0A = _BV(WGM01) | _BV(COM0B0);
    DDRB |= _BV(PB1);
    TCNT0 = 0;
    OCR0A = ocr;
    TCCR0B = prescal;
}

void noTone(void)
{
    TCCR0A = 0;
    DDRB |= _BV(PB1);
    PORTB |= _BV(PB1);
}

uint16_t myrand(uint16_t n)
{
    return (ran() * (unsigned long)n) / 65536UL;
}

static const uint8_t efftimes[] PROGMEM = { 15, 10, 10, 40 };

void audioEffect(uint8_t which)
{

    if (!which) {
        effectTime = 0;

    } else {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            effectTime = pgm_read_byte(efftimes + which - 1);
            effect = which;
        }
    }
}

uint8_t effectLocking(void)
{
    return effect & ~AUDIO_EFFECT_PUSH;
}
