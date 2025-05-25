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
#include "esp_mac.h"
#include "melody_math.h"


// ========================== PWM stuff ==========================
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

// calculates PWM duty cycle
int angle_to_duty(int angle) {
    return (SERVO_MIN_PULSEWIDTH + (angle * (SERVO_MAX_PULSEWIDTH - SERVO_MIN_PULSEWIDTH) / SERVO_MAX_DEGREE));
}

// moves servo to a specific angle
void servo_set_angle(int angle) {
    int duty = angle_to_duty(angle); // convert angle to duty cycle
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

// ========================== melodies for sound effects ==========================

int melody[] = { 330, 349, 392, 294, 330, 262, 330, 440, 330 }; // in Hz
int noteDurations[] = { 300, 400, 500, 300, 400, 500, 300, 600, 800 }; // in ms 

int game_over_melody[] = { 500, 300, 100}; 
int game_over_durations[] = { 200, 200, 400,};

int wrong_melody[] = { 300, 368, 300 }; // A3
int wrong_duration[] = { 200, 200, 300 }; // ms

int correct_melody[] = { 784, 988 }; // C5
int correct_duration[] = {200, 300}; // ms

int next_level_melody[] = { 262, 330, 392, 523 }; // C4, E4, G4, C5
int next_levle_duration[] = { 200, 200, 300, 500 }; // ms

int pauseDuration = 100; // for the pause between the notes 

// ========================== variables for math and music mode ==========================
// for the music mode 
int musicMode = 0; // flag: 0 if not in music mode, 1 if we are in music mode 
bool music_answer_selected = 0; // flag: 0 if nothing selected yet, 1 if seelcted 
bool music_level_passed = 0; // flag: 0 if music level not passed, 1 if passed
int music_user_answer = -1; // the result the answer chose 
int music_level = 1; // represents number of tunes per music level 

int saved_melody[100]; // assuming we won't get more than 100 rounds 
int saved_note_duration[100]; // duration for the saved melody to be played 

int music_answer_correct[100]; // stores the correct melody 
int music_answer_guessed[100]; // stores the melody the user inputted 
int music_guessed_count = 0; 
int music_correct_count = 0; // counts how many notes are correct so far 

// for the math mode 
int correct_answer = 0; 
int answer_choices[4]; // four answer choices 
int math_correct_count = 0; 

int game_round = 1; // start at round 1 

// ========================== LCD screen macros ==========================
#define UART_NUM UART_NUM_1  
#define TXD_PIN 16           // esp32 TX to lcd RX
#define BAUD_RATE 9600       

// ========================== push button macros ==========================
#define button1 5  
#define button2 0 
#define button3 1 
#define button4 2  

// ========================== distance sensor things ==========================
// I2C
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SDA_IO 6 // sda
#define I2C_MASTER_SCL_IO 7 // scl 
#define I2C_MASTER_FREQ_HZ 100000
#define VL53L0X_ADDR 0x29

// registers needed to read distance 
#define VL53L0X_REG_SYSRANGE_START 0x00
#define VL53L0X_REG_RESULT_RANGE_STATUS 0x14
#define VL53L0X_REG_RESULT_RANGE 0x1E

// global vars for the distance sensor 
uint16_t distance; 
esp_err_t distance_err; // holds potential error message from the distance sensor 


// ------ i2c helper functions ------ 
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


// ============ LCD Functions   =============

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



// ========================== initializing components  ============================

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

// ============================= math mode functions =============================
void generate_random_equation(void) {
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
    lcd_clear(); // clear display 
    vTaskDelay(pdMS_TO_TICKS(100));
    lcd_write_first(equation); // print on first line of LCD 
    vTaskDelay(pdMS_TO_TICKS(100)); // small delay
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

void math_game(void) {
    if(!musicMode) {
        generate_random_equation();
        generate_answer_choices(); 
    }
    
}

// ============================= speaker and music functions =============================
void play_tune(int frequency, int duration) { // only plays a singular tune 
    init_speaker();

    // set frequency and enable sound
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER, frequency);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL, 512); // 50% duty cycle
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL);

    vTaskDelay(duration / portTICK_PERIOD_MS);

    // stop sound
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL);

    vTaskDelay(pauseDuration / portTICK_PERIOD_MS);
}

void play_soundEffect(int melodyLength, int* melody, int* noteDuration) {
    for (int i = 0; i < melodyLength; i++) {
        play_tune(melody[i], noteDurations[i]);
    }
}

void play_game_melody(int melodyLength) {
    int melody[melodyLength]; 
    int noteDurations[melodyLength]; 
    
    // create melody 
    for (int i = 0; i < melodyLength; i++) {
        melody[i] = (rand() % 500) + 300; // random frequency between 200Hz - 1000Hz
        noteDurations[i] = (rand() % 400) + 100; // random duration between 100ms - 500ms 
    }

    for (int i = 0; i < melodyLength; i++) {
        init_speaker();
        int frequency = melody[i];
        int duration = noteDurations[i];

        saved_melody[i] = melody[i]; 
        saved_note_duration[i] = noteDurations[i];
        // choose a random number out of 6
        int rand_num = (rand() % 5); // rand num between 0 and 5
        music_answer_correct[i] = rand_num; 

        print_single_note(rand_num); 

        play_tune(frequency, duration); // play the tun e

        vTaskDelay(duration / portTICK_PERIOD_MS);
        lcd_clear(); 
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    lcd_clear(); 
    print_music_answer_choices(); 
}

void print_music_answer_choices(void) {
    static char upper[16] = "a  b   c   d   e";
    lcd_write_first(upper);

    static char lower[20] = "               f";
    lcd_write_second(lower);
}

void print_single_note(int position) { // zero-indexed 
    static char music_note[17]; 
    switch(position) {
        case 0:
            strcpy(music_note, "a");
            break; 
        case 1:
            strcpy(music_note, "   b"); 
            break; 
        case 2:
            strcpy(music_note, "       c");
            break; 
        case 3:
            strcpy(music_note, "           d");
            break; 
        case 4:
            strcpy(music_note, "               e");
            break; 
        case 5: 
            strcpy(music_note, "               f");
            break; 
        default: 
            strcpy(music_note, "z"); 
    }

    if (position == 5) { // only f goes on the second line 
        lcd_write_second(music_note); 
    } else {
        lcd_write_first(music_note); 
    }
}

void correct_note(void){
     // display note chosen 
     lcd_clear(); 
     print_single_note(music_answer_correct[music_correct_count]); 
     vTaskDelay(pdMS_TO_TICKS(300));

     play_tune(saved_melody[music_guessed_count], saved_note_duration[music_guessed_count]); 
     vTaskDelay(pdMS_TO_TICKS(600));

     music_correct_count++; 
     music_guessed_count++; 
     music_answer_selected = 0; // turn flag back off 

     if (music_correct_count != music_level) {
        print_music_answer_choices(); 
     }
     
}

void wrong_note(int wrong_choice) {
    lcd_clear();
    print_single_note(wrong_choice); 
    vTaskDelay(pdMS_TO_TICKS(600));
    lcd_clear();

    char msg[16] = "    Wrong :(";
    lcd_write_first(msg);  
    play_soundEffect(3, wrong_melody, wrong_duration);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // reset variables 
    music_level = 1; 
    music_mode_reset();
    
    game_over(); 
}

// ================================== game mechanics functions ==================================
void game_over(void) {
    char upper[16] = "    Game Over"; 
    char lower[16] = "   Try again!";
    lcd_write_first(upper); 
    lcd_write_second(lower); 

    play_soundEffect(3, game_over_melody, game_over_durations);

    vTaskDelay(pdMS_TO_TICKS(3000)); 

    // reset variables and flags 
    musicMode = 0; // flag - 0 since we are not in music mode 
    music_answer_selected = 0;
    music_user_answer = -1; // the result the answer chose 
    music_level_passed = 0;  
    music_level = 1; // start with one tune 

    correct_answer = 0; 
    math_correct_count = 0; // to track how many equations have been played 

    music_guessed_count = 0; 
    music_correct_count = 0; // counts how many notes are correct so far 

    game_round = 1; 

    lcd_clear(); 
    vTaskDelay(pdMS_TO_TICKS(500)); 
    math_game(); 

}

void next_round(void) {
    lcd_clear(); 
    game_round++; 
    
    char msg[40]; 
    sprintf(msg, "    Round %d!", game_round); 
    lcd_write_first(msg);  
    play_soundEffect(4, next_level_melody, next_levle_duration); // play sound effect 
    vTaskDelay(pdMS_TO_TICKS(1000));

    music_level++; 
    music_level_passed = 1; 

    music_mode_reset(); // we only advance to next round after the music mode has been passed 

    math_game();
}

void check_music_answer(int button_index) {
    // a = 0, b = 1, c = 2, d = 3, e < 200, f > 200
    if(button_index == music_answer_correct[music_correct_count]) {
        correct_note(); 
    } else {
        wrong_note(button_index); 
    }

    if (music_correct_count == music_level) { // all correct !
        lcd_clear(); 
        vTaskDelay(pdMS_TO_TICKS(600)); 
        next_round(); 
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); 
}

void music_mode_reset(void) {
    lcd_clear(); 
    vTaskDelay(pdMS_TO_TICKS(500)); // delay before next question is shown 

    // reset variables 
    musicMode = 0; // flag down
    music_user_answer = -1; // reset answer choice 
    music_answer_selected = 0; // music answer not selected 
    music_correct_count = 0;
    music_guessed_count = 0; 
    music_level_passed = 0; 
}

void music_mode(void) {
    musicMode = 1; // raise flag 
    play_game_melody(music_level);
}

void check_answer(int button_index) {
    if(answer_choices[button_index] == correct_answer) {
        math_correct_count++; 
        lcd_clear(); 
        vTaskDelay(pdMS_TO_TICKS(100));
        char msg[16] = "   Correct!"; 
        lcd_write_first(msg); 

        play_soundEffect(2, correct_melody, correct_duration);

        vTaskDelay(pdMS_TO_TICKS(1000)); // delay before next question is shown 
    } else {
        lcd_clear(); 
        vTaskDelay(pdMS_TO_TICKS(100));
        char msg[16] = "    Wrong :("; 
        lcd_write_first(msg); 
        play_soundEffect(3, wrong_melody, wrong_duration);
        vTaskDelay(pdMS_TO_TICKS(1000)); // delay before next question is shown 
        
        game_over(); 
    }


    if (math_correct_count == 3) { // to be able to move to music mode 
        math_correct_count = 0; // reset for next cycle 
        vTaskDelay(pdMS_TO_TICKS(500));
        lcd_clear(); 
        music_mode(); // go into music mode 
    }

    // go back to math game 
    math_game(); 
}

// ================================== component tasks ==================================
void button_task(void *pvParameter) {
    while (1) {
        if(!musicMode) { // the following is for math mode
            if (gpio_get_level(button1) == 0) { 
                check_answer(0); 
                vTaskDelay(pdMS_TO_TICKS(50)); // debounce delay
            } else if (gpio_get_level(button2) == 0) {
                check_answer(1); 
                vTaskDelay(pdMS_TO_TICKS(50)); // debounce delay
            } else if (gpio_get_level(button3) == 0) { 
                check_answer(2); 
                vTaskDelay(pdMS_TO_TICKS(50)); // debounce delay
            } else if (gpio_get_level(button4) == 0) { 
                check_answer(3); 
                vTaskDelay(pdMS_TO_TICKS(50)); // debounce delay
            } 
            vTaskDelay(pdMS_TO_TICKS(10)); // check every 10ms
        } else { // the following is for music mode
            if (gpio_get_level(button1) == 0) { 
                music_answer_selected = 1; 
                music_user_answer = 0; 
                check_music_answer(0); 
                vTaskDelay(pdMS_TO_TICKS(50)); // debounce delay
            } else if (gpio_get_level(button2) == 0) { 
                music_answer_selected = 1; 
                music_user_answer = 1; 
                check_music_answer(1);
                vTaskDelay(pdMS_TO_TICKS(50)); // debounce delay
            } else if (gpio_get_level(button3) == 0) { 
                music_answer_selected = 1; 
                music_user_answer = 2; 
                check_music_answer(2);
                vTaskDelay(pdMS_TO_TICKS(50)); // debounce delay
            } else if (gpio_get_level(button4) == 0) { 
                music_answer_selected = 1; 
                music_user_answer = 3; 
                check_music_answer(3);
                vTaskDelay(pdMS_TO_TICKS(50)); // debounce delay
            } 
            vTaskDelay(pdMS_TO_TICKS(10)); // check every 10ms
        }
    }
}

void read_distance() { // read distance from the sensor 
    i2c_write_byte(VL53L0X_REG_SYSRANGE_START, 0x01);
    vTaskDelay(50 / portTICK_PERIOD_MS);

    //update  the the result into variable 'distance'  
    distance_err = i2c_read_word(VL53L0X_REG_RESULT_RANGE, &distance);

    if (distance_err != ESP_OK) { // issue with being able to read from the sensor properly 
        char msg[16]; 
        sprintf(msg, "Error: %s", esp_err_to_name(distance_err)); // read the error message
        // lcd_write_second(msg); 
    } 

    vTaskDelay(500 / portTICK_PERIOD_MS );
}

void distance_sensor_task(void *pvParameter) {
    while (1) {
        read_distance();
        if (musicMode && !music_answer_selected) { // don't read if not in music mode or if an answer has already been selected 
            if (distance > 20 && distance < 200) { // choice e 
                char msg[16] = "e";
                lcd_write_second(msg);  
                music_user_answer = 4;
                music_answer_selected = 1;
                if (4 == music_answer_correct[music_correct_count]) {
                    correct_note(); 
                } else {
                    wrong_note(music_user_answer); 
                }

                // check if we go to next round 
                if(music_correct_count == music_level) {
                    lcd_clear(); 
                    vTaskDelay(pdMS_TO_TICKS(600)); 
                    next_round(); 
                }
                vTaskDelay(pdMS_TO_TICKS(1000)); 
            } else if (distance > 200 && distance < 500) { // choice f 
                char msg[16] = "f";
                lcd_write_second(msg); 
                music_user_answer = 5; 
                music_answer_selected = 1; 
                if (5 == music_answer_correct[music_correct_count]) {
                    correct_note(); 
                } else {
                    wrong_note(music_user_answer); 
                }
                
                // check if we go to next round 
                if(music_correct_count == music_level) {
                    lcd_clear(); 
                    vTaskDelay(pdMS_TO_TICKS(600)); 
                    next_round(); 
                }
                vTaskDelay(pdMS_TO_TICKS(1000)); 
                
            }
        }
    }
}

void app_main(void) {
    // initialize everything 
    init_lcd(); 
    init_button(); 
    init_distance_sensor(); 
    servo_init();
    servo_set_angle(0);
    init_speaker();

    srand(time(NULL)); // seed random number generator
    
    math_game(); 

    xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL); // run button as a separate task
    xTaskCreate(distance_sensor_task, "distance_sensor_task", 2048, NULL, 5, NULL); // run distance sensor as a separate task 
}