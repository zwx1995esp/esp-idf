#include "esp_ieee802154.h"

__attribute__((weak)) void esp_ieee802154_test_receive_done(uint8_t *data, esp_ieee802154_frame_info_t *frame_info)
{

}

__attribute__((weak)) void esp_ieee802154_test_receive_sfd_done(void)
{

}

__attribute__((weak)) void esp_ieee802154_test_transmit_done(const uint8_t *frame, const uint8_t *ack, esp_ieee802154_frame_info_t *ack_frame_info)
{

}

__attribute__((weak)) void esp_ieee802154_test_transmit_failed(const uint8_t *frame, esp_ieee802154_tx_error_t error)
{

}

__attribute__((weak)) void esp_ieee802154_test_transmit_sfd_done(uint8_t *frame)
{

}


__attribute__((weak)) void esp_ieee802154_test_cca_done(bool channel_free)
{

}

__attribute__((weak)) void esp_ieee802154_test_energy_detect_done(int8_t power)
{

}


__attribute__((weak)) void esp_ieee802154_ot_receive_done(uint8_t *data, esp_ieee802154_frame_info_t *frame_info)
{

}

__attribute__((weak)) void esp_ieee802154_ot_receive_sfd_done(void)
{

}

__attribute__((weak)) void esp_ieee802154_ot_transmit_done(const uint8_t *frame, const uint8_t *ack, esp_ieee802154_frame_info_t *ack_frame_info)
{

}

__attribute__((weak)) void esp_ieee802154_ot_transmit_failed(const uint8_t *frame, esp_ieee802154_tx_error_t error)
{

}

__attribute__((weak)) void esp_ieee802154_ot_transmit_sfd_done(uint8_t *frame)
{

}


__attribute__((weak)) void esp_ieee802154_ot_cca_done(bool channel_free)
{

}

__attribute__((weak)) void esp_ieee802154_ot_energy_detect_done(int8_t power)
{

}





