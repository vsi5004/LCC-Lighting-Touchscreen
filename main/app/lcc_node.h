/**
 * @file lcc_node.h
 * @brief LCC/OpenMRN Node Interface (C-compatible header)
 * 
 * Provides the C interface for OpenMRN/LCC node initialization and operations.
 * The implementation is in C++ but this header can be included from C code.
 * 
 * @see docs/ARCHITECTURE.md §5 for OpenMRN Integration
 * @see docs/SPEC.md FR-002 for initialization requirements
 */

#ifndef LCC_NODE_H_
#define LCC_NODE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief LCC Node status
 */
typedef enum {
    LCC_STATUS_UNINITIALIZED = 0,
    LCC_STATUS_INITIALIZING,
    LCC_STATUS_RUNNING,
    LCC_STATUS_ERROR,
} lcc_status_t;

/**
 * @brief LCC initialization configuration
 */
typedef struct {
    const char *nodeid_path;        /**< Path to node ID file on SD card */
    const char *config_path;        /**< Path to config file (for OpenMRN EEPROM emulation) */
    int twai_rx_gpio;               /**< TWAI RX GPIO pin */
    int twai_tx_gpio;               /**< TWAI TX GPIO pin */
} lcc_config_t;

/**
 * @brief Default LCC configuration
 */
#define LCC_CONFIG_DEFAULT() { \
    .nodeid_path = "/sdcard/nodeid.txt", \
    .config_path = "/sdcard/lcc_config.bin", \
    .twai_rx_gpio = 16, \
    .twai_tx_gpio = 15, \
}

/**
 * @brief Initialize the LCC node
 * 
 * Reads node ID from SD card, initializes TWAI (CAN) hardware,
 * and starts the OpenMRN stack.
 * 
 * @param config Configuration options
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t lcc_node_init(const lcc_config_t *config);

/**
 * @brief Get the current LCC node status
 * 
 * @return Current status of the LCC node
 */
lcc_status_t lcc_node_get_status(void);

/**
 * @brief Get the node ID
 * 
 * @return 48-bit node ID, or 0 if not initialized
 */
uint64_t lcc_node_get_node_id(void);

/**
 * @brief Get the configured base event ID
 * 
 * @return 64-bit base event ID
 */
uint64_t lcc_node_get_base_event_id(void);

/**
 * @brief Get auto-apply first scene on boot setting
 * 
 * @return true if auto-apply is enabled, false otherwise
 */
bool lcc_node_get_auto_apply_enabled(void);

/**
 * @brief Get auto-apply transition duration in seconds
 * 
 * @return Duration in seconds (0-300)
 */
uint16_t lcc_node_get_auto_apply_duration_sec(void);

/**
 * @brief Send a lighting parameter event
 * 
 * Constructs an event ID from base_event_id + parameter offset + value
 * and sends it to the LCC bus.
 * 
 * @param parameter Parameter index (0=Red, 1=Green, 2=Blue, 3=White, 4=Brightness)
 * @param value Parameter value (0-255)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t lcc_node_send_lighting_event(uint8_t parameter, uint8_t value);

/**
 * @brief Shutdown the LCC node
 */
void lcc_node_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // LCC_NODE_H_
