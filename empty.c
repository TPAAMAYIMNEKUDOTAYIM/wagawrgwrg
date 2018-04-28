/*
 * Copyright (c) 2015, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  ======== empty.c ========
 */

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* XDCtools Header files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Error.h>

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Semaphore.h>

/* TI-RTOS Header files */
// #include <ti/drivers/EMAC.h>
#include <ti/drivers/GPIO.h>
// #include <ti/drivers/I2C.h>
// #include <ti/drivers/SDSPI.h>
// #include <ti/drivers/SPI.h>
#include <ti/drivers/UART.h>
// #include <ti/drivers/USBMSCHFatFs.h>
// #include <ti/drivers/Watchdog.h>
// #include <ti/drivers/WiFi.h>

/* Board Header file */
#include "Board.h"

/* Graphics Libraries */
#include "grlib/grlib.h"
#include "driverlib/rom.h"
#include "driverlib/rom_map.h"
#include "driverlib/sysctl.h"
#include "drivers/frame.h"
#include "drivers/kentec320x240x16_ssd2119.h"
#include "drivers/pinout.h"

#include "driverlib/hibernate.h"

UART_Handle uart;

#define BUFFER_SIZE 100

typedef struct UartBuffer {
    int length;
    char data[BUFFER_SIZE];
} UartBuffer;

UartBuffer buffer, whitespace;
tContext g_sContext;
Semaphore_Handle sem;

void initUart(UART_Params* uartParams, UART_Handle* uartHandle);
void uartPoller();
void lcdWriter();
void initHibernate(uint32_t sysClock);
void initTime();
void drawTime();
void updateTime();
/*
 *  ======== main ========
 */
int main(void)
{
    buffer.length = 0;
    whitespace.length = 0;
    /* Call board init functions */
    Board_initGeneral();
    // Board_initEMAC();
    Board_initGPIO();
    // Board_initI2C();
    // Board_initSDSPI();
    // Board_initSPI();
    Board_initUART();
    // Board_initUSB(Board_USBDEVICE);
    // Board_initUSBMSCHFatFs();
    // Board_initWatchdog();
    // Board_initWiFi();

    UART_Params uartParams;
    initUart(&uartParams, &uart);

    /* Setup gfx */
    uint32_t ui32SysClock;
    ui32SysClock = MAP_SysCtlClockFreqSet((SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN | SYSCTL_USE_PLL | SYSCTL_CFG_VCO_480), 120000000);
    PinoutSet();
    Kentec320x240x16_SSD2119Init(ui32SysClock);
    GrContextInit(&g_sContext, &g_sKentec320x240x16_SSD2119);
    FrameDraw(&g_sContext, "Lab 5");

    /* Initialize semaphore */
    Error_Block eb;
    Error_init(&eb);
    Semaphore_Params sem_params;
    Semaphore_Params_init(&sem_params);
    sem = Semaphore_create(0, &sem_params, &eb );
    if (sem == NULL) {
        System_abort("Semaphore create failed");
    }

    /* Init hibernate module */
    initHibernate(ui32SysClock);

    /* Init date/time */
    initTime();


    /* Start BIOS */
    BIOS_start();

    return (0);
}


void initUart(UART_Params* uartParams, UART_Handle* uartHandle) {
    UART_Params_init(uartParams);
    uartParams->writeDataMode = UART_DATA_BINARY;
    uartParams->readDataMode = UART_DATA_BINARY;
    uartParams->readReturnMode = UART_RETURN_FULL;
    uartParams->readEcho = UART_ECHO_OFF;
    uartParams->baudRate = 9600;
    uartParams->readMode = UART_MODE_BLOCKING;
    uartParams->writeMode = UART_MODE_BLOCKING;
    *uartHandle = UART_open(Board_UART0, uartParams);

    if ((*uartHandle) == NULL) {
        System_abort("Error opening the UART");
    }
}

void uartPoller() {
    char input;
    /* Echo forever */
    while (1) {
        UART_read(uart, &input, 1);
        buffer.data[buffer.length++] = input;
        buffer.data[buffer.length] = '\0';
        UART_write(uart, &input, 1);

        if (input == '\r') {
            Semaphore_post(sem);
        }
    }
}

void lcdWriter() {
    int prevLength = 0;
    while (1) {
        Semaphore_pend(sem, BIOS_WAIT_FOREVER);

        int i;
        for (i = 0; i < prevLength + 5; i++) {
            whitespace.data[i] = ' ';
        }
        whitespace.data[i++] = '\0';
        GrStringDraw(&g_sContext, whitespace.data, -1, 100, 108, 1);
        prevLength = buffer.length;

        GrStringDraw(&g_sContext, buffer.data, -1, 100, 108, 1);
        buffer.length = 0;
        buffer.data[buffer.length] = '\0';
    }
}


void initHibernate(uint32_t sysClock) {
    SysCtlPeripheralEnable(SYSCTL_PERIPH_HIBERNATE);
    HibernateEnableExpClk(sysClock);
    HibernateClockConfig(HIBERNATE_OSC_LOWDRIVE);
    HibernateRTCEnable();
    HibernateCounterMode(HIBERNATE_COUNTER_24HR);
}

void initTime() {
    struct tm initTime;
    initTime.tm_hour = 12;
    initTime.tm_min = 30;
    initTime.tm_mon = 3;
    initTime.tm_mday = 27;
    initTime.tm_year = 118;
    HibernateCalendarSet(&initTime);
}

void drawTime() {
    struct tm sTime;
    char timeStr[26];
    HibernateCalendarGet(&sTime);
    strftime(timeStr, 26, "%Y-%m-%d %H:%M:%S", &sTime);
    GrStringDraw(&g_sContext, timeStr, -1, 100, 32, 1);
}

void updateTime() {
    drawTime();
}

