#include <stdio.h>
#include <stdlib.h>  
#include <time.h>    
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "driver/gpio.h"
#include <unistd.h>
#include "vl53l0x.h"
#include "driver/i2c.h"
#include "driver/ledc.h"


// PWM stuff 
#define SERVO_PIN 10 // for the servo 
#define AUDIO_PIN 18 // PWM output pin for the speaker


// LEDC PWM Config
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_FREQUENCY          50  // 50Hz for servo control
#define LEDC_RESOLUTION         LEDC_TIMER_16_BIT
#define SERVO_MIN_PULSEWIDTH    500  // 0.5ms (0 degrees)
#define SERVO_MAX_PULSEWIDTH    2500 // 2.5ms (180 degrees)
#define SERVO_MAX_DEGREE        180  // Max rotation

#define LEDC_TIMER LEDC_TIMER_0

// int melody[] = { 262, 294, 330, 349, 392, 440, 494, 523 };
// int noteDurations[] = { 400, 400, 400, 400, 400, 400, 400, 800 };
int melody[] = { 330, 349, 392, 294, 330, 262, 330, 440, 330 }; // in Hz
int noteDurations[] = { 300, 400, 500, 300, 400, 500, 300, 600, 800 }; // in ms 
int errorMelody[] = { 500, 300, 100 };  // Descending error sound
int errorDurations[] = { 200, 200, 200 };

int melodyLength = sizeof(melody) / sizeof(melody[0]);

int errorLength = sizeof(errorMelody) / sizeof(errorMelody[0]);
int pauseDuration = 100; // for the pause between the notes 



#define UART_NUM UART_NUM_1  
#define TXD_PIN 16           // esp32 TX to lcd RX
#define BAUD_RATE 9600       

#define button1 5  
#define button2 0 
#define button3 1 
#define button4 2  

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SDA_IO 6 // sda
#define I2C_MASTER_SCL_IO 7 // scl 
#define I2C_MASTER_FREQ_HZ 100000
#define VL53L0X_ADDR 0x29

// registers needed to read distance 
#define VL53L0X_REG_SYSRANGE_START 0x00
#define VL53L0X_REG_RESULT_RANGE_STATUS 0x14
#define VL53L0X_REG_RESULT_RANGE 0x1E

int correct_answer = 0; 
int answer_choices[4]; // four answer choices 
int equation_count = 0; // to track how many equations have been played 

// global vars for the distance sensor 
uint16_t distance; 
esp_err_t distance_err; 


void init_speaker() { 
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,      
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0, // uses a different timer than the pwm for the servo 
        .freq_hz = 1000 // default frequency
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .gpio_num = AUDIO_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, // uses a different channel for than the one for the servo 
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0 // we start with no sound 
    };
    ledc_channel_config(&ledc_channel);
}

// original: 
// void init_pwm() { // for the speaker 
//     ledc_timer_config_t ledc_timer = {
//         .speed_mode = LEDC_LOW_SPEED_MODE,      
//         .duty_resolution = LEDC_TIMER_10_BIT,
//         .timer_num = LEDC_TIMER,
//         .freq_hz = 1000 // default frequency
//     };
//     ledc_timer_config(&ledc_timer);

//     ledc_channel_config_t ledc_channel = {
//         .gpio_num = AUDIO_PIN,
//         .speed_mode = LEDC_LOW_SPEED_MODE,
//         .channel = LEDC_CHANNEL,
//         .intr_type = LEDC_INTR_DISABLE,
//         .timer_sel = LEDC_TIMER,
//         .duty = 0 // we start with no sound 
//     };
//     ledc_channel_config(&ledc_channel);
// }


// Function to calculate PWM duty cycle
int angle_to_duty(int angle) {
    return (SERVO_MIN_PULSEWIDTH + (angle * (SERVO_MAX_PULSEWIDTH - SERVO_MIN_PULSEWIDTH) / SERVO_MAX_DEGREE));
}

void servo_init() { // uses different timer and channel than the speaker 
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_RESOLUTION,
        .freq_hz = LEDC_FREQUENCY
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .gpio_num = SERVO_PIN,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER,
        .duty = 0, // Start with 0 duty cycle
        .hpoint = 0
    };
    ledc_channel_config(&ledc_channel);
}

// Move servo to a specific angle
void servo_set_angle(int angle) {
    int duty = angle_to_duty(angle); // Convert angle to duty cycle
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}


void play_melody(int melodyLength, int* melody, int* noteDuration) {
    for (int i = 0; i < melodyLength; i++) {
        int frequency = melody[i];
        int duration = noteDurations[i];

        // ESP_LOGI(TAG, "Playing: %d Hz, Duration: %d ms", frequency, duration);

        // Set frequency and enable sound
        ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER, frequency);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL, 512); // 50% duty cycle
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL);

        vTaskDelay(duration / portTICK_PERIOD_MS);

        // Stop sound
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL);

        vTaskDelay(pauseDuration / portTICK_PERIOD_MS);
    }
}


// ========================== i2c helper functions ==========================
esp_err_t i2c_write_byte(uint8_t reg, uint8_t data) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (VL53L0X_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return err;
}

esp_err_t i2c_read_byte(uint8_t reg, uint8_t *data) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (VL53L0X_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (VL53L0X_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, data, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return err;
}

esp_err_t i2c_read_word(uint8_t reg, uint16_t *data) {
    uint8_t high, low;
    esp_err_t err = i2c_read_byte(reg, &high);
    if (err != ESP_OK) return err;
    err = i2c_read_byte(reg + 1, &low);
    if (err != ESP_OK) return err;
    *data = (high << 8) | low;
    return ESP_OK;
}

// =============================================================================


// ============ functions to make it easier to write to the LCD  =============

void send_command(uint8_t command) {
    uint8_t cmd[] = {0xFE, command}; // 0xFE is command prefix for SerLCD
    uart_write_bytes(UART_NUM, (const char *)cmd, sizeof(cmd));
    vTaskDelay(pdMS_TO_TICKS(100)); // small delay 
}

void lcd_clear() { // to clear the LCD dispay
    send_command(0x01); // clear display
}

void lcd_write_first(char msg[16]) { // to write to the first line of the screen 
    send_command(0x80); // first line 
    uart_write_bytes(UART_NUM, msg, strlen(msg));
}

void lcd_write_second(char msg[16]) { // to write to the second line of the screen 
    send_command(0xC0); // first line 
    uart_write_bytes(UART_NUM, msg, strlen(msg));
}

// =============================================================================


// ========================== initializing things  ============================

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

void init_distance_sensor() {
    i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &i2c_config));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, I2C_MODE_MASTER, 0, 0, 0));

    // send_command(0x80); // first line 
    // char msg[] = "initializied";
    // uart_write_bytes(UART_NUM, msg, strlen(msg));
}

// =============================================================================


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

void music_mode(void) {
    lcd_clear(); 
    char msg[16] = "   Music Mode";
    lcd_write_first(msg); 
    // send_command(0x80); // first line 
    // uart_write_bytes(UART_NUM, msg, strlen(msg));
    vTaskDelay(pdMS_TO_TICKS(2000)); // show message for 2 seconds 

    play_melody(melodyLength, melody, noteDurations);
    vTaskDelay(1000 / portTICK_PERIOD_MS); // wait before next cycle
}

void check_answer(int button_index) {
    if(answer_choices[button_index] == correct_answer) {
        send_command(0x01); // clear screen 
        vTaskDelay(pdMS_TO_TICKS(100));
        // send_command(0x80); 
        char msg[16] = "Correct!"; 
        lcd_write_first(msg); 
        // uart_write_bytes(UART_NUM, msg, strlen(msg)); 
        vTaskDelay(pdMS_TO_TICKS(100)); // small delay
    } else {
        send_command(0x01); // clear screen 
        vTaskDelay(pdMS_TO_TICKS(100));
        char msg[16] = "Wrong :("; 
        lcd_write_first(msg); 
        vTaskDelay(pdMS_TO_TICKS(100)); // small delay
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // delay before next question is shown 


    if (equation_count == 3) {
        equation_count = 0; // reset for next cycle 
        music_mode(); // go into music mode 
    }

    // generate new question 
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


// esp_err_t read_distance() { // read distance from the sensor 
//     while (1) {
//         i2c_write_byte(VL53L0X_REG_SYSRANGE_START, 0x01);
//         vTaskDelay(50 / portTICK_PERIOD_MS);

//         // the the result into variable 'distance'  
//         distance_err = i2c_read_word(VL53L0X_REG_RESULT_RANGE, &distance);

//         if (distance_err == ESP_OK) {
//             lcd_clear(); 
//             if (distance > 20 && distance < 500) {
//                 char msg[16];
//                 sprintf(msg, "dist: %d", distance); 
//                 lcd_write_second(msg); 
//             } else {
//                 send_command(0xC0); // first line 
//                 char msg[] = "out of bounds"; 
//                 // lcd_write_second(msg); 
//                 uart_write_bytes(UART_NUM, msg, strlen(msg));
//             }    
//         } else { // issue with being able to read from the sensor properly 
//             char msg[16]; 
//             sprintf(msg, "Error: %s", esp_err_to_name(distance_err)); // read the error message
//             // lcd_write_second(msg); 
//         }

//         vTaskDelay(500 / portTICK_PERIOD_MS );
//     }
// }


esp_err_t read_distance() { // read distance from the sensor 
    while (1) {
        i2c_write_byte(VL53L0X_REG_SYSRANGE_START, 0x01);
        vTaskDelay(50 / portTICK_PERIOD_MS);

        // the the result into variable 'distance'  
        distance_err = i2c_read_word(VL53L0X_REG_RESULT_RANGE, &distance);

        if (distance_err == ESP_OK) {
            lcd_clear(); 
            if (!(distance > 20 && distance < 500)){
                send_command(0xC0); // first line 
                char msg[] = "out of bounds"; 
                // lcd_write_second(msg); 
                uart_write_bytes(UART_NUM, msg, strlen(msg));
            } else if (distance > 200) {
                char msg[16];
                sprintf(msg, "dist: %d", distance); 
                lcd_write_first(msg); 
                char msg2[16] = "melody 1"; 
                lcd_write_second(msg2); 
                play_melody(melodyLength, melody, noteDurations);
                vTaskDelay(pdMS_TO_TICKS(1000));
            } else  { 
                char msg[16];
                sprintf(msg, "dist: %d", distance); 
                lcd_write_first(msg); 
                char msg2[16] = "melody 2"; 
                lcd_write_second(msg2); 

                play_melody(errorLength, errorMelody, errorDurations); 
                vTaskDelay(pdMS_TO_TICKS(1000));
            }

        } else { // issue with being able to read from the sensor properly 
            char msg[16]; 
            sprintf(msg, "Error: %s", esp_err_to_name(distance_err)); // read the error message
            // lcd_write_second(msg); 
        }

        vTaskDelay(500 / portTICK_PERIOD_MS );
    }
}
void speaker_task(void *pvParameter) {
    while (1) {
        play_melody(melodyLength, melody, noteDurations); 
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void) {
    // initialize everything 
    init_lcd(); 
    init_button(); 
    init_distance_sensor(); 
    init_speaker();

    // play_melody(); 

    // srand(time(NULL)); // seed random number generator
    // generate_random_equation();
    // generate_answer_choices(); 

    // xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL); // run button as a separate task
    read_distance(); // continuously read distance from the distance sensor 
    // xTaskCreate(speaker_task, "speaker_task", 2048, NULL, 5, NULL);

    servo_init();

    // while (1) {
        // start at 180 
        
        // printf("Rotating to 180°\n");
        // servo_set_angle(180);
        // for (int i = 0; i < 20; i++) {
        //     char msg[16];
        //     sprintf(msg, "i: %d", i);  
        //     lcd_write_first(msg);
        //     servo_set_angle(i * 9); 
        //     vTaskDelay(pdMS_TO_TICKS(500));

        // }
        
        // servo_init();
        // char msg[16] = "degree 0"; 
        // lcd_clear(); 
        // lcd_write_first(msg); 
        // servo_set_angle(0); 
        // vTaskDelay(pdMS_TO_TICKS(2000));
        // init_speaker(); 
        // char msg1[16] = "playing melody"; 
        // lcd_clear(); 
        // lcd_write_first(msg1); 
        // play_melody(); 
        // vTaskDelay(pdMS_TO_TICKS(2000));
        // servo_init();
        // char msg2 [16] = "degree 180"; 
        // lcd_clear(); 
        // lcd_write_first(msg2); 
        // servo_set_angle(180); 
        // vTaskDelay(pdMS_TO_TICKS(2000));

        // servo_set_angle(180);

        // // printf("Rotating to 0°\n");
        // char msg[16] = "rotating"; 
        // // lcd_write_first(msg);
        // servo_set_angle(0);
        // vTaskDelay(pdMS_TO_TICKS(2000));

        // printf("Rotating to 90°\n");
        // servo_set_angle(90);
        // vTaskDelay(pdMS_TO_TICKS(2000));         
    // } 
}





// void app_main() {
//     // i2c_master_init();
//     i2c_config_t i2c_config = {
//         .mode = I2C_MODE_MASTER,
//         .sda_io_num = I2C_MASTER_SDA_IO,
//         .scl_io_num = I2C_MASTER_SCL_IO,
//         .sda_pullup_en = GPIO_PULLUP_ENABLE,
//         .scl_pullup_en = GPIO_PULLUP_ENABLE,
//         .master.clk_speed = I2C_MASTER_FREQ_HZ,
//     };
//     ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &i2c_config));
//     ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, I2C_MODE_MASTER, 0, 0, 0));

//     pwm_init();

//     while (1) {
//         // uint16_t distance;
//         // if (read_distance(&distance) == ESP_OK) {
//         //     ESP_LOGI(TAG, "Distance: %d mm", distance);
//         // } else {
//         //     ESP_LOGE(TAG, "Failed to read distance");
//         // }

//         play_melody();
//         vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait before next cycle
//     }
// }




// // speaker is output if pin 10 (7 from the bottom on the right )

// the following is for the servo 


// void app_main(void) {
//     servo_init();

//     while (1) {
//         printf("Rotating to 0°\n");
//         servo_set_angle(0);
//         vTaskDelay(pdMS_TO_TICKS(2000));

//         printf("Rotating to 90°\n");
//         servo_set_angle(90);
//         vTaskDelay(pdMS_TO_TICKS(2000));

//         printf("Rotating to 180°\n");
//         servo_set_angle(180);
//         vTaskDelay(pdMS_TO_TICKS(2000));
//     }
// }




// over  or under certain range are two diff choices 
// 