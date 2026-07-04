/*
 * wifi_manager.h — WiFi management interface
 *
 * Uses wpa_supplicant / wpa_cli for all wireless operations.
 * Designed for Linux (Buildroot 2019, kernel 5.4.61).
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of APs returned from a single scan */
#define WIFI_MAX_AP_COUNT   32

/* Buffer sizes (including NUL terminator) */
#define WIFI_MAX_SSID_LEN   33
#define WIFI_MAX_BSSID_LEN  18
#define WIFI_MAX_PASS_LEN   64
#define WIFI_MAX_IP_LEN     16
#define WIFI_MAX_FLAGS_LEN  64

/* Wireless interface name – override with -DWIFI_IFACE=\"wlan1\" if needed */
#ifndef WIFI_IFACE
#define WIFI_IFACE  "wlan0"
#endif

/* Default wpa_supplicant configuration file path */
#ifndef WPA_CONF_PATH
#define WPA_CONF_PATH  "/etc/wpa_supplicant.conf"
#endif

/* wpa_supplicant control socket directory */
#ifndef WPA_CTRL_DIR
#define WPA_CTRL_DIR  "/var/run/wpa_supplicant"
#endif

/* ------------------------------------------------------------------ */
/* Data types                                                           */
/* ------------------------------------------------------------------ */

/** Connection state of the wireless interface */
typedef enum {
    WIFI_STATE_UNKNOWN       = 0,  /**< State not yet queried          */
    WIFI_STATE_DISCONNECTED,       /**< Interface up, not associated   */
    WIFI_STATE_CONNECTING,         /**< Association in progress        */
    WIFI_STATE_CONNECTED,          /**< Fully connected (IP obtained)  */
    WIFI_STATE_ERROR               /**< wpa_supplicant error           */
} wifi_state_t;

/** Information about a single access point */
typedef struct {
    char ssid[WIFI_MAX_SSID_LEN];      /**< Network name (NUL-terminated)  */
    char bssid[WIFI_MAX_BSSID_LEN];    /**< MAC address (xx:xx:xx:xx:xx:xx) */
    char flags[WIFI_MAX_FLAGS_LEN];    /**< Raw wpa_cli flags field         */
    int  signal_dbm;                   /**< Signal level in dBm            */
    int  frequency_mhz;               /**< Channel frequency in MHz        */
    int  encrypted;                    /**< 1 = requires password           */
} wifi_ap_t;

/** Global WiFi information struct (filled by scan / status calls) */
typedef struct {
    wifi_ap_t    ap_list[WIFI_MAX_AP_COUNT]; /**< Array of discovered APs    */
    int          ap_count;                   /**< Number of entries in ap_list*/
    wifi_state_t state;                      /**< Current connection state   */
    char         connected_ssid[WIFI_MAX_SSID_LEN]; /**< SSID if connected   */
    char         ip_addr[WIFI_MAX_IP_LEN];           /**< IP if connected    */
} wifi_info_t;

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/**
 * @brief  Bring up the WiFi interface and start wpa_supplicant.
 *
 * Creates a minimal WPA_CONF_PATH if it does not exist.
 * Safe to call multiple times.
 *
 * @return 0 on success, -1 on failure.
 */
int wifi_init(void);

/**
 * @brief  Trigger a passive scan and populate info->ap_list.
 *
 * Blocks for up to ~4 seconds while the scan completes.
 *
 * @param[out] info  Struct to fill; ap_count is set to the number of results.
 * @return 0 on success, -1 on failure.
 */
int wifi_scan(wifi_info_t *info);

/**
 * @brief  Connect to a WiFi network.
 *
 * For open networks, pass NULL (or empty string) for password.
 * Issues udhcpc after a successful association to obtain an IP.
 *
 * @param[in] ssid      Target network name.
 * @param[in] password  Passphrase, or NULL / "" for open networks.
 * @return 0 on success, -1 on failure.
 */
int wifi_connect(const char *ssid, const char *password);

/**
 * @brief  Disconnect from the current network.
 *
 * Terminates the DHCP lease and disassociates.
 *
 * @return 0 on success, -1 on failure.
 */
int wifi_disconnect(void);

/**
 * @brief  Query the current connection state.
 *
 * Fills info->state, info->connected_ssid, and info->ip_addr.
 *
 * @param[out] info  Struct to fill.
 * @return 0 on success, -1 on failure.
 */
int wifi_get_status(wifi_info_t *info);

/**
 * @brief  Return a human-readable signal strength label.
 *
 * @param signal_dbm  Signal level in dBm.
 * @return Static string: "Excellent", "Good", "Fair", or "Weak".
 */
const char *wifi_signal_str(int signal_dbm);

/**
 * @brief  Return the number of signal bars (1–4) for UI rendering.
 *
 * @param signal_dbm  Signal level in dBm.
 * @return Integer in [1, 4].
 */
int wifi_signal_bars(int signal_dbm);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MANAGER_H */
