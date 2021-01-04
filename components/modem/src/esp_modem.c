// Copyright 2015-2018 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_modem.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define ESP_MODEM_LINE_BUFFER_SIZE (CONFIG_UART_RX_BUFFER_SIZE / 2)
#define ESP_MODEM_EVENT_QUEUE_SIZE (16)

#define MIN_PATTERN_INTERVAL (9)
#define MIN_POST_IDLE (0)
#define MIN_PRE_IDLE (0)

/**
 * @brief Macro defined for error checking
 *
 */
static const char *MODEM_TAG = "esp-modem";
#define MODEM_CHECK(a, str, goto_tag, ...)                                              \
    do                                                                                  \
    {                                                                                   \
        if (!(a))                                                                       \
        {                                                                               \
            ESP_LOGE(MODEM_TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            goto goto_tag;                                                              \
        }                                                                               \
    } while (0)

ESP_EVENT_DEFINE_BASE(ESP_MODEM_EVENT);

/**
 * @brief ESP32 Modem DTE
 *
 */
typedef struct {
    uart_port_t uart_port;                  /*!< UART port */
    uint8_t *buffer;                        /*!< Internal buffer to store response lines/data from DCE */
    uint16_t buffer_len;
    QueueHandle_t event_queue;              /*!< UART event queue handle */
    esp_event_loop_handle_t event_loop_hdl; /*!< Event loop handle */
    TaskHandle_t uart_event_task_hdl;       /*!< UART event task handle */
    SemaphoreHandle_t process_sem;          /*!< Semaphore used for indicating processing status */
    modem_dte_t parent;                     /*!< DTE interface that should extend */
    esp_modem_on_receive receive_cb;        /*!< ptr to data reception */
    void *receive_cb_ctx;                   /*!< ptr to rx fn context data */
    int line_buffer_size;                   /*!< line buffer size in commnad mode */
    int pattern_queue_size;                 /*!< UART pattern queue size */
} esp_modem_dte_t;

/**
 * @brief Returns true if the supplied string contains only CR or LF
 *
 * @param str string to check
 * @param len length of string
 */
static inline bool is_only_cr_lf(const char *str, uint32_t len)
{
    for (int i=0; i<len; ++i) {
        if (str[i] != '\r' && str[i] != '\n') {
            return false;
        }
    }
    return true;
}

uint8_t crc8(const char *src, size_t len, uint8_t polynomial, uint8_t initial_value,
	  bool reversed)
{
	uint8_t crc = initial_value;
	size_t i, j;

	for (i = 0; i < len; i++) {
		crc ^= src[i];

		for (j = 0; j < 8; j++) {
			if (reversed) {
				if (crc & 0x01) {
					crc = (crc >> 1) ^ polynomial;
				} else {
					crc >>= 1;
				}
			} else {
				if (crc & 0x80) {
					crc = (crc << 1) ^ polynomial;
				} else {
					crc <<= 1;
				}
			}
		}
	}

	return crc;
}

esp_err_t esp_modem_set_rx_cb(modem_dte_t *dte, esp_modem_on_receive receive_cb, void *receive_cb_ctx)
{
    esp_modem_dte_t *esp_dte = __containerof(dte, esp_modem_dte_t, parent);
    esp_dte->receive_cb_ctx = receive_cb_ctx;
    esp_dte->receive_cb = receive_cb;
    return ESP_OK;
}


/**
 * @brief Handle one line in DTE
 *
 * @param esp_dte ESP modem DTE object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t esp_dte_handle_line(esp_modem_dte_t *esp_dte)
{
    modem_dce_t *dce = esp_dte->parent.dce;
    MODEM_CHECK(dce, "DTE has not yet bind with DCE", err);
    const char *line = (const char *)(esp_dte->buffer);
    size_t len = strlen(line);
    /* Skip pure "\r\n" lines */
    if (len > 2 && !is_only_cr_lf(line, len)) {
        MODEM_CHECK(dce->handle_line, "no handler for line", err_handle);
        MODEM_CHECK(dce->handle_line(dce, line) == ESP_OK, "handle line failed", err_handle);
    }
    return ESP_OK;
err_handle:
    /* Send ESP_MODEM_EVENT_UNKNOWN signal to event loop */
    esp_event_post_to(esp_dte->event_loop_hdl, ESP_MODEM_EVENT, ESP_MODEM_EVENT_UNKNOWN,
                      (void *)line, strlen(line) + 1, pdMS_TO_TICKS(100));
err:
    return ESP_FAIL;
}

/**
 * @brief Handle one line in DTE
 *
 * @param esp_dte ESP modem DTE object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t esp_dte_handle_cmux_frame(esp_modem_dte_t *esp_dte)
{
    modem_dce_t *dce = esp_dte->parent.dce;
    char *frame = (char *)(esp_dte->buffer);

    MODEM_CHECK(dce, "DTE has not yet bind with DCE", err);
    uint8_t dlci = frame[1] >> 2;
    uint8_t type = frame[2];
    uint8_t length = frame[3] >> 1;
    
    ESP_LOGD(MODEM_TAG, "CMUX FR: A:%02x T:%02x L:%d Buf:%d", dlci, type, length, esp_dte->buffer_len);
//    printf("buffer >>> ");
//	for (uint16_t i = 0; i < length; i++)
//	    printf("%02x ", frame[i]);
//	printf("\n");

    if (dce->handle_cmux_frame != NULL) {
            MODEM_CHECK(dce->handle_cmux_frame(dce, frame) == ESP_OK, "handle cmux frame failed", err_handle);
    }
    else if ((type == FT_UIH || type == (FT_UIH | PF)) && dlci == 1 && dce->handle_line != NULL 
             && strlen(&frame[6]) > 2)
    {
        // Handle CONNECT message on DLCI 1
        ESP_LOGI(MODEM_TAG, "Handle Line: %s for DLCI 1", &frame[6]);
        MODEM_CHECK(dce->handle_line, "no handler for line", err_handle);
        MODEM_CHECK(dce->handle_line(dce, &frame[6]) == ESP_OK, "handle line failed", err_handle);
        dce->handle_line = NULL;
    }
    else if ((type == FT_UIH || type == (FT_UIH | PF)) && dlci == 2 && dce->handle_line != NULL)
    {
        ESP_LOGD(MODEM_TAG, "Handle line from DLCI 2");
        frame[4 + length] = '\0';
        /* Skipping first two \r\n */
        if (strlen(&frame[6]) > 2)
        {
            ESP_LOGD(MODEM_TAG, "Line: %s", &frame[6]);
            MODEM_CHECK(dce->handle_line, "no handler for line", err_handle);
            MODEM_CHECK(dce->handle_line(dce, &frame[6]) == ESP_OK, "handle line failed", err_handle);
        }
    }
    else if ((type == FT_UIH || type == (FT_UIH | PF)) && length && dlci == 1 && esp_dte->receive_cb != NULL)
    {
        // Handle DCLI 1
        ESP_LOGD(MODEM_TAG, "Pass data with length %d from DLCI: %d to receive_cb", length, dlci);
        esp_dte->receive_cb(&esp_dte->buffer[4], length, esp_dte->receive_cb_ctx);
    }
    else if (dlci != 0)
    {
        ESP_LOGW(MODEM_TAG, "Unknown state...");
    }
    return ESP_OK;

err_handle:
    /* Send ESP_MODEM_EVENT_UNKNOWN signal to event loop */
    esp_event_post_to(esp_dte->event_loop_hdl, ESP_MODEM_EVENT, ESP_MODEM_EVENT_UNKNOWN,
                      "cmux frame invalid", 4, pdMS_TO_TICKS(100));

err:
    return ESP_FAIL;
}

/**
 * @brief Handle when a pattern has been detected by UART
 *
 * @param esp_dte ESP32 Modem DTE object
 */
static void esp_handle_uart_pattern(esp_modem_dte_t *esp_dte)
{
    int pos = uart_pattern_pop_pos(esp_dte->uart_port);
    int read_len = 0;
    if (pos != -1) {
        if (pos < esp_dte->line_buffer_size - 1) {
            /* read one line(include '\n') */
            read_len = pos + 1;
        } else {
            ESP_LOGW(MODEM_TAG, "ESP Modem Line buffer too small");
            read_len = esp_dte->line_buffer_size - 1;
        }
        read_len = uart_read_bytes(esp_dte->uart_port, esp_dte->buffer, read_len, pdMS_TO_TICKS(100));
        if (read_len) {
            /* make sure the line is a standard string */
            esp_dte->buffer[read_len] = '\0';
            ESP_LOGD(MODEM_TAG, "< line: %s", esp_dte->buffer);
            /* Send new line to handle */
            esp_dte_handle_line(esp_dte);
        } else {
            ESP_LOGE(MODEM_TAG, "uart read bytes failed");
        }
    } else {
        ESP_LOGW(MODEM_TAG, "Pattern Queue Size too small");
        uart_flush(esp_dte->uart_port);
    }
}

static void esp_handle_uart_frame(esp_modem_dte_t *esp_dte)
{
    
    uint16_t fl, frame_length_full;

    handle:

    fl = esp_dte->buffer[3] >> 1;
    frame_length_full = fl + 6;
    ESP_LOGD(MODEM_TAG, "Check frame with buffer length: %d, frame length: %d", esp_dte->buffer_len, frame_length_full);

    if (esp_dte->buffer_len < 5) {
        return; 
    }

    if (esp_dte->buffer[0] != SOF_MARKER) {
        ESP_LOGW(MODEM_TAG, "Missing start SOF"); 
        return; 
    }

    if (esp_dte->buffer_len < frame_length_full) {
        // Frame incomplete
        return;
    }

    if (esp_dte->buffer[frame_length_full-1] != SOF_MARKER) {
        ESP_LOGW(MODEM_TAG, "Missing end SOF");
        return; 
    }
    
    // handle one complete frame
    esp_dte_handle_cmux_frame(esp_dte);

    // check if there is data from next frame
    if (esp_dte->buffer_len > frame_length_full)
    {
        uint16_t frame_length_next = esp_dte->buffer_len - frame_length_full;
        ESP_LOGD(MODEM_TAG, "Copy %d from next frame to beginning of the buffer", frame_length_next);
//        printf("copy >>> ");
//        for (uint16_t i = 0; i < esp_dte->buffer_len; i++)
//            printf("%02x ", esp_dte->buffer[i]);
//        printf("\n");

        memcpy(esp_dte->buffer, &esp_dte->buffer[frame_length_full], frame_length_next);
        esp_dte->buffer_len = frame_length_next;

//        printf("after copy >>> ");
//        for (uint16_t i = 0; i < esp_dte->buffer_len; i++)
//            printf("%02x ", esp_dte->buffer[i]);
//        printf("\n");

        if (esp_dte->buffer_len > 4)
            goto handle;
    }
    else
    {
        // set back to beginning
        esp_dte->buffer_len = 0;
    }
}

/**
 * @brief Handle when new data received by UART
 *
 * @param esp_dte ESP32 Modem DTE object
 */
static void esp_handle_uart_data(esp_modem_dte_t *esp_dte)
{
    size_t length = 0;
    length = MIN(esp_dte->line_buffer_size, length);
    uart_get_buffered_data_len(esp_dte->uart_port, &length);
    length = uart_read_bytes(esp_dte->uart_port, &esp_dte->buffer[esp_dte->buffer_len], length, portMAX_DELAY);
    esp_dte->buffer_len += length;
//        printf("received < ");
//	    for (uint16_t i = 0; i < length; i++)
//	        printf("%02x ", buffer[i]);
//	    printf("\n");

    if (esp_dte->buffer_len > 0)
    {
        if (esp_dte->buffer[0] == SOF_MARKER)
        {
            esp_handle_uart_frame(esp_dte);
        }
        else
        {
//            ESP_LOGW(MODEM_TAG, "RX data is out of sync. Missing SOF. Buffer length: %d", esp_dte->buffer_len);
//            memcpy(esp_dte->buffer, &esp_dte->buffer[1], esp_dte->buffer_len - 1);
//            esp_dte->buffer_len--;
//            vTaskDelay(100 / portTICK_PERIOD_MS);
//            goto handle;
        }
    }
}

/**
 * @brief UART Event Task Entry
 *
 * @param param task parameter
 */
static void uart_event_task_entry(void *param)
{
    esp_modem_dte_t *esp_dte = (esp_modem_dte_t *)param;
    uart_event_t event;
    while (1) {
        if (xQueueReceive(esp_dte->event_queue, &event, pdMS_TO_TICKS(100))) {
            switch (event.type) {
            case UART_DATA:
                esp_handle_uart_data(esp_dte);
                break;
            case UART_FIFO_OVF:
                ESP_LOGW(MODEM_TAG, "HW FIFO Overflow");
                uart_flush_input(esp_dte->uart_port);
                xQueueReset(esp_dte->event_queue);
                break;
            case UART_BUFFER_FULL:
                ESP_LOGW(MODEM_TAG, "Ring Buffer Full");
                uart_flush_input(esp_dte->uart_port);
                xQueueReset(esp_dte->event_queue);
                break;
            case UART_BREAK:
                ESP_LOGW(MODEM_TAG, "Rx Break");
                break;
            case UART_PARITY_ERR:
                ESP_LOGE(MODEM_TAG, "Parity Error");
                break;
            case UART_FRAME_ERR:
                ESP_LOGE(MODEM_TAG, "Frame Error");
                break;
            case UART_PATTERN_DET:
                esp_handle_uart_pattern(esp_dte);
                break;
            default:
                ESP_LOGW(MODEM_TAG, "unknown uart event type: %d", event.type);
                break;
            }
        }
        /* Drive the event loop */
        esp_event_loop_run(esp_dte->event_loop_hdl, pdMS_TO_TICKS(50));
    }
    vTaskDelete(NULL);
}

/**
 * @brief Send command to DCE
 *
 * @param dte Modem DTE object
 * @param command command string
 * @param timeout timeout value, unit: ms
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t esp_modem_dte_send_cmd(modem_dte_t *dte, const char *command, uint32_t timeout)
{
    esp_err_t ret = ESP_FAIL;
    modem_dce_t *dce = dte->dce;
    MODEM_CHECK(dce, "DTE has not yet bind with DCE", err);
    MODEM_CHECK(command, "command is NULL", err);
    esp_modem_dte_t *esp_dte = __containerof(dte, esp_modem_dte_t, parent);
    /* Calculate timeout clock tick */
    /* Reset runtime information */
    dce->state = MODEM_STATE_PROCESSING;
    /* Send command via UART */
    uart_write_bytes(esp_dte->uart_port, command, strlen(command));
    /* Check timeout */
    MODEM_CHECK(xSemaphoreTake(esp_dte->process_sem, pdMS_TO_TICKS(timeout)) == pdTRUE, "process command timeout", err);
    ret = ESP_OK;
err:
    dce->handle_line = NULL;
    return ret;
}

static esp_err_t esp_modem_dte_send_sabm(modem_dte_t *dte, uint8_t dlci, uint32_t timeout)
{
  esp_err_t ret = ESP_FAIL;
  modem_dce_t *dce = dte->dce;
  MODEM_CHECK(dce, "DTE has not yet bind with DCE", err);
  esp_modem_dte_t *esp_dte = __containerof(dte, esp_modem_dte_t, parent);
  char frame[6];
  frame[0] = SOF_MARKER;
  frame[1] = (dlci << 2) | 0x3;
  frame[2] = FT_SABM | PF;
  frame[3] = 1;
  frame[4] = 0xFF - crc8(&frame[1], 3, FCS_POLYNOMIAL, FCS_INIT_VALUE, true);
  frame[5] = SOF_MARKER;
	/*printf("sabm > ");
  for (uint8_t i = 0; i < 6; i++)
    printf("%02x ", frame[i]);
  printf("\n");*/
  /* Calculate timeout clock tick */
  /* Reset runtime information */
  dce->state = MODEM_STATE_PROCESSING;
  /* Send command via UART */
  uart_write_bytes(esp_dte->uart_port, frame, 6);
  /* Check timeout */
  MODEM_CHECK(xSemaphoreTake(esp_dte->process_sem, pdMS_TO_TICKS(timeout)) == pdTRUE, "process command timeout", err);
  ret = ESP_OK;
err:
  dce->handle_cmux_frame = NULL;
  return ret;
  //f9 03 2f 01 09 f9
}

/**
 * @brief Send command to DCE
 *
 * @param dte Modem DTE object
 * @param command command string
 * @param timeout timeout value, unit: ms
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t esp_modem_dte_send_cmux_cmd(modem_dte_t *dte, const char *command, uint32_t timeout)
{
    esp_err_t ret = ESP_FAIL;
    modem_dce_t *dce = dte->dce;
    MODEM_CHECK(dce, "DTE has not yet bind with DCE", err);
    MODEM_CHECK(command, "command is NULL", err);
    esp_modem_dte_t *esp_dte = __containerof(dte, esp_modem_dte_t, parent);
    char *frame;
		frame = (char *) malloc(6 + strlen(command));
		if (strcmp(command, "ATD*99***1#\r") == 0)
		{
			ESP_LOGI(MODEM_TAG, "Got ATD");
			frame[1] = (0x1 << 2) + 1;
		}
		else
		{
			frame[1] = (0x2 << 2) + 1;
		}
    frame[0] = SOF_MARKER;
    frame[2] = FT_UIH;
    frame[3] = (strlen(command) << 1) + 1;
    memcpy(&frame[4], command, strlen(command));
    frame[4 + strlen(command)] = 0xFF - crc8(&frame[1], 3, FCS_POLYNOMIAL, FCS_INIT_VALUE, true);
    frame[5 + strlen(command)] = SOF_MARKER;
    ESP_LOGD(MODEM_TAG, "> %s", command);
    //printf("cmd > ");
    //for (uint8_t i = 0; i < 6 + strlen(command); i++)
    //  printf("%02x ", frame[i]);
    //printf("\n");

    /* Calculate timeout clock tick */
    /* Reset runtime information */
    dce->state = MODEM_STATE_PROCESSING;
    /* Send command via UART */
    uart_write_bytes(esp_dte->uart_port, frame, 6 + strlen(command));
	vTaskDelay(100 / portTICK_PERIOD_MS);
    /* Check timeout */
    MODEM_CHECK(xSemaphoreTake(esp_dte->process_sem, pdMS_TO_TICKS(timeout)) == pdTRUE, "process command timeout", err);
    ret = ESP_OK;
		free(frame);
err:
    dce->handle_cmux_frame = NULL;
    return ret;
}

/**
 * @brief Send data to DCE
 *
 * @param dte Modem DTE object
 * @param data data buffer
 * @param length length of data to send
 * @return int actual length of data that has been send out
 */
static int esp_modem_dte_send_data(modem_dte_t *dte, const char *data, uint32_t length)
{
    MODEM_CHECK(data, "data is NULL", err);
    esp_modem_dte_t *esp_dte = __containerof(dte, esp_modem_dte_t, parent);
		/*printf("> ");
		for (uint8_t i = 0; i < length; i++)
			printf("%02x ", data[i]);
		printf("\n");*/
    return uart_write_bytes(esp_dte->uart_port, data, length);
err:
    return -1;
}

/**
 * @brief Send CMUX data to DCE
 *
 * @param dte Modem DTE object
 * @param data data buffer
 * @param length length of data to send
 * @return int actual length of data that has been send out
 */
static int esp_modem_dte_send_cmux_data(modem_dte_t *dte, const char *data, uint32_t length)
{
    MODEM_CHECK(data, "data is NULL", err);
    esp_modem_dte_t *esp_dte = __containerof(dte, esp_modem_dte_t, parent);
    uint32_t length_to_transmit = length;
    while (length_to_transmit > 0)
    {
        uint8_t current_frame_length = length_to_transmit;
        if (length_to_transmit > 127)
            current_frame_length = 127;

        char *frame;
        frame = (char *)malloc(6 + current_frame_length);
        frame[0] = SOF_MARKER;
        frame[1] = (0x1 << 2) + 1;
        frame[2] = FT_UIH;
        frame[3] = (current_frame_length << 1) + 1;
        memcpy(&frame[4], &data[length - length_to_transmit], current_frame_length);
        frame[4 + current_frame_length] = 0xFF - crc8(&frame[1], 3, FCS_POLYNOMIAL, FCS_INIT_VALUE, true);
        frame[5 + current_frame_length] = SOF_MARKER;
        /* Calculate timeout clock tick */
        /* Reset runtime information */
        uart_write_bytes(esp_dte->uart_port, frame, 6 + current_frame_length);
        ESP_LOGD(MODEM_TAG, ">>>> Send %d", current_frame_length);
        free(frame);
        length_to_transmit -= current_frame_length;
    }
    return length;
err:
    return -1;
}

/**
 * @brief Send data and wait for prompt from DCE
 *
 * @param dte Modem DTE object
 * @param data data buffer
 * @param length length of data to send
 * @param prompt pointer of specific prompt
 * @param timeout timeout value (unit: ms)
 * @return esp_err_t
 *      ESP_OK on success
 *      ESP_FAIL on error
 */
static esp_err_t esp_modem_dte_send_wait(modem_dte_t *dte, const char *data, uint32_t length,
        const char *prompt, uint32_t timeout)
{
    MODEM_CHECK(data, "data is NULL", err_param);
    MODEM_CHECK(prompt, "prompt is NULL", err_param);
    esp_modem_dte_t *esp_dte = __containerof(dte, esp_modem_dte_t, parent);
    // We'd better disable pattern detection here for a moment in case prompt string contains the pattern character
    uart_disable_pattern_det_intr(esp_dte->uart_port);
    // uart_disable_rx_intr(esp_dte->uart_port);
    MODEM_CHECK(uart_write_bytes(esp_dte->uart_port, data, length) >= 0, "uart write bytes failed", err_write);
    uint32_t len = strlen(prompt);
    uint8_t *buffer = calloc(len + 1, sizeof(uint8_t));
    int res = uart_read_bytes(esp_dte->uart_port, buffer, len, pdMS_TO_TICKS(timeout));
    MODEM_CHECK(res >= len, "wait prompt [%s] timeout", err, prompt);
    MODEM_CHECK(!strncmp(prompt, (const char *)buffer, len), "get wrong prompt: %s", err, buffer);
    free(buffer);
    uart_enable_pattern_det_baud_intr(esp_dte->uart_port, '\n', 1, MIN_PATTERN_INTERVAL, MIN_POST_IDLE, MIN_PRE_IDLE);
    return ESP_OK;
err:
    free(buffer);
err_write:
    uart_enable_pattern_det_baud_intr(esp_dte->uart_port, '\n', 1, MIN_PATTERN_INTERVAL, MIN_POST_IDLE, MIN_PRE_IDLE);
err_param:
    return ESP_FAIL;
}

/**
 * @brief Change Modem's working mode
 *
 * @param dte Modem DTE object
 * @param new_mode new working mode
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t esp_modem_dte_change_mode(modem_dte_t *dte, modem_mode_t new_mode)
{
    modem_dce_t *dce = dte->dce;
    MODEM_CHECK(dce, "DTE has not yet bind with DCE", err);
    esp_modem_dte_t *esp_dte = __containerof(dte, esp_modem_dte_t, parent);
    MODEM_CHECK(dce->mode != new_mode, "already in mode: %d", err, new_mode);
    switch (new_mode) {
    case MODEM_PPP_MODE:
        ESP_LOGI(MODEM_TAG, "PPP MODE");
        MODEM_CHECK(dce->set_working_mode(dce, new_mode) == ESP_OK, "set new working mode:%d failed", err, new_mode);
        uart_disable_pattern_det_intr(esp_dte->uart_port);
        uart_enable_rx_intr(esp_dte->uart_port);
        break;
    case MODEM_COMMAND_MODE:
        uart_disable_rx_intr(esp_dte->uart_port);
        uart_flush(esp_dte->uart_port);
        uart_enable_pattern_det_baud_intr(esp_dte->uart_port, '\n', 1, MIN_PATTERN_INTERVAL, MIN_POST_IDLE, MIN_PRE_IDLE);
//        uart_pattern_queue_reset(esp_dte->uart_port, esp_dte->pattern_queue_size);
        MODEM_CHECK(dce->set_working_mode(dce, new_mode) == ESP_OK, "set new working mode:%d failed", err, new_mode);
        break;
    case MODEM_CMUX_MODE:
        MODEM_CHECK(dce->set_working_mode(dce, new_mode) == ESP_OK, "set new working mode:%d failed", err, new_mode);
        uart_disable_pattern_det_intr(esp_dte->uart_port);
        uart_enable_rx_intr(esp_dte->uart_port);
        dce->setup_cmux(dce);
         break;
    default:
        break;
    }
    return ESP_OK;
err:
    return ESP_FAIL;
}

static esp_err_t esp_modem_dte_process_cmd_done(modem_dte_t *dte)
{
    esp_modem_dte_t *esp_dte = __containerof(dte, esp_modem_dte_t, parent);
    return xSemaphoreGive(esp_dte->process_sem) == pdTRUE ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Deinitialize a Modem DTE object
 *
 * @param dte Modem DTE object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t esp_modem_dte_deinit(modem_dte_t *dte)
{
    esp_modem_dte_t *esp_dte = __containerof(dte, esp_modem_dte_t, parent);
    /* Delete UART event task */
    vTaskDelete(esp_dte->uart_event_task_hdl);
    /* Delete semaphore */
    vSemaphoreDelete(esp_dte->process_sem);
    /* Delete event loop */
    esp_event_loop_delete(esp_dte->event_loop_hdl);
    /* Uninstall UART Driver */
    uart_driver_delete(esp_dte->uart_port);
    /* Free memory */
    free(esp_dte->buffer);
    if (dte->dce) {
        dte->dce->dte = NULL;
    }
    free(esp_dte);
    return ESP_OK;
}



modem_dte_t *esp_modem_dte_init(const esp_modem_dte_config_t *config)
{
    esp_err_t res;
    /* malloc memory for esp_dte object */
    esp_modem_dte_t *esp_dte = calloc(1, sizeof(esp_modem_dte_t));
    MODEM_CHECK(esp_dte, "calloc esp_dte failed", err_dte_mem);
    /* malloc memory to storing lines from modem dce */
    esp_dte->line_buffer_size = config->line_buffer_size;
    esp_dte->buffer = calloc(1, config->line_buffer_size);
    MODEM_CHECK(esp_dte->buffer, "calloc line memory failed", err_line_mem);

    esp_dte->buffer_len = 0;

    /* Set attributes */
    esp_dte->uart_port = config->port_num;
    esp_dte->parent.flow_ctrl = config->flow_control;
    /* Bind methods */
    esp_dte->parent.send_cmd = esp_modem_dte_send_cmd;
    esp_dte->parent.send_cmux_cmd = esp_modem_dte_send_cmux_cmd;
    esp_dte->parent.send_sabm = esp_modem_dte_send_sabm;
    esp_dte->parent.send_data = esp_modem_dte_send_data;
	esp_dte->parent.send_cmux_data = esp_modem_dte_send_cmux_data;
    esp_dte->parent.send_wait = esp_modem_dte_send_wait;
    esp_dte->parent.change_mode = esp_modem_dte_change_mode;
    esp_dte->parent.process_cmd_done = esp_modem_dte_process_cmd_done;
    esp_dte->parent.deinit = esp_modem_dte_deinit;
    esp_dte->parent.cmux = config->cmux;

    /* Config UART */
    uart_config_t uart_config = {
        .baud_rate = config->baud_rate,
        .data_bits = config->data_bits,
        .parity = config->parity,
        .stop_bits = config->stop_bits,
        .source_clk = UART_SCLK_REF_TICK,
        .flow_ctrl = (config->flow_control == MODEM_FLOW_CONTROL_HW) ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE
    };
    MODEM_CHECK(uart_param_config(esp_dte->uart_port, &uart_config) == ESP_OK, "config uart parameter failed", err_uart_config);
    if (config->flow_control == MODEM_FLOW_CONTROL_HW) {
        res = uart_set_pin(esp_dte->uart_port, config->tx_io_num, config->rx_io_num,
                           config->rts_io_num, config->cts_io_num);
    } else {
        res = uart_set_pin(esp_dte->uart_port, config->tx_io_num, config->rx_io_num,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    MODEM_CHECK(res == ESP_OK, "config uart gpio failed", err_uart_config);
    /* Set flow control threshold */
    if (config->flow_control == MODEM_FLOW_CONTROL_HW) {
        res = uart_set_hw_flow_ctrl(esp_dte->uart_port, UART_HW_FLOWCTRL_CTS_RTS, UART_FIFO_LEN - 8);
    } else if (config->flow_control == MODEM_FLOW_CONTROL_SW) {
        res = uart_set_sw_flow_ctrl(esp_dte->uart_port, true, 8, UART_FIFO_LEN - 8);
    }
    MODEM_CHECK(res == ESP_OK, "config uart flow control failed", err_uart_config);
    /* Install UART driver and get event queue used inside driver */
    res = uart_driver_install(esp_dte->uart_port, config->rx_buffer_size, config->tx_buffer_size,
                              config->event_queue_size, &(esp_dte->event_queue), 0);
    MODEM_CHECK(res == ESP_OK, "install uart driver failed", err_uart_config);
    res = uart_set_rx_timeout(esp_dte->uart_port, 1);
    MODEM_CHECK(res == ESP_OK, "set rx timeout failed", err_uart_config);

    /* Set pattern interrupt, used to detect the end of a line. */
    res = uart_enable_pattern_det_baud_intr(esp_dte->uart_port, '\n', 1, MIN_PATTERN_INTERVAL, MIN_POST_IDLE, MIN_PRE_IDLE);
    /* Set pattern queue size */
    esp_dte->pattern_queue_size = config->pattern_queue_size;
    res |= uart_pattern_queue_reset(esp_dte->uart_port, config->pattern_queue_size);
    /* Starting in command mode -> explicitly disable RX interrupt */
    uart_disable_rx_intr(esp_dte->uart_port);

    MODEM_CHECK(res == ESP_OK, "config uart pattern failed", err_uart_pattern);
    /* Create Event loop */
    esp_event_loop_args_t loop_args = {
        .queue_size = ESP_MODEM_EVENT_QUEUE_SIZE,
        .task_name = NULL
    };
    MODEM_CHECK(esp_event_loop_create(&loop_args, &esp_dte->event_loop_hdl) == ESP_OK, "create event loop failed", err_eloop);
    /* Create semaphore */
    esp_dte->process_sem = xSemaphoreCreateBinary();
    MODEM_CHECK(esp_dte->process_sem, "create process semaphore failed", err_sem);
    /* Create UART Event task */
    BaseType_t ret = xTaskCreate(uart_event_task_entry,             //Task Entry
                                 "uart_event",              //Task Name
                                 config->event_task_stack_size,           //Task Stack Size(Bytes)
                                 esp_dte,                           //Task Parameter
                                 config->event_task_priority,             //Task Priority
                                 & (esp_dte->uart_event_task_hdl)   //Task Handler
                                );
    MODEM_CHECK(ret == pdTRUE, "create uart event task failed", err_tsk_create);
    uart_write_bytes(esp_dte->uart_port, "+++", 3);
    char cmd_cld[8] = {0xf9, 0x03, 0xef, 0x05, 0xc3, 0x01, 0xf2, 0xf9};
    uart_write_bytes(esp_dte->uart_port, cmd_cld, 8);
    return &(esp_dte->parent);
    /* Error handling */
err_tsk_create:
    vSemaphoreDelete(esp_dte->process_sem);
err_sem:
    esp_event_loop_delete(esp_dte->event_loop_hdl);
err_eloop:
    uart_disable_pattern_det_intr(esp_dte->uart_port);
err_uart_pattern:
    uart_driver_delete(esp_dte->uart_port);
err_uart_config:
    free(esp_dte->buffer);
err_line_mem:
    free(esp_dte);
err_dte_mem:
    return NULL;
}

esp_err_t esp_modem_set_event_handler(modem_dte_t *dte, esp_event_handler_t handler, int32_t event_id, void *handler_args)
{
    esp_modem_dte_t *esp_dte = __containerof(dte, esp_modem_dte_t, parent);
    return esp_event_handler_register_with(esp_dte->event_loop_hdl, ESP_MODEM_EVENT, event_id, handler, handler_args);
}

esp_err_t esp_modem_remove_event_handler(modem_dte_t *dte, esp_event_handler_t handler)
{
    esp_modem_dte_t *esp_dte = __containerof(dte, esp_modem_dte_t, parent);
    return esp_event_handler_unregister_with(esp_dte->event_loop_hdl, ESP_MODEM_EVENT, ESP_EVENT_ANY_ID, handler);
}

esp_err_t esp_modem_start_ppp(modem_dte_t *dte)
{
    modem_dce_t *dce = dte->dce;
    MODEM_CHECK(dce, "DTE has not yet bind with DCE", err);
    esp_modem_dte_t *esp_dte = __containerof(dte, esp_modem_dte_t, parent);
    /* Set PDP Context */
    ESP_LOGI(MODEM_TAG, "APN: %s", CONFIG_COMPONENT_MODEM_APN);
    MODEM_CHECK(dce->define_pdp_context(dce, 1, "IP", CONFIG_COMPONENT_MODEM_APN) == ESP_OK, "set MODEM APN failed", err);
    /* Enter PPP mode */
    MODEM_CHECK(dte->change_mode(dte, MODEM_PPP_MODE) == ESP_OK, "enter ppp mode failed", err);

    /* post PPP mode started event */
    esp_event_post_to(esp_dte->event_loop_hdl, ESP_MODEM_EVENT, ESP_MODEM_EVENT_PPP_START, NULL, 0, 0);
    return ESP_OK;
err:
    return ESP_FAIL;
}

esp_err_t esp_modem_start_cmux(modem_dte_t *dte) 
{
    modem_dce_t *dce = dte->dce;
    MODEM_CHECK(dce, "DTE has not yet bind with DCE", err);

    /* Enter cmux mode */
    MODEM_CHECK(dte->change_mode(dte, MODEM_CMUX_MODE) == ESP_OK, "enter command mode failed", err);

    return ESP_OK;
err:
    return ESP_FAIL;
}

esp_err_t esp_modem_stop_ppp(modem_dte_t *dte)
{
    modem_dce_t *dce = dte->dce;
    MODEM_CHECK(dce, "DTE has not yet bind with DCE", err);
    esp_modem_dte_t *esp_dte = __containerof(dte, esp_modem_dte_t, parent);

    /* post PPP mode stopped event */
    esp_event_post_to(esp_dte->event_loop_hdl, ESP_MODEM_EVENT, ESP_MODEM_EVENT_PPP_STOP, NULL, 0, 0);
    /* Enter command mode */
    MODEM_CHECK(dte->change_mode(dte, MODEM_COMMAND_MODE) == ESP_OK, "enter command mode failed", err);
    /* Hang up */
    MODEM_CHECK(dce->hang_up(dce) == ESP_OK, "hang up failed", err);
    return ESP_OK;
err:
    return ESP_FAIL;
}
