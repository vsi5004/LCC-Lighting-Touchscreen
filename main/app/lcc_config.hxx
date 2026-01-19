/**
 * @file lcc_config.hxx
 * @brief LCC/OpenMRN CDI Configuration Definition
 * 
 * Defines the Configuration Description Information (CDI) for this node.
 * The base_event_id is configurable via LCC memory configuration protocol.
 * 
 * @see docs/SPEC.md §3 for LCC Event Model
 * @see docs/ARCHITECTURE.md §5 for OpenMRN Integration
 */

#ifndef LCC_CONFIG_HXX_
#define LCC_CONFIG_HXX_

#include "openlcb/ConfigRepresentation.hxx"
#include "openlcb/MemoryConfig.hxx"

namespace openlcb
{

/// Configuration version. Increment when making incompatible changes.
static constexpr uint16_t CANONICAL_VERSION = 0x0002;

/// Default base event ID: 05.01.01.01.22.60.00.00
static constexpr uint64_t DEFAULT_BASE_EVENT_ID = 0x0501010122600000ULL;

/// Default auto-apply duration in seconds
static constexpr uint16_t DEFAULT_AUTO_APPLY_DURATION_SEC = 10;

/// Default screen timeout in seconds (0 = disabled)
static constexpr uint16_t DEFAULT_SCREEN_TIMEOUT_SEC = 60;

/// CDI segment for startup behavior settings
CDI_GROUP(StartupConfig);

/// Enable auto-apply of first scene on boot
CDI_GROUP_ENTRY(auto_apply_enabled, Uint8ConfigEntry,
    Name("Auto-Apply First Scene on Boot"),
    Description("When enabled (1), automatically applies the first scene in the "
                "scene list after startup. Assumes initial state is all LEDs off. "
                "Set to 0 to disable."),
    Default(1),
    Min(0),
    Max(1));

/// Auto-apply transition duration in seconds
CDI_GROUP_ENTRY(auto_apply_duration_sec, Uint16ConfigEntry,
    Name("Auto-Apply Transition Duration (seconds)"),
    Description("Duration in seconds for the automatic scene transition at startup. "
                "Range: 0-300 seconds. Default: 10 seconds."),
    Default(DEFAULT_AUTO_APPLY_DURATION_SEC),
    Min(0),
    Max(300));

/// Screen timeout for power saving
CDI_GROUP_ENTRY(screen_timeout_sec, Uint16ConfigEntry,
    Name("Screen Backlight Timeout (seconds)"),
    Description("Time in seconds before the screen backlight turns off when idle. "
                "Touch the screen to wake. Set to 0 to disable (always on). "
                "Range: 0 or 10-3600 seconds. Default: 60 seconds."),
    Default(DEFAULT_SCREEN_TIMEOUT_SEC),
    Min(0),
    Max(3600));

CDI_GROUP_END();

/// CDI segment for lighting controller settings
CDI_GROUP(LightingConfig);

/// Base Event ID for lighting commands
/// Format: 05.01.01.01.22.60.0x.00 where x selects the parameter
CDI_GROUP_ENTRY(base_event_id, EventConfigEntry,
    Name("Base Event ID"),
    Description("Base event ID for lighting commands. The last two bytes "
                "encode parameter type and value. Default: 05.01.01.01.22.60.00.00"));

CDI_GROUP_END();

/// Main CDI segment containing all user-configurable options
/// Laid out at origin 128 to give space for the ACDI user data at the beginning.
CDI_GROUP(LccConfigSegment, Segment(MemoryConfigDefs::SPACE_CONFIG), Offset(128));

/// Internal configuration data (version info for factory reset)
CDI_GROUP_ENTRY(internal_config, InternalConfigData);

/// Startup configuration
CDI_GROUP_ENTRY(startup, StartupConfig, Name("Startup Behavior"));

/// Lighting configuration
CDI_GROUP_ENTRY(lighting, LightingConfig, Name("Lighting Configuration"));

CDI_GROUP_END();

/// The complete CDI definition for this node
CDI_GROUP(ConfigDef, MainCdi());

/// Standard identification section (populated from SNIP_STATIC_DATA)
CDI_GROUP_ENTRY(ident, Identification);

/// Standard ACDI section
CDI_GROUP_ENTRY(acdi, Acdi);

/// User info segment (standard SNIP user-editable fields)
CDI_GROUP_ENTRY(userinfo, UserInfoSegment);

/// Main configuration segment
CDI_GROUP_ENTRY(seg, LccConfigSegment);

CDI_GROUP_END();

} // namespace openlcb

#endif // LCC_CONFIG_HXX_
