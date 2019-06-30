#include "Barman.h"
#include <avr/sleep.h>
#include <stdlib.h>

#include "Common.c"

static const uint8_t bitmaps[][8] PROGMEM = {
#include "chargen.h"
};

static const uint8_t digits[][4] PROGMEM = {
    {0x3e, 0x41, 0x01, 0x3e},
    {0, 2, 0x7f, 0},
    {0x72, 0x49, 0x49, 0x46},
    {0x22, 0x41, 0x49, 0x36},
    {0x0f, 0x08, 0x08, 0x7f},
    {0x4f, 0x49, 0x49, 0x31},
    {0x3e, 0x49, 0x49, 0x32},
    {0x1, 0x71, 0x0b, 0x03},
    {0x36, 0x49, 0x49, 0x36},
    {0x26, 0x49, 0x49, 0x3e}
};

void putNumber(uint8_t * buf, uint16_t number)
{

    int8_t i;
    memset(buf, 0, 24);
    buf += 20;
    memcpy_P(buf, digits[number % 10], 4);
    number /= 10;
    for (i = 0; i < 4 && number; i++) {
        buf -= 5;
        memcpy_P(buf, digits[number % 10], 4);
        number /= 10;
    }
}

#define MAXTABLE 5

uint8_t cnoMove;
uint8_t tableMugsOn[4];
uint8_t tableMugs[4][8];
uint8_t tablePlaces[4];
uint16_t barman, old_barman;    // 13238
uint16_t bcmr;
uint8_t newC;
uint8_t nccnt;
uint8_t pauseMode;
uint32_t pauseStart;
uint16_t score;
uint8_t lives;

#define BARMAN_X(a) ((a) & 127)
#define BARMAN_Y(a) (((a) >> 7) & 3)
#define SET_BARMAN_X(a,b) (a) = ((a) & 0xff80) | (b)
#define SET_BARMAN_Y(a,b) (a) = ((a) & 0xfe7f) | ((b) << 7)

#define BARMAN_DIR_P (1<<9)
#define BARMAN_BARREL_P (1<<10)
#define BARMAN_TABLE_P (1<<11)
#define BARMAN_MUG_P (1<<12)
#define BARMAN_OUTS_P (1<<13)

uint8_t readADC(uint8_t pin)
{
    ADMUX = (1 << ADLAR) | pin;
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC)) ;
    ADCSRA |= (1 << ADIF);
    return ADCH;
}

#define KEY_UP 1
#define KEY_DOWN 2
#define KEY_LEFT 4
#define KEY_RIGHT 8
#define KEY_FIRE1 16
#define KEY_FIRE2 32
#define KEY_FIRE 48
#define KEY_PAUSE 64

#define KMASK_H (KEY_LEFT | KEY_RIGHT)
#define KMASK_V (KEY_UP | KEY_DOWN)
#define KMASK_FIRE (KEY_FIRE | KEY_PAUSE)

#define MIDPOT_X 132
#define MIDPOT_Y 132
#define LO1_X 90
#define LO2_X 80
#define HI1_X 150
#define HI2_X 160
#define LO1_Y 90
#define LO2_Y 80
#define HI1_Y 150
#define HI2_Y 160
#define PAUSE_LEVEL 170
#define FIRE1_LEVEL 195
#define FIRE2_LEVEL 215

uint8_t stick = 0;
uint32_t lastMillikeys = 0;

uint8_t getKey(void)
{
    uint8_t keys = 0;
    uint8_t x, y, z;
    uint8_t oldstick = stick;

    ADCSRA |= 1 << ADEN;
    z = readADC(0);
    for (;;) {
        while (!(PINB & _BV(PB1))) ;
        x = readADC(2);
        y = readADC(3);
        if (PINB & _BV(PB1))
            break;
    }
    ADCSRA &= ~(1 << ADEN);

    if (z < PAUSE_LEVEL)
        stick = (stick & ~KEY_FIRE) | KEY_PAUSE;
    else if (z < FIRE1_LEVEL)
        stick = (stick & ~(KEY_FIRE2 | KEY_PAUSE)) | KEY_FIRE1;
    else if (z < FIRE2_LEVEL)
        stick = (stick & ~(KEY_FIRE1 | KEY_PAUSE)) | KEY_FIRE2;
    else
        stick &= ~KMASK_FIRE;

    if (x < LO2_X)
        stick = (stick & ~KEY_RIGHT) | KEY_LEFT;
    else if (x > HI2_X)
        stick = (stick & ~KEY_LEFT) | KEY_RIGHT;
    else if (x > LO1_X && x < HI1_X)
        stick &= ~KMASK_H;

    if (y < LO2_Y)
        stick = (stick & ~KEY_DOWN) | KEY_UP;
    else if (y > HI2_Y)
        stick = (stick & ~KEY_UP) | KEY_DOWN;
    else if (y > LO1_Y && y < HI1_Y)
        stick &= ~KMASK_V;

    keys = stick & ~oldstick;
    if (keys)
        lastMillikeys = millis();
    else if (millis() > lastMillikeys + (pauseMode ? 300000UL : 30000UL)) {
        cli();
        display_displayOff();
        DDRB = 0;
        PORTB = 0;
        set_sleep_mode(SLEEP_MODE_PWR_DOWN);
        sleep_enable();
        sleep_bod_disable();
        sleep_cpu();
    }
    return keys;
}

#define SHAPE_LEFT_TABLE 0
#define SHAPE_LEFT_TABLE_BROKEN 2
#define SHAPE_LEFT_WALL 1
#define SHAPE_RIGHT_TABLE 3
#define SHAPE_MUG_EMPTY_T 4
#define MASK_MUG_EMPTY_T 5
#define SHAPE_MUG_FULL_T 6
#define MASK_MUG_FULL_T 7

#define SHAPE_CLIENT_WAITS 8
#define SHAPE_CLIENT_WAITS2 10
#define SHAPE_CLIENT_KEEPFULL 12
#define SHAPE_CLIENT_DRINKS 14
#define SHAPE_BARMAN 17
#define SHAPE_BARMAN_MUG 29
#define SHAPE_BARREL 33
#define SHAPE_RIGHT_TABLE_BROKEN 35
#define SHAPE_BARMAN_OUTS 37
#define SHAPE_PIVO 43

void putShape(uint8_t * buffer, const uint8_t * mask, const uint8_t * shape)
{
    uint8_t i;
    for (i = 8; i; i--) {
        //if (mask) 
        *buffer &= pgm_read_byte(mask++);
        *buffer++ |= pgm_read_byte(shape++);

        //*buffer++ = ((*buffer) & pgm_read_byte(mask++)) | pgm_read_byte(shape++);
    }
}

void drawBuffer(uint8_t x, uint8_t y, uint8_t * buf1, uint8_t * buf2,
                uint8_t size)
{
    int8_t i, j;
    uint8_t *workbuf = buf1 - 4;
    for (i = 0; i < size;) {
        if (buf1[i] == buf2[i]) {
            i++;
            continue;
        }
        for (j = i; j < size; j++)
            if (buf1[j] == buf2[i])
                break;
        display_drawPartial(x + i, y, buf2 + i, j - i, workbuf);
        i = j;
    }
}

void drawScore(uint8_t w)
{
    uint8_t buf[52];
    static uint16_t oldScore = 0;
    if (w)
        memset(buf + 4, 0, 24);
    else
        putNumber(buf + 4, oldScore);
    putNumber(buf + 28, score);
    oldScore = score;
    drawBuffer(104, 0, buf + 4, buf + 4 + 24, 24);
}

uint16_t clientData[4 * MAXTABLE + 6];

#define CLIENT_PHASE(a) (((a) >> 7) & 7)
#define CLIENT_PHASE_S(a) ((a) & (7 << 7))
#define SET_CLIENT_PHASE(a, b) (a) = ((a) & 0xfc7f) | ((b) << 7)

#define CLIENT_POSX(a) ((a) & 0x7f)
#define CLIENT_POSX_S(a) ((a).bb & 0x7f)

#define CLIENT_PLACE(a) (((a) >> 3) & 0x0f)
#define SET_CLIENT_POSX(a, b) (a) = ((a) & 0xff80) | (b)

#define CLIENT_BEER_COUNT(a) (((a) >> 10) & 3)
#define SET_CLIENT_BEER_COUNT(a, b) (a) = ((a) & 0xf3ff) | ((b) << 10)
#define DEC_CLIENT_BEER_COUNT(a) (a) -= 1<<10

#define CLIENT_BEER_COUNT_S(a) ((a) & (3 << 10))

#define CLIENT_DRINK_PHASE(a) (((a) >> 12) & 0x0f)
#define CLIENT_DRINK_PHASE_BX(a) (((a) >> 9) & (0x0f << 3))

#define SET_CLIENT_DRINK_PHASE(a, b) (a) = ((a) & 0x0fff) | ((b) << 12)
#define SET_CLIENT_PHASES(a, ph, phd) (a) = ((a) & 0x0c7f) | ((phd) << 12) | ((ph) << 7)

#define CLIENT_PHASE_WAITS 1
#define CLIENT_PHASE_DRINKS 2
#define CLIENT_PHASE_OUTS1 3
#define CLIENT_PHASE_GOTOPLACE 4

#define NEW_CLIENT(beers) ((CLIENT_PHASE_WAITS << 7) | ((beers) << 10))

void drawTheClientsS(uint8_t * buf, uint16_t * clients, uint8_t cno,
                     uint8_t phn, uint16_t man)
{
    memset(buf, 0, 72);
    uint8_t i;
    for (i = cno; i; i--) {
        if (CLIENT_PHASE_S(clients[i - 1]) != CLIENT_PHASE_OUTS1 << 7 &&
            CLIENT_PHASE_S(clients[i - 1]) != CLIENT_PHASE_GOTOPLACE << 7)
            continue;
        putShape(buf + CLIENT_POSX(clients[i - 1]),
                 bitmaps[SHAPE_CLIENT_KEEPFULL + 1],
                 bitmaps[SHAPE_CLIENT_KEEPFULL]);
    }
    for (i = cno; i; i--) {
        if (CLIENT_PHASE_S(clients[i - 1]) != CLIENT_PHASE_DRINKS << 7)
            continue;
        putShape(buf + CLIENT_POSX(clients[i - 1]),
                 bitmaps[SHAPE_CLIENT_DRINKS + 1],
                 bitmaps[SHAPE_CLIENT_DRINKS + 2 * phn]);
    }
    for (i = cno; i; i--) {
        if (CLIENT_PHASE_S(clients[i - 1]) != CLIENT_PHASE_WAITS << 7)
            continue;
        uint8_t n = SHAPE_CLIENT_WAITS;
        if (clients[i - 1] & 4)
            n += 2;
        putShape(buf + CLIENT_POSX(clients[i - 1]), bitmaps[n + 1], bitmaps[n]);
    }
    if (BARMAN_Y(man) == cno - MAXTABLE && (man & BARMAN_TABLE_P)) {
        uint8_t n = SHAPE_BARMAN + 2 - ((man & BARMAN_DIR_P) >> 8);
        putShape(buf + BARMAN_X(man), bitmaps[n + 1], bitmaps[n]);
    }

}

void drawClients(uint16_t * oldies, uint16_t * newbies, uint8_t cno)
{
    uint8_t buffers[2 * 72 + 4];
    drawTheClientsS(buffers + 4, oldies, cno, (cnoMove & 1) ^ 1, old_barman);
    drawTheClientsS(buffers + 4 + 72, newbies, cno, cnoMove & 1, barman);
    drawBuffer(40 - 4 * cno, 2 * (cno - 5), buffers + 4, buffers + 4 + 72,
               8 * (cno + 1));
}

void drawTheTableS(uint8_t * buf, uint8_t map, uint8_t * mugs, uint8_t y,
                   uint16_t man)
{
    memset(buf, 0x41, 72);
    uint8_t i;
    for (i = 0; i < 8; i++) {
        if ((map & (1 << i)) && !(mugs[i] & 0x80)) {
            putShape(buf + (mugs[i] & 0x7f), bitmaps[MASK_MUG_EMPTY_T],
                     bitmaps[SHAPE_MUG_EMPTY_T]);
        }
    }
    for (i = 0; i < 8; i++) {
        if ((map & (1 << i)) && (mugs[i] & 0x80)) {
            putShape(buf + (mugs[i] & 0x7f), bitmaps[MASK_MUG_FULL_T],
                     bitmaps[SHAPE_MUG_FULL_T]);
        }
    }
    if (BARMAN_Y(man) == y) {
        if (man & BARMAN_TABLE_P) {
            uint8_t n = SHAPE_BARMAN + 6 - ((man & BARMAN_DIR_P) >> 8);
            putShape(buf + BARMAN_X(man), bitmaps[n + 1], bitmaps[n]);
        } else if (man & BARMAN_OUTS_P) {
            uint8_t *b = buf + BARMAN_X(man);
            putShape(b, bitmaps[SHAPE_BARMAN_OUTS + 3],
                     bitmaps[SHAPE_BARMAN_OUTS]);
            putShape(b + 8, bitmaps[SHAPE_BARMAN_OUTS + 4],
                     bitmaps[SHAPE_BARMAN_OUTS + 1]);
            putShape(b + 16, bitmaps[SHAPE_BARMAN_OUTS + 5],
                     bitmaps[SHAPE_BARMAN_OUTS + 2]);
        }

    }
}

void drawTheTable(uint8_t oldmask, uint8_t newmask, uint8_t * oldmugs,
                  uint8_t * newmugs, uint8_t cno)
{
    uint8_t buffer[4 + 2 * 72];
    drawTheTableS(buffer + 4, oldmask, oldmugs, cno, old_barman);
    drawTheTableS(buffer + 4 + 72, newmask, newmugs, cno, barman);
    drawBuffer(20 - 4 * cno, 2 * cno + 1, buffer + 4, buffer + 4 + 72,
               48 + 8 * cno);
}

void initStart(void)
{
    static const uint8_t s[] PROGMEM = { 0, 1, 2, 3, 1, 4,
        5, 6, 4, 7, 9, 8
    };
    display_clear();
    int8_t i, n = 0;
    if (!lives)
        n = 6;
    for (i = 0; i < 6; i++)
        display_drawShape(40 + 8 * i, 3,
                          bitmaps[SHAPE_PIVO + pgm_read_byte(s + n++)]);
    for (i = 0; i < lives; i++)
        display_drawShape(40 + 8 * i, 4, bitmaps[SHAPE_PIVO + 10]);
    if (lives != 4) {
        uint8_t buf[52];
        putNumber(buf + 28, score);
        display_drawPartial(64, 4, buf + 28, 24, buf);
    }

}

void initBar(void)
{
    display_clear();
    int8_t i;
    uint8_t buf[72 + 72 + 4];
    for (i = 0; i < 4; i++) {
        display_drawShape(12 - 4 * i, 2 * i, bitmaps[SHAPE_LEFT_WALL]);
        display_drawShape(12 - 4 * i, 2 * i + 1, bitmaps[SHAPE_LEFT_TABLE]);
        memset(buf, 0x41, 72);
        display_drawPartial(20 - 4 * i, 2 * i + 1, buf, 49 + 8 * i, buf + 72);
        display_drawShape(68 + 4 * i, 2 * i + 1, bitmaps[SHAPE_RIGHT_TABLE]);
    }
    barman = 0;
    nccnt = 0;
    bcmr = 20;
    memset(clientData, 0, sizeof(clientData));
    memset(tableMugs, 0, sizeof(tableMugs));
    memset(tableMugsOn, 0, sizeof(tableMugsOn));
    memset(tablePlaces, 0, sizeof(tablePlaces));
    for (i = 0; i < lives; i++)
        display_drawShape(120 - 8 * i, 1, bitmaps[SHAPE_PIVO + 10]);
    drawScore(1);
}

uint8_t moveClients(uint16_t * clients, uint8_t cno)
{
    uint8_t i, j, k, table = cno - MAXTABLE;
    uint8_t mugmask = tableMugsOn[table], *mugs = tableMugs[table], rc =
        0, xfree = 1;

    for (i = 8, j = 0; i; i--) {
        uint8_t n = CLIENT_PHASE(clients[i - 1]);
        if (!n)
            continue;
        j++;
        uint8_t x = CLIENT_POSX(clients[i - 1]);
        uint8_t bx;
        if (n == CLIENT_PHASE_WAITS) {

            if (x < 13)
                xfree = 0;
            if (x < 8 * cno) {
                uint8_t kmin = (0x80 | (x - 4)), kmax = x + 0x84;
                for (k = 0; k < 8; k++) {
                    if ((mugmask & (1 << k)) &&
                        (mugs[k] >= kmin) && (mugs[k] <= kmax)) {
                        uint8_t beers = CLIENT_BEER_COUNT(clients[i - 1]);
                        if (beers) {
                            int8_t el, mp = 8;
                            for (el = 0; el <= x >> 3; el++)
                                if (!(tablePlaces[table] & (1 << el))) {
                                    mp = el;
                                    break;
                                }
                            if (mp == 8) {
                                SET_CLIENT_PHASE(clients[i - 1],
                                                 CLIENT_PHASE_OUTS1);
                                score += 10;
                            } else {
                                tablePlaces[table] |= 1 << mp;
                                SET_CLIENT_PHASES(clients[i - 1],
                                                  CLIENT_PHASE_GOTOPLACE, mp);
                                DEC_CLIENT_BEER_COUNT(clients[i - 1]);
                                score += 15;
                            }
                        } else {
                            SET_CLIENT_PHASE(clients[i - 1],
                                             CLIENT_PHASE_OUTS1);
                            score += 20;
                        }
                        tableMugsOn[table] &= ~(1 << k);
                        continue;
                    }
                }
                if ((cnoMove & 1) == (cno & 1)) {
                    SET_CLIENT_POSX(clients[i - 1], x + 1);
                }
            } else {
                rc = RETCODE_ANGRY_CLIENT;
                barman = BARMAN_OUTS_P | 24 | ((4 + 128) * table);      // + (table <<7);
            }
        } else if (n == CLIENT_PHASE_GOTOPLACE) {

            bx = CLIENT_DRINK_PHASE_BX(clients[i - 1]);
            if (x >= bx + 8) {
                SET_CLIENT_POSX(clients[i - 1], x - 1);
            } else {
                clients[i - 1] = (clients[i - 1] & 0x0c00) |
                    (15 << 12) | (CLIENT_PHASE_DRINKS << 7) | bx;
                /*
                   SET_CLIENT_POSX(clients[i-1], bx);
                   SET_CLIENT_DRINK_PHASE(clients[i-1], 15);
                   SET_CLIENT_PHASE(clients[i-1], CLIENT_PHASE_DRINKS);
                 */
            }
        } else if (n == CLIENT_PHASE_DRINKS) {

            bx = CLIENT_DRINK_PHASE(clients[i - 1]);
            if (bx--) {
                if (!(cnoMove & 3))
                    SET_CLIENT_DRINK_PHASE(clients[i - 1], bx);
                continue;
            }
            for (bx = 0; bx < 8; bx++)
                if (!(tableMugsOn[table] & (1 << bx)))
                    break;
            if (bx & 8)
                continue;       /* pijemy dalej */
            tableMugsOn[table] |= 1 << bx;
            tableMugs[table][bx] = x;

            tablePlaces[table] &= ~(1 << (x >> 3));
            SET_CLIENT_PHASE(clients[i - 1], CLIENT_PHASE_WAITS);

        } else {

            if (x < 3) {
                clients[i - 1] = 0;
            } else {
                SET_CLIENT_POSX(clients[i - 1], x - 3);
            }
        }

    }
    if (rc)
        return RETCODE_ANGRY_CLIENT;
    if (!xfree || j >= cno || table + 1 != newC)
        return 0;
    for (i = cno; i; i--)
        if (!clients[i - 1]) {
            uint8_t nb = rand8() & 3;
            clients[i - 1] = NEW_CLIENT(nb);
            break;
        }
    return 0;

}

uint8_t moveMugs(uint8_t * mugmask, uint8_t * mugs, uint8_t cno)
{
    int8_t i, rc = 0;
    for (i = 0; i < 8; i++) {
        if (!(*mugmask & (1 << i)))
            continue;
        uint8_t x = mugs[i] & 0x7f;
        if (mugs[i] & 0x80) {
            if (x < 3) {
                rc |= RETCODE_CRASH_WALL;
                *mugmask &= ~(1 << i);
            } else {
                mugs[i] = (x - 3) | 0x80;
            }
        } else {
            if (x >= cno * 8 + 39) {
                if (!(barman * BARMAN_BARREL_P) && cno != BARMAN_Y(barman))
                    rc |= RETCODE_CRASH_FLOOR;
                *mugmask &= ~(1 << i);
            } else {
                if ((barman & BARMAN_TABLE_P) && (cno == BARMAN_Y(barman))) {
                    uint8_t bax = BARMAN_X(barman);
                    if (x <= bax + 4 && x > ((bax > 4 ? bax - 4 : 0))) {
                        *mugmask &= ~(1 << i);
                        continue;
                    }
                }
                mugs[i] = x + 1;
            }
        }

    }
    return rc;
}

uint8_t moveBarman(uint8_t cmd)
{
    if (!barman)
        barman = BARMAN_BARREL_P;
    if (barman & BARMAN_OUTS_P) {
        if (BARMAN_X(barman) > 2)
            barman -= 2;
        else {
            SET_BARMAN_X(barman, 0);
            return RETCODE_CRASH_MAN;
        }
        return 0;
    }
    if (effectLocking())
        return 0;
    if (barman & BARMAN_TABLE_P) {
        if (stick & KEY_LEFT) {
            barman &= ~BARMAN_DIR_P;
            if (BARMAN_X(barman) > 2)
                barman -= 2;
        } else if (stick & KEY_RIGHT) {
            barman |= BARMAN_DIR_P;
            if (BARMAN_X(barman) < 40 + BARMAN_Y(barman) * 8)
                barman += 2;
            else {
                barman =
                    (barman & ~(BARMAN_TABLE_P | BARMAN_MUG_P)) |
                    BARMAN_BARREL_P;
            }
        }
        return 0;
    }
    if (barman & BARMAN_BARREL_P) {
        if (cmd & KEY_FIRE) {
            if ((barman & BARMAN_DIR_P)) {
                if (!(barman & BARMAN_MUG_P)) {
                    audioEffect(AUDIO_EFFECT_FILL);
                    barman |= BARMAN_MUG_P;
                }
            } else if (barman & BARMAN_MUG_P) {
                uint8_t y = BARMAN_Y(barman), j;
                for (j = 0; j < 8; j++)
                    if (!(tableMugsOn[y] & (1 << j))) {
                        tableMugsOn[y] |= 1 << j;
                        tableMugs[y][j] = 0x80 | (y * 8 + 39);
                        barman &= ~BARMAN_MUG_P;
                        audioEffect(AUDIO_EFFECT_PUSH);
                        break;
                    }
            }
        }

        else if ((cmd & KEY_UP) && BARMAN_Y(barman))
            barman -= 128;
        else if ((cmd & KEY_DOWN) && BARMAN_Y(barman) < 3)
            barman += 128;
        else if (cmd & KEY_RIGHT)
            barman |= BARMAN_DIR_P;
        else if (cmd & KEY_LEFT) {
            if (barman & BARMAN_DIR_P)
                barman &= ~BARMAN_DIR_P;
            else if (!(barman & BARMAN_MUG_P)) {
                barman =
                    BARMAN_TABLE_P | (barman & 0x180) | (BARMAN_Y(barman) * 8 +
                                                         40);
            }
        }
    }
    return 0;
}

void drawBarmanS(uint8_t * buf, uint16_t man, uint8_t y)
{
    uint8_t n, my = 2 * BARMAN_Y(man);
    memset(buf, 0, 24);
    if (!man)
        return;
    memcpy_P(buf + 16, bitmaps[SHAPE_BARREL + (y & 1)], 8);
    if (!(man & BARMAN_BARREL_P))
        return;
    if (y == my) {
        n = SHAPE_BARMAN + 2 - ((man & BARMAN_DIR_P) >> 8);
        putShape(buf + 8, bitmaps[n + 3], bitmaps[n]);
    } else if (y == my + 1) {
        n = SHAPE_BARMAN + 6 - ((man & BARMAN_DIR_P) >> 8);
        putShape(buf + 8, bitmaps[n + 3], bitmaps[n]);
        if (man & BARMAN_MUG_P) {
            n = (man & BARMAN_DIR_P) ? SHAPE_BARMAN_MUG : SHAPE_BARMAN_MUG + 2;
            putShape(buf + ((man & BARMAN_DIR_P) >> 5), bitmaps[n + 1],
                     bitmaps[n]);
        }
    } else if (y == my + 2) {
        n = SHAPE_BARMAN + 10 - ((man & BARMAN_DIR_P) >> 8);
        putShape(buf + 4, bitmaps[n + 1], bitmaps[n]);
    }
}

void drawBarmanAt(uint8_t y)
{
    uint8_t buf[4 + 24 + 24];

    drawBarmanS(buf + 4, old_barman, y);
    drawBarmanS(buf + 4 + 24, barman, y);
    drawBuffer(76 + 2 * (y & 0x6), y, buf + 4, buf + 4 + 24, 24);
}

void drawBarman(void)
{
    uint8_t y;
    for (y = 0; y < 8; y++)
        drawBarmanAt(y);
}

void finishRound()
{
    initStart();
    startTimer();
    while (!(getKey() & KEY_FIRE))
        ran();                  //delayTo(50);
    if (!lives) {
        lives = 3;
        score = 0;
    } else
        lives--;
    initBar();
    startTimer();
    bcmr = 20;
}

void loop(void);
void main(void) __attribute__ ((noreturn));

void main(void)
{
    display_init();
    ADCSRA = (1 << ADPS2) | (1 << ADPS1) | (0 << ADPS0);
    DDRB |= _BV(PB1);
    PORTB |= _BV(PB1);

    TCNT0 = 0;
    OCR0A = 0;
    OCR0B = 0;
    TCCR1 = (1 << CTC1) | (6 << CS10);
    OCR1C = F_CPU / 32000UL - 1;
    TIMSK |= (1 << OCIE1A);
    sei();

    display_setContrast(63);
    lives = 4;
    score = 0;
    finishRound();
    for (;;) {
        loop();
    }
}

void loop(void)
{

    int8_t i, j, cm, bg, col;

    cm = getKey();
    if (!pauseMode && (cm & KEY_PAUSE)) {
        pauseMode = 1;
        pauseStart = millis();
    }
    if (pauseMode) {
        if (cm && millis() > pauseStart + 500) {
            pauseMode = 0;
        } else {
            delayTo(50);
            return;
        }
    }
    cnoMove = (cnoMove + 1) & 7;
    if (bcmr < 6000)
        bcmr++;

    old_barman = barman;
    col = moveBarman(cm);
    newC = 0;
    do {
        uint8_t bcnt[4], ccnt = 0, mcnt = (26 * (bcmr / 20)) / 300;
        for (i = 0, cm = MAXTABLE, bg = 0; i < 4; i++, bg += cm++) {
            bcnt[i] = cm;
            for (j = 0; j < cm; j++) {
                if (CLIENT_PHASE(clientData[j + bg])) {
                    bcnt[i]--;
                    ccnt++;
                }
            }
        }
        if (mcnt < 1)
            mcnt = 1;
        if (mcnt > 26)
            mcnt = 26;
        if (ccnt < mcnt) {
            if (!nccnt)
                nccnt = myrand(ccnt?(100 - bcmr / 100):20) + 10;
            else if (!--nccnt) {
                for (i = j = 0; i < 4; i++)
                    if (bcnt[i])
                        bcnt[j++] = i;
                newC = myrand(j) + 1;
            }
        }
    } while (0);

    for (i = 0, cm = MAXTABLE, bg = 0; i < 4; i++, bg += cm++) {
        uint8_t oldmugs[8], oldmugmask, collision = 0;
        uint16_t old_clients[cm];
        memcpy(oldmugs, tableMugs[i], 8);
        oldmugmask = tableMugsOn[i];
        memcpy(old_clients, clientData + bg, 2 * cm);
        if (!(barman & BARMAN_OUTS_P)) {
            collision = moveMugs(&tableMugsOn[i], tableMugs[i], i);
            collision |= moveClients(clientData + bg, cm);
        }
        if (collision & RETCODE_CRASH_WALL)
            display_drawShape(12 - 4 * i, 2 * i + 1,
                              bitmaps[SHAPE_LEFT_TABLE_BROKEN]);
        if (collision & RETCODE_CRASH_FLOOR) {
            display_drawShape(68 + 4 * i, 2 * i + 1,
                              bitmaps[SHAPE_RIGHT_TABLE_BROKEN]);
            if (i < 3)
                display_drawShape(72 + 4 * i, 2 * i + 2,
                                  bitmaps[SHAPE_RIGHT_TABLE_BROKEN + 1]);
        }
        drawTheTable(oldmugmask, tableMugsOn[i], oldmugs, tableMugs[i], i);
        drawClients(old_clients, clientData + bg, cm);
        col |= collision;
    }
    drawBarman();
    if (col & 0x07)
        audioEffect((col & 2) + 2);
    drawScore(0);
    delayTo(50);

    if (col & 0xd) {
        delay(1500);
        audioEffect(0);
        finishRound();
    }

}
