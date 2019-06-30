#include <stddef.h>
#include <stdbool.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <util/delay_basic.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/cpufunc.h>
#include <util/atomic.h>

#include "Barman.h"

#define T2_TWI    2             // >4,7us
#define T4_TWI    1             // >4,0us

// Defines error code generating
//#define PARAM_VERIFICATION
//#define NOISE_TESTING
//#define SIGNAL_VERIFY         // This should probably be on always.

/****************************************************************************
  Bit and byte definitions
****************************************************************************/
#define TWI_READ_BIT  0         // Bit position for R/W bit in "address byte".
#define TWI_ADR_BITS  1         // Bit position for LSB of the slave address bits in the init byte.
#define TWI_NACK_BIT  0         // Bit position for (N)ACK bit.

// Note these have been renumbered from the Atmel Apps Note. Most likely errors are now
//              lowest numbers so they're easily recognized as LED flashes.
#define USI_TWI_NO_DATA             0x08        // Transmission buffer is empty
#define USI_TWI_DATA_OUT_OF_BOUND   0x09        // Transmission buffer is outside SRAM space
#define USI_TWI_UE_START_CON        0x07        // Unexpected Start Condition
#define USI_TWI_UE_STOP_CON         0x06        // Unexpected Stop Condition
#define USI_TWI_UE_DATA_COL         0x05        // Unexpected Data Collision (arbitration)
#define USI_TWI_NO_ACK_ON_DATA      0x02        // The slave did not acknowledge  all data
#define USI_TWI_NO_ACK_ON_ADDRESS   0x01        // The slave did not acknowledge  the address
#define USI_TWI_MISSING_START_CON   0x03        // Generated Start Condition not detected on bus
#define USI_TWI_MISSING_STOP_CON    0x04        // Generated Stop Condition not detected on bus
#define USI_TWI_BAD_MEM_READ	    0x0A        // Error during external memory read

// Device dependant defines ADDED BACK IN FROM ORIGINAL ATMEL .H

#if defined(__AVR_AT90Mega169__) | defined(__AVR_ATmega169__) | \
    defined(__AVR_AT90Mega165__) | defined(__AVR_ATmega165__) | \
    defined(__AVR_ATmega325__) | defined(__AVR_ATmega3250__) | \
    defined(__AVR_ATmega645__) | defined(__AVR_ATmega6450__) | \
    defined(__AVR_ATmega329__) | defined(__AVR_ATmega3290__) | \
    defined(__AVR_ATmega649__) | defined(__AVR_ATmega6490__)
#define DDR_USI             DDRE
#define PORT_USI            PORTE
#define PIN_USI             PINE
#define PORT_USI_SDA        PORTE5
#define PORT_USI_SCL        PORTE4
#define PIN_USI_SDA         PINE5
#define PIN_USI_SCL         PINE4
#endif

#if defined(__AVR_ATtiny25__) | defined(__AVR_ATtiny45__) | defined(__AVR_ATtiny85__) | \
    defined(__AVR_AT90Tiny26__) | defined(__AVR_ATtiny26__)
#define DDR_USI             DDRB
#define PORT_USI            PORTB
#define PIN_USI             PINB
#define PORT_USI_SDA        PORTB0
#define PORT_USI_SCL        PORTB2
#define PIN_USI_SDA         PINB0
#define PIN_USI_SCL         PINB2
#endif

#if defined(__AVR_ATtiny84__) | defined(__AVR_ATtiny44__)
#define DDR_USI           DDRA
#define PORT_USI          PORTA
#define PIN_USI           PINA
#define PORT_USI_SDA      PORTA6
#define PORT_USI_SCL      PORTA4
#define PIN_USI_SDA       PINA6
#define PIN_USI_SCL       PINA4
#endif

#if defined(__AVR_AT90Tiny2313__) | defined(__AVR_ATtiny2313__)
#define DDR_USI             DDRB
#define PORT_USI            PORTB
#define PIN_USI             PINB
#define PORT_USI_SDA        PORTB5
#define PORT_USI_SCL        PORTB7
#define PIN_USI_SDA         PINB5
#define PIN_USI_SCL         PINB7
#endif

/* From the original .h
// Device dependant defines - These for ATtiny2313. // CHANGED FOR ATtiny85

    #define DDR_USI             DDRB
    #define PORT_USI            PORTB
    #define PIN_USI             PINB
    #define PORT_USI_SDA        PORTB0   // was PORTB5 - N/U
    #define PORT_USI_SCL        PORTB2   // was PORTB7 - N/U
    #define PIN_USI_SDA         PINB0    // was PINB5
    #define PIN_USI_SCL         PINB2    // was PINB7
*/

// General defines
#define TRUE  1
#define FALSE 0

//********** Prototypes **********//

static void USI_TWI_Master_Initialise(void);
//static unsigned char USI_TWI_Start_Random_Read( unsigned char * , unsigned char );
static unsigned char USI_TWI_Start_Read_Write(unsigned char *, unsigned char);
static unsigned char USI_TWI_Master_Stop(void);
//static unsigned char USI_TWI_Get_State_Info(void);

static unsigned char USI_TWI_Start_Transceiver_With_Data(unsigned char *,
                                                         unsigned char);
static unsigned char USI_TWI_Master_Transfer(unsigned char);
static unsigned char USI_TWI_Master_Start(void);

union USI_TWI_state {
    unsigned char errorState;   // Can reuse the TWI_state for error states since it will not be needed if there is an error.
    struct {
        unsigned char addressMode:1;
        unsigned char masterWriteDataMode:1;
        unsigned char memReadMode:1;
        unsigned char unused:5;
    };
} USI_TWI_state;

/*---------------------------------------------------------------
 USI TWI single master initialization function
---------------------------------------------------------------*/
static void USI_TWI_Master_Initialise(void)
{
    PORT_USI |= (1 << PIN_USI_SDA);     // Enable pullup on SDA, to set high as released state.
    PORT_USI |= (1 << PIN_USI_SCL);     // Enable pullup on SCL, to set high as released state.

    DDR_USI |= (1 << PIN_USI_SCL);      // Enable SCL as output.
    DDR_USI |= (1 << PIN_USI_SDA);      // Enable SDA as output.

    USIDR = 0xFF;               // Preload dataregister with "released level" data.
    USICR = (0 << USISIE) | (0 << USIOIE) |     // Disable Interrupts.
        (1 << USIWM1) | (0 << USIWM0) | // Set USI in Two-wire mode.
        (1 << USICS1) | (0 << USICS0) | (1 << USICLK) | // Software stobe as counter clock source
        (0 << USITC);
    USISR = (1 << USISIF) | (1 << USIOIF) | (1 << USIPF) | (1 << USIDC) |       // Clear flags,
        (0x0 << USICNT0);       // and reset counter.
}

/*---------------------------------------------------------------
Use this function to get hold of the error message from the last transmission
---------------------------------------------------------------*/
/*
static unsigned char USI_TWI_Get_State_Info(void)
{
    return (USI_TWI_state.errorState);  // Return error state.
}
*/
/*---------------------------------------------------------------
 USI Random (memory) Read function. This function sets up for call
 to USI_TWI_Start_Transceiver_With_Data which does the work.
 Doesn't matter if read/write bit is set or cleared, it'll be set
 correctly in this function.
 
 The msgSize is passed to USI_TWI_Start_Transceiver_With_Data.
 
 Success or error code is returned. Error codes are defined in 
 USI_TWI_Master.h
---------------------------------------------------------------*/
/*---------------------------------------------------------------
 USI Normal Read / Write Function
 Transmit and receive function. LSB of first byte in buffer 
 indicates if a read or write cycles is performed. If set a read
 operation is performed.

 Function generates (Repeated) Start Condition, sends address and
 R/W, Reads/Writes Data, and verifies/sends ACK.
 
 Success or error code is returned. Error codes are defined in 
 USI_TWI_Master.h
---------------------------------------------------------------*/
static unsigned char USI_TWI_Start_Read_Write(unsigned char *msg,
                                              unsigned char msgSize)
{

    USI_TWI_state.errorState = 0;       // Clears all mode bits also

    return (USI_TWI_Start_Transceiver_With_Data(msg, msgSize));

}

/*---------------------------------------------------------------
 USI Transmit and receive function. LSB of first byte in buffer 
 indicates if a read or write cycles is performed. If set a read
 operation is performed.

 Function generates (Repeated) Start Condition, sends address and
 R/W, Reads/Writes Data, and verifies/sends ACK.
 
 This function also handles Random Read function if the memReadMode
 bit is set. In that case, the function will:
 The address in memory will be the second
 byte and is written *without* sending a STOP. 
 Then the Read bit is set (lsb of first byte), the byte count is 
 adjusted (if needed), and the function function starts over by sending
 the slave address again and reading the data.
 
 Success or error code is returned. Error codes are defined in 
 USI_TWI_Master.h
---------------------------------------------------------------*/
static unsigned char USI_TWI_Start_Transceiver_With_Data(unsigned char *msg,
                                                         unsigned char msgSize)
{
    unsigned char const tempUSISR_8bit = (1 << USISIF) | (1 << USIOIF) | (1 << USIPF) | (1 << USIDC) |  // Prepare register value to: Clear flags, and
        (0x0 << USICNT0);       // set USI to shift 8 bits i.e. count 16 clock edges.
    unsigned char const tempUSISR_1bit = (1 << USISIF) | (1 << USIOIF) | (1 << USIPF) | (1 << USIDC) |  // Prepare register value to: Clear flags, and
        (0xE << USICNT0);       // set USI to shift 1 bit i.e. count 2 clock edges.
    unsigned char *savedMsg;
    unsigned char savedMsgSize;

//This clear must be done before calling this function so that memReadMode can be specified.
//  USI_TWI_state.errorState = 0;                               // Clears all mode bits also

    USI_TWI_state.addressMode = TRUE;   // Always true for first byte

    if (!(*msg & (1 << TWI_READ_BIT)))  // The LSB in the address byte determines if is a masterRead or masterWrite operation.
    {
        USI_TWI_state.masterWriteDataMode = TRUE;
    }
//      if (USI_TWI_state.memReadMode)
//      {
    savedMsg = msg;
    savedMsgSize = msgSize;
//      }

    if (!USI_TWI_Master_Start()) {
        return (FALSE);         // Send a START condition on the TWI bus.
    }

/*Write address and Read/Write data */
    do {
        /* If masterWrite cycle (or inital address tranmission) */
        if (USI_TWI_state.addressMode || USI_TWI_state.masterWriteDataMode) {
            /* Write a byte */
            PORT_USI &= ~(1 << PIN_USI_SCL);    // Pull SCL LOW.
            USIDR = *(msg++);   // Setup data.
            USI_TWI_Master_Transfer(tempUSISR_8bit);    // Send 8 bits on bus.

            /* Clock and verify (N)ACK from slave */
            DDR_USI &= ~(1 << PIN_USI_SDA);     // Enable SDA as input.
            if (USI_TWI_Master_Transfer(tempUSISR_1bit) & (1 << TWI_NACK_BIT)) {
                if (USI_TWI_state.addressMode)
                    USI_TWI_state.errorState = USI_TWI_NO_ACK_ON_ADDRESS;
                else
                    USI_TWI_state.errorState = USI_TWI_NO_ACK_ON_DATA;
                return (FALSE);
            }

            if ((!USI_TWI_state.addressMode) && USI_TWI_state.memReadMode)      // means memory start address has been written
            {
                msg = savedMsg; // start at slave address again
                *(msg) |= (TRUE << TWI_READ_BIT);       // set the Read Bit on Slave address
                USI_TWI_state.errorState = 0;
                USI_TWI_state.addressMode = TRUE;       // Now set up for the Read cycle
                msgSize = savedMsgSize; // Set byte count correctly
                // NOte that the length should be Slave adrs byte + # bytes to read + 1 (gets decremented below)
                if (!USI_TWI_Master_Start()) {
                    USI_TWI_state.errorState = USI_TWI_BAD_MEM_READ;
                    return (FALSE);     // Send a START condition on the TWI bus.
                }
            } else {
                USI_TWI_state.addressMode = FALSE;      // Only perform address transmission once.
            }
        }
        /* Else masterRead cycle */
        else {
            /* Read a data byte */
            DDR_USI &= ~(1 << PIN_USI_SDA);     // Enable SDA as input.
            *(msg++) = USI_TWI_Master_Transfer(tempUSISR_8bit);

            /* Prepare to generate ACK (or NACK in case of End Of Transmission) */
            if (msgSize == 1)   // If transmission of last byte was performed.
            {
                USIDR = 0xFF;   // Load NACK to confirm End Of Transmission.
            } else {
                USIDR = 0x00;   // Load ACK. Set data register bit 7 (output for SDA) low.
            }
            USI_TWI_Master_Transfer(tempUSISR_1bit);    // Generate ACK/NACK.
        }
    } while (--msgSize);        // Until all data sent/received.

    // usually a stop condition is sent here, but TinyWireM needs to choose whether or not to send it

/* Transmission successfully completed*/
    return (TRUE);
}

/*---------------------------------------------------------------
 Core function for shifting data in and out from the USI.
 Data to be sent has to be placed into the USIDR prior to calling
 this function. Data read, will be return'ed from the function.
---------------------------------------------------------------*/
static unsigned char USI_TWI_Master_Transfer(unsigned char temp)
{
    USISR = temp;               // Set USISR according to temp.
    // Prepare clocking.
    temp = (0 << USISIE) | (0 << USIOIE) |      // Interrupts disabled
        (1 << USIWM1) | (0 << USIWM0) | // Set USI in Two-wire mode.
        (1 << USICS1) | (0 << USICS0) | (1 << USICLK) | // Software clock strobe as source.
        (1 << USITC);           // Toggle Clock Port.
    do {
        _NOP();                 //_delay_us(T2_TWI);
        USICR = temp;           // Generate positve SCL edge.
        while (!(PIN_USI & (1 << PIN_USI_SCL))) ;       // Wait for SCL to go high.
        _delay_loop_1(2);       //_delay_us(T4_TWI);
        USICR = temp;           // Generate negative SCL edge.
    } while (!(USISR & (1 << USIOIF))); // Check for transfer complete.

    _NOP();                     //_delay_us(T2_TWI);
    temp = USIDR;               // Read out data.
    USIDR = 0xFF;               // Release SDA.
    DDR_USI |= (1 << PIN_USI_SDA);      // Enable SDA as output.

    return temp;                // Return the data from the USIDR
}

/*---------------------------------------------------------------
 Function for generating a TWI Start Condition. 
---------------------------------------------------------------*/
static unsigned char USI_TWI_Master_Start(void)
{
/* Release SCL to ensure that (repeated) Start can be performed */
    PORT_USI |= (1 << PIN_USI_SCL);     // Release SCL.
    while (!(PORT_USI & (1 << PIN_USI_SCL))) ;  // Verify that SCL becomes high.
    _NOP();                     //_delay_us(T2_TWI);

/* Generate Start Condition */
    PORT_USI &= ~(1 << PIN_USI_SDA);    // Force SDA LOW.
    _delay_loop_1(2);           //_delay_us(T4_TWI);                         
    PORT_USI &= ~(1 << PIN_USI_SCL);    // Pull SCL LOW.
    PORT_USI |= (1 << PIN_USI_SDA);     // Release SDA.

#ifdef SIGNAL_VERIFY
    if (!(USISR & (1 << USISIF))) {
        USI_TWI_state.errorState = USI_TWI_MISSING_START_CON;
        return (FALSE);
    }
#endif
    return (TRUE);
}

/*---------------------------------------------------------------
 Function for generating a TWI Stop Condition. Used to release 
 the TWI bus.
---------------------------------------------------------------*/
static unsigned char USI_TWI_Master_Stop(void)
{
    PORT_USI &= ~(1 << PIN_USI_SDA);    // Pull SDA low.
    PORT_USI |= (1 << PIN_USI_SCL);     // Release SCL.
    while (!(PIN_USI & (1 << PIN_USI_SCL))) ;   // Wait for SCL to go high.  
    _delay_loop_1(2);           //_delay_us(T4_TWI);
    PORT_USI |= (1 << PIN_USI_SDA);     // Release SDA.
    _NOP();                     //_delay_us(T2_TWI);

#ifdef SIGNAL_VERIFY
    if (!(USISR & (1 << USIPF))) {
        USI_TWI_state.errorState = USI_TWI_MISSING_STOP_CON;
        return (FALSE);
    }
#endif

    return (TRUE);
}

//#define USI_BUF_SIZE 72
#define USI_SEND         0      // indicates sending to TWI
#define USI_RCVE         1      // indicates receiving from TWI

//uint8_t USI_Buf[USI_BUF_SIZE];             // holds I2C send and receive data
uint8_t USI_BufIdx = 0;         // current number of bytes in the send buff
uint8_t *USI_Buf, USI_BUF_SIZE;

void TWM_begin(void)
{                               // initialize I2C lib
    USI_TWI_Master_Initialise();
}

void TWM_beginTransmission(uint8_t slaveAddr, uint8_t * buf, uint8_t bufsize)
{                               // setup address & write bit
    USI_Buf = buf;
    USI_BUF_SIZE = bufsize;
    USI_BufIdx = 0;
    USI_Buf[USI_BufIdx] = (slaveAddr << TWI_ADR_BITS) | USI_SEND;
}

size_t TWM_write(uint8_t data)
{                               // buffers up data to send
    if (USI_BufIdx >= USI_BUF_SIZE - 1)
        return 0;               // dont blow out the buffer
    USI_BufIdx++;               // inc for next byte in buffer
    USI_Buf[USI_BufIdx] = data;
    return 1;
}
/* EndTransmission powinna być void */
#if 0
uint8_t TWM_endTransmission(uint8_t stop)
{                               // actually sends the buffer
    bool xferOK = false;
    uint8_t errorCode = 0;
    xferOK = USI_TWI_Start_Read_Write(USI_Buf, USI_BufIdx + 1); // core func that does the work
    USI_BufIdx = 0;
    if (xferOK) {
        if (stop) {
            errorCode = USI_TWI_Master_Stop();
            if (errorCode == 0) {
                errorCode = USI_TWI_Get_State_Info();
                return errorCode;
            }
        }
        return 0;
    } else {                    // there was an error
        errorCode = USI_TWI_Get_State_Info();   // this function returns the error number
        return errorCode;
    }
}
#else
void TWM_endTransmission(void) // simplified version
{    
   // bool xferOK = false;
    if (USI_TWI_Start_Read_Write(USI_Buf, USI_BufIdx + 1)) {
        USI_TWI_Master_Stop();
    }
}
#endif
