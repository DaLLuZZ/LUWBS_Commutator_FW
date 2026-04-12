/*-
 * BSD 2-Clause License
 *
 * Copyright (c) 2012-2018, Jan Breuer
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file   scpi-def.c
 * @date   Thu Nov 15 10:58:45 UTC 2012
 *
 * @brief  SCPI parser test
 *
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "scpi/scpi.h"
#include "scpi-def.h"

// 1 <=> ROUTe:CLOSe, ROUTe:OPEN and ROUTe:OPEN:ALL will return error code 0
// in case there was no any error during command execution
// ATTENTION: enabling this option contradicts SCPI-99 standart!
#ifndef SCPI_ENABLE_ROUTE_NO_ERROR_OUTPUT
#define SCPI_ENABLE_ROUTE_NO_ERROR_OUTPUT 0
#endif

static scpi_result_t TEST_Bool(scpi_t * context) {
    scpi_bool_t param1;
    fprintf(stderr, "TEST:BOOL\r\n"); /* debug command name */

    /* read first parameter if present */
    if (!SCPI_ParamBool(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }

    fprintf(stderr, "\tP1=%d\r\n", param1);

    return SCPI_RES_OK;
}

scpi_choice_def_t trigger_source[] = {
    {"BUS", 5},
    {"IMMediate", 6},
    {"EXTernal", 7},
    SCPI_CHOICE_LIST_END /* termination of option list */
};

static scpi_result_t TEST_ChoiceQ(scpi_t * context) {

    int32_t param;
    const char * name;

    if (!SCPI_ParamChoice(context, trigger_source, &param, TRUE)) {
        return SCPI_RES_ERR;
    }

    SCPI_ChoiceToName(trigger_source, param, &name);
    fprintf(stderr, "\tP1=%s (%ld)\r\n", name, (long int) param);

    SCPI_ResultInt32(context, param);

    return SCPI_RES_OK;
}

static scpi_result_t TEST_Numbers(scpi_t * context) {
    int32_t numbers[2];

    SCPI_CommandNumbers(context, numbers, 2, 1);

    fprintf(stderr, "TEST numbers %d %d\r\n", numbers[0], numbers[1]);

    return SCPI_RES_OK;
}

static scpi_result_t TEST_Text(scpi_t * context) {
    char buffer[100];
    size_t copy_len;

    if (!SCPI_ParamCopyText(context, buffer, sizeof (buffer), &copy_len, FALSE)) {
        buffer[0] = '\0';
    }

    fprintf(stderr, "TEXT: ***%s***\r\n", buffer);

    return SCPI_RES_OK;
}

static scpi_result_t TEST_ArbQ(scpi_t * context) {
    const char * data;
    size_t len;

    if (SCPI_ParamArbitraryBlock(context, &data, &len, FALSE)) {
        SCPI_ResultArbitraryBlock(context, data, len);
    }

    return SCPI_RES_OK;
}

#define MAXROW                  4    /* maximum number of rows */
#define MAXCOL                  4    /* maximum number of columns */
#define MAXDIM                  2    /* maximum number of dimensions */

#define ROW_INPUT_VOL_POSITIVE  0
#define ROW_INPUT_VOL_NEGATIVE  1
#define ROW_INPUT_AMP_POSITIVE  2
#define ROW_INPUT_AMP_NEGATIVE  3

#define COL_OUTPUT_CHANNEL_A    0
#define COL_OUTPUT_CHANNEL_B    1
#define COL_OUTPUT_CHANNEL_C    2
#define COL_OUTPUT_CHANNEL_D    3

#define GPIO_CLOSED_STATE       GPIO_PIN_SET
#define GPIO_OPENED_STATE       GPIO_PIN_RESET

typedef enum
{
    SCPI_ROUTE_OPENED = 0,
    SCPI_ROUTE_CLOSED,
} scpi_route_state_t;

typedef struct
{
    int32_t row;
    int32_t col;
} scpi_channel_value_t;

// 1 - closed, 0 - opened
// rows are inputs, columns are outputs
scpi_route_state_t scpi_route_status[MAXROW][MAXCOL] = {SCPI_ROUTE_OPENED};

// attention: this function does not change values in scpi_route_status
// but it physically opens all routes
// so after the call the values in scpi_route_status will not correspond the reality
static scpi_result_t SCPI_OpenAllRoutes()
{
    HAL_GPIO_WritePin(R_A_VP_GPIO_Port, R_A_VP_Pin, GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_A_VN_GPIO_Port, R_A_VN_Pin, GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_A_AP_GPIO_Port, R_A_AP_Pin, GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_A_AN_GPIO_Port, R_A_AN_Pin, GPIO_OPENED_STATE);

    HAL_GPIO_WritePin(R_B_VP_GPIO_Port, R_B_VP_Pin, GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_B_VN_GPIO_Port, R_B_VN_Pin, GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_B_AP_GPIO_Port, R_B_AP_Pin, GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_B_AN_GPIO_Port, R_B_AN_Pin, GPIO_OPENED_STATE);

    HAL_GPIO_WritePin(R_C_VP_GPIO_Port, R_C_VP_Pin, GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_C_VN_GPIO_Port, R_C_VN_Pin, GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_C_AP_GPIO_Port, R_C_AP_Pin, GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_C_AN_GPIO_Port, R_C_AN_Pin, GPIO_OPENED_STATE);

    HAL_GPIO_WritePin(R_D_VP_GPIO_Port, R_D_VP_Pin, GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_D_VN_GPIO_Port, R_D_VN_Pin, GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_D_AP_GPIO_Port, R_D_AP_Pin, GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_D_AN_GPIO_Port, R_D_AN_Pin, GPIO_OPENED_STATE);

    DBG_PRINTF("All routes are opened");

    return SCPI_RES_OK;
}

static scpi_result_t SCPI_ApplyAllRoutes()
{
    HAL_GPIO_WritePin(R_A_VP_GPIO_Port, R_A_VP_Pin, scpi_route_status[ROW_INPUT_VOL_POSITIVE][COL_OUTPUT_CHANNEL_A] == SCPI_ROUTE_CLOSED ? GPIO_CLOSED_STATE : GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_A_VN_GPIO_Port, R_A_VN_Pin, scpi_route_status[ROW_INPUT_VOL_NEGATIVE][COL_OUTPUT_CHANNEL_A] == SCPI_ROUTE_CLOSED ? GPIO_CLOSED_STATE : GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_A_AP_GPIO_Port, R_A_AP_Pin, scpi_route_status[ROW_INPUT_AMP_POSITIVE][COL_OUTPUT_CHANNEL_A] == SCPI_ROUTE_CLOSED ? GPIO_CLOSED_STATE : GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_A_AN_GPIO_Port, R_A_AN_Pin, scpi_route_status[ROW_INPUT_AMP_NEGATIVE][COL_OUTPUT_CHANNEL_A] == SCPI_ROUTE_CLOSED ? GPIO_CLOSED_STATE : GPIO_OPENED_STATE);

    HAL_GPIO_WritePin(R_B_VP_GPIO_Port, R_B_VP_Pin, scpi_route_status[ROW_INPUT_VOL_POSITIVE][COL_OUTPUT_CHANNEL_B] == SCPI_ROUTE_CLOSED ? GPIO_CLOSED_STATE : GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_B_VN_GPIO_Port, R_B_VN_Pin, scpi_route_status[ROW_INPUT_VOL_NEGATIVE][COL_OUTPUT_CHANNEL_B] == SCPI_ROUTE_CLOSED ? GPIO_CLOSED_STATE : GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_B_AP_GPIO_Port, R_B_AP_Pin, scpi_route_status[ROW_INPUT_AMP_POSITIVE][COL_OUTPUT_CHANNEL_B] == SCPI_ROUTE_CLOSED ? GPIO_CLOSED_STATE : GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_B_AN_GPIO_Port, R_B_AN_Pin, scpi_route_status[ROW_INPUT_AMP_NEGATIVE][COL_OUTPUT_CHANNEL_B] == SCPI_ROUTE_CLOSED ? GPIO_CLOSED_STATE : GPIO_OPENED_STATE);

    HAL_GPIO_WritePin(R_C_VP_GPIO_Port, R_C_VP_Pin, scpi_route_status[ROW_INPUT_VOL_POSITIVE][COL_OUTPUT_CHANNEL_C] == SCPI_ROUTE_CLOSED ? GPIO_CLOSED_STATE : GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_C_VN_GPIO_Port, R_C_VN_Pin, scpi_route_status[ROW_INPUT_VOL_NEGATIVE][COL_OUTPUT_CHANNEL_C] == SCPI_ROUTE_CLOSED ? GPIO_CLOSED_STATE : GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_C_AP_GPIO_Port, R_C_AP_Pin, scpi_route_status[ROW_INPUT_AMP_POSITIVE][COL_OUTPUT_CHANNEL_C] == SCPI_ROUTE_CLOSED ? GPIO_CLOSED_STATE : GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_C_AN_GPIO_Port, R_C_AN_Pin, scpi_route_status[ROW_INPUT_AMP_NEGATIVE][COL_OUTPUT_CHANNEL_C] == SCPI_ROUTE_CLOSED ? GPIO_CLOSED_STATE : GPIO_OPENED_STATE);

    HAL_GPIO_WritePin(R_D_VP_GPIO_Port, R_D_VP_Pin, scpi_route_status[ROW_INPUT_VOL_POSITIVE][COL_OUTPUT_CHANNEL_D] == SCPI_ROUTE_CLOSED ? GPIO_CLOSED_STATE : GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_D_VN_GPIO_Port, R_D_VN_Pin, scpi_route_status[ROW_INPUT_VOL_NEGATIVE][COL_OUTPUT_CHANNEL_D] == SCPI_ROUTE_CLOSED ? GPIO_CLOSED_STATE : GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_D_AP_GPIO_Port, R_D_AP_Pin, scpi_route_status[ROW_INPUT_AMP_POSITIVE][COL_OUTPUT_CHANNEL_D] == SCPI_ROUTE_CLOSED ? GPIO_CLOSED_STATE : GPIO_OPENED_STATE);
    HAL_GPIO_WritePin(R_D_AN_GPIO_Port, R_D_AN_Pin, scpi_route_status[ROW_INPUT_AMP_NEGATIVE][COL_OUTPUT_CHANNEL_D] == SCPI_ROUTE_CLOSED ? GPIO_CLOSED_STATE : GPIO_OPENED_STATE);

    return SCPI_RES_OK;
}

/**
 * @brief parses channel list
 * @param array - array to store the result
 * @param size - pointer to size_t variable to store the array size
 */
static scpi_result_t SCPI_ParseChanlst(scpi_t* context, scpi_channel_value_t* array, size_t* size)
{
    scpi_parameter_t channel_list_param;
    size_t chanlst_idx; /* index for channel list */
    size_t n, m = 1; /* counters for row (n) and columns (m) */
    size_t arr_idx = 0; /* index for array */

    /* get channel list */
    if (SCPI_Parameter(context, &channel_list_param, TRUE))
    {
        scpi_result_t res;
        scpi_bool_t is_range;
        int32_t values_from[MAXDIM];
        int32_t values_to[MAXDIM];
        size_t dimensions;

        bool for_stop_row = FALSE; /* true if iteration for rows has to stop */
        bool for_stop_col = FALSE; /* true if iteration for columns has to stop */
        int32_t dir_row = 1; /* direction of counter for rows, +/-1 */
        int32_t dir_col = 1; /* direction of counter for columns, +/-1 */

        /* the next statement is valid usage and it gets only real number of dimensions for the first item (index 0) */
        if (!SCPI_ExprChannelListEntry(context, &channel_list_param, 0, &is_range, NULL, NULL, 0, &dimensions))
        {
            chanlst_idx = 0; /* call first index */
            arr_idx = 0; /* set arr_idx to 0 */
            do
            { /* if valid, iterate over channel_list_param index while res == valid (do-while cause we have to do it once) */
                res = SCPI_ExprChannelListEntry(context, &channel_list_param, chanlst_idx, &is_range, values_from, values_to, 4, &dimensions);
                if (is_range == FALSE)
                { /* still can have multiple dimensions */
                    if (arr_idx >= MAXROW * MAXCOL)
                    {
                        return SCPI_RES_ERR;
                    }
                    if (dimensions == 1)
                    {
                        /* here we have our values
                         * row == values_from[0]
                         * col == 0 (fixed number)
                         * call a function or something */
                        array[arr_idx].row = values_from[0];
                        array[arr_idx].col = 0;
                    }
                    else if (dimensions == 2)
                    {
                        /* here we have our values
                         * row == values_fom[0]
                         * col == values_from[1]
                         * call a function or something */
                        array[arr_idx].row = values_from[0];
                        array[arr_idx].col = values_from[1];
                    }
                    else
                    {
                        return SCPI_RES_ERR;
                    }
                    arr_idx++; /* inkrement array where we want to save our values to, not neccessary otherwise */
                }
                else if (is_range == TRUE)
                {
                    if (values_from[0] > values_to[0])
                    {
                        dir_row = -1; /* we have to decrement from values_from */
                    }
                    else
                    { /* if (values_from[0] < values_to[0]) */
                        dir_row = +1; /* default, we increment from values_from */
                    }

                    /* iterating over rows, do it once -> set for_stop_row = false
                     * needed if there is channel list index isn't at end yet */
                    for_stop_row = FALSE;
                    for (n = values_from[0]; for_stop_row == FALSE; n += dir_row)
                    {
                        /* usual case for ranges, 2 dimensions */
                        if (dimensions == 2)
                        {
                            if (values_from[1] > values_to[1])
                            {
                                dir_col = -1;
                            }
                            else if (values_from[1] < values_to[1])
                            {
                                dir_col = +1;
                            }
                            /* iterating over columns, do it at least once -> set for_stop_col = false
                             * needed if there is channel list index isn't at end yet */
                            for_stop_col = FALSE;
                            for (m = values_from[1]; for_stop_col == FALSE; m += dir_col)
                            {
                                /* here we have our values
                                 * row == n
                                 * col == m
                                 * call a function or something */
                                if (arr_idx >= MAXROW * MAXCOL)
                                {
                                    return SCPI_RES_ERR;
                                }
                                array[arr_idx].row = n;
                                array[arr_idx].col = m;
                                if (m == (size_t)values_to[1])
                                {
                                    /* endpoint reached, stop column for-loop */
                                    for_stop_col = TRUE;
                                }
                                arr_idx++;
                            }
                            /* special case for range, example: (@2!1) */
                        }
                        else if (dimensions == 1)
                        {
                            /* here we have values
                             * row == n
                             * col == 0 (fixed number)
                             * call function or sth. */
                            if (arr_idx >= MAXROW * MAXCOL)
                            {
                                return SCPI_RES_ERR;
                            }
                            array[arr_idx].row = n;
                            array[arr_idx].col = 0;
                            arr_idx++;
                        }
                        if (n == (size_t)values_to[0])
                        {
                            /* endpoint reached, stop row for-loop */
                            for_stop_row = TRUE;
                        }
                    }
                }
                else
                {
                    return SCPI_RES_ERR;
                }
                /* increase index */
                chanlst_idx++;
            }
            while (SCPI_EXPR_OK == SCPI_ExprChannelListEntry(context, &channel_list_param, chanlst_idx, &is_range, values_from, values_to, 4, &dimensions));
            /* while checks, whether incremented index is valid */
        }
        /* do something at the end if needed */
        /* array[arr_idx].row = 0; */
        /* array[arr_idx].col = 0; */
    }
    {
        fprintf(stderr, "Chanlst: ");
        for (size_t i = 0; i < arr_idx; i++)
        {
            fprintf(stderr, "%d!%d, ", array[i].row, array[i].col);
        }
        fprintf(stderr, "\r\n");
    }
    *size = arr_idx;
    return SCPI_RES_OK;
}

/**
 * @brief
 * parses lists
 * channel numbers > 0.
 * no checks yet.
 * valid: (@1), (@3!1:1!3), ...
 * (@1!1:3!2) would be 1!1, 1!2, 2!1, 2!2, 3!1, 3!2.
 * (@3!1:1!3) would be 3!1, 3!2, 3!3, 2!1, 2!2, 2!3, ... 1!3.
 *
 * @param channel_list channel list, compare to SCPI99 Vol 1 Ch. 8.3.2
 */
static scpi_result_t TEST_Chanlst(scpi_t* context)
{
    scpi_channel_value_t array[MAXROW * MAXCOL]; /* array which holds values in order (2D) */
    size_t array_size; // number of elements in the array after parsing
    scpi_result_t res = SCPI_ParseChanlst(context, array, &array_size);

    if (res != SCPI_RES_OK)
    {
        DBG_PRINTF("Error during channel list parsing");
    }
    else
    {
        DBG_PRINTF("Channel list was parsed successfully, size: %d", array_size);
    }

    return res;
}

/**
 * @brief ROUTe subsystem: Closes channels
 * The CLOSe command allows specific individual channels to be closed.
 * If all the specified channels cannot be closed, an execution error is reported.
 * If a command tries to close two inputs on the same output, the instrument should report an execution error.
 */
static scpi_result_t SCPI_RouteClose(scpi_t* context)
{
    scpi_channel_value_t array[MAXROW * MAXCOL]; /* array which holds values in order (2D) */
    size_t array_size; // number of elements in the array after parsing
    scpi_result_t res = SCPI_ParseChanlst(context, array, &array_size);

    if (res != SCPI_RES_OK)
    {
        DBG_PRINTF("Error during channel list parsing");
        scpi_error_t err;
        err.error_code = SCPI_ERROR_EXPRESSION_PARSING_ERROR; // or SCPI_ERROR_EXECUTION_ERROR ?
#if USE_DEVICE_DEPENDENT_ERROR_INFORMATION
        err.device_dependent_info = "Error during channel list parsing";
#endif
        SCPI_ResultError(context, &err);
        // Hack: we need to write new line manually cause the command does not contain "?" at the end
        context->interface->write(context, SCPI_LINE_ENDING, strlen(SCPI_LINE_ENDING));
        return res;
    }

    // check that all specified routes can be closed
    scpi_route_state_t loc_scpi_route_status[MAXROW][MAXCOL];
    memcpy(loc_scpi_route_status, scpi_route_status, sizeof(scpi_route_status));
    for (size_t i = 0; i < array_size; i++)
    {
        // apply changes to the local array for simplifing check procedure
        loc_scpi_route_status[array[i].row][array[i].col] = SCPI_ROUTE_CLOSED;
    }
    // check new configuration
    for (size_t i = 0; i < MAXCOL; i++)
    {
        // never let two or more inputs (rows) to be closed on the same output (column)
        uint8_t closed = 0;
        for (size_t j = 0; j < MAXROW; j++)
        {
            if (loc_scpi_route_status[j][i] == SCPI_ROUTE_CLOSED)
            {
                closed++;
            }
        }
        if (closed > 1)
        {
            // error in configuration
            DBG_PRINTF("Error in configuration for output with index %d", i);
            scpi_error_t err;
            err.error_code = SCPI_ERROR_EXECUTION_ERROR;
#if USE_DEVICE_DEPENDENT_ERROR_INFORMATION
            err.device_dependent_info = "Error in configuration (two or more inputs are about to be closed on the same output)";
#endif
            SCPI_ResultError(context, &err);
            // Hack: we need to write new line manually cause the command does not contain "?" at the end
            context->interface->write(context, SCPI_LINE_ENDING, strlen(SCPI_LINE_ENDING));
            return SCPI_RES_ERR;
        }
    }
    for (size_t i = 0; i < MAXROW; i++)
    {
        // never let two or more outputs (columns) to be closed on the same input (row)
        uint8_t closed = 0;
        for (size_t j = 0; j < MAXCOL; j++)
        {
            if (loc_scpi_route_status[i][j] == SCPI_ROUTE_CLOSED)
            {
                closed++;
            }
        }
        if (closed > 1)
        {
            // error in configuration
            DBG_PRINTF("Error in configuration for input with index %d", i);
            scpi_error_t err;
            err.error_code = SCPI_ERROR_EXECUTION_ERROR;
#if USE_DEVICE_DEPENDENT_ERROR_INFORMATION
            err.device_dependent_info = "Error in configuration (two or more outputs are about to be closed on the same input)";
#endif
            SCPI_ResultError(context, &err);
            // Hack: we need to write new line manually cause the command does not contain "?" at the end
            context->interface->write(context, SCPI_LINE_ENDING, strlen(SCPI_LINE_ENDING));
            return SCPI_RES_ERR;
        }
    }

    // checks were passed => apply new configuration
    memcpy(scpi_route_status, loc_scpi_route_status, sizeof(scpi_route_status));
    osDelay(2); // sleep 2 ms (2 ms max release time according to EDR201A0500 datasheet)
    SCPI_ApplyAllRoutes(); // apply new changes
    osDelay(2); // sleep 2 ms (2 ms max release time according to EDR201A0500 datasheet)

#if SCPI_ENABLE_ROUTE_NO_ERROR_OUTPUT
    scpi_error_t err;
    err.error_code = SCPI_ERROR_NO_ERROR;
#if USE_DEVICE_DEPENDENT_ERROR_INFORMATION
    err.device_dependent_info = "Successfully Closed";
#endif // USE_DEVICE_DEPENDENT_ERROR_INFORMATION
    SCPI_ResultError(context, &err);
    // Hack: we need to write new line manually cause the command does not contain "?" at the end
    context->interface->write(context, SCPI_LINE_ENDING, strlen(SCPI_LINE_ENDING));
#endif // SCPI_ENABLE_ROUTE_NO_ERROR_OUTPUT

    return res;
}

/**
 * @brief ROUTe subsystem: Opens channels
 * The OPEN command allows specific channels to be opened.
 */
static scpi_result_t SCPI_RouteOpen(scpi_t* context)
{
    scpi_channel_value_t array[MAXROW * MAXCOL]; /* array which holds values in order (2D) */
    size_t array_size; // number of elements in the array after parsing
    scpi_result_t res = SCPI_ParseChanlst(context, array, &array_size);

    if (res != SCPI_RES_OK)
    {
        DBG_PRINTF("Error during channel list parsing");
        scpi_error_t err;
        err.error_code = SCPI_ERROR_EXPRESSION_PARSING_ERROR; // or SCPI_ERROR_EXECUTION_ERROR ?
#if USE_DEVICE_DEPENDENT_ERROR_INFORMATION
        err.device_dependent_info = "Error during channel list parsing";
#endif
        SCPI_ResultError(context, &err);
        // Hack: we need to write new line manually cause the command does not contain "?" at the end
        context->interface->write(context, SCPI_LINE_ENDING, strlen(SCPI_LINE_ENDING));
        return res;
    }

    // open routes without any checks (opening is safe)
    for (size_t i = 0; i < array_size; i++)
    {
        scpi_route_status[array[i].row][array[i].col] = SCPI_ROUTE_OPENED;
        DBG_PRINTF("Opened route %d!%d", array[i].row, array[i].col);
    }
    SCPI_ApplyAllRoutes();
    osDelay(5); // sleep 5 ms (2 ms max release time according to EDR201A0500 datasheet)

#if SCPI_ENABLE_ROUTE_NO_ERROR_OUTPUT
    scpi_error_t err;
    err.error_code = SCPI_ERROR_NO_ERROR;
#if USE_DEVICE_DEPENDENT_ERROR_INFORMATION
    err.device_dependent_info = "Successfully Opened";
#endif // USE_DEVICE_DEPENDENT_ERROR_INFORMATION
    SCPI_ResultError(context, &err);
    // Hack: we need to write new line manually cause the command does not contain "?" at the end
    context->interface->write(context, SCPI_LINE_ENDING, strlen(SCPI_LINE_ENDING));
#endif // SCPI_ENABLE_ROUTE_NO_ERROR_OUTPUT

    return res;
}

/*
From SCPI-99:
    1. The ROUTe:CLOSe? query allows the condition of individual switches to be queried. The instrument
       returns a 1 or 0 for each channel in the list, in the same order that the list is specified. A
       response of 1 means the channel is closed and a 0 means the channel is open.
    2. The ROUTe:CLOSe:STATe? query, which has no parameters, returns an IEEE 488.2
       definite length block which contains a <channel_list> of all the closed switches in the entire
       instrument.
*/
static scpi_result_t SCPI_RouteCloseQ(scpi_t* context)
{
    scpi_channel_value_t array[MAXROW * MAXCOL]; /* array which holds values in order (2D) */
    size_t array_size; // number of elements in the array after parsing
    scpi_result_t res = SCPI_ParseChanlst(context, array, &array_size);

    if (res != SCPI_RES_OK)
    {
        DBG_PRINTF("Error during channel list parsing, array_size: %d", array_size);
        scpi_error_t err;
        err.error_code = SCPI_ERROR_EXPRESSION_PARSING_ERROR; // or SCPI_ERROR_EXECUTION_ERROR ?
#if USE_DEVICE_DEPENDENT_ERROR_INFORMATION
        err.device_dependent_info = "Error during channel list parsing";
#endif
        SCPI_ResultError(context, &err);
        return res;
    }

    // fill return-array with current states
    uint8_t ret_array[MAXROW * MAXCOL];
    for (size_t i = 0; i < array_size; i++)
    {
        if (scpi_route_status[array[i].row][array[i].col] == SCPI_ROUTE_CLOSED)
        {
            ret_array[i] = 1;
        }
        else
        {
            ret_array[i] = 0;
        }
    }

    // return result array
    SCPI_ResultArrayUInt8(context, ret_array, array_size, SCPI_FORMAT_ASCII);

    return res;
}

/*
From SCPI-99:
    The query OPEN? allows the condition of switches to be queried. The instrument returns a 1
    or 0 for each channel in the list, in the same order that the list is specified. A response of 0
    means the channel is closed and a 1 means the channel is open
*/
static scpi_result_t SCPI_RouteOpenQ(scpi_t* context)
{
    scpi_channel_value_t array[MAXROW * MAXCOL]; /* array which holds values in order (2D) */
    size_t array_size; // number of elements in the array after parsing
    scpi_result_t res = SCPI_ParseChanlst(context, array, &array_size);

    if (res != SCPI_RES_OK)
    {
        DBG_PRINTF("Error during channel list parsing, array_size: %d", array_size);
        scpi_error_t err;
        err.error_code = SCPI_ERROR_EXPRESSION_PARSING_ERROR; // or SCPI_ERROR_EXECUTION_ERROR ?
#if USE_DEVICE_DEPENDENT_ERROR_INFORMATION
        err.device_dependent_info = "Error during channel list parsing";
#endif
        SCPI_ResultError(context, &err);
        return res;
    }

    // fill return-array with current states
    uint8_t ret_array[MAXROW * MAXCOL];
    for (size_t i = 0; i < array_size; i++)
    {
        if (scpi_route_status[array[i].row][array[i].col] == SCPI_ROUTE_OPENED)
        {
            ret_array[i] = 1;
        }
        else
        {
            ret_array[i] = 0;
        }
    }

    // return result array
    SCPI_ResultArrayUInt8(context, ret_array, array_size, SCPI_FORMAT_ASCII);

    return res;
}

/**
 * @brief ROUTe subsystem: Opens channels
 * The OPEN command allows specific channels to be opened.
 */
static scpi_result_t SCPI_RouteOpenAll(scpi_t* context)
{
    // open all routes
    for (uint8_t i = 0; i < MAXROW; i++)
    {
        for (uint8_t j = 0; j < MAXCOL; j++)
        {
            scpi_route_status[i][j] = SCPI_ROUTE_OPENED;
        }
    }
    SCPI_OpenAllRoutes(); // open all
    osDelay(5); // sleep 5 ms (2 ms max release time according to EDR201A0500 datasheet)

#if SCPI_ENABLE_ROUTE_NO_ERROR_OUTPUT
    scpi_error_t err;
    err.error_code = SCPI_ERROR_NO_ERROR;
#if USE_DEVICE_DEPENDENT_ERROR_INFORMATION
    err.device_dependent_info = "Successfully Opened All";
#endif // USE_DEVICE_DEPENDENT_ERROR_INFORMATION
    SCPI_ResultError(context, &err);
    // Hack: we need to write new line manually cause the command does not contain "?" at the end
    context->interface->write(context, SCPI_LINE_ENDING, strlen(SCPI_LINE_ENDING));
#endif // SCPI_ENABLE_ROUTE_NO_ERROR_OUTPUT

    return SCPI_RES_OK;
}

/**
 * Reimplement IEEE488.2 *TST?
 *
 * Result should be 0 if everything is ok
 * Result should be 1 if something goes wrong
 *
 * Return SCPI_RES_OK
 */
static scpi_result_t My_CoreTstQ(scpi_t * context) {

    SCPI_ResultInt32(context, 0);

    return SCPI_RES_OK;
}

const scpi_command_t scpi_commands[] = {
    /* IEEE Mandated Commands (SCPI std V1999.0 4.1.1) */
    { .pattern = "*CLS", .callback = SCPI_CoreCls,},
    { .pattern = "*ESE", .callback = SCPI_CoreEse,},
    { .pattern = "*ESE?", .callback = SCPI_CoreEseQ,},
    { .pattern = "*ESR?", .callback = SCPI_CoreEsrQ,},
    { .pattern = "*IDN?", .callback = SCPI_CoreIdnQ,},
    { .pattern = "*OPC", .callback = SCPI_CoreOpc,},
    { .pattern = "*OPC?", .callback = SCPI_CoreOpcQ,},
    { .pattern = "*RST", .callback = SCPI_CoreRst,}, // opens all routes and resets scpi_route_status to SCPI_ROUTE_OPENED
    { .pattern = "*SRE", .callback = SCPI_CoreSre,},
    { .pattern = "*SRE?", .callback = SCPI_CoreSreQ,},
    { .pattern = "*STB?", .callback = SCPI_CoreStbQ,},
    { .pattern = "*TST?", .callback = My_CoreTstQ,},
    { .pattern = "*WAI", .callback = SCPI_CoreWai,},

    /* Required SCPI commands (SCPI std V1999.0 4.2.1) */
    {.pattern = "SYSTem:ERRor[:NEXT]?", .callback = SCPI_SystemErrorNextQ,},
    {.pattern = "SYSTem:ERRor:COUNt?", .callback = SCPI_SystemErrorCountQ,},
    {.pattern = "SYSTem:VERSion?", .callback = SCPI_SystemVersionQ,},

    {.pattern = "STATus:QUEStionable[:EVENt]?", .callback = SCPI_StatusQuestionableEventQ,},
    {.pattern = "STATus:QUEStionable:ENABle", .callback = SCPI_StatusQuestionableEnable,},
    {.pattern = "STATus:QUEStionable:ENABle?", .callback = SCPI_StatusQuestionableEnableQ,},

    {.pattern = "STATus:PRESet", .callback = SCPI_StatusPreset,},

    /* SCPI-99 ROUTe Subsystem */
    {.pattern = "ROUTe:CLOSe", .callback = SCPI_RouteClose,},
    {.pattern = "ROUTe:OPEN", .callback = SCPI_RouteOpen,},
    {.pattern = "ROUTe:CLOSe[:STATe]?", .callback = SCPI_RouteCloseQ,},
    {.pattern = "ROUTe:OPEN?", .callback = SCPI_RouteOpenQ,},
    {.pattern = "ROUTe:OPEN:ALL", .callback = SCPI_RouteOpenAll,},

    {.pattern = "SYSTem:COMMunication:TCPIP:CONTROL?", .callback = SCPI_SystemCommTcpipControlQ,},

    {.pattern = "TEST:BOOL", .callback = TEST_Bool,},
    {.pattern = "TEST:CHOice?", .callback = TEST_ChoiceQ,},
    {.pattern = "TEST#:NUMbers#", .callback = TEST_Numbers,},
    {.pattern = "TEST:TEXT", .callback = TEST_Text,},
    {.pattern = "TEST:ARBitrary?", .callback = TEST_ArbQ,},
    {.pattern = "TEST:CHANnellist", .callback = TEST_Chanlst,},

    SCPI_CMD_LIST_END
};

scpi_result_t SCPI_Reset(scpi_t * context)
{
    (void)context;

    // open all routes on reset
    for (uint8_t i = 0; i < MAXROW; i++)
    {
        for (uint8_t j = 0; j < MAXCOL; j++)
        {
            scpi_route_status[i][j] = SCPI_ROUTE_OPENED;
        }
    }
    SCPI_OpenAllRoutes(); // open all
    osDelay(5); // sleep 5 ms (2 ms max release time according to EDR201A0500 datasheet)

    printf("**Reset: opened all routes\r\n");
    return SCPI_RES_OK;
}

scpi_interface_t scpi_interface = {
    .error = SCPI_Error,
    .write = SCPI_Write,
    .control = SCPI_Control,
    .flush = SCPI_Flush,
    .reset = SCPI_Reset,
};

char scpi_input_buffer[SCPI_INPUT_BUFFER_LENGTH];
scpi_error_t scpi_error_queue_data[SCPI_ERROR_QUEUE_SIZE];

scpi_t scpi_context;
