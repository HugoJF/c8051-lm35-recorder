#include <stdio.h>
#include <stdlib.h>
#include "config.c"
#include "def_pinos.h"

// BIG8051
// Timer0 -> Delay
// Timer1 -> UART
// Timer2 -> Timer for recording interrupts

// TODO
// - remove delay
// - print usage
// - debug macro?
// - remove debug functions
// - play around with periods and shit to get more range in interval + add a conversion of seconds to units
// - handle eeprom limits

// Macros to make code more readable
#define true (1)
#define false (0)

// CLI Operations
#define OP_INTERVAL 'i'
#define OP_TEMPERATURE 't'
#define OP_START 's'
#define OP_STOP 'p'
#define OP_VIEW 'v'
#define OP_RESET 'r'
#define OP_GET 'g'
#define OP_RECORD 'z'

// EEPROM
#define EEPROM_WRITE (0)
#define EEPROM_READ (1)
#define EEPROM_DEVICE (0xA0)

// ADC AMUX selection
#define ADC_AIN_0_0 (0)
#define ADC_AIN_0_1 (1)
#define ADC_AIN_0_2 (2)
#define ADC_AIN_0_3 (3)
#define ADC_HVDA (4)
#define ADC_AGND (5)
#define ADC_P3EVEN (6)
#define ADC_P3ODD (7)
#define ADC_TEMP (8)

// ADC Gains
#define ADC_G1 (0)
#define ADC_G2 (1)
#define ADC_G4 (2)
#define ADC_G8 (3)
#define ADC_ADC_G16 (4)
#define ADC_GHALF (6)

// temperature limits
#define MAX_TEMPERATURE (51.0f)
#define MIN_TEMPERATURE (0.0f)

// lm35 voltage/temperature relation
#define VOLTS_PER_CELSIUS (0.01f)

// useful datatype limits
#define CHAR_MIN (0)
#define CHAR_MAX (255)

// ADC/DAC conversion ratio = 4096 (2^n) / 2.43 (VREF) with n=12 bits
#define ADC_RATIO (1685.5967f)

// Timer2 period in milliseconds
#define TIMER2_PERIOD_MS (20) /*ms*/;


// Last key that was pressed by serial input
volatile unsigned char g_cKeypress = '\0';

// Input buffer
unsigned char g_szBuffer[33];

// Flag to signal uC is waiting user input
unsigned char g_bSuppressOutput = false;

// If firmware is recording temperatures
unsigned char g_bRecording = false;

// Time units since last recording
unsigned int g_iLastRecording = 0;

// Time units to wait to record temperature
unsigned int g_iRecordingInterval = 5000 /*ms*/;


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


unsigned char write_control_byte(unsigned char device, __bit RW) {
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

    ret = write_control_byte(device, EEPROM_WRITE);
    if (ret != 0) return - (int) ret;

    ret = write_data_byte(address);
    if (ret != 0) return - (int) ret;

    ret = write_data_byte(data);
    if (ret != 0) return - (int) ret;

    STO = 1;
    SI = 0;
    while (STO == 1);

    while (1) {
        ret = write_control_byte(device, EEPROM_WRITE);
        if (ret == 0) break;
        if (ret != 0x20) return - (int) ret;
    }

    return 0;
}


int read_eeprom(unsigned char device, unsigned char address) {
    int data;
    unsigned char ret;

    ret = write_control_byte(device, EEPROM_WRITE);
    if (ret != 0) return - (int) ret;

    ret = write_data_byte(address);
    if (ret != 0) return - (int) ret;

    ret = write_control_byte(device, EEPROM_READ);
    if (ret != 0) return - (int) ret;

    AA = 0;
    SI = 0;
    while (SI == 0);

    if (SMB0STA != 0x58) {
        return - (int) SMB0STA;
    }
    data = (int) SMB0DAT; // TODO: check data length

    STO = 1;
    SI = 0;
    while (STO == 1);

    return data;
}

float clamp_temperature(float t) {
    if (t > MAX_TEMPERATURE) {
        return MAX_TEMPERATURE;
    }

    if (t < MIN_TEMPERATURE) {
        return MIN_TEMPERATURE;
    }

    return t;
}

unsigned char temperature_to_byte(float f) {
    f = clamp_temperature(f);

    return f / MAX_TEMPERATURE * CHAR_MAX /* temperature range 0C to 51C */;
}


// TODO: extract constants
float byte_to_temperature(unsigned char c) {
    return c /  (float) CHAR_MAX * MAX_TEMPERATURE /* temperature range 0C to 51C */;
}

float voltage_to_temperature(float v) {
    return v / VOLTS_PER_CELSIUS;
}


float temperature_to_voltage(float t) {
    return t * VOLTS_PER_CELSIUS;
}


unsigned int voltage_to_dac(float v) {
    return v * ADC_RATIO;
}


float dac_to_voltage (unsigned int dac) {
    return dac / ADC_RATIO;
}


void write_dac (unsigned int d) {
    DAC0L = d;
    DAC0H = d >> 8;
}


unsigned int read_adc (unsigned char channel, unsigned char gain) {
    AMX0SL = channel;
    ADC0CF = (ADC0CF & 0xf8) | gain;
    
    // Start conversion
    AD0INT = 0;
    AD0BUSY = 1;
    while (!AD0INT);

    return (ADC0H << 8) | ADC0L;
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


// read a single char from serial terminal
unsigned char read_char(void) {
    unsigned char caught;

    while (g_cKeypress == '\0');

    // use temp variable to allow this function to reset the 'g_cKeypress' flag
    caught = g_cKeypress;
    g_cKeypress = '\0';

    // feedback to user
    putchar(caught);

    return caught;
}


// reads `len` chars and store them in `data`
// TODO: reprecated
unsigned char read_string(unsigned char data[], unsigned char len) {
    unsigned char i, key;

    for (i = 0; i < len; ++i) {
        key = read_char();

        // since C and E are control characters, bail if they are read
        // TODO: these are not needed
        if (key == 'c' || key == 'e') {
            return false;
        }

        data[i] = key;
    }

    data[i] = '\0';

    return true;
}

// TODO: testar o overflow
void read_line(unsigned char buffer[], unsigned char len) {
    unsigned int index = 0;
    unsigned char c = '\0';

    do {
        c = read_char();

        if (c == '\n' || c == '\r') {
            break;
        }

        buffer[index] = c;
    } while (++index < len - 1); // reserve 1 byte for null termination

    buffer[index] = '\0';
}


// handles key presses emulation
void int_serial(void) __interrupt INTERRUPT_UART0 {
    if (RI0 == 1) {
        g_cKeypress = SBUF0; // TODO: maybe use an input buffer
        RI0 = 0;
    }
}


// handles key presses
void int_record_timer(void) __interrupt INTERRUPT_TIMER2 {
    float temperature;
    int last_index;
    char write_value;

    TF2 = 0;

    // only record if flag is set
    if (g_bRecording != true) {
        return;
    }

    g_iLastRecording += TIMER2_PERIOD_MS;

    if (g_iLastRecording < g_iRecordingInterval) {
        return;
    }

    g_iLastRecording = 0;

    // read ADC
    temperature = clamp_temperature(voltage_to_temperature(dac_to_voltage(read_adc(ADC_AIN_0_0, ADC_G1))));
    if (g_bSuppressOutput == false) {
        printf_fast_f("Read %f from ADC\n", temperature);
    }

    // read last index
    last_index = read_eeprom(EEPROM_DEVICE, 0); // TODO: macro address
    if (g_bSuppressOutput == false) {
        printf_fast_f("Last Index: %d\n", last_index);
    }

    // check if eeprom returned errors
    if (last_index < 0) {
        printf_fast_f("Failed to read from EEPROM\n");

        return;
    }

    // convert value
    write_value = temperature_to_byte(temperature);
    if (g_bSuppressOutput == false) {
        printf_fast_f("Writing %d to EEPROM\n", write_value);
    }

    // write value
    if (write_eeprom(EEPROM_DEVICE, last_index + 1, write_value) < 0) {
        printf_fast_f("Failed to write temperature to EEPROM\n");
    }

    // update last index
    if (write_eeprom(EEPROM_DEVICE, 0, last_index + 1) < 0) {
        printf_fast_f("Failed to update EEPROM pointer\n");
    }
}


void op_interval(void) {

    printf_fast_f("Enter new recording interval (seconds): ");

    read_line(g_szBuffer, sizeof(g_szBuffer));
    newline();

    g_iRecordingInterval = atoi(g_szBuffer) * 1000;

    printf_fast_f("New recording interval: %u seconds!\n", g_iRecordingInterval / 1000);
}


void op_temperature(void) {
    float fNewTemperature;
    
    printf_fast_f("Enter new temperature (e.g., 23.3): ");

    read_line(g_szBuffer, sizeof(g_szBuffer));
    newline();

    fNewTemperature = clamp_temperature(atof(g_szBuffer));
    write_dac(voltage_to_dac(temperature_to_voltage(fNewTemperature)));

    printf_fast_f("Simulated temperature set at %f C!\n", fNewTemperature);
}


void op_start(void) {
    g_bRecording = true;

    printf_fast_f("Temperature recording enabled!\n");
}


void op_stop(void) {
    g_bRecording = false;

    printf_fast_f("Temperature recording disabled!\n");
}


// TODO: handle empty EEPROM pointer==1
// TODO: are ints needed?
void op_view(void) {
    int pointer, value, i;

    pointer = read_eeprom(EEPROM_DEVICE, 0);

    if (pointer < 0) {
        printf_fast_f("Failed to read EEPROM data pointer\n");

        return;
    }

    if (pointer == 0) {
        printf_fast_f("EEPROM is empty!\n");

        return;
    }

    // skip first value since it's the pointer
    for (i = 1; i <= pointer; ++i) {
        value = read_eeprom(EEPROM_DEVICE, i);

        if (value < 0) {
            printf_fast_f("Failed to read value at address %d, aborting\n", i);

            break;
        }

        printf_fast_f("[%d]=%fC\n", i, byte_to_temperature(value));
    }
}


void op_reset(void) {
    if (write_eeprom(EEPROM_DEVICE, 0, 0) < 0) {
        printf_fast_f("Failed to reset EEPROM data pointer\n");

        return;
    }

    printf_fast_f("EEPROM address pointer reset!\n");
}


void op_get(void) {
    float temperature;

    temperature = voltage_to_temperature(dac_to_voltage(read_adc(ADC_AIN_0_0, ADC_G1)));

    printf_fast_f("Read %fC from ADC\n", temperature);
}


void op_record(void) {
    float temperature;
    int last_index;
    char write_value;

    // read ADC
    printf_fast_f("ADC\n");
    temperature = voltage_to_temperature(dac_to_voltage(read_adc(ADC_AIN_0_0, ADC_G1)));
    printf_fast_f("Read %f from ADC\n", temperature);

    // read last index
    last_index = read_eeprom(EEPROM_DEVICE, 0); // TODO: macro address
    printf_fast_f("Last Index: %d\n", last_index);

    // convert value
    write_value = temperature_to_byte(temperature);
    printf_fast_f("Writing %d to EEPROM\n", write_value);

    // write value
    write_eeprom(EEPROM_DEVICE, last_index + 1, write_value);

    // update last index
    write_eeprom(EEPROM_DEVICE, 0, last_index + 1);
}


void main(void) {
    unsigned char operation = '\0';

    Init_Device();
    SFRPAGE = LEGACY_PAGE;

    printf_fast_f("running with buffer%d\n", sizeof(g_szBuffer));

    while (true) {
        // read operation
        printf_fast_f("Enter operation (i, t, s, p, v, r, g, z): ");
        operation = read_char();
        newline();

        g_bSuppressOutput = true;

        // handle operation
        // TODO: switch case
        if (operation == OP_INTERVAL) {
            op_interval();
        } else if (operation == OP_TEMPERATURE) {
            op_temperature();
        } else if (operation == OP_START) {
            op_start();
        } else if (operation == OP_STOP) {
            op_stop();
        } else if (operation == OP_VIEW) {
            op_view();
        } else if (operation == OP_RESET) {
            op_reset();
        } else if (operation == OP_GET) {
            op_get();
        } else if (operation == OP_RECORD) {
            op_record();
        } else {
            printf_fast_f("\nInvalid operation, please try again!\n");
        }

        g_bSuppressOutput = false;
    }
}