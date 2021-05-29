#include <stdio.h>
#include <stdlib.h>
#include "config.c"
#include "def_pinos.h"

// BIG8051
// Timer0 -> Delay
// Timer1 -> UART
// Timer2 -> Timer for recording interrupts

// TODO
// - remove delay (own commit)
// - remove debug functions (own commit)
// - play around with periods and shit to get more range in interval + add a conversion of seconds to units
// - handle eeprom limits
// - check eeprom address overflow
// - use __bit where needed
// - check adc/dac data ranges (12 bits)

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
#define EEPROM_DATA_POINTER_ADDRESS (0)

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

// Temperature limits
#define MAX_TEMPERATURE (51.0f)
#define MIN_TEMPERATURE (0.0f)

// LM35 voltage/temperature relation
#define VOLTS_PER_CELSIUS (0.01f)

// Useful datatype limits
#define CHAR_MIN (0)
#define CHAR_MAX (255)

// ADC/DAC conversion ratio = 4096 (2^n) / 2.43 (VREF) with n=12 bits
#define ADC_RATIO (1685.5967f)

// Timer2 period in milliseconds
#define TIMER2_PERIOD_MS (20) /*ms*/;


// Last key that was pressed by serial input
volatile unsigned char g_cKeypress = '\0';

// User input buffer
unsigned char g_szBuffer[33];

// Flag to signal firmware is waiting for user input
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
    data = (int) SMB0DAT;

    STO = 1;
    SI = 0;
    while (STO == 1);

    return data;
}


// clamp temperature between 0C and 51C
float clamp_temperature(float t) {
    if (t > MAX_TEMPERATURE) {
        return MAX_TEMPERATURE;
    }

    if (t < MIN_TEMPERATURE) {
        return MIN_TEMPERATURE;
    }

    return t;
}


// map a float from 0~51 to a char 0~255
unsigned char temperature_to_byte(float f) {
    f = clamp_temperature(f);

    return f / MAX_TEMPERATURE * CHAR_MAX /* temperature range 0C to 51C */;
}


// map char 0~255 to float 0~51
float byte_to_temperature(unsigned char c) {
    return c /  (float) CHAR_MAX * MAX_TEMPERATURE /* temperature range 0C to 51C */;
}


// lm35 voltage to temperature conversion
float voltage_to_temperature(float v) {
    return v / VOLTS_PER_CELSIUS;
}


// lm35 temperature to voltage conversion
float temperature_to_voltage(float t) {
    return t * VOLTS_PER_CELSIUS;
}


// dac voltage to digital conversion
unsigned int voltage_to_dac(float v) {
    return v * ADC_RATIO;
}


// adc digital to voltage conversion
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


// reads an entire line from terminal
void read_line(unsigned char buffer[], unsigned char len) {
    unsigned int index = 0;
    unsigned char c = '\0';

    do {
        c = read_char();

        // if any end of line chars are found, stop reading
        if (c == '\n' || c == '\r') {
            break;
        }

        buffer[index] = c;
    } while (++index < len - 1); // reserve 1 byte for null termination

    // always null terminate strings
    buffer[index] = '\0';
}


// handles key presses emulation
void int_serial(void) __interrupt INTERRUPT_UART0 {
    if (RI0 == 1) {
        g_cKeypress = SBUF0;
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

    // update elapsed time since last recording
    g_iLastRecording += TIMER2_PERIOD_MS;

    // check if this interrupt should record a temperature
    if (g_iLastRecording < g_iRecordingInterval) {
        return;
    }

    // at this point, the interrupt will record a temperature reading
    // and needs to reset the elapsed time
    g_iLastRecording = 0;

    // read ADC
    temperature = clamp_temperature(voltage_to_temperature(dac_to_voltage(read_adc(ADC_AIN_0_0, ADC_G1))));
    if (g_bSuppressOutput == false) {
        printf_fast_f("Read %f from ADC\n", temperature);
    }

    // read last index
    last_index = read_eeprom(EEPROM_DEVICE, EEPROM_DATA_POINTER_ADDRESS);
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


// handles interval update operation
void op_interval(void) {
    printf_fast_f("Enter new recording interval (seconds) and press ENTER to set (e.g., 5<enter>): ");

    // read user input
    read_line(g_szBuffer, sizeof(g_szBuffer));
    newline();

    // set interval
    g_iRecordingInterval = atoi(g_szBuffer) * 1000;

    printf_fast_f("New recording interval: %u seconds!\n", g_iRecordingInterval / 1000);
}


// handles temperature update operation
void op_temperature(void) {
    float fNewTemperature;
    
    printf_fast_f("Enter new temperature (celsius) and press ENTER to set (e.g., 23.3<enter>): ");

    // read user input
    read_line(g_szBuffer, sizeof(g_szBuffer));
    newline();

    // clean value and write to DAC
    fNewTemperature = clamp_temperature(atof(g_szBuffer));
    write_dac(voltage_to_dac(temperature_to_voltage(fNewTemperature)));

    printf_fast_f("Simulated temperature set at %f C!\n", fNewTemperature);
}


// handles recording start operation
void op_start(void) {
    // TODO: move interesting messages
    g_bRecording = true;

    printf_fast_f("Temperature recording enabled!\n");
}


// handles recording stop operation
void op_stop(void) {
    // TODO: move interesting messages
    g_bRecording = false;

    printf_fast_f("Temperature recording disabled!\n");
}


// handles view operation
void op_view(void) {
    int pointer, value, i;

    pointer = read_eeprom(EEPROM_DEVICE, EEPROM_DATA_POINTER_ADDRESS);

    // check if pointer was read successfully
    if (pointer < 0) {
        printf_fast_f("Failed to read EEPROM data pointer\n");

        return;
    }

    // if EEPROM is empty, report that to user
    if (pointer == 0) {
        printf_fast_f("EEPROM is empty!\n");

        return;
    }

    // skip first value since it's the pointer
    for (i = 1; i <= pointer; ++i) {
        // read recorded temperature
        value = read_eeprom(EEPROM_DEVICE, i);

        // check if read was successful
        if (value < 0) {
            printf_fast_f("Failed to read value at address %d, aborting\n", i);

            break;
        }

        printf_fast_f("[%d]=%fC\n", i, byte_to_temperature(value));
    }
}


// handles eeprom data pointer reset operation
void op_reset(void) {
    if (write_eeprom(EEPROM_DEVICE, EEPROM_DATA_POINTER_ADDRESS, 0) < 0) {
        printf_fast_f("Failed to reset EEPROM data pointer\n");

        return;
    }

    printf_fast_f("EEPROM address pointer reset!\n");
}


// handles single ADC read operation
void op_get(void) {
    float temperature;

    temperature = voltage_to_temperature(dac_to_voltage(read_adc(ADC_AIN_0_0, ADC_G1)));

    printf_fast_f("Read %fC from ADC\n", temperature);
}


// handles manual record operation
void op_record(void) {
    float temperature;
    int last_index;
    char write_value;

    // read ADC
    printf_fast_f("ADC\n");
    temperature = voltage_to_temperature(dac_to_voltage(read_adc(ADC_AIN_0_0, ADC_G1)));
    printf_fast_f("Read %f from ADC\n", temperature);

    // read last index
    last_index = read_eeprom(EEPROM_DEVICE, EEPROM_DATA_POINTER_ADDRESS);
    printf_fast_f("Last Index: %d\n", last_index);

    // convert value
    write_value = temperature_to_byte(temperature);
    printf_fast_f("Writing %d to EEPROM\n", write_value);

    // write value
    write_eeprom(EEPROM_DEVICE, last_index + 1, write_value);

    // update last index
    write_eeprom(EEPROM_DEVICE, 0, last_index + 1);
}


void print_usage(void) {
    printf_fast_f("OPERATION LIST:\n");
    printf_fast_f("  i - set temperature recording interval\n");
    printf_fast_f("  t - set temperature to simulate with DAC\n");
    printf_fast_f("  s - start temperature recording\n");
    printf_fast_f("  p - stop temperature recording\n");
    printf_fast_f("  v - view all temperature records\n");
    printf_fast_f("  r - reset EEPROM data pointer\n");
    printf_fast_f("  g - read ADC without recording\n");
    printf_fast_f("  z - trigger manual temperature recording\n");
}


void main(void) {
    unsigned char operation = '\0';

    Init_Device();
    SFRPAGE = LEGACY_PAGE;

    // TODO: boot data pointer if it's at 0

    while (true) {
        // read operation
        print_usage();
        printf_fast_f("\nEnter operation (i, t, s, p, v, r, g, z): ");

        // wait for user input
        operation = read_char();
        newline();

        // suppress output from recording interrupt while running operations
        // to avoid confusion of what user is supposed to do
        g_bSuppressOutput = true;

        // handle operation
        switch (operation) {
            case OP_INTERVAL: op_interval(); break;
            case OP_TEMPERATURE: op_interval(); break;
            case OP_START: op_start(); break;
            case OP_STOP: op_stop(); break;
            case OP_VIEW: op_view(); break;
            case OP_RESET: op_reset(); break;
            case OP_GET: op_get(); break;
            case OP_RECORD: op_record(); break;
            default: printf_fast_f("Invalid operation, please try again!\n");
        }

        // restore outputs
        g_bSuppressOutput = false;
    }
}