#include <stdio.h>
#include <stdlib.h>  // For rand()
#include <time.h>    // For seeding rand()
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"

#define UART_NUM UART_NUM_1  
#define TXD_PIN 16           // ESP32-C6 TX connected to SerLCD RX
#define BAUD_RATE 9600       

void send_command(uint8_t command) {
    uint8_t cmd[] = {0xFE, command}; // 0xFE is command prefix for SerLCD
    uart_write_bytes(UART_NUM, (const char *)cmd, sizeof(cmd));
    vTaskDelay(pdMS_TO_TICKS(100)); // small delay 
}

void init_lcd() {
    // Configure UART1 for communication with SerLCD
    uart_config_t uart_config = {
        .baud_rate = BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    // Install UART driver
    uart_driver_install(UART_NUM, 256, 0, 0, NULL, 0);
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, TXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    vTaskDelay(pdMS_TO_TICKS(1000)); // wait for SerLCD to init

    send_command(0x03); // reset LCD
    vTaskDelay(pdMS_TO_TICKS(500)); // wait for LCD to process

    send_command(0x01); // clear display
    vTaskDelay(pdMS_TO_TICKS(1000)); // wait for LCD to reset
}

void app_main(void) {
    init_lcd(); // init LCD

    srand(time(NULL)); // Seed random number generator

    while (1) {
        // two rand numbers between 1 and 10 
        int num1 = (rand() % 10) + 1;
        int num2 = (rand() % 10) + 1;

        int operation = rand() % 2; // choose num between 0 (addition) or 1 (subtraction )
        char op_char = operation ? '-' : '+';

        // so we don't get negative numbers, make sure num1 is bigger 
        if (operation == 1 && num1 < num2) {
            int temp = num1;
            num1 = num2;
            num2 = temp;
        }

        // Format equation into proper string 
        char equation[16];
        sprintf(equation, "%d %c %d =", num1, op_char, num2);

        send_command(0x01); // clear display 
        vTaskDelay(pdMS_TO_TICKS(100));

        send_command(0x80); // move cursor to the start of the first line

        // print on LCD 
        uart_write_bytes(UART_NUM, equation, strlen(equation));

        vTaskDelay(pdMS_TO_TICKS(3000)); // update every 3 seconds
    }
}
