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

// Operations
#define OP_SET_INTERVAL 'i'
#define OP_SET_TEMP 't'
#define OP_START 's'
#define OP_STOP 'p'
#define OP_VIEW 'v'
#define OP_RESET_POINTER 'r'
#define OP_GET 'g'
#define OP_RECORD 'z'

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
volatile unsigned char keypress = '\0';

// Input buffer
unsigned char g_szBuffer[33];

// If firmware is recording temperatures
unsigned char g_bRecording = 0;

// Time units since last recording
unsigned int g_iLastRecording = 0;

// Time units to wait to record temperature
unsigned int g_iRecordingInterval = 5000;

// Recording timer period
unsigned int g_iRecordingTimerPeriod = 20 /*ms*/; // TODO: macro?


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

unsigned char in_array(unsigned char needle, unsigned char pool[], unsigned int size) {
    unsigned int i;
    for (i = 0; i < size; ++i)
    {
        if (pool[i] == needle) {
            return true;
        }
    }

    return false;
}

unsigned char is_operation(unsigned char c) {
    unsigned char operations[] = {
        OP_SET_INTERVAL, 
        OP_SET_TEMP, 
        OP_START, 
        OP_STOP, 
        OP_VIEW, 
        OP_RESET_POINTER, 
        OP_GET,
        OP_RECORD
    };

    return in_array(c, operations, 8);
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
    int dado;
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
    dado = (int) SMB0DAT; // TODO: check data length

    STO = 1;
    SI = 0;
    while (STO == 1);

    return dado;
}

// TODO: rename
unsigned char temperature_to_byte(float f) {
    if (f > 51.0f) {
        return 255;
    }

    if (f < 0.0f) {
        return 0;
    }

    return f / 51.0f * 255;
}


// TODO: rename
float byte_to_temperature(unsigned char c) {
    return c / 255.0f * 51.0f;
}

float voltage_to_temperature(float v) {
    return v / 0.01;
}

float temperature_to_voltage(float t) {
    return t * 0.01;
}

unsigned int voltage_to_dac(float v) {
    return v * RATIO;
}

float dac_to_voltage (unsigned int dac) {
    return dac / RATIO;
}

void write_dac (unsigned int d) {
    DAC0L = d;
    DAC0H = d >> 8;
}


unsigned int read_adc (unsigned char channel, unsigned char gain) {
    AMX0SL = channel;
    ADC0CF = (ADC0CF & 0xf8) | gain;
    
    // Start conversion
    // AD0BUSY = 1;
    // while (AD0BUSY);

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


// read a single char from keypresses
unsigned char read_char() {
    unsigned char caught;

    while (keypress == '\0');

    // use temp variable to allow this function to reset the 'keypress' flag
    caught = keypress;
    keypress = '\0';

    // feedback to user
    putchar(caught);

    return caught;
}


// reads `len` chars and store them in `data`
unsigned char read_string(unsigned char data[], unsigned char len) {
    unsigned char i, key;

    for (i = 0; i < len; ++i) {
        key = read_char();

        // since C and E are control characters, bail if they are read
        if (key == 'c' || key == 'e') {
            return false;
        }

        data[i] = key;
    }

    data[i] = '\0';

    return true;
}

unsigned char read_line(unsigned char buffer[], unsigned char len, unsigned char stop_char) {
    unsigned int index = 0;
    unsigned char c = '\0';

    do {
        c = read_char();

        if (c == stop_char) {
            break;
        }

        buffer[index] = c;
    } while (++index <= len - 1); // reserve 1 byte for null termination

    buffer[index] = '\0';

    return index >= len; // if buffer overflowed
}


// handles key presses emulation
void int_serial(void) __interrupt INTERRUPT_UART0 {
    if (RI0 == 1) {
        keypress = SBUF0; // TODO: maybe use an input buffer
        RI0 = 0;
    }
}


// handles key presses
void int_record_timer(void) __interrupt INTERRUPT_TIMER2 {
    float temperature;
    char last_index;
    char write_value;

    TF2 = 0;

    // only record if flag is set
    if (g_bRecording != true) {
        return;
    }

    g_iLastRecording += g_iRecordingTimerPeriod;

    if (g_iLastRecording < g_iRecordingInterval) {
        return;
    }

    g_iLastRecording = 0;

    // read ADC
    printf_fast_f("ADC\n");
    temperature = voltage_to_temperature(dac_to_voltage(read_adc(AIN_0_0, G1)));
    printf_fast_f("Read %f from ADC\n", temperature);

    // read last index
    last_index = read_eeprom(DEVICE, 0); // TODO: macro address
    printf_fast_f("Last Index: %d\n", last_index);

    // convert value
    write_value = temperature_to_byte(temperature);
    printf_fast_f("Writing %d to EEPROM\n", write_value);

    // write value
    write_eeprom(DEVICE, last_index + 1, write_value);

    // update last index
    write_eeprom(DEVICE, 0, last_index + 1);
}

void op_set_interval(void) {
    int iNewIntervalSeconds;

    printf_fast_f("Enter new recording interval (seconds): ");

    read_line(g_szBuffer, sizeof(g_szBuffer), '\n');
    newline();

    iNewIntervalSeconds = atoi(g_szBuffer);
    g_iRecordingInterval = iNewIntervalSeconds * 1000; // TODO: macro second to unit

    printf_fast_f("New recording interval: %d seconds!\n", iNewIntervalSeconds);
}


void op_set_temp(void) {
    float fNewTemperature;

    printf_fast_f("Enter new temperature: ");

    read_line(g_szBuffer, sizeof(g_szBuffer), '\n');
    newline();

    fNewTemperature = atof(g_szBuffer); // TODO: handle both . and ,
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


// TODO: empty EEPROM
void op_view(void) {
    int pointer, value, i;

    pointer = read_eeprom(DEVICE, 0);

    for (i = 1; i <= pointer; ++i) {
        value = read_eeprom(DEVICE, i);

        printf_fast_f("[%d]=%f\n", i, byte_to_temperature(value));
    }
}

void op_reset_pointer(void) {
    write_eeprom(DEVICE, 0, 0);
    printf_fast_f("EEPROM address pointer reset!\n");
}

void op_get(void) {
    float temperature;

    temperature = voltage_to_temperature(dac_to_voltage(read_adc(AIN_0_0, G1)));

    printf_fast_f("Read %fC from ADC\n", temperature);
}

void op_record(void) {
    float temperature;
    char last_index;
    char write_value;

    // read ADC
    printf_fast_f("ADC\n");
    temperature = voltage_to_temperature(dac_to_voltage(read_adc(AIN_0_0, G1)));
    printf_fast_f("Read %f from ADC\n", temperature);

    // read last index
    last_index = read_eeprom(DEVICE, 0); // TODO: macro address
    printf_fast_f("Last Index: %d\n", last_index);

    // convert value
    write_value = temperature_to_byte(temperature);
    printf_fast_f("Writing %d to EEPROM\n", write_value);

    // write value
    write_eeprom(DEVICE, last_index + 1, write_value);

    // update last index
    write_eeprom(DEVICE, 0, last_index + 1);
}

void main(void) {
    unsigned char operation = '\0';
    float v;
    float t;
    unsigned int d;
    unsigned char zzz = 8;

    Init_Device();
    SFRPAGE = LEGACY_PAGE;
    keypress = '\0';

    t = 51;
    v = temperature_to_voltage(t);
    d = voltage_to_dac(v);
    write_dac(zzz);
    printf_fast_f("\nWRITING: Temp: %f, Voltage: %f, DAC: %d\n", t, v, d);

    delay(5000);

    d = read_adc(AIN_0_0, G1);
    v = dac_to_voltage(d);
    t = voltage_to_temperature(v);
    printf_fast_f("READING: Temp: %f, Voltage: %f, ADC: %d\n", t, v, d);

    while (true) {
        // read operation
        printf_fast_f("Enter operation (i, t, s, p, v, r, g, z): ");
        operation = read_char();
        newline();

        // if operation is not valid, reset loop
        if (is_operation(operation) != true) {
            printf_fast_f("\nInvalid operation, please try again!\n");
            continue;
        }

        printf_fast_f("Received operation: %c\n", operation);

        // handle operation
        if (operation == OP_SET_INTERVAL) {
            op_set_interval();
        } else if (operation == OP_SET_TEMP) {
            op_set_temp();
        } else if (operation == OP_START) {
            op_start();
        } else if (operation == OP_STOP) {
            op_stop();
        } else if (operation == OP_VIEW) {
            op_view();
        } else if (operation == OP_RESET_POINTER) {
            op_reset_pointer();
        } else if (operation == OP_GET) {
            op_get();
        } else if (operation == OP_RECORD) {
            op_record();
        } else {
            printf_fast_f("this should never happen\n"); // todo kill this
        }
    }
}