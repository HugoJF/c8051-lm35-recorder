#include <stdio.h>
#include <stdlib.h>
#include "config.c"
#include "def_pinos.h"

// BIG8051
// Timer0 -> Delay
// Timer1 -> UART
// Timer2 -> Timer

// Macros to make code more readable
#define true 1
#define false 0

// Operations are now ASCII chars!
#define OP_WRITE '0'
#define OP_READ '1'

// EEPROM stuff
#define EEPROM_WRITE 0
#define EEPROM_READ 1
#define DEVICE 0xA0

// AMUX selection
#define AIN_0_0 0
#define AIN_0_1 1
#define AIN_0_2 2
#define AIN_0_3 3
#define HVDA 4
#define AGND 5
#define P3EVEN 6
#define P3ODD 7
#define TEMP 8

// Gain
#define G1 0
#define G2 1
#define G4 2
#define G8 3
#define G16 4
#define GHALF 6

// Ratio = 4096 (2^n) / 2.43 (VREF)
#define RATIO 1685.5967

// Last key that was pressed
volatile unsigned char keypress = NULL_KEY;


unsigned char g_bRecording = 0;


unsigned char esc_byte_cntr(unsigned char device, __bit RW) {
    STA = 1;
    SI = 0;

    while (SI == 0);
    if (SMB0STA != 0x08 && SMB0STA != 0x10) {
        return SMB0STA;
    }

    SMB0DAT = (device & 0xfe) | RW;
    STA = 0;
    SI = 0;
    while (SI == 0);

    if (RW == EEPROM_WRITE) {
        if (SMB0STA != 0x18) return SMB0STA;
    } else {
        if (SMB0STA != 0x40) return SMB0STA;
    }

    return 0;
}


unsigned char write_data_byte(unsigned char data) {
    SMB0DAT = data;

    SI = 0;
    while (SI == 0);

    if (SMB0STA != 0x28) {
        return SMB0STA;
    } else {
        return 0;
    }
}


int write_eeprom(unsigned char device, unsigned char address, unsigned char data) {
    unsigned char ret;

    ret = esc_byte_cntr(device, EEPROM_WRITE);
    if (ret != 0) return - (int) ret;

    ret = write_data_byte(address);
    if (ret != 0) return - (int) ret;

    ret = write_data_byte(data);
    if (ret != 0) return - (int) ret;

    STO = 1;
    SI = 0;
    while (STO == 1);

    while (1) {
        ret = esc_byte_cntr(device, EEPROM_WRITE);
        if (ret == 0) break;
        if (ret != 0x20) return - (int) ret;
    }

    return 0;
}


int read_eeprom(unsigned char device, unsigned char address) {
    int dado;
    unsigned char ret;

    ret = esc_byte_cntr(device, EEPROM_WRITE);
    if (ret != 0) return - (int) ret;

    ret = write_data_byte(address);
    if (ret != 0) return - (int) ret;

    ret = esc_byte_cntr(device, EEPROM_READ);
    if (ret != 0) return - (int) ret;

    AA = 0;
    SI = 0;
    while (SI == 0);

    if (SMB0STA != 0x58) {
        return - (int) SMB0STA;
    }
    dado = (int) SMB0DAT;

    STO = 1;
    SI = 0;
    while (STO == 1);

    return dado;
}



void write_dac (unsigned char v) {
    unsigned int dac;
    dac = v * RATIO;
    DAC0L = dac;
    DAC0H = dac >> 8;
}


unsigned int read_dac (unsigned char channel, unsigned char gain) {
    AMX0SL = channel;
    ADC0CF = (ADC0CF & 0xf8) | gain;
    
    // Start convertion
    AD0BUSY = 1;
    while (AD0BUSY);

    return (ADC0H << 8) | ADC0L;
}


// handles printf
void putchar(unsigned char c) {
    SBUF0 = c;
    while (TI0 == 0);
    TI0 = 0;
}


// just prints a new line
void newline(void) {
    printf_fast_f("\n");
}


// delay execution
void delay(unsigned int ms) {
    TMOD |= 0x01;
    TMOD &= ~0x02;

    while (ms-- > 0) {
        TR0 = 0;
        TF0 = 0;
        TL0 = 0x58;
        TH0 = 0x9e;
        TR0 = 1;
        while (TF0 == 0);
    }
}


// handles key presses emulation
void int_serial(void) __interrupt 4 {
    if (RI0 == 1) {
        switch (SBUF0) {
            case 'i':
                // set timer interval
                break;
            case 't':
                // set emulated temperature
                break;
            case 's':
                // start temperature reading
                break;
            case 'p':
                // pause temperature reading
                break;
            case 'v':
                // print temperatures
                break;
            default: break; 
        }
        RI0=0;
    }
}


char float_to_byte(float f) {
    if (f > 51f) {
        return 255;
    }

    if (f < 0f) {
        return 0;
    }

    return f / 51.0f * 255;
}


float byte_to_float(char c) {
    return c / 255f * 51f;
}


// handles key presses
void int_record_timer(void) __interrupt 5 {
    char cLastIndex;
    float fValue;
    char cWriteValue;

    // only record if flag is set
    if (!g_bRecording) {
        return;
    }

    // read ADC
    fValue = read_adc();

    // read last index
    cLastIndex = read_eeprom(DEVICE, 0); // TODO: macro address

    // convert value
    cWriteValue = float_to_byte(fValue);

    // write value
    write_eeprom(DEVICE, cLastIndex + 1, cWriteValue);

    // update last index
    write_eeprom(DEVICE, 0, cLastIndex + 1);
}


void main(void) {
    unsigned char operation = 0;
    unsigned char address[5]; // 4 chars + null termination
    unsigned char data[4]; // 3 chars + null termination

    Init_Device();
    SFRPAGE = LEGACY_PAGE;
    keypress = NULL_KEY;

    while (true) {
        /*
        |-------------------------------------
        | Reading operation
        |-------------------------------------
        */
        printf_fast_f("Enter operation: %c to write, %c to read: ", OP_WRITE, OP_READ);
        operation = read_char();
        newline();

        // if operation is not valid, reset loop
        if (operation != OP_WRITE && operation != OP_READ) {
            printf_fast_f("\nInvalid operation, please try again!\n");
            continue;
        }

        /*
        |-------------------------------------
        | Reading address
        |-------------------------------------
        */
        printf_fast_f("Enter address: ");
        if (read_string(address, sizeof(address) - 1) == false) {
            printf_fast_f("\nFailed to read address.\n");
            continue;
        }
        newline();

        // expect E
        printf_fast_f("Confirm address %s by pressing E: ", address);
        if (read_char() != 'e') {
            printf_fast_f("\nFailed to confirm address\n");
            continue;
        }
        newline();

        /*
        |-------------------------------------
        | Reading data
        |-------------------------------------
        */
        if (operation == OP_WRITE) {
            // if writing, tries to read a data value
            printf_fast_f("Enter data: ");
            if (read_string(data, sizeof(data) - 1) == false) {
                printf_fast_f("\nFailed to read data\n");
                continue;
            }
            newline();

            // since data overflow is not fatal, just warn user
            if (atoi(data) > 255) {
                printf_fast_f("WARNING: data overflow (out of 0-255 range)\n");
            }

            // expect E
            printf_fast_f("Confirm data %s (%d) by pressing E: ", data, atoi (data));
            if (read_char() != 'e') {
                printf_fast_f("\nFailed to confirm data.\n");
                continue;
            }
            newline();
        }

        /*
        |-------------------------------------
        | Sending operation to RAM
        |-------------------------------------
        */
        if (operation == OP_WRITE) {
            // TODO validation of address
            printf_fast_f("Writing %s(%d) to %s\n", data, atoi(data), address);
            write_ram(atoi(address), atoi(data));
        }

        else if (operation == OP_READ) {
            printf_fast_f("Read from address %s: %d\n", address, read_ram(atoi(address)));
        }
    }
}