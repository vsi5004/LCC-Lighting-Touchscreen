/**
 * @file lcc_node.cpp
 * @brief LCC/OpenMRN Node Implementation
 * 
 * Implements the OpenMRN/LCC stack initialization and event production.
 * Reads node ID from SD card, initializes TWAI hardware, and provides
 * event production for lighting control.
 * 
 * @see docs/ARCHITECTURE.md §5 for OpenMRN Integration
 * @see docs/SPEC.md FR-002 for initialization requirements
 */

#include "lcc_node.h"
#include "lcc_config.hxx"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_vfs.h"

#include "openlcb/SimpleStack.hxx"
#include "openlcb/SimpleNodeInfoDefs.hxx"
#include "openlcb/ConfiguredProducer.hxx"
#include "openlcb/ConfigUpdateFlow.hxx"
#include "utils/ConfigUpdateListener.hxx"
#include "freertos_drivers/esp32/Esp32HardwareTwai.hxx"
#include "utils/format_utils.hxx"

static const char *TAG = "lcc_node";

namespace {

/// LCC node status
static lcc_status_t s_status = LCC_STATUS_UNINITIALIZED;

/// Node ID read from SD card
static openlcb::NodeID s_node_id = 0;

/// Default node ID if SD card read fails (should be unique per device!)
static constexpr openlcb::NodeID DEFAULT_NODE_ID = 0x050101012260ULL;

/// TWAI hardware driver instance
static Esp32HardwareTwai *s_twai = nullptr;

/// OpenMRN CAN stack instance (dynamically allocated)
static openlcb::SimpleCanStack *s_stack = nullptr;

/// Configuration definition instance (dynamically allocated to avoid static init issues)
static openlcb::ConfigDef *s_cfg = nullptr;

/// Cached base event ID (read from config at startup, updated on config changes)
static uint64_t s_base_event_id = openlcb::DEFAULT_BASE_EVENT_ID;

/// Cached auto-apply enabled setting
static bool s_auto_apply_enabled = true;

/// Cached auto-apply duration in seconds
static uint16_t s_auto_apply_duration_sec = openlcb::DEFAULT_AUTO_APPLY_DURATION_SEC;

/// Cached screen timeout in seconds
static uint16_t s_screen_timeout_sec = openlcb::DEFAULT_SCREEN_TIMEOUT_SEC;

/// Config file path
static std::string s_config_path;

/**
 * @brief Parse a node ID from a string
 * 
 * Accepts formats like:
 *   "05.01.01.01.22.60"
 *   "050101012260"
 *   "0x050101012260"
 * 
 * @param str String to parse
 * @param out_id Output node ID
 * @return true if parsing succeeded
 */
static bool parse_node_id(const char *str, openlcb::NodeID *out_id)
{
    if (!str || !out_id) {
        return false;
    }

    // Skip whitespace
    while (*str && (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r')) {
        str++;
    }

    // Try dotted hex format first: "05.01.01.01.22.60"
    unsigned int bytes[6];
    if (sscanf(str, "%02x.%02x.%02x.%02x.%02x.%02x",
               &bytes[0], &bytes[1], &bytes[2], 
               &bytes[3], &bytes[4], &bytes[5]) == 6) {
        *out_id = ((uint64_t)bytes[0] << 40) |
                  ((uint64_t)bytes[1] << 32) |
                  ((uint64_t)bytes[2] << 24) |
                  ((uint64_t)bytes[3] << 16) |
                  ((uint64_t)bytes[4] << 8) |
                  ((uint64_t)bytes[5]);
        return true;
    }

    // Try hex string format: "050101012260" or "0x050101012260"
    char *endptr;
    uint64_t val = strtoull(str, &endptr, 16);
    if (endptr != str && val != 0) {
        *out_id = val;
        return true;
    }

    return false;
}

/**
 * @brief Read node ID from file on SD card
 * 
 * @param path Path to node ID file
 * @param out_id Output node ID
 * @return true if read succeeded
 */
static bool read_node_id_from_file(const char *path, openlcb::NodeID *out_id)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGW(TAG, "Node ID file not found: %s", path);
        return false;
    }

    FILE *file = fopen(path, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open node ID file: %s", path);
        return false;
    }

    char buf[64];
    size_t read_size = fread(buf, 1, sizeof(buf) - 1, file);
    fclose(file);

    if (read_size == 0) {
        ESP_LOGE(TAG, "Empty node ID file");
        return false;
    }
    buf[read_size] = '\0';

    if (!parse_node_id(buf, out_id)) {
        ESP_LOGE(TAG, "Invalid node ID format in file: %s", buf);
        return false;
    }

    ESP_LOGI(TAG, "Read node ID from file: %012llx", (unsigned long long)*out_id);
    return true;
}

/**
 * @brief Create default node ID file on SD card
 */
static void create_default_nodeid_file(const char *path)
{
    ESP_LOGI(TAG, "Creating default nodeid.txt with node ID: %012llx", 
             (unsigned long long)DEFAULT_NODE_ID);
    
    FILE *file = fopen(path, "w");
    if (!file) {
        ESP_LOGE(TAG, "Failed to create nodeid.txt");
        return;
    }

    // Write in dotted hex format
    fprintf(file, "%02X.%02X.%02X.%02X.%02X.%02X\n",
            (unsigned)((DEFAULT_NODE_ID >> 40) & 0xFF),
            (unsigned)((DEFAULT_NODE_ID >> 32) & 0xFF),
            (unsigned)((DEFAULT_NODE_ID >> 24) & 0xFF),
            (unsigned)((DEFAULT_NODE_ID >> 16) & 0xFF),
            (unsigned)((DEFAULT_NODE_ID >> 8) & 0xFF),
            (unsigned)(DEFAULT_NODE_ID & 0xFF));
    
    fclose(file);
    ESP_LOGI(TAG, "Created nodeid.txt");
}

/**
 * @brief Configuration update listener
 * 
 * Handles config changes and factory reset
 */
class LccConfigListener : public DefaultConfigUpdateListener
{
public:
    UpdateAction apply_configuration(
        int fd, bool initial_load, BarrierNotifiable *done) override
    {
        AutoNotify n(done);
        
        // Read base event ID from config
        uint64_t new_base_event_id = s_cfg->seg().lighting().base_event_id().read(fd);
        
        if (new_base_event_id != s_base_event_id) {
            ESP_LOGI(TAG, "Base event ID changed: %016llx -> %016llx",
                     (unsigned long long)s_base_event_id,
                     (unsigned long long)new_base_event_id);
            s_base_event_id = new_base_event_id;
        }
        
        // Read startup configuration
        uint8_t auto_apply_val = s_cfg->seg().startup().auto_apply_enabled().read(fd);
        s_auto_apply_enabled = (auto_apply_val != 0);
        
        s_auto_apply_duration_sec = s_cfg->seg().startup().auto_apply_duration_sec().read(fd);
        
        // Read screen timeout configuration
        s_screen_timeout_sec = s_cfg->seg().startup().screen_timeout_sec().read(fd);
        
        if (initial_load) {
            ESP_LOGI(TAG, "Startup config: auto_apply=%s, duration=%u sec, screen_timeout=%u sec",
                     s_auto_apply_enabled ? "enabled" : "disabled",
                     s_auto_apply_duration_sec,
                     s_screen_timeout_sec);
        }
        
        // IMPORTANT: Sync config file to SD card after any changes
        // FAT filesystem doesn't auto-flush, so without this, changes made
        // via JMRI/LCC configuration would be lost on reboot
        if (!initial_load) {
            ESP_LOGI(TAG, "Config changed - syncing to SD card");
            fsync(fd);
        }
        
        return UPDATED;
    }

    void factory_reset(int fd) override
    {
        ESP_LOGI(TAG, "Factory reset - restoring defaults");
        
        // Set default user info
        s_cfg->userinfo().name().write(fd, "LCC Lighting Controller");
        s_cfg->userinfo().description().write(fd, "ESP32-S3 Touch LCD Scene Controller");
        
        // Set default startup config
        s_cfg->seg().startup().auto_apply_enabled().write(fd, 1);
        s_cfg->seg().startup().auto_apply_duration_sec().write(fd, openlcb::DEFAULT_AUTO_APPLY_DURATION_SEC);
        s_cfg->seg().startup().screen_timeout_sec().write(fd, openlcb::DEFAULT_SCREEN_TIMEOUT_SEC);
        s_auto_apply_enabled = true;
        s_auto_apply_duration_sec = openlcb::DEFAULT_AUTO_APPLY_DURATION_SEC;
        s_screen_timeout_sec = openlcb::DEFAULT_SCREEN_TIMEOUT_SEC;
        
        // Set default base event ID
        s_cfg->seg().lighting().base_event_id().write(fd, openlcb::DEFAULT_BASE_EVENT_ID);
        s_base_event_id = openlcb::DEFAULT_BASE_EVENT_ID;
        
        // Sync to SD card
        fsync(fd);
    }
};

/// Config listener instance (dynamically allocated to avoid static init issues)
static LccConfigListener *s_config_listener = nullptr;

} // anonymous namespace

/// Path to the configuration file on SD card
static const char LCC_CONFIG_FILE[] = "/sdcard/openmrn_config";

// ============================================================================
// OpenMRN required external symbols
// These must be in the openlcb namespace for OpenMRN to find them
// ============================================================================

namespace openlcb {

/// SNIP static data - manufacturer info reported to other nodes
extern const SimpleNodeStaticValues SNIP_STATIC_DATA = {
    4,                                    // version
    "IvanBuilds",                         // manufacturer_name (41 chars max)
    "LCC Touchscreen Controller",         // model_name (41 chars max)
    "ESP32S3 TouchLCD 4.3",                   // hardware_version (21 chars max)
    "1.0.0"                               // software_version (21 chars max)
};

/// CDI XML data - defines the configuration interface for this node
const char CDI_DATA[] =
    R"xmldata(<?xml version="1.0"?>
<cdi xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="http://openlcb.org/schema/cdi/1/1/cdi.xsd">
<identification>
  <manufacturer>IvanBuilds</manufacturer>
  <model>LCC Touchscreen Controller</model>
  <hardwareVersion>Waveshare ESP32-S3 Touch LCD 4.3B</hardwareVersion>
  <softwareVersion>1.0.0</softwareVersion>
</identification>
<acdi/>
<segment space="251" origin="1">
  <group>
    <name>User Info</name>
    <string size="63"><name>User Name</name></string>
    <string size="64"><name>User Description</name></string>
  </group>
</segment>
<segment space="253" origin="132">
  <group>
    <name>Lighting Configuration</name>
    <eventid>
      <name>Base Event ID</name>
      <description>Base event ID for lighting commands. The last two bytes encode parameter type and value. Default: 05.01.01.01.22.60.00.00</description>
    </eventid>
  </group>
</segment>
</cdi>)xmldata";

/// Configuration file path
const char *const CONFIG_FILENAME = LCC_CONFIG_FILE;

/// Size of the configuration file (computed from ConfigDef layout)
/// ConfigDef::size() gives the total size, offset() isn't static so we use a fixed value
const size_t CONFIG_FILE_SIZE = ConfigDef::size() + 128;

/// SNIP user data file (same as config file)
const char *const SNIP_DYNAMIC_FILENAME = LCC_CONFIG_FILE;

} // namespace openlcb

extern "C" {

esp_err_t lcc_node_init(const lcc_config_t *config)
{
    if (s_status != LCC_STATUS_UNINITIALIZED) {
        ESP_LOGW(TAG, "LCC node already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    s_status = LCC_STATUS_INITIALIZING;

    lcc_config_t cfg;
    if (config) {
        cfg = *config;
    } else {
        cfg = LCC_CONFIG_DEFAULT();
    }

    ESP_LOGI(TAG, "Initializing LCC node...");
    ESP_LOGI(TAG, "  Node ID file: %s", cfg.nodeid_path);
    ESP_LOGI(TAG, "  Config file: %s", cfg.config_path);
    ESP_LOGI(TAG, "  TWAI RX: GPIO%d, TX: GPIO%d", cfg.twai_rx_gpio, cfg.twai_tx_gpio);

    // Save config path for later
    s_config_path = cfg.config_path;

    // Read node ID from SD card
    if (!read_node_id_from_file(cfg.nodeid_path, &s_node_id)) {
        ESP_LOGW(TAG, "Using default node ID: %012llx", (unsigned long long)DEFAULT_NODE_ID);
        s_node_id = DEFAULT_NODE_ID;
        
        // Create the file with default ID so user can edit it
        create_default_nodeid_file(cfg.nodeid_path);
    }

    ESP_LOGI(TAG, "Node ID: %012llx", (unsigned long long)s_node_id);

    // Allocate ConfigDef (must be done before using config)
    s_cfg = new openlcb::ConfigDef(0);

    // Initialize TWAI hardware
    ESP_LOGI(TAG, "Initializing TWAI hardware...");
    s_twai = new Esp32HardwareTwai(
        cfg.twai_rx_gpio,  // RX pin
        cfg.twai_tx_gpio,  // TX pin
        true               // Enable statistics reporting
    );
    s_twai->hw_init();
    ESP_LOGI(TAG, "TWAI hardware initialized");

    // Create OpenMRN stack (must be done BEFORE creating config listener)
    ESP_LOGI(TAG, "Creating OpenMRN stack...");
    s_stack = new openlcb::SimpleCanStack(s_node_id);
    
    // Now we can create the config listener (it registers with ConfigUpdateService
    // which is created by SimpleCanStack)
    s_config_listener = new LccConfigListener();
    
    // Create config file if needed (this also handles factory reset)
    ESP_LOGI(TAG, "Checking config file...");
    
    // We need to create the config file before starting the stack
    int config_fd = s_stack->create_config_file_if_needed(
        s_cfg->seg().internal_config(),
        openlcb::CANONICAL_VERSION,
        openlcb::CONFIG_FILE_SIZE
    );
    
    if (config_fd < 0) {
        ESP_LOGE(TAG, "Failed to create/open config file");
        s_status = LCC_STATUS_ERROR;
        return ESP_FAIL;
    }

    // IMPORTANT: Sync config file to SD card - FAT filesystem doesn't auto-flush
    // This is needed because OpenMRN's factory_reset writes don't call fsync()
    ESP_LOGI(TAG, "Syncing config file to SD card...");
    fsync(config_fd);

    // Debug: log the actual offsets being used
    ESP_LOGI(TAG, "Config offsets - userinfo.name: %d, userinfo.desc: %d, lighting.base_event_id: %d",
             (int)s_cfg->userinfo().name().offset(),
             (int)s_cfg->userinfo().description().offset(),
             (int)s_cfg->seg().lighting().base_event_id().offset());

    // Read initial base event ID from config
    s_base_event_id = s_cfg->seg().lighting().base_event_id().read(config_fd);
    ESP_LOGI(TAG, "Base event ID: %016llx", (unsigned long long)s_base_event_id);

    // Add CAN port using select-based API (works with ESP-IDF VFS)
    ESP_LOGI(TAG, "Adding CAN port...");
    s_stack->add_can_port_select("/dev/twai/twai0");

    // Start the executor thread
    ESP_LOGI(TAG, "Starting executor thread...");
    s_stack->start_executor_thread("lcc_exec", 5, 4096);

    s_status = LCC_STATUS_RUNNING;
    ESP_LOGI(TAG, "LCC node initialized and running");

    return ESP_OK;
}

lcc_status_t lcc_node_get_status(void)
{
    return s_status;
}

uint64_t lcc_node_get_node_id(void)
{
    return s_node_id;
}

uint64_t lcc_node_get_base_event_id(void)
{
    return s_base_event_id;
}

bool lcc_node_get_auto_apply_enabled(void)
{
    return s_auto_apply_enabled;
}

uint16_t lcc_node_get_auto_apply_duration_sec(void)
{
    return s_auto_apply_duration_sec;
}

uint16_t lcc_node_get_screen_timeout_sec(void)
{
    return s_screen_timeout_sec;
}

esp_err_t lcc_node_send_lighting_event(uint8_t parameter, uint8_t value)
{
    if (s_status != LCC_STATUS_RUNNING || !s_stack) {
        ESP_LOGW(TAG, "LCC node not running");
        return ESP_ERR_INVALID_STATE;
    }

    if (parameter > 4) {
        ESP_LOGE(TAG, "Invalid parameter index: %d", parameter);
        return ESP_ERR_INVALID_ARG;
    }

    // Construct event ID: base_event_id with parameter in byte 6 and value in byte 7
    // Base: XX.XX.XX.XX.XX.XX.00.00
    // Result: XX.XX.XX.XX.XX.XX.PP.VV
    uint64_t event_id = (s_base_event_id & 0xFFFFFFFFFFFF0000ULL) |
                        ((uint64_t)parameter << 8) |
                        ((uint64_t)value);

    ESP_LOGD(TAG, "Sending event: %016llx (param=%d, value=%d)",
             (unsigned long long)event_id, parameter, value);

    s_stack->send_event(event_id);

    return ESP_OK;
}

void lcc_node_shutdown(void)
{
    if (s_status == LCC_STATUS_UNINITIALIZED) {
        return;
    }

    ESP_LOGI(TAG, "Shutting down LCC node...");
    
    // Note: OpenMRN doesn't have a clean shutdown mechanism for the executor
    // In practice, this would only be called at device shutdown
    
    s_status = LCC_STATUS_UNINITIALIZED;
    
    // Don't delete the stack or TWAI - they don't support clean shutdown
    // and the device is likely resetting anyway
}

} // extern "C"
