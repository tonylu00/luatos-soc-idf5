
#include "luat_base.h"
#include "luat_uart.h"
#include "luat_shell.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"

#include "luat_log.h"
#define LUAT_LOG_TAG "uart"

#ifdef LUAT_USE_SHELL
static uint8_t shell_state = 0;
static char shell_buffer[1024] = {0};
#endif
typedef struct ifd_uart_port{
    TaskHandle_t xHandle;
    QueueHandle_t xQueue;
    uint8_t send;
    uint8_t recv;
}ifd_uart_port_t;

ifd_uart_port_t uart_port[SOC_UART_NUM] = {0};

#define UART_CHECK(uartid) ((uartid<0||uartid>=SOC_UART_NUM)?-1:0)

int luat_uart_exist(int uartid){
    return (UART_CHECK(uartid)==0);
}

static void uart0_irq_task(void *arg){
    uart_event_t event = {0};
    rtos_msg_t msg = {0};
    for (;;){
        if (xQueueReceive(uart_port[0].xQueue, (void *)&event, (TickType_t)portMAX_DELAY)){
            if (event.timeout_flag || event.size > (1024 * 2 - 200)){
                if (uart_port[0].recv){
                    msg.handler = l_uart_handler;
                    msg.ptr = NULL;
                    msg.arg1 = 0;
                    msg.arg2 = 1;
                    luat_msgbus_put(&msg, 0);
                }
#ifdef LUAT_USE_SHELL
            if (shell_state==2){
                int len = luat_uart_read(0, shell_buffer, 1024);
                if (len < 1)
                    continue;
                shell_buffer[len] = 0x00;
                luat_shell_push(shell_buffer, len);
            }
#endif
                xQueueReset(uart_port[0].xQueue);
            }
        }
    }
    vTaskDelete(NULL);
}

static void uart1_irq_task(void *arg){
    uart_event_t event = {0};
    rtos_msg_t msg = {0};
    for (;;){
        if (xQueueReceive(uart_port[1].xQueue, (void *)&event, (TickType_t)portMAX_DELAY)){
            if (event.timeout_flag || event.size > (1024 * 2 - 200)){
                if (uart_port[1].recv){
                    msg.handler = l_uart_handler;
                    msg.ptr = NULL;
                    msg.arg1 = 1;
                    msg.arg2 = 1;
                    luat_msgbus_put(&msg, 0);
                }
                xQueueReset(uart_port[1].xQueue);
            }
        }
    }
    vTaskDelete(NULL);
}

int luat_uart_setup(luat_uart_t *uart){
    int id = uart->id;
    if (UART_CHECK(id)){
        return -1;
    }
    uart_config_t uart_config = {0};
    uart_config.baud_rate = uart->baud_rate;
    switch (uart->data_bits){
    case 8:
        uart_config.data_bits = UART_DATA_8_BITS;
        break;
    case 7:
        uart_config.data_bits = UART_DATA_7_BITS;
        break;
    default:
        uart_config.data_bits = UART_DATA_8_BITS;
        break;
    }
    switch (uart->parity){
    case LUAT_PARITY_NONE:
        uart_config.parity = UART_PARITY_DISABLE;
        break;
    case LUAT_PARITY_ODD:
        uart_config.parity = UART_PARITY_ODD;
        break;
    case LUAT_PARITY_EVEN:
        uart_config.parity = UART_PARITY_EVEN;
        break;
    default:
        uart_config.parity = UART_PARITY_DISABLE;
        break;
    }
    uart_config.stop_bits = uart->stop_bits;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_XTAL;
    switch (id){
    case 0:
#ifdef LUAT_USE_SHELL
        shell_state++;
        if (shell_state==1){
            shell_state = 0;
        }else if(shell_state==3){
            shell_state = 0;
        }
#endif
        if (uart_port[id].xHandle==NULL){
            uart_driver_install(0, uart->bufsz * 2, uart->bufsz * 2, 20, &(uart_port[0].xQueue), 0);
            uart_set_pin(0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
            uart_param_config(id, &uart_config);
            uart_pattern_queue_reset(id, 20);
            xTaskCreate(uart0_irq_task, "uart0_irq_task", 2048, NULL, 10, &uart_port[id].xHandle);
        }
        break;
    case 1:
        if (uart_port[id].xHandle==NULL){
            uart_driver_install(1, uart->bufsz * 2, uart->bufsz * 2, 20, &(uart_port[1].xQueue), 0);
            uart_set_pin(1, 18, 19, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
            uart_param_config(id, &uart_config);
            uart_pattern_queue_reset(id, 20);
            xTaskCreate(uart1_irq_task, "uart1_irq_task", 2048, NULL, 10, &uart_port[id].xHandle);
        }
        break;
    default:
        return -1;
        break;
    }
    return 0;
}

int luat_uart_write(int uartid, void *data, size_t length){
    if (UART_CHECK(uartid)){
        return -1;
    }
    return uart_write_bytes(uartid, (const char *)data, length);
}

int luat_uart_read(int uartid, void *buffer, size_t length){
    if (UART_CHECK(uartid)){
        return -1;
    }
    return uart_read_bytes(uartid, buffer, length, 100 / portTICK_PERIOD_MS);
}

int luat_uart_close(int uartid){
    if (UART_CHECK(uartid)){
        return -1;
    }
    if (uart_port[uartid].xHandle){
        vTaskDelete(uart_port[uartid].xHandle);
        uart_port[uartid].xHandle = NULL;
        vQueueDelete(uart_port[uartid].xQueue);
    }
    return uart_driver_delete(uartid)? -1 : 0;
}


int luat_setup_cb(int uartid, int received, int sent){
    if (UART_CHECK(uartid)){
        return -1;
    }
    if (received){
        uart_port[uartid].recv = 1;
    }
    return 0;
}

#ifdef LUAT_USE_SHELL
void luat_shell_poweron(int _drv) {
    uart_set_baudrate(0, 921600);
    shell_state = 1;
    luat_uart_t shell_uart = {
        .id = _drv,
        .baud_rate=921600,
        .baud_rate=8,
        .parity=LUAT_PARITY_NONE,
        .bit_order=LUAT_BIT_ORDER_LSB,
        .bufsz=1024,
        .pin485=0xffffffff,
    };
    luat_uart_setup(&shell_uart);
}
#endif



