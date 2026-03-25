#include "FreeRTOS.h"
#include "cmsis_os.h"
#include "task.h"
#include "stdio.h"
#include "main.h"
#include "gpio.h"
#include "i2c.h"
#include "buttons.h"

static osThreadId_t buttonTaskHandle;
static uint32_t buttonTaskBuffer[2048];
static StaticTask_t buttonTaskControlBlock;
static const osThreadAttr_t buttonTask_attributes = {
    .name = "buttonTask",
    .cb_mem = &buttonTaskControlBlock,
    .cb_size = sizeof(buttonTaskControlBlock),
    .stack_mem = &buttonTaskBuffer[0],
    .stack_size = sizeof(buttonTaskBuffer),
    .priority = (osPriority_t)osPriorityNormal,
};

static void button_left_pressed_cb(void);
static void button_mid_pressed_cb(void);
static void button_right_pressed_cb(void);
static void button_left_released_cb(void);
static void button_mid_released_cb(void);
static void button_right_released_cb(void);
static GPIO_PinState button_left_read(void);
static GPIO_PinState button_mid_read(void);
static GPIO_PinState button_right_read(void);

static void lcd_send_cmd(char cmd);
static void lcd_send_data(char data);
static void lcd_put_cursor(lcd_ctx_t* lcd_ctx, int row, int col);
static void lcd_send_string(lcd_ctx_t* lcd_ctx, char *str);

// LCD FSM
static uint8_t lcd_buf_a[32];
static uint8_t lcd_buf_b[32];
static lcd_ctx_t lcd_ctx = {
    .lcd_state = LCD_STATE_INVALID,
    .lcd_timestamp_ms = 0,
    .lcd_cursor = {
        .pos = 0,
        .blink = 0,
        .reserved = 0,
    },
    .lcd_upd_flag = 0,
    .lcd_line_main = (char*)lcd_buf_a,
    .lcd_line_secondary = (char*)lcd_buf_b,
    .lcd_cmd = lcd_send_cmd,
    .lcd_data = lcd_send_data,
    .button_left = {
        .state = BUTTON_STATE_RELEASED,
        .timestamp_ms = 0,
        .pressed_cb = button_left_pressed_cb,
        .released_cb = button_left_released_cb,
        .read_pin = button_left_read,
    },
    .button_mid = {
        .state = BUTTON_STATE_RELEASED,
        .timestamp_ms = 0,
        .pressed_cb = button_mid_pressed_cb,
        .released_cb = button_mid_released_cb,
        .read_pin = button_mid_read,
    },
    .button_right = {
        .state = BUTTON_STATE_RELEASED,
        .timestamp_ms = 0,
        .pressed_cb = button_right_pressed_cb,
        .released_cb = button_right_released_cb,
        .read_pin = button_right_read,
    },
};

// redefine weak funciton from stm32f4xx_hal_gpio.c
// attention! this function is called from irq context
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    button_ctx_t* button_ctx;

    switch (GPIO_Pin)
    {
        case INPUT_LEFT_Pin:
        {
            button_ctx = &lcd_ctx.button_left;
            break;
        }
        case INPUT_MID_Pin:
        {
            button_ctx = &lcd_ctx.button_mid;
            break;
        }
        case INPUT_RIGHT_Pin:
        {
            button_ctx = &lcd_ctx.button_right;
            break;
        }
        default:
        {
            // ignore interrupts from unknown pins
            return;
        }
    }

    if (button_ctx->state == BUTTON_STATE_RELEASED)
    {
        button_ctx->timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
        button_ctx->state = BUTTON_STATE_PENDING;
    }
}

static void lcd_init(lcd_ctx_t* lcd_ctx)
{
    // 4 bit initialisation
    osDelay(50); // wait for 40+ ms
    lcd_ctx->lcd_cmd(0x30);
    osDelay(5); // wait for > 4.1+ ms
    lcd_ctx->lcd_cmd(0x30);
    osDelay(1); // wait for > 100+ us
    lcd_ctx->lcd_cmd(0x30);
    osDelay(10);
    lcd_ctx->lcd_cmd(0x20); // 4bit mode
    osDelay(10);

    // display initialisation
    lcd_ctx->lcd_cmd(0x28); // Function set --> DL=0 (4 bit mode), N = 1 (2 line display) F = 0 (5x8 characters)
    osDelay(1);
    lcd_ctx->lcd_cmd(0x08); // Display on/off control --> D=0,C=0, B=0  ---> display off
    osDelay(1);
    lcd_ctx->lcd_cmd(0x01); // clear display
    osDelay(2);
    lcd_ctx->lcd_cmd(0x06); // Entry mode set --> I/D = 1 (increment cursor) & S = 0 (no shift)
    osDelay(1);
    lcd_ctx->lcd_cmd(0x0C); // Display on/off control --> D = 1, C and B = 0. (Cursor and blink, last two bits)
}

static void button_process_state(button_ctx_t* button_ctx)
{
    switch (button_ctx->state)
    {
        case BUTTON_STATE_PRESSED:
        {
            uint32_t timedelta = GET_CURRENT_TIMESTAMP_MS() - button_ctx->timestamp_ms;
            GPIO_PinState pin_state = button_ctx->read_pin();

            if (pin_state == BUTTON_READ_PRESSED)
            {
                button_ctx->timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
            }
            else if (timedelta > BUTTON_DEBOUNCE_TIME_MS && pin_state == BUTTON_READ_RELEASED)
            {
                button_ctx->state = BUTTON_STATE_RELEASED;
                button_ctx->released_cb();
                button_ctx->timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
            }

            break;
        }
        case BUTTON_STATE_PENDING:
        {
            uint32_t timedelta = GET_CURRENT_TIMESTAMP_MS() - button_ctx->timestamp_ms;
            GPIO_PinState pin_state = button_ctx->read_pin();

            if (pin_state == BUTTON_READ_RELEASED)
            {
                button_ctx->state = BUTTON_STATE_RELEASED;
                button_ctx->timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
            }
            else if (timedelta > BUTTON_DEBOUNCE_TIME_MS && pin_state == BUTTON_READ_PRESSED)
            {
                button_ctx->state = BUTTON_STATE_PRESSED;
                button_ctx->pressed_cb();
                button_ctx->timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
            }

            break;
        }
        case BUTTON_STATE_RELEASED:
        {
            break;
        }
        default:
        {
            return; // ignore unknown button states
        }
    }
}

static void lcd_set_state(lcd_ctx_t* lcd_ctx, lcd_state_t new_state)
{
    if (lcd_ctx->lcd_state == new_state || new_state == LCD_STATE_INVALID || new_state >= LCD_STATE_UNKNOWN)
    {
        return; // nothing to do
    }

    lcd_ctx->lcd_state = new_state;

    switch (lcd_ctx->lcd_state)
    {
        case LCD_STATE_SHOW_GREATING:
        {
            snprintf(lcd_ctx->lcd_line_main,      LCD_LINE_LEN, "     Hello!     ");
            snprintf(lcd_ctx->lcd_line_secondary, LCD_LINE_LEN, "Press any button");
            lcd_ctx->lcd_upd_flag = 1;
            break;
        }
        case LCD_STATE_SHOW_IP_ADDRESS:
        case LCD_STATE_SHOW_SUBNET_MASK:
        case LCD_STATE_SHOW_GATEWAY:
        {
            // TODO: display real values
            snprintf(lcd_ctx->lcd_line_main,      LCD_LINE_LEN, LCD_IP_ADDR_FORMAT_STRING, 192, 168, 0, 1);
            snprintf(lcd_ctx->lcd_line_secondary, LCD_LINE_LEN, "[<]  [EDIT]  [>]");
            lcd_ctx->lcd_upd_flag = 1;
            break;
        }
        case LCD_STATE_SHOW_IP_MODE:
        {
            snprintf(lcd_ctx->lcd_line_main,      LCD_LINE_LEN, "IP mode:  Static");
            snprintf(lcd_ctx->lcd_line_secondary, LCD_LINE_LEN, "[<]  [EDIT]  [>]");
            lcd_ctx->lcd_upd_flag = 1;
            break;
        }
        case LCD_STATE_CONFIG_IP_ADDRESS:
        case LCD_STATE_CONFIG_SUBNET_MASK:
        case LCD_STATE_CONFIG_GATEWAY:
        {
            // TODO: display real values
            snprintf(lcd_ctx->lcd_line_main,      LCD_LINE_LEN, LCD_IP_ADDR_FORMAT_STRING, 192, 168, 0, 1);
            snprintf(lcd_ctx->lcd_line_secondary, LCD_LINE_LEN, "[+]  [SAVE]  [>]");
            lcd_ctx->lcd_cursor.pos = LCD_IP_ADDR_START_POS;
            lcd_ctx->lcd_cursor.blink = 0;
            lcd_ctx->lcd_timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
            lcd_ctx->lcd_upd_flag = 1;
            break;
        }
        case LCD_STATE_CONFIG_IP_MODE:
        {
            snprintf(lcd_ctx->lcd_line_main,      LCD_LINE_LEN, "IP mode:  Static");
            snprintf(lcd_ctx->lcd_line_secondary, LCD_LINE_LEN, "[<]  [SAVE]  [>]");
            lcd_ctx->lcd_upd_flag = 1;
            break;
        }
        default:
        {
            return; // ignore unknown lcd state codes
        }
    }
}

static void lcd_set_prev_show_screen_state(lcd_ctx_t* lcd_ctx)
{
    lcd_state_t state = lcd_ctx->lcd_state;

    if (state <= LCD_STATE_SHOW_MIN || state >= LCD_STATE_SHOW_MAX)
    {
        return; // nothing to do
    }

    state--;

    // cycle screens
    if (state <= LCD_STATE_SHOW_MIN)
    {
        state = LCD_STATE_SHOW_MAX - 1;
    }

    lcd_set_state(lcd_ctx, state);
}

static void lcd_set_next_show_screen_state(lcd_ctx_t* lcd_ctx)
{
    lcd_state_t state = lcd_ctx->lcd_state;

    if (state <= LCD_STATE_SHOW_MIN || state >= LCD_STATE_SHOW_MAX)
    {
        return; // nothing to do
    }

    state++;

    // cycle screens
    if (state <= LCD_STATE_SHOW_MAX)
    {
        state = LCD_STATE_SHOW_MIN + 1;
    }

    lcd_set_state(lcd_ctx, state);
}

static void lcd_ip_config_process_inc(lcd_ctx_t* lcd_ctx)
{
    // increment the digit at the cursor position
    // the first digit can be in range     [0;2]
    // the last two digits can be in range [0;9]
    lcd_ctx->lcd_line_main[lcd_ctx->lcd_cursor.pos]++;

    if ((lcd_ctx->lcd_cursor.pos == LCD_IP_ADDR_START_POS +  0) ||
        (lcd_ctx->lcd_cursor.pos == LCD_IP_ADDR_START_POS +  4) ||
        (lcd_ctx->lcd_cursor.pos == LCD_IP_ADDR_START_POS +  8) ||
        (lcd_ctx->lcd_cursor.pos == LCD_IP_ADDR_START_POS + 12))
    {
        if (lcd_ctx->lcd_line_main[lcd_ctx->lcd_cursor.pos] > '2')
        {
            lcd_ctx->lcd_line_main[lcd_ctx->lcd_cursor.pos] = '0'; // cycle
        }
    }
    else
    {
        if (lcd_ctx->lcd_line_main[lcd_ctx->lcd_cursor.pos] > '9')
        {
            lcd_ctx->lcd_line_main[lcd_ctx->lcd_cursor.pos] = '0'; // cycle
        }
    }
    lcd_ctx->lcd_upd_flag = 1;
}

static void lcd_ip_config_process_move(lcd_ctx_t* lcd_ctx)
{
    // move cursor position
    lcd_ctx->lcd_cursor.pos++;

    if ((lcd_ctx->lcd_cursor.pos == LCD_IP_ADDR_START_POS +  3) ||
        (lcd_ctx->lcd_cursor.pos == LCD_IP_ADDR_START_POS +  7) ||
        (lcd_ctx->lcd_cursor.pos == LCD_IP_ADDR_START_POS + 11))
    {
        // skip positions with dots: "000.000.000.000"
        lcd_ctx->lcd_cursor.pos++;
    }
    if (lcd_ctx->lcd_cursor.pos > LCD_IP_ADDR_END_POS)
    {
        // cycle the cursor at IP address line
        lcd_ctx->lcd_cursor.pos = LCD_IP_ADDR_START_POS;
    }
}

static void lcd_ip_config_process_exit(lcd_ctx_t* lcd_ctx, uint8_t* new_ip)
{
    uint16_t _new_ip[4];
    sscanf(lcd_ctx->lcd_line_main, LCD_IP_ADDR_FORMAT_STRING, &_new_ip[0], &_new_ip[1], &_new_ip[2], &_new_ip[3]);

    // note: newlib-nano supports only those conversion specifiers defined in the C89 standard
    // the "hh" modifier is not available in the C89 standard for "%hhu" ("hh" - convert input to char, store in char object)
    for (uint8_t i = 0; i < sizeof(new_ip); i++)
    {
        new_ip[i] = (uint8_t)(_new_ip[i] & 0xFF);
    }
}

static void lcd_ip_config_process_idle(lcd_ctx_t* lcd_ctx)
{
    // blinking character at the current cursor position
    uint32_t timedelta = GET_CURRENT_TIMESTAMP_MS() - lcd_ctx->lcd_timestamp_ms;

    if (!lcd_ctx->lcd_cursor.blink && timedelta > LCD_CURSOR_BLINK_PERIOD_MS)
    {
        lcd_put_cursor(lcd_ctx, 0, lcd_ctx->lcd_cursor.pos);
        lcd_send_string(lcd_ctx, LCD_CURSOR_BLINK_CHARACTER); // display blink character
        lcd_ctx->lcd_cursor.blink = !lcd_ctx->lcd_cursor.blink;
        lcd_ctx->lcd_timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
    }
    else if (lcd_ctx->lcd_cursor.blink && timedelta > LCD_CURSOR_BLINK_DURATION_MS)
    {
        lcd_ctx->lcd_upd_flag = 1; // display the actual symbol back
        lcd_ctx->lcd_cursor.blink = !lcd_ctx->lcd_cursor.blink;
        lcd_ctx->lcd_timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
    }
}

static void lcd_process_state(lcd_ctx_t* lcd_ctx)
{
    switch (lcd_ctx->lcd_state)
    {
        case LCD_STATE_INVALID:
        {
            // init lcd content
            lcd_set_state(lcd_ctx, LCD_STATE_SHOW_GREATING);

            break;
        }
        case LCD_STATE_SHOW_GREATING:
        {
            if ((lcd_ctx->button_left.state  == BUTTON_STATE_PRESSED) ||
                (lcd_ctx->button_mid.state   == BUTTON_STATE_PRESSED) ||
                (lcd_ctx->button_right.state == BUTTON_STATE_PRESSED))
            {
                lcd_set_state(lcd_ctx, LCD_STATE_SHOW_IP_ADDRESS);

                lcd_ctx->button_left.state  = BUTTON_STATE_RELEASED;
                lcd_ctx->button_mid.state   = BUTTON_STATE_RELEASED;
                lcd_ctx->button_right.state = BUTTON_STATE_RELEASED;
            }
            break;
        }
        case LCD_STATE_SHOW_IP_ADDRESS:
        case LCD_STATE_SHOW_SUBNET_MASK:
        case LCD_STATE_SHOW_GATEWAY:
        {
            if (lcd_ctx->button_left.state == BUTTON_STATE_PRESSED)
            {
                lcd_set_prev_show_screen_state(lcd_ctx);
                lcd_ctx->button_left.state = BUTTON_STATE_RELEASED;
            }
            else if (lcd_ctx->button_mid.state == BUTTON_STATE_PRESSED)
            {
                lcd_set_state(lcd_ctx, lcd_ctx->lcd_state + (LCD_STATE_CONFIG_IP_ADDRESS - LCD_STATE_SHOW_IP_ADDRESS));
                lcd_ctx->button_mid.state = BUTTON_STATE_RELEASED;
            }
            else if (lcd_ctx->button_right.state == BUTTON_STATE_PRESSED)
            {
                lcd_set_next_show_screen_state(lcd_ctx);
                lcd_ctx->button_right.state = BUTTON_STATE_RELEASED;
            }
            break;
        }
        case LCD_STATE_SHOW_IP_MODE:
        {
            if (lcd_ctx->button_left.state == BUTTON_STATE_PRESSED)
            {
                lcd_set_prev_show_screen_state(lcd_ctx);
                lcd_ctx->button_left.state = BUTTON_STATE_RELEASED;
            }
            else if (lcd_ctx->button_mid.state == BUTTON_STATE_PRESSED)
            {
                lcd_set_state(lcd_ctx, LCD_STATE_CONFIG_IP_MODE);
                lcd_ctx->button_mid.state = BUTTON_STATE_RELEASED;
            }
            else if (lcd_ctx->button_right.state == BUTTON_STATE_PRESSED)
            {
                lcd_set_next_show_screen_state(lcd_ctx);
                lcd_ctx->button_right.state = BUTTON_STATE_RELEASED;
            }
            break;
        }
        case LCD_STATE_CONFIG_IP_MODE:
        {
            if (lcd_ctx->button_left.state == BUTTON_STATE_PRESSED)
            {
                // TODO: handle mode switching
                lcd_ctx->button_left.state = BUTTON_STATE_RELEASED;
            }
            else if (lcd_ctx->button_mid.state == BUTTON_STATE_PRESSED)
            {
                // TODO: save the result
                lcd_set_state(lcd_ctx, LCD_STATE_SHOW_IP_MODE);
                lcd_ctx->button_mid.state = BUTTON_STATE_RELEASED;
            }
            else if (lcd_ctx->button_right.state == BUTTON_STATE_PRESSED)
            {
                // TODO: handle mode switching
                lcd_ctx->button_right.state = BUTTON_STATE_RELEASED;
            }
            break;
        }
        case LCD_STATE_CONFIG_IP_ADDRESS:
        case LCD_STATE_CONFIG_SUBNET_MASK:
        case LCD_STATE_CONFIG_GATEWAY:
        {
            if (lcd_ctx->button_left.state == BUTTON_STATE_PRESSED)
            {
                lcd_ip_config_process_inc(lcd_ctx);
                lcd_ctx->button_left.state = BUTTON_STATE_RELEASED;
                lcd_ctx->lcd_timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
            }
            else if (lcd_ctx->button_mid.state == BUTTON_STATE_PRESSED)
            {
                uint8_t new_ip[4];
                lcd_ip_config_process_exit(lcd_ctx, new_ip); // get new value from the main lcd line
                // TODO: save the result to NVS and apply it

                lcd_set_state(lcd_ctx, lcd_ctx->lcd_state - (LCD_STATE_CONFIG_IP_ADDRESS - LCD_STATE_SHOW_IP_ADDRESS));
                lcd_ctx->button_mid.state = BUTTON_STATE_RELEASED;
            }
            else if (lcd_ctx->button_right.state == BUTTON_STATE_PRESSED)
            {
                lcd_ip_config_process_move(lcd_ctx);
                lcd_ctx->button_right.state = BUTTON_STATE_RELEASED;
                lcd_ctx->lcd_timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
            }
            else
            {
                lcd_ip_config_process_idle(lcd_ctx);
            }
            break;
        }
        default:
        {
            return; // ignore unknown lcd state codes
        }
    }

    if (lcd_ctx->lcd_upd_flag)
    {
        lcd_put_cursor(lcd_ctx, 0, 0);
        lcd_send_string(lcd_ctx, lcd_ctx->lcd_line_main);
        lcd_put_cursor(lcd_ctx, 1, 0);
        lcd_send_string(lcd_ctx, lcd_ctx->lcd_line_secondary);

        lcd_ctx->lcd_upd_flag = 0;
    }
}

static void buttonTaskHandler(void *argument)
{
    lcd_init(&lcd_ctx); // init lcd in 4-bit mode

    /* Infinite loop */
    while (1)
    {
        button_process_state(&lcd_ctx.button_left);
        button_process_state(&lcd_ctx.button_mid);
        button_process_state(&lcd_ctx.button_right);
        lcd_process_state(&lcd_ctx);

        osDelay(10);
    }
}

static GPIO_PinState button_left_read(void)
{
    return HAL_GPIO_ReadPin(INPUT_LEFT_GPIO_Port, INPUT_LEFT_Pin);
}

static GPIO_PinState button_mid_read(void)
{
    return HAL_GPIO_ReadPin(INPUT_MID_GPIO_Port, INPUT_MID_Pin);
}

static GPIO_PinState button_right_read(void)
{
    return HAL_GPIO_ReadPin(INPUT_RIGHT_GPIO_Port, INPUT_RIGHT_Pin);
}

static void button_left_pressed_cb(void)
{
    DBG_PRINTF("");
}

static void button_mid_pressed_cb(void)
{
    DBG_PRINTF("");
}

static void button_right_pressed_cb(void)
{
    DBG_PRINTF("");
}

static void button_left_released_cb(void)
{
    DBG_PRINTF("");
}

static void button_mid_released_cb(void)
{
    DBG_PRINTF("");
}

static void button_right_released_cb(void)
{
    DBG_PRINTF("");
}

static void lcd_send_cmd(char cmd)
{
    char data_up, data_low;
    data_up = (cmd & 0xF0);           // extract upper 4 bits
    data_low = ((cmd << 4) & 0xF0);   // extract lower 4 bits

    uint8_t data_b[4];

    // send upper 4 bits with enable pulse
    data_b[0] = data_up | 0x0C;         // EN = 1, RS = 0  -> bxxxx1100
    data_b[1] = data_up | 0x08;         // EN = 0, RS = 0  -> bxxxx1000

    // send lower 4 bits with enable pulse
    data_b[2] = data_low | 0x0C;        // EN = 1, RS = 0  -> bxxxx1100
    data_b[3] = data_low | 0x08;        // EN = 0, RS = 0  -> bxxxx1000

    HAL_I2C_Master_Transmit(&hi2c1, LCD_SLAVE_ADDRESS, (uint8_t*)data_b, sizeof(data_b), 100);
}

static void lcd_send_data(char data)
{
    char data_up, data_low;
    uint8_t data_b[4];
    data_up = (data & 0xF0);
    data_low = ((data << 4) & 0xF0);
    data_b[0] = data_up  | 0x0D;  // EN = 1, RS = 1 -> bxxxx1101
    data_b[1] = data_up  | 0x09;  // EN = 0, RS = 1 -> bxxxx1001
    data_b[2] = data_low | 0x0D;  // EN = 1, RS = 1 -> bxxxx1101
    data_b[3] = data_low | 0x09;  // EN = 0, RS = 1 -> bxxxx1001

    HAL_I2C_Master_Transmit(&hi2c1, LCD_SLAVE_ADDRESS, (uint8_t*)data_b, sizeof(data_b), 100);
}

static void lcd_put_cursor(lcd_ctx_t* lcd_ctx, int row, int col)
{
    switch (row)
    {
        case 0:
        {
            col |= 0x80;
            break;
        }
        case 1:
        {
            col |= 0xC0;
            break;
        }
    }
    lcd_ctx->lcd_cmd(col);
}

static void lcd_send_string(lcd_ctx_t* lcd_ctx, char *str)
{
    while (*str) 
    {
        lcd_ctx->lcd_data(*str++);
    }
}

void ButtonTASK_Init(void)
{
    buttonTaskHandle = osThreadNew(buttonTaskHandler, NULL, &buttonTask_attributes);
}
