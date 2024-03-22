/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The radio state types.
 */
typedef enum {
    ESP_IEEE802154_RADIO_DISABLE,   /*!< Radio not up */
    ESP_IEEE802154_RADIO_IDLE,      /*!< Radio in the idle state */
    ESP_IEEE802154_RADIO_SLEEP,     /*!< Radio in the sleep state */
    ESP_IEEE802154_RADIO_RECEIVE,   /*!< Radio in the receive state */
    ESP_IEEE802154_RADIO_TRANSMIT,  /*!< Radio in the transmit state */
} esp_ieee802154_state_t;

/**
 * @brief The transmit error types.
 */
typedef enum {
    ESP_IEEE802154_TX_ERR_NONE,         /*!< No transmit error */
    ESP_IEEE802154_TX_ERR_CCA_BUSY,     /*!< Channel is busy */
    ESP_IEEE802154_TX_ERR_ABORT,        /*!< Transmit abort */
    ESP_IEEE802154_TX_ERR_NO_ACK,       /*!< No Ack frame received until timeout */
    ESP_IEEE802154_TX_ERR_INVALID_ACK,  /*!< Invalid Ack frame */
    ESP_IEEE802154_TX_ERR_COEXIST,      /*!< Rejected by coexist system */
    ESP_IEEE802154_TX_ERR_SECURITY,     /*!< Invalid security configuration */
} esp_ieee802154_tx_error_t;

/**
 * @brief The CCA mode types.
 */
typedef enum {
    ESP_IEEE802154_CCA_MODE_CARRIER,         /*!< Carrier only */
    ESP_IEEE802154_CCA_MODE_ED,              /*!< Energy Detect only */
    ESP_IEEE802154_CCA_MODE_CARRIER_OR_ED,   /*!< Carrier or Energy Detect */
    ESP_IEEE802154_CCA_MODE_CARRIER_AND_ED,  /*!< Carrier and Energy Detect */
} esp_ieee802154_cca_mode_t;

/**
 * @brief The frame pending mode types.
 */
typedef enum {
    ESP_IEEE802154_AUTO_PENDING_DISABLE,   /*!< Frame pending bit always set to 1 in the ack to Data Request */
    ESP_IEEE802154_AUTO_PENDING_ENABLE,    /*!< Frame pending bit set to 1 if src address matches, in the ack to Data Request */
    ESP_IEEE802154_AUTO_PENDING_ENHANCED,  /*!< Frame pending bit set to 1 if src address matches, in all ack frames */
    ESP_IEEE802154_AUTO_PENDING_ZIGBEE,    /*!< Frame pending bit set to 0 only if src address is short address and matches in table, in the ack to Data Request */
} esp_ieee802154_pending_mode_t;

/**
* @brief The four groups of mac filter interface index.
*/
typedef enum {
    ESP_IEEE802154_MULTIPAN_0 = 0,
    ESP_IEEE802154_MULTIPAN_1 = 1,
    ESP_IEEE802154_MULTIPAN_2 = 2,
    ESP_IEEE802154_MULTIPAN_3 = 3,
    ESP_IEEE802154_MULTIPAN_MAX
} esp_ieee802154_multipan_index_t;

/**
 * @brief The information of received 15.4 frame.
 *
 */
typedef struct {
    bool pending;                /*!< The frame was acked with frame pending set */
    bool process;                /*!< The frame needs to be processed by the upper layer */
    uint8_t channel;             /*!< Channel */
    int8_t rssi;                 /*!< RSSI */
    uint8_t lqi;                 /*!< LQI */
    uint64_t timestamp;          /*!< The timestamp when the frame's SFD field was received */
} esp_ieee802154_frame_info_t;

typedef enum {
    ESP_IEEE802154_RADIO_ACTION_TX,
    ESP_IEEE802154_RADIO_ACTION_TX_AT,
    ESP_IEEE802154_RADIO_ACTION_RX,
    ESP_IEEE802154_RADIO_ACTION_ED,
    ESP_IEEE802154_RADIO_ACTION_CCA,
    ESP_IEEE802154_RADIO_ACTION_SLEEP,
    ESP_IEEE802154_RADIO_ACTION_TX_DONE,
    ESP_IEEE802154_RADIO_ACTION_TX_FAILED,
    ESP_IEEE802154_RADIO_ACTION_RX_DONE,
    ESP_IEEE802154_RADIO_ACTION_ED_DONE,
    ESP_IEEE802154_RADIO_ACTION_CCA_DONE,
} esp_ieee802154_mpf_radio_action_t;

typedef enum {
    ESP_IEEE802154_RADIO_STATE_IDLE,
    ESP_IEEE802154_RADIO_STATE_SLEEP,
    ESP_IEEE802154_RADIO_STATE_RX,
    ESP_IEEE802154_RADIO_STATE_ED,
    ESP_IEEE802154_RADIO_STATE_CCA,
    ESP_IEEE802154_RADIO_STATE_TX,
} esp_ieee802154_mpf_radio_state_t;


typedef void (*receive_done_cb_t)(uint8_t *data, esp_ieee802154_frame_info_t *frame_info);
typedef void (*receive_sfd_done_cb_t)(void);
typedef void (*receive_failed_cb_t)(uint16_t error);
typedef void (*transmit_done_cb_t)(const uint8_t *frame, const uint8_t *ack, esp_ieee802154_frame_info_t *ack_frame_info);
typedef void (*transmit_failed_cb_t)(const uint8_t *frame, esp_ieee802154_tx_error_t error);
typedef void (*transmit_sfd_done_cb_t)(uint8_t *frame);
typedef void (*cca_done_cb_t)(bool channel_free);
typedef void (*energy_detect_done_cb_t)(int8_t power);
typedef void (*ed_failed_cb_t)(uint16_t error);
typedef esp_err_t (*enh_ack_generator_cb_t)(uint8_t *frame, esp_ieee802154_frame_info_t *frame_info, uint8_t* enhack_frame);

typedef struct {
    receive_done_cb_t rx_done_cb;
    receive_sfd_done_cb_t rx_sfd_done_cb;
    receive_failed_cb_t rx_failed_cb;
    transmit_done_cb_t tx_done_cb;
    transmit_failed_cb_t tx_failed_cb;
    transmit_sfd_done_cb_t tx_sfd_done_cb;
    cca_done_cb_t cca_done_cb;
    energy_detect_done_cb_t ed_done_cb;
    ed_failed_cb_t ed_failed_cb;
    enh_ack_generator_cb_t enh_ack_generator_cb;
} esp_ieee802154_mpf_cb_list_t;

typedef struct {
    bool pending_tx_cca;
    bool pending_tx_start;
    bool pending_ed;
    bool pending_cca;
    bool rx_on_when_idle;
    bool is_receiving;
    bool is_transmiting;
    bool promiscuous;
    bool is_ed_processing;
    bool is_cca_processing;
    const uint8_t *pending_tx_frame;
    int8_t tx_power;
    uint8_t channel;
    uint32_t pending_ed_duration;
    uint32_t pending_tx_start_time;
    esp_ieee802154_mpf_cb_list_t cb_list;
    bool frame_under_process[CONFIG_IEEE802154_RX_BUFFER_SIZE];
} esp_ieee802154_mpf_if_attr_t;

#ifdef __cplusplus
}
#endif
