/////////////////////////////////////
//  Generated Initialization File  //
/////////////////////////////////////

#include "compiler_defs.h"
#include "C8051F040_defs.h"

// Peripheral specific initialization functions,
// Called from the Init_Device() function
void Reset_Sources_Init()
{
    WDTCN     = 0xDE;
    WDTCN     = 0xAD;
}

void Timer_Init()
{
    SFRPAGE   = TIMER01_PAGE;
    TCON      = 0x51;
    TMOD      = 0x21;
    CKCON     = 0x18;
    TH1       = 0xAF;
    SFRPAGE   = TMR2_PAGE;
    TMR2CN    = 0x04;
    RCAP2L    = 0x9F;
    RCAP2H    = 0xAE;
    SFRPAGE   = TMR3_PAGE;
    TMR3CF    = 0x1A;
    RCAP3L    = 0xDC;
    RCAP3H    = 0x0B;
}

void UART_Init()
{
    SFRPAGE   = UART0_PAGE;
    SCON0     = 0x70;
}

void SMBus_Init()
{
    SFRPAGE   = SMB0_PAGE;
    SMB0CN    = 0x41;
    SMB0CR    = 0xE9;
}

void ADC_Init()
{
    SFRPAGE   = ADC0_PAGE;
    ADC0CN    = 0x80;
}

void DAC_Init()
{
    SFRPAGE   = DAC0_PAGE;
    DAC0CN    = 0x80;
}

void Voltage_Reference_Init()
{
    SFRPAGE   = ADC0_PAGE;
    REF0CN    = 0x03;
}

void Port_IO_Init()
{
    // P0.0  -  TX0 (UART0), Push-Pull,  Digital
    // P0.1  -  RX0 (UART0), Open-Drain, Digital
    // P0.2  -  SCK  (SPI0), Open-Drain, Digital
    // P0.3  -  MISO (SPI0), Open-Drain, Digital
    // P0.4  -  MOSI (SPI0), Open-Drain, Digital
    // P0.5  -  NSS  (SPI0), Open-Drain, Digital
    // P0.6  -  SDA (SMBus), Open-Drain, Digital
    // P0.7  -  SCL (SMBus), Open-Drain, Digital

    // P1.0  -  Unassigned,  Open-Drain, Digital
    // P1.1  -  Unassigned,  Open-Drain, Digital
    // P1.2  -  Unassigned,  Open-Drain, Digital
    // P1.3  -  Unassigned,  Open-Drain, Digital
    // P1.4  -  Unassigned,  Open-Drain, Digital
    // P1.5  -  Unassigned,  Open-Drain, Digital
    // P1.6  -  Unassigned,  Open-Drain, Digital
    // P1.7  -  Unassigned,  Open-Drain, Digital

    // P2.0  -  Unassigned,  Open-Drain, Digital
    // P2.1  -  Unassigned,  Open-Drain, Digital
    // P2.2  -  Unassigned,  Open-Drain, Digital
    // P2.3  -  Unassigned,  Open-Drain, Digital
    // P2.4  -  Unassigned,  Open-Drain, Digital
    // P2.5  -  Unassigned,  Open-Drain, Digital
    // P2.6  -  Unassigned,  Open-Drain, Digital
    // P2.7  -  Unassigned,  Open-Drain, Digital

    // P3.0  -  Unassigned,  Open-Drain, Digital
    // P3.1  -  Unassigned,  Open-Drain, Digital
    // P3.2  -  Unassigned,  Open-Drain, Digital
    // P3.3  -  Unassigned,  Open-Drain, Digital
    // P3.4  -  Unassigned,  Open-Drain, Digital
    // P3.5  -  Unassigned,  Open-Drain, Digital
    // P3.6  -  Unassigned,  Open-Drain, Digital
    // P3.7  -  Unassigned,  Open-Drain, Digital

    SFRPAGE   = CONFIG_PAGE;
    P0MDOUT   = 0x01;
    XBR0      = 0x07;
    XBR2      = 0x40;
}

void Oscillator_Init()
{
    int i = 0;
    SFRPAGE   = CONFIG_PAGE;
    OSCXCN    = 0x67;
    for (i = 0; i < 3000; i++);  // Wait 1ms for initialization
    while ((OSCXCN & 0x80) == 0);
    CLKSEL    = 0x01;
    OSCICN    = 0x00;
}

void Interrupts_Init()
{
    IE        = 0xB0;
    IP        = 0xD0;
}

// Initialization function for device,
// Call Init_Device() from your main program
void Init_Device(void)
{
    Reset_Sources_Init();
    Timer_Init();
    UART_Init();
    SMBus_Init();
    ADC_Init();
    DAC_Init();
    Voltage_Reference_Init();
    Port_IO_Init();
    Oscillator_Init();
    Interrupts_Init();
}