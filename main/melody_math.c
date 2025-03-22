#include <stdio.h>
#include <stdlib.h>  
#include <time.h>    
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "driver/gpio.h"


#define UART_NUM UART_NUM_1  
#define TXD_PIN 16           // ESP32-C6 TX -> SerLCD RX
#define BAUD_RATE 9600       

#define button1 18  // button1 -> GPIO18
#define button2 19  // button2 -> GPIO19
#define button3 20  // button3 -> GPIO20
#define button4 21  // button3 -> GPIO20

int correct_answer = 0; 
int answer_choices[4]; // four answer choices 
int equation_count = 0; // to track how many equations have been played 

void send_command(uint8_t command) {
    uint8_t cmd[] = {0xFE, command}; // 0xFE is command prefix for SerLCD
    uart_write_bytes(UART_NUM, (const char *)cmd, sizeof(cmd));
    vTaskDelay(pdMS_TO_TICKS(100)); // small delay 
}

void init_lcd() {
    // config UART1 for communication with SerLCD
    uart_config_t uart_config = {
        .baud_rate = BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    // install UART driver
    uart_driver_install(UART_NUM, 256, 0, 0, NULL, 0);
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, TXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    vTaskDelay(pdMS_TO_TICKS(1000)); // wait for SerLCD to init

    send_command(0x03); // reset LCD
    vTaskDelay(pdMS_TO_TICKS(500)); // wait for LCD to process

    send_command(0x01); // clear display
    vTaskDelay(pdMS_TO_TICKS(1000)); // wait for LCD to reset
}

void init_button() {
    gpio_set_direction(button1, GPIO_MODE_INPUT);
    gpio_set_pull_mode(button1, GPIO_PULLUP_ONLY); 

    gpio_set_direction(button2, GPIO_MODE_INPUT);
    gpio_set_pull_mode(button2, GPIO_PULLUP_ONLY); 

    gpio_set_direction(button3, GPIO_MODE_INPUT);
    gpio_set_pull_mode(button3, GPIO_PULLUP_ONLY); 

    gpio_set_direction(button4, GPIO_MODE_INPUT);
    gpio_set_pull_mode(button4, GPIO_PULLUP_ONLY); 
}

void generate_answer_choices(void) {
    // randomly generate 3 unique incorrect answers 
    answer_choices[0] = correct_answer; 
    for (int i = 1; i < 4; i++) {
        int duplicate; // a duplicate flag to check if this choice is already present 
        do {
            duplicate = 0; 
            answer_choices[i] = (rand() % 20) + 1; // generate rand num between 1 and 20 

            for (int j = 0; j < i; j++) { // loop through prev entries to check if this choice already exists 
                if (answer_choices[i] == answer_choices[j]) {
                    duplicate = 1; 
                    break; 
                }
            }
            
        } while (duplicate); // continue looping if we have a duplicate entry 
    }

    // shuffle the answer choices 
    for (int i = 0; i < 4; i++) {
        int j = rand() % 4; 
        int temp = answer_choices[i]; 
        answer_choices[i] = answer_choices[j]; // randomly switch the order around 
        answer_choices[j] = temp; 
    }

    // display the answer choices on the second line 
    char answers_display[16]; 
    sprintf(answers_display, "%d  %d   %d   %d", answer_choices[0],  answer_choices[1],  answer_choices[2],  answer_choices[3]); 
    send_command(0xC0); 
    uart_write_bytes(UART_NUM, answers_display, strlen(answers_display)); 
    vTaskDelay(pdMS_TO_TICKS(100)); // small delay
}

void generate_random_equation(void) {
    equation_count++; 

    int num1 = 0; 
    int num2 = 0; 
    // two rand numbers between 1 and 10 
    num1 = (rand() % 10) + 1;
    num2 = (rand() % 10) + 1;

    int operation = rand() % 2; // choose num between 0 (addition) or 1 (subtraction)
    char op_char = operation ? '-' : '+';

    // so we don't get negative numbers, make sure num1 is bigger 
    if (operation == 1 && num1 < num2) {
        int temp = num1;
        num1 = num2;
        num2 = temp;
    }

    // calculate the correct answer 
    correct_answer = operation == 0 ? num1 + num2 : num1 - num2; 

    // format equation into proper string to be displayed on the screen 
    char equation[16];
    sprintf(equation, "   %d %c %d = ?", num1, op_char, num2);

    // set up LCD 
    send_command(0x01); // clear display 
    vTaskDelay(pdMS_TO_TICKS(100));
    send_command(0x80); // move cursor to the start of the first line
    // print on LCD 
    uart_write_bytes(UART_NUM, equation, strlen(equation));
    vTaskDelay(pdMS_TO_TICKS(100)); // small delay
}

void check_answer(int button_index) {
    if(answer_choices[button_index] == correct_answer) {
        send_command(0x01); // clear screen 
        vTaskDelay(pdMS_TO_TICKS(100));
        send_command(0x80); 
        char msg[] = "Correct!"; 
        uart_write_bytes(UART_NUM, msg, strlen(msg)); 
        vTaskDelay(pdMS_TO_TICKS(100)); // small delay
    } else {
        send_command(0x01); // clear screen 
        vTaskDelay(pdMS_TO_TICKS(100));
        send_command(0x80); 
        char msg[] = "Wrong :("; 
        uart_write_bytes(UART_NUM, msg, strlen(msg)); 
        vTaskDelay(pdMS_TO_TICKS(100)); // small delay
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // delay before next question is shown 


    if (equation_count == 3) {
        equation_count = 0; // reset for next cycle 
        vTaskDelay(pdMS_TO_TICKS(100)); // small delay
        send_command(0x01); // clear screen 
        send_command(0x80); // first line 
        char msg[] = "music mode";
        uart_write_bytes(UART_NUM, msg, strlen(msg)); 
        vTaskDelay(pdMS_TO_TICKS(2000)); // show message for 2 seconds 
    }
    

    generate_random_equation(); 
    generate_answer_choices(); 
}

void button_task(void *pvParameter) {
    while (1) {
        if (gpio_get_level(button1) == 0) { // since it's active low 
            check_answer(0); 
            // while (gpio_get_level(button1) == 0); // wait until release 
            vTaskDelay(pdMS_TO_TICKS(50)); // debounce delay
        } else if (gpio_get_level(button2) == 0) { // since it's active low 
            check_answer(1); 
            // while (gpio_get_level(button1) == 0); // wait until release 
            vTaskDelay(pdMS_TO_TICKS(50)); // debounce delay
        } else if (gpio_get_level(button3) == 0) { // since it's active low 
            check_answer(2); 
            // while (gpio_get_level(button1) == 0); // wait until release 
            vTaskDelay(pdMS_TO_TICKS(50)); // debounce delay
        } else if (gpio_get_level(button4) == 0) { // since it's active low 
            check_answer(3); 
            // while (gpio_get_level(button1) == 0); // wait until release 
            vTaskDelay(pdMS_TO_TICKS(50)); // debounce delay
        } 
        vTaskDelay(pdMS_TO_TICKS(10)); // check every 10ms
    }
}


void app_main(void) {
    init_lcd(); 
    init_button(); 

    srand(time(NULL)); // seed random number generator
    generate_random_equation();
    generate_answer_choices(); 

    xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL); // run button as a separate task
}