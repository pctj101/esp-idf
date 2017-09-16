/* Uart Events Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "soc/uart_struct.h"

/**
 * This is a example example which echos any data it receives on UART1 back to the sender.
 *
 * - port: UART1
 * - rx buffer: on
 * - tx buffer: off
 * - flow control: off
 * - pin assignment: txd(io17), rxd(io16), rts(5), cts(Not Used)
 *
 * This example has been tested on a 3 node RS485 Serial Bus
 * 
 */

#define ECHO_TEST_TXD  (17)
#define ECHO_TEST_RXD  (16)

// RTS for RS485 Half-Duplex Mode manages DE/~RE
#define ECHO_TEST_RTS  (5)

// CTS is not used in RS485 Half-Duplex Mode
#define ECHO_TEST_CTS  UART_PIN_NO_CHANGE

#define BUF_SIZE (1024)

//an example of echo test with hardware flow control on UART1
static void echo_task()
{
    const int uart_num = UART_NUM_1;
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    //Configure UART1 parameters
    uart_param_config(uart_num, &uart_config);
    //Set UART1 pins(TX: IO4, RX: I05, RTS: IO18, CTS: IO19)
    uart_set_pin(uart_num, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS);

    //Install UART driver (we don't need an event queue here)
    //In this example we don't even use a buffer for sending data.
    uart_driver_install(uart_num, BUF_SIZE * 2, 0, 0, NULL, 0);

    uart_set_rs485_hd_mode(uart_num, true);


    uint8_t* data = (uint8_t*) malloc(BUF_SIZE);
    while(1) {
        //Read data from UART
        int len = uart_read_bytes(uart_num, data, BUF_SIZE, 20 / portTICK_RATE_MS);

        //Write data back to UART
        if (len > 0) {
            uart_write_bytes(uart_num, "\r\n", 2);
            char prefix[] = "RS485 Received: [";
            uart_write_bytes(uart_num, prefix, sizeof(prefix));
            for (int i = 0; i < len; i++) {
                uart_write_bytes(uart_num, (const char*)data+i, 1);
                // Add a Newline character if you get a return charater from paste (Paste tests multibyte receipt/buffer)
                if (data[i] == '\r') {
                    uart_write_bytes(uart_num, "\n", 1);
                }
            }
            uart_write_bytes(uart_num, "]\r\n", 3);
        } else {
            // Echo a "." to show we are alive while we wait for input
            uart_write_bytes(uart_num, ".", 3);
        }
    }
}

void app_main()
{
    //A uart read/write example without event queue;
    xTaskCreate(echo_task, "uart_echo_task", 1024, NULL, 10, NULL);
}
