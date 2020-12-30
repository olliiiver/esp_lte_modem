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
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_types.h"
#include "esp_err.h"
#include "esp_modem_dte.h"

typedef struct modem_dce modem_dce_t;
typedef struct modem_dte modem_dte_t;

/**
 * @brief Result Code from DCE
 *
 */
#define MODEM_RESULT_CODE_SUCCESS "OK"              /*!< Acknowledges execution of a command */
#define MODEM_RESULT_CODE_CONNECT "CONNECT"         /*!< A connection has been established */
#define MODEM_RESULT_CODE_RING "RING"               /*!< Detect an incoming call signal from network */
#define MODEM_RESULT_CODE_NO_CARRIER "NO CARRIER"   /*!< Connection termincated or establish a connection failed */
#define MODEM_RESULT_CODE_ERROR "ERROR"             /*!< Command not recognized, command line maximum length exceeded, parameter value invalid */
#define MODEM_RESULT_CODE_NO_DIALTONE "NO DIALTONE" /*!< No dial tone detected */
#define MODEM_RESULT_CODE_BUSY "BUSY"               /*!< Engaged signal detected */
#define MODEM_RESULT_CODE_NO_ANSWER "NO ANSWER"     /*!< Wait for quiet answer */

/**
 * @brief Specific Length Constraint
 *
 */
#define MODEM_MAX_NAME_LENGTH (32)     /*!< Max Module Name Length */
#define MODEM_MAX_OPERATOR_LENGTH (32) /*!< Max Operator Name Length */
#define MODEM_IMEI_LENGTH (15)         /*!< IMEI Number Length */
#define MODEM_IMSI_LENGTH (15)         /*!< IMSI Number Length */

/**
 * @brief Specific Timeout Constraint, Unit: millisecond
 *
 */
#define MODEM_COMMAND_TIMEOUT_DEFAULT (1500)     /*!< Default timeout value for most commands */
#define MODEM_COMMAND_TIMEOUT_OPERATOR (75000)   /*!< Timeout value for getting operator status */
#define MODEM_COMMAND_TIMEOUT_MODE_CHANGE (5000) /*!< Timeout value for changing working mode */
#define MODEM_COMMAND_TIMEOUT_HANG_UP (90000)    /*!< Timeout value for hang up */
#define MODEM_COMMAND_TIMEOUT_POWEROFF (1000)    /*!< Timeout value for power down */

/**
 * @brief Working state of DCE
 *
 */
typedef enum {
    MODEM_STATE_PROCESSING, /*!< In processing */
    MODEM_STATE_SUCCESS,    /*!< Process successfully */
    MODEM_STATE_FAIL        /*!< Process failed */
} modem_state_t;

/* CRC8 is the reflected CRC8/ROHC algorithm */
#define FCS_POLYNOMIAL 0xe0 /* reversed crc8 */
#define FCS_INIT_VALUE 0xFF
#define FCS_GOOD_VALUE 0xCF

#define EA 0x01  /* Extension bit      */
#define CR 0x02  /* Command / Response */
#define PF 0x10  /* Poll / Final       */

/* Frame types */
#define FT_RR      0x01  /* Receive Ready                            */
#define FT_UI      0x03  /* Unnumbered Information                   */
#define FT_RNR     0x05  /* Receive Not Ready                        */
#define FT_REJ     0x09  /* Reject                                   */
#define FT_DM      0x0F  /* Disconnected Mode                        */
#define FT_SABM    0x2F  /* Set Asynchronous Balanced Mode           */
#define FT_DISC    0x43  /* Disconnect                               */
#define FT_UA      0x63  /* Unnumbered Acknowledgement               */
#define FT_UIH     0xEF  /* Unnumbered Information with Header check */

/* Control channel commands */
#define CMD_NSC    0x08  /* Non Supported Command Response           */
#define CMD_TEST   0x10  /* Test Command                             */
#define CMD_PSC    0x20  /* Power Saving Control                     */
#define CMD_RLS    0x28  /* Remote Line Status Command               */
#define CMD_FCOFF  0x30  /* Flow Control Off Command                 */
#define CMD_PN     0x40  /* DLC parameter negotiation                */
#define CMD_RPN    0x48  /* Remote Port Negotiation Command          */
#define CMD_FCON   0x50  /* Flow Control On Command                  */
#define CMD_CLD    0x60  /* Multiplexer close down                   */
#define CMD_SNC    0x68  /* Service Negotiation Command              */
#define CMD_MSC    0x70  /* Modem Status Command                     */

/* Flag sequence field between messages (start of frame) */
#define SOF_MARKER 0xF9

/**
 * @brief DCE(Data Communication Equipment)
 *
 */
struct modem_dce {
    char imei[MODEM_IMEI_LENGTH + 1];                                                 /*!< IMEI number */
    char imsi[MODEM_IMSI_LENGTH + 1];                                                 /*!< IMSI number */
    char name[MODEM_MAX_NAME_LENGTH];                                                 /*!< Module name */
    char oper[MODEM_MAX_OPERATOR_LENGTH];                                             /*!< Operator name */
    bool needpin;
    modem_state_t state;                                                              /*!< Modem working state */
    modem_mode_t mode;                                                                /*!< Working mode */
    modem_dte_t *dte;                                                                 /*!< DTE which connect to DCE */
    esp_err_t (*handle_line)(modem_dce_t *dce, const char *line);                     /*!< Handle line strategy */
    esp_err_t (*handle_cmux_frame)(modem_dce_t *dce, const char *frame);              /*!< Handle line strategy */
    esp_err_t (*sync)(modem_dce_t *dce);                                              /*!< Synchronization */
    esp_err_t (*echo_mode)(modem_dce_t *dce, bool on);                                /*!< Echo command on or off */
    esp_err_t (*store_profile)(modem_dce_t *dce);                                     /*!< Store user settings */
    esp_err_t (*set_flow_ctrl)(modem_dce_t *dce, modem_flow_ctrl_t flow_ctrl);        /*!< Flow control on or off */
    esp_err_t (*get_signal_quality)(modem_dce_t *dce, uint32_t *rssi, uint32_t *ber); /*!< Get signal quality */
    esp_err_t (*get_battery_status)(modem_dce_t *dce, uint32_t *bcs,
                                    uint32_t *bcl, uint32_t *voltage);  /*!< Get battery status */
    esp_err_t (*define_pdp_context)(modem_dce_t *dce, uint32_t cid,
                                    const char *type, const char *apn); /*!< Set PDP Contex */
    esp_err_t (*set_working_mode)(modem_dce_t *dce, modem_mode_t mode); /*!< Set working mode */
    esp_err_t (*hang_up)(modem_dce_t *dce);                             /*!< Hang up */
    esp_err_t (*power_down)(modem_dce_t *dce);                          /*!< Normal power down */
    esp_err_t (*deinit)(modem_dce_t *dce);                              /*!< Deinitialize */
    esp_err_t (*setup_cmux)(modem_dce_t *dce);                              /*!< Setup CMUX */
};

#ifdef __cplusplus
}
#endif
