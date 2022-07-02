// ===================================================================================
// Project:   PocketRadio - FM Tuner with RDS based on ATtiny402/412 and RDA5807MP
// Version:   v1.0
// Year:      2022
// Author:    Stefan Wagner
// Github:    https://github.com/wagiminator
// EasyEDA:   https://easyeda.com/wagiminator
// License:   http://creativecommons.org/licenses/by-sa/3.0/
// ===================================================================================
//
// Description:
// ------------
// This firmware implements the basic functionality of the Pocket Radio. By pressing
// the CH+ button the RDA5807 seeks the next radio station, presseng the VOL+/VOL-
// button increases/decreases the volume. The selected frequency and volume are
// stored in the EEPROM. Station name, frequency, signal strength, volume and battery
// state of charge are shown on an OLED display.
//
// References:
// -----------
// RDA5807 datasheet:
// https://datasheet.lcsc.com/szlcsc/1806121226_RDA-Microelectronics-RDA5807MP_C167245.pdf
//
// SSD1306 OLED datasheet:
// https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf
//
// The implementation is based on the ATtiny85-version (TinyFMRadio):
// https://github.com/wagiminator/ATtiny85-TinyFMRadio
//
// The OLED font was adapted from Neven Boyanov and Stephen Denne:
// https://github.com/datacute/Tiny4kOLED
//
// The RDA8507 implementation was adapted from Maarten Janssen:
// https://hackaday.io/project/9009-arduino-radio-with-rds
//
// Wiring:
// -------
//                           +-\/-+
//                     Vdd  1|°   |8  GND
//  CH+ button --- TXD PA6  2|    |7  PA3 AIN3 -------- VOL+ button
// VOL- button --- RXD PA7  3|    |6  PA0 AIN0 UPDI --- UPDI
//     I2C SDA --- SDA PA1  4|    |5  PA2 AIN2 SCL ---- I2C SCL
//                           +----+
//
// Compilation Settings:
// ---------------------
// Core:    megaTinyCore (https://github.com/SpenceKonde/megaTinyCore)
// Board:   ATtiny412/402/212/202
// Chip:    ATtiny402 or ATtiny412
// Clock:   1 MHz internal
//
// Leave the rest on default settings. Use a programmer that works with 3.3V.
// Don't forget to "Burn bootloader". Compile and upload the code.
//
// No Arduino core functions or libraries are used. To compile and upload without
// Arduino IDE download AVR 8-bit toolchain at:
// https://www.microchip.com/mplab/avr-support/avr-and-arm-toolchains-c-compilers
// and extract to tools/avr-gcc. Use the makefile to compile and upload.
//
// Fuse Settings: 0:0x00 1:0x00 2:0x01 4:0x00 5:0xC5 6:0x04 7:0x00 8:0x00


// ===================================================================================
// Libraries, Definitions and Macros
// ===================================================================================

// Libraries
#include <avr/io.h>                 // for GPIO
#include <avr/eeprom.h>             // for storing user settings into EEPROM
#include <util/delay.h>             // for delays

// Pin assignments
#define PIN_SDA       PA1           // I2C Serial Data,  connect to OLED/RDA
#define PIN_SCL       PA2           // I2C Serial Clock, connect to OLED/RDA
#define PIN_VOL_UP    PA3           // VOL+ button
#define PIN_CH_UP     PA6           // CH+  button
#define PIN_VOL_DOWN  PA7           // VOL- button

// Firmware parameters
#define EEPROM_IDENT  0x6CE7        // to identify if EEPROM was written by this program
#define OLED_CONTRAST 96            // 0 .. 255    brightness of OLED 
#define HEADER        "FM Pocket Radio  v1.0"   // text on top of the screen

// Variables
uint16_t  channel;                  // 0 .. 1023   current channel
uint8_t   volume = 5;               // 0 .. 15     current volume

// Pin manipulation macros
enum {PA0, PA1, PA2, PA3, PA4, PA5, PA6, PA7};      // enumerate pin designators
#define pinInput(x)       VPORTA.DIR &= ~(1<<(x))   // set pin to INPUT
#define pinOutput(x)      VPORTA.DIR |=  (1<<(x))   // set pin to OUTPUT
#define pinLow(x)         VPORTA.OUT &= ~(1<<(x))   // set pin to LOW
#define pinHigh(x)        VPORTA.OUT |=  (1<<(x))   // set pin to HIGH
#define pinToggle(x)      VPORTA.IN  |=  (1<<(x))   // TOGGLE pin
#define pinRead(x)        (VPORTA.IN &   (1<<(x)))  // READ pin
#define pinPullup(x)      (&PORTA.PIN0CTRL)[x] |= PORT_PULLUPEN_bm  // enable pullup

// ===================================================================================
// I2C Master Implementation (Software Bit-Banging)
// ===================================================================================

// I2C macros
#define I2C_SDA_HIGH()  pinInput(PIN_SDA)       // release SDA   -> pulled HIGH by resistor
#define I2C_SDA_LOW()   pinOutput(PIN_SDA)      // SDA as output -> pulled LOW  by MCU
#define I2C_SCL_HIGH()  pinInput(PIN_SCL)       // release SCL   -> pulled HIGH by resistor
#define I2C_SCL_LOW()   pinOutput(PIN_SCL)      // SCL as output -> pulled LOW  by MCU
#define I2C_SDA_READ()  pinRead(PIN_SDA)        // read SDA line
#define I2C_CLOCKOUT()  I2C_SCL_HIGH();I2C_SCL_LOW()  // clock out

// I2C transmit one data byte to the slave, ignore ACK bit, no clock stretching allowed
void I2C_write(uint8_t data) {
  for(uint8_t i=8; i; i--, data<<=1) {          // transmit 8 bits, MSB first
    (data&0x80)?I2C_SDA_HIGH():I2C_SDA_LOW();   // SDA depending on bit
    I2C_CLOCKOUT();                             // clock out -> slave reads the bit
  }
  I2C_SDA_HIGH();                               // release SDA for ACK bit of slave
  I2C_CLOCKOUT();                               // 9th clock pulse is for the ignored ACK bit
}

// I2C start transmission
void I2C_start(uint8_t addr) {
  I2C_SDA_LOW();                                // start condition: SDA goes LOW first
  I2C_SCL_LOW();                                // start condition: SCL goes LOW second
  I2C_write(addr);                              // send slave address
}

// I2C restart transmission
void I2C_restart(uint8_t addr) {
  I2C_SDA_HIGH();                               // prepare SDA for HIGH to LOW transition
  I2C_SCL_HIGH();                               // restart condition: clock HIGH
  I2C_start(addr);                              // start again
}

// I2C stop transmission
void I2C_stop(void) {
  I2C_SDA_LOW();                                // prepare SDA for LOW to HIGH transition
  I2C_SCL_HIGH();                               // stop condition: SCL goes HIGH first
  I2C_SDA_HIGH();                               // stop condition: SDA goes HIGH second
}

// I2C receive one data byte from the slave (ack=0 for last byte, ack>0 if more bytes to follow)
uint8_t I2C_read(uint8_t ack) {
  uint8_t data = 0;                             // variable for the received byte
  I2C_SDA_HIGH();                               // release SDA -> will be toggled by slave
  for(uint8_t i=8; i; i--) {                    // receive 8 bits
    data <<= 1;                                 // bits shifted in right (MSB first)
    I2C_SCL_HIGH();                             // clock HIGH
    if(I2C_SDA_READ()) data |= 1;               // read bit
    I2C_SCL_LOW();                              // clock LOW -> slave prepares next bit
  }
  if(ack) I2C_SDA_LOW();                        // pull SDA LOW to acknowledge (ACK)
  I2C_CLOCKOUT();                               // clock out -> slave reads ACK bit
  return data;                                  // return the received byte
}

// ===================================================================================
// OLED Implementation
// ===================================================================================

// OLED definitions
#define OLED_ADDR       0x78                      // OLED write address
#define OLED_CMD_MODE   0x00                      // set command mode
#define OLED_DAT_MODE   0x40                      // set data modearray

// OLED init settings
const uint8_t OLED_INIT_CMD[] = {
  0xC8, 0xA1,                                     // flip screen
  0xA8, 0x1F,                                     // set multiplex ratio
  0xDA, 0x02,                                     // set com pins hardware configuration
  0x8D, 0x14,                                     // set DC-DC enable
  0xAF,                                           // display on
  0x81, OLED_CONTRAST                             // set brightness
};

// Standard ASCII 5x8 font (adapted from Neven Boyanov and Stephen Denne)
const uint8_t OLED_FONT[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2F, 0x00, 0x00, 0x00, 0x07, 0x00, 0x07, 0x00,
  0x14, 0x7F, 0x14, 0x7F, 0x14, 0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x23, 0x13, 0x08, 0x64, 0x62,
  0x36, 0x49, 0x55, 0x22, 0x50, 0x00, 0x05, 0x03, 0x00, 0x00, 0x00, 0x1C, 0x22, 0x41, 0x00,
  0x00, 0x41, 0x22, 0x1C, 0x00, 0x14, 0x08, 0x3E, 0x08, 0x14, 0x08, 0x08, 0x3E, 0x08, 0x08,
  0x00, 0x00, 0xA0, 0x60, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x60, 0x60, 0x00, 0x00,
  0x20, 0x10, 0x08, 0x04, 0x02, 0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00, 0x42, 0x7F, 0x40, 0x00,
  0x42, 0x61, 0x51, 0x49, 0x46, 0x21, 0x41, 0x45, 0x4B, 0x31, 0x18, 0x14, 0x12, 0x7F, 0x10,
  0x27, 0x45, 0x45, 0x45, 0x39, 0x3C, 0x4A, 0x49, 0x49, 0x30, 0x01, 0x71, 0x09, 0x05, 0x03,
  0x36, 0x49, 0x49, 0x49, 0x36, 0x06, 0x49, 0x49, 0x29, 0x1E, 0x00, 0x36, 0x36, 0x00, 0x00,
  0x00, 0x56, 0x36, 0x00, 0x00, 0x08, 0x14, 0x22, 0x41, 0x00, 0x14, 0x14, 0x14, 0x14, 0x14,
  0x00, 0x41, 0x22, 0x14, 0x08, 0x02, 0x01, 0x51, 0x09, 0x06, 0x32, 0x49, 0x59, 0x51, 0x3E,
  0x7C, 0x12, 0x11, 0x12, 0x7C, 0x7F, 0x49, 0x49, 0x49, 0x36, 0x3E, 0x41, 0x41, 0x41, 0x22,
  0x7F, 0x41, 0x41, 0x22, 0x1C, 0x7F, 0x49, 0x49, 0x49, 0x41, 0x7F, 0x09, 0x09, 0x09, 0x01,
  0x3E, 0x41, 0x49, 0x49, 0x7A, 0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00, 0x41, 0x7F, 0x41, 0x00,
  0x20, 0x40, 0x41, 0x3F, 0x01, 0x7F, 0x08, 0x14, 0x22, 0x41, 0x7F, 0x40, 0x40, 0x40, 0x40,
  0x7F, 0x02, 0x0C, 0x02, 0x7F, 0x7F, 0x04, 0x08, 0x10, 0x7F, 0x3E, 0x41, 0x41, 0x41, 0x3E,
  0x7F, 0x09, 0x09, 0x09, 0x06, 0x3E, 0x41, 0x51, 0x21, 0x5E, 0x7F, 0x09, 0x19, 0x29, 0x46,
  0x46, 0x49, 0x49, 0x49, 0x31, 0x01, 0x01, 0x7F, 0x01, 0x01, 0x3F, 0x40, 0x40, 0x40, 0x3F,
  0x1F, 0x20, 0x40, 0x20, 0x1F, 0x3F, 0x40, 0x38, 0x40, 0x3F, 0x63, 0x14, 0x08, 0x14, 0x63,
  0x07, 0x08, 0x70, 0x08, 0x07, 0x61, 0x51, 0x49, 0x45, 0x43, 0x00, 0x7F, 0x41, 0x41, 0x00,
  0x02, 0x04, 0x08, 0x10, 0x20, 0x00, 0x41, 0x41, 0x7F, 0x00, 0x04, 0x02, 0x01, 0x02, 0x04,
  0x40, 0x40, 0x40, 0x40, 0x40, 0x00, 0x01, 0x02, 0x04, 0x00, 0x20, 0x54, 0x54, 0x54, 0x78,
  0x7F, 0x48, 0x44, 0x44, 0x38, 0x38, 0x44, 0x44, 0x44, 0x20, 0x38, 0x44, 0x44, 0x48, 0x7F,
  0x38, 0x54, 0x54, 0x54, 0x18, 0x08, 0x7E, 0x09, 0x01, 0x02, 0x18, 0xA4, 0xA4, 0xA4, 0x7C,
  0x7F, 0x08, 0x04, 0x04, 0x78, 0x00, 0x44, 0x7D, 0x40, 0x00, 0x40, 0x80, 0x84, 0x7D, 0x00,
  0x7F, 0x10, 0x28, 0x44, 0x00, 0x00, 0x41, 0x7F, 0x40, 0x00, 0x7C, 0x04, 0x18, 0x04, 0x78,
  0x7C, 0x08, 0x04, 0x04, 0x78, 0x38, 0x44, 0x44, 0x44, 0x38, 0xFC, 0x24, 0x24, 0x24, 0x18,
  0x18, 0x24, 0x24, 0x18, 0xFC, 0x7C, 0x08, 0x04, 0x04, 0x08, 0x48, 0x54, 0x54, 0x54, 0x20,
  0x04, 0x3F, 0x44, 0x40, 0x20, 0x3C, 0x40, 0x40, 0x20, 0x7C, 0x1C, 0x20, 0x40, 0x20, 0x1C,
  0x3C, 0x40, 0x30, 0x40, 0x3C, 0x44, 0x28, 0x10, 0x28, 0x44, 0x1C, 0xA0, 0xA0, 0xA0, 0x7C,
  0x44, 0x64, 0x54, 0x4C, 0x44, 0x08, 0x36, 0x41, 0x41, 0x00, 0x00, 0x00, 0x7F, 0x00, 0x00,
  0x00, 0x41, 0x41, 0x36, 0x08, 0x08, 0x04, 0x08, 0x10, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

// OLED variables
uint8_t OLED_x, OLED_y;                           // current cursor position
const uint16_t DIVIDER[] = {1, 10, 100, 1000, 10000};
//const uint16_t DIVIDER[] = {10000, 1000, 100, 10, 1}; // BCD conversion array

// OLED init function
void OLED_init(void) {
  I2C_start(OLED_ADDR);                           // start transmission to OLED
  I2C_write(OLED_CMD_MODE);                       // set command mode
  for (uint8_t i = 0; i < sizeof(OLED_INIT_CMD); i++)
    I2C_write(OLED_INIT_CMD[i]);                  // send the command bytes
  I2C_stop();                                     // stop transmission
}

// OLED set the cursor
void OLED_setCursor(uint8_t xpos, uint8_t ypos) {
  xpos &= 0x7F; ypos &= 0x03;                     // limit cursor position values
  I2C_start(OLED_ADDR);                           // start transmission to OLED
  I2C_write(OLED_CMD_MODE);                       // set command mode
  I2C_write(xpos & 0x0F);                         // set low nibble of start column
  I2C_write(0x10 | (xpos >> 4));                  // set high nibble of start column
  I2C_write(0xB0 |  ypos);                        // set start page
  I2C_stop();                                     // stop transmission
  OLED_x = xpos; OLED_y = ypos;                   // set the cursor variables
}

// OLED clear rest of the current line
void OLED_clearLine(void) {
  I2C_start(OLED_ADDR);                           // start transmission to OLED
  I2C_write(OLED_DAT_MODE);                       // set data mode
  while(OLED_x++ < 128) I2C_write(0);             // clear rest of the line
  I2C_stop();                                     // stop transmission
  OLED_setCursor(0, ++OLED_y);                    // set cursor to start of next line
}

// OLED clear screen
void OLED_clearScreen(void) {
  OLED_setCursor(0, 0);                           // set cursor to home position
  for(uint8_t i=4; i; i--) OLED_clearLine();      // clear all 4 lines
}

// OLED plot a single character
void OLED_plotChar(char c) {
  uint16_t ptr = c - 32;                          // character pointer
  ptr += ptr << 2;                                // -> ptr = (ch - 32) * 5;
  I2C_write(0x00);                                // write space between characters
  for (uint8_t i=5 ; i; i--) I2C_write(OLED_FONT[ptr++]);
  OLED_x += 6;                                    // update cursor
  if (OLED_x > 122) {                             // line end ?
    I2C_stop();                                   // stop data transmission
    OLED_setCursor(0,++OLED_y);                   // set next line start
    I2C_start(OLED_ADDR);                         // start transmission to OLED
    I2C_write(OLED_DAT_MODE);                     // set data mode
  }
}

// OLED print a string
void OLED_print(const char* str) {
  I2C_start(OLED_ADDR);                           // start transmission to OLED
  I2C_write(OLED_DAT_MODE);                       // set data mode
  while(*str) OLED_plotChar(*str++);              // plot each character
  I2C_stop();                                     // stop transmission
}

// OLED print a string with new line
void OLED_println(const char* str) {
  OLED_print(str);                                // print the string
  OLED_clearLine();                               // clear rest of the line
}

// OLED print value (BCD conversion by substraction method)
void OLED_printVal(uint16_t value, uint8_t digits, uint8_t decimal) {
  uint8_t leadflag = 0;                           // flag for leading spaces
  I2C_start(OLED_ADDR);                           // start transmission to OLED
  I2C_write(OLED_DAT_MODE);                       // set data mode
  while(digits--) {                               // for all digits digits
    uint8_t digitval = 0;                         // start with digit value 0
    uint16_t divider = DIVIDER[digits];           // read current divider
    while(value >= divider) {                     // if current divider fits into the value
      leadflag = 1;                               // end of leading spaces
      digitval++;                                 // increase digit value
      value -= divider;                           // decrease value by divider
    }
    if(digits == decimal) leadflag++;             // end leading spaces before decimal
    if(leadflag) OLED_plotChar(digitval + '0');   // print the digit
    else         OLED_plotChar(' ');              // or print leading space
    if(decimal && (digits == decimal)) OLED_plotChar('.');  // print decimal
  }
  I2C_stop();                                     // stop transmission
}

// ===================================================================================
// RDA5807 Implementation
// ===================================================================================

// RDA definitions
#define RDA_ADDR_SEQ    0x20                      // RDA I2C write address for sequential access
#define RDA_ADDR_INDEX  0x22                      // RDA I2C write address for indexed access
#define RDA_VOL         1                         // start volume

// RDA register definitions
enum{ RDA_REG_2, RDA_REG_3, RDA_REG_4, RDA_REG_5, RDA_REG_6, RDA_REG_7 };
enum{ RDA_REG_A, RDA_REG_B, RDA_REG_C, RDA_REG_D, RDA_REG_E, RDA_REG_F };
uint16_t RDA_read_regs[6];                        // RDA registers for reading
uint16_t RDA_write_regs[6] = {                    // RDA registers for writing:
  0b1101001000001101,                             // RDA register 0x02 preset
  0b0001010111000000,                             // RDA register 0x03 preset
  0b0000101000000000,                             // RDA register 0x04 preset
  0b1000100010000000,                             // RDA register 0x05 preset
  0b0000000000000000,                             // RDA register 0x06 preset
  0b0000000000000000                              // RDA register 0x07 preset
};

// RDA state macros
#define RDA_hasRdsData        ( RDA_read_regs[RDA_REG_A] & 0x8000 )
#define RDA_isTuning          (~RDA_read_regs[RDA_REG_A] & 0x4000 )
#define RDA_tuningError       ( RDA_read_regs[RDA_REG_A] & 0x2000 )
#define RDA_hasRdsBlockE      ( RDA_read_regs[RDA_REG_A] & 0x0800 )
#define RDA_isStereo          ( RDA_read_regs[RDA_REG_A] & 0x0400 )
#define RDA_channel           ( RDA_read_regs[RDA_REG_A] & 0x03FF )
#define RDA_isTunedToChannel  ( RDA_read_regs[RDA_REG_B] & 0x0100 )
#define RDA_rdsBlockE         ( RDA_read_regs[RDA_REG_B] & 0x0010 )
#define RDA_rdsBlockErrors    ( RDA_read_regs[RDA_REG_B] & 0x000F )
#define RDA_signalStrength    ((RDA_read_regs[RDA_REG_B] & 0xFE00 ) >> 9 )

// RDA variables
char RDA_stationName[9];                          // string for the station name
char RDA_rdsStationName[8];                       // just for internal use

// RDA write specified register
void RDA_writeReg(uint8_t reg) {
  I2C_start(RDA_ADDR_INDEX);                      // start I2C for index write to RDA
  I2C_write(0x02 + reg);                          // set the register to write
  I2C_write(RDA_write_regs[reg] >> 8);            // send high byte
  I2C_write(RDA_write_regs[reg]);                 // send low byte
  I2C_stop();                                     // stop I2C
}

// RDA write all registers
void RDA_writeAllRegs(void) {
  I2C_start(RDA_ADDR_SEQ);                        // start I2C for sequential write to RDA
  for(uint8_t i=0; i<6; i++) {                    // write to 6 registers
    I2C_write(RDA_write_regs[i] >> 8);            // send high byte
    I2C_write(RDA_write_regs[i]);                 // send low byte
  }
  I2C_stop();                                     // stop I2C
}

// RDA read all registers
void RDA_readAllRegs(void) {
  I2C_start(RDA_ADDR_SEQ | 1);                    // start I2C for sequential read from RDA
  for(uint8_t i=0; i<6; i++)                      // read 6 registers
    RDA_read_regs[i] = (uint16_t)(I2C_read(1) << 8) | I2C_read(5-i);
  I2C_stop();                                     // stop I2C
}

// RDA clear station
void RDA_resetStation(void) {
  for(uint8_t i=0; i<8; i++) RDA_stationName[i] = ' ';
}

// RDA initialize tuner
void RDA_init(void) {
  RDA_resetStation();                             // reset station available
  RDA_stationName[8] = 0;                         // set string terminator
  RDA_write_regs[RDA_REG_2] |=  0x0002;           // set soft reset
  RDA_write_regs[RDA_REG_5] |=  RDA_VOL;          // set start volume
  RDA_writeAllRegs();                             // write all registers
  RDA_write_regs[RDA_REG_2] &= ~0x0002;           // clear soft reset
  RDA_writeReg(RDA_REG_2);                        // write to register 0x02
}

// RDA set volume
void RDA_setVolume(uint8_t vol) {
  RDA_write_regs[RDA_REG_5] &= ~0x000F;           // clear volume bits
  RDA_write_regs[RDA_REG_5] |=  vol;              // set volume
  RDA_writeReg(RDA_REG_5);                        // write to register 0x05
}

// RDA tune to a specified channel
void RDA_setChannel(uint16_t chan) {
  RDA_resetStation();
  RDA_write_regs[RDA_REG_3] &= ~0xFFC0;           // clear channel
  RDA_write_regs[RDA_REG_3] |= (chan << 6) | 0x0010;  // set channel and tune enable
  RDA_writeReg(RDA_REG_3);                        // write register
}

// RDA seek next channel
void RDA_seekUp(void) {
  RDA_resetStation();                             // clear station name
  RDA_write_regs[RDA_REG_2] |=  0x0100;           // set seek enable bit
  RDA_writeReg(RDA_REG_2);                        // write to register 0x02
}

// RDA update status and handle RDS
void RDA_updateStatus(void) {
  RDA_readAllRegs();                              // read all registers

  // When tuned disable tuning and stop seeking
  if (!RDA_isTuning) {
    RDA_write_regs[RDA_REG_3] &= ~0x0010;         // clear tune enable flag
    RDA_writeReg(RDA_REG_3);
    RDA_write_regs[RDA_REG_2] &= ~0x0100;         // clear seek enable flag
    RDA_writeReg(RDA_REG_2);
  }

  // Check for RDS data
  if(RDA_hasRdsData) {                            // RDS ready?
    // Toggle RDS flag to request new data
    RDA_write_regs[RDA_REG_2] &= ~0x0008;         // clear RDS flag
    RDA_writeReg(RDA_REG_2);                      // write to register 0x02
    RDA_write_regs[RDA_REG_2] |=  0x0008;         // set RDS flag
    RDA_writeReg(RDA_REG_2);                      // write to register 0x02

    // Decode RDS message (station name)
    if(!RDA_rdsBlockE) {                                         // REG_B..F carrying blocks A-D?
      if( (RDA_read_regs[RDA_REG_D] & 0xF800) == 0x0000) {       // is it station name?
        uint8_t offset = (RDA_read_regs[RDA_REG_D] & 0x03) << 1; // get character position
        uint8_t c1 = RDA_read_regs[RDA_REG_F] >> 8;              // get character 1
        uint8_t c2 = RDA_read_regs[RDA_REG_F];                   // get character 2

        // Copy station name characters only if received twice in a row...
        if(RDA_rdsStationName[offset] == c1)                     // 1st char received twice?
             RDA_stationName[offset] = c1;                       // copy to station name
        else RDA_rdsStationName[offset] = c1;                    // save for next test
        if(RDA_rdsStationName[offset + 1] == c2)                 // 2nd char received twice?
             RDA_stationName[offset + 1] = c2;                   // copy to station name
        else RDA_rdsStationName[offset + 1] = c2;                // save for next test
      }
    }
  }
}

// Calculate frequency in 10kHz
uint16_t RDA_getFrequency(void) {
  return(8700 + (RDA_channel << 3) + (RDA_channel << 1));
}

// Wait until tuning completed
void RDA_waitTuning(void) {
  do {
    _delay_ms(100);
    RDA_updateStatus();
  } while(RDA_isTuning);
}

// ===================================================================================
// ADC Implementation for Supply Voltage Measurement
// ===================================================================================

// ADC init for VCC measurements
void ADC_init(void) {
  VREF.CTRLA  = VREF_ADC0REFSEL_1V1_gc;           // select 1.1V internal reference
  ADC0.MUXPOS = ADC_MUXPOS_INTREF_gc;             // set internal reference as ADC input
  ADC0.CTRLC  = ADC_REFSEL_VDDREF_gc              // set VCC as reference
              | ADC_PRESC_DIV2_gc;                // set prescaler for 500 kHz ADC clock
  ADC0.CTRLD  = ADC_INITDLY_DLY64_gc;             // delay to settle internal reference
  ADC0.CTRLA  = ADC_RESSEL_bm                     // select 8-bit resolution
              | ADC_ENABLE_bm;                    // enable ADC, single shot
}

// ADC check supply voltage; return "TRUE" if battery is weak
uint8_t ADC_isWeak(void) {
  ADC0.INTFLAGS = ADC_RESRDY_bm;                  // clear result ready intflag
  ADC0.COMMAND  = ADC_STCONV_bm;                  // start sampling supply voltage
  while(~ADC0.INTFLAGS & ADC_RESRDY_bm);          // wait for ADC sampling to complete
  return(ADC0.RESL > 88);                         // true if VCC < 3.2V (88=256*1.1V/3.2V)
}

// ===================================================================================
// EEPROM Functions
// ===================================================================================

// Update frequency and volume stored in EEPROM
void EEPROM_update() {
  eeprom_update_word((uint16_t*)0, EEPROM_IDENT);
  eeprom_update_word((uint16_t*)2, RDA_channel);
  eeprom_update_byte((uint8_t*)4, volume);
}

// Read frequency and volume stored in EEPROM
uint8_t EEPROM_get() {
  uint16_t identifier = eeprom_read_word((const uint16_t*)0);
  if (identifier == EEPROM_IDENT) {
    channel = eeprom_read_word((const uint16_t*)2);
    volume  = eeprom_read_byte((const uint8_t*)4);
    return 1;
  }
  return 0;
}

// ===================================================================================
// Main Function
// ===================================================================================

int main(void) { 
  // Setup
  _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, 7);         // set clock frequency to 1 MHz
  ADC_init();                                     // setup ADC
  OLED_init();                                    // setup OLED
  pinPullup(PIN_VOL_UP);                          // enable pullups for button pins
  pinPullup(PIN_VOL_DOWN);
  pinPullup(PIN_CH_UP);

  // Print start info
  OLED_clearScreen();
  OLED_println(HEADER);
  OLED_print("Starting ...");

  // Start the tuner
  RDA_init();
  if(EEPROM_get()) {
    RDA_setChannel(channel - 2); 
    RDA_waitTuning();
  }
  RDA_setVolume(volume);
  RDA_seekUp();
  RDA_waitTuning();

  // Loop
  while(1) {
    // Update information on OLED
    RDA_updateStatus();
    OLED_setCursor(0, 1);
    OLED_print("Station:  ");
    OLED_println(RDA_stationName);
    OLED_print("Vol: ");
    OLED_printVal(volume, 2, 0);
    OLED_print("   Frq: ");
    OLED_printVal(RDA_getFrequency(), 5, 2);
    OLED_print("Sig: ");
    OLED_printVal(RDA_signalStrength, 2, 0);
    OLED_print("   Bat: ");
    if(ADC_isWeak()) OLED_println("weak");
    else             OLED_println("OK");

    // Check CH+ button
    if(!pinRead(PIN_CH_UP)) {
      OLED_setCursor(0, 1);
      OLED_println("Tuning ...");
      OLED_clearLine(); OLED_clearLine();
      RDA_seekUp();
      RDA_waitTuning();
      EEPROM_update();
      while(!pinRead(PIN_CH_UP));
    }

    // Check VOL+ button
    if (!pinRead(PIN_VOL_UP)) {
      if (volume < 15) volume++;
      RDA_setVolume(volume);
      EEPROM_update();
      while(!pinRead(PIN_VOL_UP));
    }

    // Check VOL- button
    if (!pinRead(PIN_VOL_DOWN)) {
      if (volume) volume--;
      RDA_setVolume(volume);
      EEPROM_update();
      while(!pinRead(PIN_VOL_DOWN));
    }
  }
}
