/*
 * SPDX-FileCopyrightText: 2020-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstddef>
#include <stdint.h>
#include <string.h>
#include "include/esp_ieee802154_types.h"
#include "sdkconfig.h"
#include "esp_ieee802154.h"
#include "esp_err.h"
#include "esp_phy_init.h"
#include "esp_ieee802154_ack.h"
#include "esp_ieee802154_dev.h"
#include "esp_ieee802154_frame.h"
#include "esp_ieee802154_pib.h"
#include "esp_ieee802154_sec.h"
#include "esp_ieee802154_util.h"
#include "esp_log.h"
#include "esp_coex_i154.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include "hal/ieee802154_ll.h"
#include "hal/ieee802154_common_ll.h"
#include <stdatomic.h>
atomic_bool s_i154_mpf_running;
static _lock_t s_i154_mpf_lock;
static QueueHandle_t s_i154_mpf_action_queue = NULL;
#define TASK_QUEUE_SIZE 10
#define MAX_MPF_INTERFACE_COUNT 4

static esp_ieee802154_mpf_if_attr_t s_mpf_if_attr[MAX_MPF_INTERFACE_COUNT];
static esp_ieee802154_mpf_radio_state_t s_mpf_state = ESP_IEEE802154_RADIO_STATE_IDLE;
typedef struct task_storage_t {
    esp_ieee802154_mpf_radio_action_t action;
    esp_ieee802154_multipan_index_t index;
    esp_ieee802154_mpf_process_cb mpf_process_cb;
    union {
        struct {
            const uint8_t *frame;
            bool tx_cca;
        } tx_process;
        struct {
            const uint8_t *frame;
            bool tx_cca;
            uint32_t start_time;
        } tx_at_process;

        struct {
            uint32_t duration_time;
        } ed_process;
        struct {
            uint8_t *data;
            esp_ieee802154_frame_info_t *frame_info;
        } rx_done_process;
        struct {
            uint8_t *frame;
            uint8_t *ack;
            esp_ieee802154_frame_info_t *ack_frame_info;
            esp_ieee802154_tx_error_t tx_error;
        } tx_done_process;
        struct {
            bool channel_free;
        } cca_done_process;
        struct {
            int8_t power;
            uint16_t error;
        } ed_done_process;
    } data;
} task_storage_t;

typedef void (*esp_ieee802154_mpf_process_cb)(task_storage_t radio_action);

static void ieee802154_mpf_tx_process(task_storage_t radio_action)
{
    if (s_mpf_state <= ESP_IEEE802154_RADIO_STATE_RX) {
        s_mpf_state = ESP_IEEE802154_RADIO_STATE_TX;
        s_mpf_if_attr[radio_action.index].is_transmiting = true;
        ieee802154_transmit(radio_action.data.tx_process.frame, radio_action.data.tx_process.cca);
    } else {
        // Current radio is busy, pending this TX.
        s_mpf_if_attr[radio_action.index].pending_tx_frame = radio_action.data.tx_process.frame;
        s_mpf_if_attr[radio_action.index].pending_tx_cca = radio_action.data.tx_process.cca;
    }
}

static void ieee802154_mpf_tx_at_process(task_storage_t radio_action)
{
    if (s_mpf_state <= ESP_IEEE802154_RADIO_STATE_RX) {
        s_mpf_state = ESP_IEEE802154_RADIO_STATE_TX;
        s_mpf_if_attr[radio_action.index].is_transmiting = true;
        ieee802154_transmit_at(radio_action.data.tx_at_process.frame, radio_action.data.tx_at_process.cca, radio_action.data.tx_at_process.start_time);
    } else {
        // Current radio is busy, pending this TX.
        s_mpf_if_attr[radio_action.index].pending_tx_frame = radio_action.data.tx_at_process.frame;
        s_mpf_if_attr[radio_action.index].pending_tx_cca = radio_action.data.tx_at_process.cca;
        s_mpf_if_attr[radio_action.index].pending_tx_start_time = radio_action.data.tx_at_process.start_time;
        s_mpf_if_attr[radio_action.index].pending_tx_start = true;
    }
}

static void ieee802154_mpf_ed_process(task_storage_t radio_action)
{
    if (s_mpf_state <= ESP_IEEE802154_RADIO_STATE_RX) {
        s_mpf_state = ESP_IEEE802154_RADIO_STATE_ED;
        s_mpf_if_attr[radio_action.index].is_ed_processing = true;
        ieee802154_energy_detect(radio_action.data.ed_process.duration_time);
    } else {
        // Current radio is busy, pending this ED.
        s_mpf_if_attr[radio_action.index].pending_ed = true;
        s_mpf_if_attr[radio_action.index].pending_ed_duration = radio_action.data.ed_process.duration_time
    }
}

static void ieee802154_mpf_cca_process()
{
    if (s_mpf_state <= ESP_IEEE802154_RADIO_STATE_RX) {
        s_mpf_state = ESP_IEEE802154_RADIO_STATE_CCA;
        s_mpf_if_attr[radio_action.index].is_cca_processing = true;
        ieee802154_cca();
    } else {
        // Current radio is busy, pending this CCA.
        s_mpf_if_attr[radio_action.index].pending_cca = true;
    }
}

esp_err_t esp_ieee802154_transmit(esp_ieee802154_multipan_index_t index, const uint8_t *frame, bool cca)
{
    task_storage_t task;
    task.action = ESP_IEEE802154_RADIO_ACTION_TX;
    task.mpf_process_cb = ieee802154_mpf_tx_process;
    task.index = index;
    task.data.tx_process.frame = frame;
    task.data.tx_process.tx_cca = cca;
    // Transmission operation has a high priority, add to queue head in order to process more quickly.
    xQueueSendToFront(s_i154_mpf_action_queue, &task, portMAX_DELAY);
    return ESP_OK;

}

esp_err_t esp_ieee802154_transmit_at(esp_ieee802154_multipan_index_t index, const uint8_t *frame, bool cca, uint32_t time)
{
    task_storage_t task;
    task.action = ESP_IEEE802154_RADIO_ACTION_TX_AT;
    task.mpf_process_cb = ieee802154_mpf_tx_at_process;
    task.index = index;
    task.data.tx_at_process.frame = frame;
    task.data.tx_at_process.tx_cca = cca;
    task.data.tx_at_process.start_time = time;
    // Transmission operation has a high priority, add to queue head in order to process more quickly.
    xQueueSendToFront(s_i154_mpf_action_queue, &task, portMAX_DELAY);
    return ESP_OK;
}

esp_err_t esp_ieee802154_energy_detect(esp_ieee802154_multipan_index_t index, uint32_t duration)
{
    task_storage_t task;
    task.action = ESP_IEEE802154_RADIO_ACTION_ED;
    task.mpf_process_cb = ieee802154_mpf_ed_process;
    task.index = index;
    task.data.ed_process.duration_time = duration;
    xQueueSend(s_i154_mpf_action_queue, &task, portMAX_DELAY);
    return ESP_OK;
}

esp_err_t esp_ieee802154_cca(esp_ieee802154_multipan_index_t index)
{
    task_storage_t task;
    task.action = ESP_IEEE802154_RADIO_ACTION_CCA;
    task.mpf_process_cb = ieee802154_mpf_cca_process;
    task.index = index;
    xQueueSend(s_i154_mpf_action_queue, &task, portMAX_DELAY);
    return ESP_OK;
}

esp_err_t esp_ieee802154_receive(esp_ieee802154_multipan_index_t index)
{
    _lock_acquire_recursive(&s_i154_mpf_lock);
    bool is_radio_rx_started = false;
    for (int i = 0; i < MAX_MPF_INTERFACE_COUNT; i++) {
        is_radio_rx_started = is_radio_rx_started || s_mpf_if_attr[i].is_receiving;
    }
    s_mpf_if_attr[index].is_receiving = true;
    if (!is_radio_rx_started) {
        ieee802154_receive();
    }
    _lock_release_recursive(&s_i154_mpf_lock);
}


static void ieee802154_multipan_forward_task(void *aContext)
{
    task_storage_t radio_action;
    while (xQueueReceive(s_i154_mpf_action_queue, &radio_action, portMAX_DELAY)) {
        _lock_acquire_recursive(&s_i154_mpf_lock);
        if (!atomic_load(&s_i154_mpf_running)) {
            _lock_release_recursive(&s_i154_mpf_lock);
            break;
        }
        radio_action.mpf_process_cb(radio_action);
        _lock_release_recursive(&s_i154_mpf_lock);
    }
    _lock_close(&s_i154_mpf_lock);
    vQueueDelete(s_i154_mpf_action_queue);
    esp_ieee802154_disable();
    vTaskDelete(NULL);
}

esp_err_t esp_ieee802154_enable(void)
{
    ieee802154_enable();
    ieee802154_rf_enable();
    esp_btbb_enable();
    return ieee802154_mac_init();
}

esp_err_t esp_ieee802154_disable(void)
{
    esp_btbb_disable();
    ieee802154_rf_disable();
    ieee802154_disable();
    return ieee802154_mac_deinit();
}

esp_err_t esp_ieee802154_multipan_forward_init(void)
{
    esp_ieee802154_enable();
    s_i154_mpf_action_queue = xQueueCreate(TASK_QUEUE_SIZE, sizeof(task_storage_t));
    atomic_store(&s_i154_mpf_running, true);
    xTaskCreate(ieee802154_multipan_forward_task, "i154_mpf", 4096, xTaskGetCurrentTaskHandle(), 4, NULL);
}

void esp_ieee802154_multipan_forward_deinit(void)
{
    atomic_store(&s_i154_mpf_running, false);
}

esp_err_t esp_ieee802154_mpf_callback_list_register(esp_ieee802154_multipan_index_t index, esp_ieee802154_mpf_cb_list_t cb_list)
{

    memcpy((void*)(&s_mpf_if_attr[index].cb_list), (void*)(&cb_list), sizeof(esp_ieee802154_mpf_cb_list_t));

}

static bool is_broadcast_panid_addr(uint8_t *dest_addr, uint8_t addr_mode, uint8_t *dest_panid, bool is_dest_panid_present)
{
    uint8_t target[IEEE802154_FRAME_EXT_ADDR_SIZE] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    if (is_dest_panid_present == true) {
        if (!(dest_panid[0] == 0xff && dest_panid[1] == 0xff)) {
            return false;
        }
    }
    if (addr_mode == IEEE802154_FRAME_DST_MODE_NONE) {
        return true;
    } else {
        size_t addr_size = (addr_mode == IEEE802154_FRAME_DST_MODE_SHORT) ? IEEE802154_FRAME_SHORT_ADDR_SIZE : IEEE802154_FRAME_EXT_ADDR_SIZE;
        if (memcmp(dest_panid, target, addr_size) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_panid_addr_matched_interface(uint8_t *dest_addr, uint8_t addr_mode, uint8_t *dest_panid, bool is_dest_panid_present, esp_ieee802154_multipan_index_t index)
{
    if (is_dest_panid_present == true) {
        uint16_t panid = dest_panid[1];
        panid = (panid << 8) | dest_panid[0];
        if (panid != esp_ieee802154_get_multipan_panid(index)) {
            return false;
        }
    }
    if (addr_mode == IEEE802154_FRAME_DST_MODE_SHORT) {
        uint16_t short_addr = dest_addr[1];
        short_addr = (short_addr << 8) | dest_addr[0];
        if (short_addr != esp_ieee802154_get_multipan_short_address(index)) {
            return false;
        } else {
            return true;
        }
    } else if (addr_mode == IEEE802154_FRAME_DST_MODE_EXT) {
        uint8_t ext_addr[IEEE802154_FRAME_EXT_ADDR_SIZE] = {0};
        esp_ieee802154_get_multipan_extended_address(index, ext_addr);
        if (memcpy(dest_addr, ext_addr, IEEE802154_FRAME_EXT_ADDR_SIZE) != 0) {
            return false;
        } else {
            return true;
        }
    }
    return false;
}

static void ieee802154_mpf_next_operation()
{
    if (s_mpf_state > ESP_IEEE802154_RADIO_STATE_RX) {
        return;
    }
    // Tx has a high priority, transmit the pending packets first:
    for (int index = 0; index < MAX_MPF_INTERFACE_COUNT; index++) {
        if (s_mpf_if_attr[index].pending_tx_frame != NULL) {
            if (s_mpf_if_attr[index].pending_tx_start) {
                // Tx start at
                ieee802154_transmit_at(s_mpf_if_attr[index].pending_tx_frame, s_mpf_if_attr[index].pending_tx_cca, s_mpf_if_attr[index].pending_tx_start_time);
            } else {
                // Normal Tx
                ieee802154_transmit(s_mpf_if_attr[index].pending_tx_frame, s_mpf_if_attr[index].pending_tx_cca);
            }
            s_mpf_if_attr[index].pending_tx_frame = NULL;
            s_mpf_if_attr[index].pending_tx_cca = false;
            s_mpf_if_attr[index].pending_tx_start_time = 0;
            s_mpf_if_attr[index].pending_tx_start = false;
            s_mpf_state = ESP_IEEE802154_RADIO_STATE_TX;
            s_mpf_if_attr[index].is_transmiting = true;
            return;
        }
    }

    // Check ED
    bool need_process_ed = false;
    uint32_t duration = 0;
    for (int index = 0; index < MAX_MPF_INTERFACE_COUNT; index++) {
        if (s_mpf_if_attr[index].pending_ed) {
            if (!need_process_ed) {
                need_process_ed = true;
                duration = s_mpf_if_attr[index].pending_ed_duration;
                s_mpf_if_attr[index].pending_ed = false;
                s_mpf_if_attr[index].pending_ed_duration = 0;
                s_mpf_if_attr[index].is_ed_processing = true;
            } else {
                if (duration == s_mpf_if_attr[index].pending_ed_duration) {
                    s_mpf_if_attr[index].pending_ed = false;
                    s_mpf_if_attr[index].pending_ed_duration = 0;
                    s_mpf_if_attr[index].is_ed_processing = true;
                }
            }
        }
    }
    if (need_process_ed) {
        ieee802154_energy_detect(duration);
        s_mpf_state = ESP_IEEE802154_RADIO_STATE_ED;
    }

    // Check CCA
    bool need_process_cca = false;
    for (int index = 0; index < MAX_MPF_INTERFACE_COUNT; index++) {
        if (s_mpf_if_attr[index].pending_cca) {
            need_process_cca = true;
            s_mpf_if_attr[index].pending_cca = false;
            s_mpf_if_attr[index].is_cca_processing = true;
        }
    }
    if (need_process_cca) {
        ieee802154_cca();
        s_mpf_state = ESP_IEEE802154_RADIO_STATE_CCA;
        return;
    }

    // Check Rx, do we need set rx state here? 

    return;
}

static void ieee802154_mpf_rx_done_process(task_storage_t radio_action)
{
    bool is_forwarded = false;
    bool is_dest_panid_present = false;
    uint8_t dest_addr_mode = IEEE802154_FRAME_DST_MODE_NONE;
    uint8_t dest_addr[IEEE802154_FRAME_EXT_ADDR_SIZE] = {0};
    uint8_t dest_panid[IEEE802154_FRAME_PANID_SIZE] = {0};
    bool is_raw_packet_broadcast = false;
    // Get dest addr and panid from the raw packet.
    is_dest_panid_present = (ieee802154_frame_get_dest_panid(radio_action.data.rx_done_process.data, dest_panid) == ESP_OK) ? true : false;
    dest_addr_mode = ieee802154_frame_get_dst_addr(radio_action.data.rx_done_process.data, dest_addr);
    // Check is this packet is Broadcast
    is_raw_packet_broadcast = is_broadcast_panid_addr(dest_addr, dest_addr_mode, dest_panid, is_dest_panid_present);

    // Check for each inf, if promiscuous mode is enabled or match the panid and address.
    for (int index = 0; index < MAX_MPF_INTERFACE_COUNT; index++) {
        if (!s_mpf_if_attr[index].is_receiving) {
            continue;
        }
        if (s_mpf_if_attr[index].channel != radio_action.data.rx_done_process.frame_info.channel) {
            continue;
        }
        if (is_raw_packet_broadcast || s_mpf_if_attr[index].promiscuous || \
            is_panid_addr_matched_interface(dest_addr, dest_addr_mode, dest_panid, is_dest_panid_present, (esp_ieee802154_multipan_index_t)i)) {
            // If current packet is broadcast or current inf is promiscuous enable or the address && panid matches the current inf
            // We forward the packet to current inf
            if (s_mpf_if_attr[index].cb_list.rx_done_cb) {
                is_forwarded = true;
                // Mark the packet frame pointer to under processing first.
                uint16_t frame_index = ieee802154_get_frame_index_in_receiving_table(radio_action.data.rx_done_process.data);
                s_mpf_if_attr[index].frame_under_process[frame_index] = true;

                // Forward the packet to upper layer.
                s_mpf_if_attr[index].cb_list.rx_done_cb(radio_action.data.rx_done_process.data, radio_action.data.rx_done_process.frame_info);

                // Determine the rx status
                s_mpf_if_attr[index].is_receiving = s_mpf_if_attr[index].rx_on_when_idle;
            }
        }
    }
    if (!is_forwarded) {
        // This packet is not forwarded to any inf, release the frame pointer.
        ieee802154_receive_handle_done(radio_action.data.rx_done_process.data);
    }
    // Process next operation.
    ieee802154_mpf_next_operation();
}

void esp_ieee802154_test_receive_done(uint8_t *data, esp_ieee802154_frame_info_t *frame_info)
{
    // This is an ISR context
    task_storage_t task;
    task.action = ESP_IEEE802154_RADIO_ACTION_RX_DONE;
    task.mpf_process_cb = ieee802154_mpf_rx_done_process;
    task.data.rx_done_process.data = data;
    task.data.rx_done_process.frame_info = frame_info;
    xQueueSendFromISR(s_i154_mpf_action_queue, &task, portMAX_DELAY);
    return ESP_OK;
}

static void ieee802154_mpf_tx_done_process(task_storage_t radio_action)
{
    esp_ieee802154_multipan_index_t tx_inf = ESP_IEEE802154_MULTIPAN_MAX;
    for (int i = 0; i < MAX_MPF_INTERFACE_COUNT; i++) {
        // Find which inf is sending frame;
        if (s_mpf_if_attr[i].is_transmiting) {
            tx_inf = i;
            break;
        }
    }
    assert(tx_inf == ESP_IEEE802154_MULTIPAN_MAX);
    s_mpf_if_attr[tx_inf].is_transmiting = false;
    if (radio_action.data.tx_done_process.tx_error != ESP_IEEE802154_TX_ERR_NONE) {
        // tx failed.
        s_mpf_if_attr[tx_inf].cb_list.tx_failed_cb(radio_action.data.tx_done_process.frame, radio_action.data.tx_done_process.tx_error);
    } else {
        // tx done.
        s_mpf_if_attr[tx_inf].cb_list.tx_done_cb(radio_action.data.tx_done_process.frame, radio_action.data.tx_done_process.ack, 
                                                    radio_action.data.tx_done_process.ack_frame_info);
        if (radio_action.data.tx_done_process.ack) {
            // Mark the packet frame pointer to under processing first.
            uint16_t frame_index = ieee802154_get_frame_index_in_receiving_table(radio_action.data.tx_done_process.ack);
            s_mpf_if_attr[tx_inf].frame_under_process[frame_index] = true;
        }
    }
    // Process next operation.
    ieee802154_mpf_next_operation();
}

void esp_ieee802154_transmit_done(const uint8_t *frame, const uint8_t *ack, esp_ieee802154_frame_info_t *ack_frame_info)
{
    // This is an ISR context
    task_storage_t task;
    task.mpf_process_cb = ieee802154_mpf_tx_done_process;
    task.data.tx_done_process.frame = frame;
    task.data.tx_done_process.ack = ack;
    task.data.tx_done_process.ack_frame_info = ack_frame_info;
    task.data.tx_done_process.error = ESP_IEEE802154_TX_ERR_NONE;
    xQueueSendToFrontFromISR(s_i154_mpf_action_queue, &task, portMAX_DELAY);
}

void esp_ieee802154_transmit_failed(const uint8_t *frame, esp_ieee802154_tx_error_t error)
{
    // This is an ISR context
    task_storage_t task;
    task.mpf_process_cb = ieee802154_mpf_tx_done_process;
    task.data.tx_done_process.frame = frame;
    task.data.tx_done_process.error = error;
    xQueueSendToFrontFromISR(s_i154_mpf_action_queue, &task, portMAX_DELAY);
}

static void ieee802154_mpf_cca_done_process(task_storage_t radio_action)
{
    for (int i = 0; i < MAX_MPF_INTERFACE_COUNT; i++) {
        // Find which inf is sending frame;
        if (s_mpf_if_attr[i].is_cca_processing) {
            s_mpf_if_attr[i].is_cca_processing = false;
            s_mpf_if_attr[i].cb_list.cca_done_cb(radio_action.data.cca_done_process.channel_free);
        }
    }
    // Process next operation.
    ieee802154_mpf_next_operation();
}

void esp_ieee802154_cca_done(bool channel_free)
{
    // This is an ISR context
    task_storage_t task;
    task.mpf_process_cb = ieee802154_mpf_cca_done_process;
    task.data.cca_done_process.channel_free = channel_free;
    xQueueSendToFrontFromISR(s_i154_mpf_action_queue, &task, portMAX_DELAY);
}

static void ieee802154_mpf_ed_done_process(task_storage_t radio_action)
{
    for (int i = 0; i < MAX_MPF_INTERFACE_COUNT; i++) {
        // Find which inf is sending frame;
        if (s_mpf_if_attr[i].is_cca_processing) {
            s_mpf_if_attr[i].is_cca_processing = false;
            if (radio_action.data.ed_done_process.error != 0) {
                s_mpf_if_attr[i].cb_list.ed_done_cb(radio_action.data.ed_done_process.power);
            } else {
                s_mpf_if_attr[i].cb_list.ed_failed_cb(radio_action.data.ed_done_process.error);
            }
        }
    }
    // Process next operation.
    ieee802154_mpf_next_operation();
}

void esp_ieee802154_energy_detect_done(int8_t power)
{
    // This is an ISR context
    task_storage_t task;
    task.mpf_process_cb = ieee802154_mpf_ed_done_process;
    task.data.ed_done_process.power = power;
    task.data.ed_done_process.error = 0;
    xQueueSendToFrontFromISR(s_i154_mpf_action_queue, &task, portMAX_DELAY);
}

void esp_ieee802154_ed_failed(uint16_t error)
{
    // This is an ISR context
    task_storage_t task;
    task.mpf_process_cb = ieee802154_mpf_ed_done_process;
    task.data.ed_done_process.power = 0;
    task.data.ed_done_process.error = error;
    xQueueSendToFrontFromISR(s_i154_mpf_action_queue, &task, portMAX_DELAY);
}