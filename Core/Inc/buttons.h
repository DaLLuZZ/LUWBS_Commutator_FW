#ifndef __BUTTONS_H_
#define __BUTTONS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal_gpio.h"

#define GET_CURRENT_TIMESTAMP_MS()      ((1000 * osKernelGetTickCount()) / osKernelGetTickFreq())

#define BUTTON_DEBOUNCE_TIME_MS         (80)

#define BUTTON_READ_PRESSED             GPIO_PIN_RESET
#define BUTTON_READ_RELEASED            GPIO_PIN_SET

#define LCD_SLAVE_ADDRESS               (0x4E)
#define LCD_SYMBOLS_PER_LINE            (16)
#define LCD_LINE_LEN                    (LCD_SYMBOLS_PER_LINE + 1)      // + null-terminator

#define LCD_IP_ADDR_FORMAT_STRING       "%03hu.%03hu.%03hu.%03hu "      //                            "000.000.000.000 "
#define LCD_IP_ADDR_START_POS           (0)                             // index of the first symbol:  ^             ^
#define LCD_IP_ADDR_END_POS             (LCD_IP_ADDR_START_POS + 14)    //                 index of the last symbol: |

#define LCD_CURSOR_BLINK_PERIOD_MS      (500)
#define LCD_CURSOR_BLINK_DURATION_MS    (250)
#define LCD_CURSOR_BLINK_CHARACTER      ("_")                           // the symbol displayed at the current cursor position for LCD_CURSOR_BLINK_DURATION_MS milliseconds

typedef enum
{
    BUTTON_STATE_RELEASED = 0,
    BUTTON_STATE_PENDING,
    BUTTON_STATE_PRESSED,

    BUTTON_STATE_INVALID
} button_state_t;

typedef struct
{
    button_state_t state;
    uint32_t timestamp_ms;
    void (*pressed_cb)(void);
    void (*released_cb)(void);
    GPIO_PinState (*read_pin)(void);
} button_ctx_t;

typedef struct
{
    uint8_t pos      : 6;                                               // current position of cursor (used for CONFIG_* states)
    uint8_t blink    : 1;                                               // 1 => blink character is displayed now
    uint8_t reserved : 1;                                               // reserved for future use
} lcd_cursor_t;

typedef enum
{
    LCD_STATE_INVALID = 0,

    LCD_STATE_SHOW_GREATING,                                            // move greating out of the screen cycle

    LCD_STATE_SHOW_MIN,
    LCD_STATE_SHOW_IP_ADDRESS,
    LCD_STATE_SHOW_SUBNET_MASK,
    LCD_STATE_SHOW_GATEWAY,
    LCD_STATE_SHOW_IP_MODE,
    LCD_STATE_SHOW_MAX,

    LCD_STATE_CONFIG_MIN,
    LCD_STATE_CONFIG_IP_ADDRESS,
    LCD_STATE_CONFIG_SUBNET_MASK,
    LCD_STATE_CONFIG_GATEWAY,
    LCD_STATE_CONFIG_IP_MODE,
    LCD_STATE_CONFIG_MAX,

    LCD_STATE_UNKNOWN
} lcd_state_t;

typedef struct
{
    lcd_state_t lcd_state;                                              // finite state machine
    uint32_t lcd_timestamp_ms;                                          // used in some states for time tracking
    lcd_cursor_t lcd_cursor;
    uint8_t lcd_upd_flag;                                               // > 0 if should be updated, 0 if not
    char* lcd_line_main;
    char* lcd_line_secondary;
    void (*lcd_cmd)(char cmd);
    void (*lcd_data)(char data);
    button_ctx_t button_left;
    button_ctx_t button_mid;
    button_ctx_t button_right;
} lcd_ctx_t;

void ButtonTASK_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __BUTTONS_H_ */
