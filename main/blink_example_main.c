#include <stdio.h>
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
    vTaskDelay(pdMS_TO_TICKS(100)); // Small delay for LCD to process
}

void app_main(void) {
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

    vTaskDelay(pdMS_TO_TICKS(1000)); // wait for SerLCD to initialize

    // Initialization sequence
    send_command(0x03); // Reset LCD
    vTaskDelay(pdMS_TO_TICKS(500)); // Wait for LCD to process

    send_command(0x01); // Clear display
    vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for LCD to reset

    // Flush UART buffer before writing
    uart_flush(UART_NUM);

    // Write first line
    const char *message = "Testing Line 1";
    uart_write_bytes(UART_NUM, message, strlen(message));

    // Move cursor to second line
    send_command(0xC0); // Move cursor
    vTaskDelay(pdMS_TO_TICKS(100)); // Short delay

    // Write second line
    const char *message2 = "Testing Line 2";
    uart_write_bytes(UART_NUM, message2, strlen(message2));
}
