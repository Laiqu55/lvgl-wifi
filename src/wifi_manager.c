/*
 * wifi_manager.c — WiFi management via wpa_cli
 *
 * All wireless operations are performed by shelling out to wpa_cli,
 * which communicates with a running wpa_supplicant daemon over its
 * Unix-domain control socket.
 *
 * Dependency on Busybox udhcpc for DHCP lease acquisition.
 */

#include "wifi_manager.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/** Shell out to cmd, capture stdout into buf (up to buf_len-1 bytes).
 *  Returns the process exit status, or -1 on fork/popen failure. */
static int run_cmd(const char *cmd, char *buf, size_t buf_len)
{
    FILE *fp = popen(cmd, "r");
    if (!fp)
        return -1;

    size_t total = 0;
    if (buf && buf_len > 1) {
        total = fread(buf, 1, buf_len - 1, fp);
        buf[total] = '\0';
    }

    int rc = pclose(fp);
    return (rc == -1) ? -1 : WEXITSTATUS(rc);
}

/** Return 1 if the wpa_supplicant control socket for WIFI_IFACE exists. */
static int wpa_supplicant_running(void)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", WPA_CTRL_DIR, WIFI_IFACE);
    struct stat st;
    return (stat(path, &st) == 0) ? 1 : 0;
}

/** Create a minimal wpa_supplicant.conf if one does not already exist. */
static void ensure_wpa_conf(void)
{
    struct stat st;
    if (stat(WPA_CONF_PATH, &st) == 0)
        return; /* already exists */

    FILE *fp = fopen(WPA_CONF_PATH, "w");
    if (!fp)
        return;

    fprintf(fp,
            "ctrl_interface=%s\n"
            "ctrl_interface_group=0\n"
            "update_config=1\n"
            "country=CN\n",
            WPA_CTRL_DIR);
    fclose(fp);
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

int wifi_init(void)
{
    /* Bring the interface up */
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "ip link set %s up 2>/dev/null", WIFI_IFACE);
    system(cmd);

    ensure_wpa_conf();

    if (!wpa_supplicant_running()) {
        /* Start wpa_supplicant in background */
        snprintf(cmd, sizeof(cmd),
                 "wpa_supplicant -B -i %s -c %s -D nl80211,wext 2>/dev/null",
                 WIFI_IFACE, WPA_CONF_PATH);
        if (system(cmd) != 0) {
            /* nl80211 may have failed; try wext only */
            snprintf(cmd, sizeof(cmd),
                     "wpa_supplicant -B -i %s -c %s -D wext 2>/dev/null",
                     WIFI_IFACE, WPA_CONF_PATH);
            system(cmd);
        }
        sleep(1); /* give the daemon time to initialise */
    }

    return wpa_supplicant_running() ? 0 : -1;
}

/* ------------------------------------------------------------------ */

/**
 * Parse one data line from `wpa_cli scan_results`.
 *
 * Format (tab-separated):
 *   bssid  frequency  signal_level  flags  ssid
 *
 * The SSID (5th field) may contain spaces, so we take everything after
 * the 4th tab character.
 */
static int parse_scan_line(const char *line, wifi_ap_t *ap)
{
    char buf[256];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Split by tabs; SSID is everything past the 4th tab */
    char *fields[5];
    char *ptr = buf;
    int   i;

    for (i = 0; i < 4; i++) {
        fields[i] = ptr;
        char *tab = strchr(ptr, '\t');
        if (!tab)
            return -1;
        *tab = '\0';
        ptr  = tab + 1;
    }
    fields[4] = ptr; /* SSID */

    /* Strip trailing newline / CR from SSID */
    size_t len = strlen(fields[4]);
    while (len > 0 && (fields[4][len - 1] == '\n' || fields[4][len - 1] == '\r'))
        fields[4][--len] = '\0';

    strncpy(ap->bssid,  fields[0], WIFI_MAX_BSSID_LEN - 1);
    ap->bssid[WIFI_MAX_BSSID_LEN - 1] = '\0';

    ap->frequency_mhz = atoi(fields[1]);
    ap->signal_dbm    = atoi(fields[2]);

    strncpy(ap->flags, fields[3], WIFI_MAX_FLAGS_LEN - 1);
    ap->flags[WIFI_MAX_FLAGS_LEN - 1] = '\0';

    strncpy(ap->ssid, fields[4], WIFI_MAX_SSID_LEN - 1);
    ap->ssid[WIFI_MAX_SSID_LEN - 1] = '\0';

    ap->encrypted = (strstr(ap->flags, "WPA") != NULL ||
                     strstr(ap->flags, "WEP") != NULL) ? 1 : 0;

    return 0;
}

int wifi_scan(wifi_info_t *info)
{
    if (!info)
        return -1;

    info->ap_count = 0;

    char cmd[256];
    char buf[4096];

    /* Trigger the scan */
    snprintf(cmd, sizeof(cmd),
             "wpa_cli -i %s scan 2>/dev/null", WIFI_IFACE);
    run_cmd(cmd, buf, sizeof(buf));

    /* Wait for the driver to complete the scan (typically 2–3 s) */
    sleep(3);

    /* Retrieve results */
    snprintf(cmd, sizeof(cmd),
             "wpa_cli -i %s scan_results 2>/dev/null", WIFI_IFACE);
    if (run_cmd(cmd, buf, sizeof(buf)) != 0)
        return -1;

    /* Parse line by line */
    char *saveptr;
    char *line = strtok_r(buf, "\n", &saveptr);
    while (line && info->ap_count < WIFI_MAX_AP_COUNT) {
        /* Skip the "Selected interface" line and the header line */
        if (strncmp(line, "Selected interface", 18) == 0 ||
            strncmp(line, "bssid",              5)  == 0) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        /* Skip empty / whitespace-only lines */
        if (strspn(line, " \t\r\n") == strlen(line)) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        wifi_ap_t ap;
        memset(&ap, 0, sizeof(ap));
        if (parse_scan_line(line, &ap) == 0) {
            /* Skip entries with empty SSID (hidden networks) */
            if (ap.ssid[0] != '\0') {
                info->ap_list[info->ap_count++] = ap;
            }
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }

    return 0;
}

/* ------------------------------------------------------------------ */

int wifi_connect(const char *ssid, const char *password)
{
    if (!ssid || ssid[0] == '\0')
        return -1;

    char cmd[512];
    char buf[256];

    /* Remove all previously saved networks so we start clean */
    snprintf(cmd, sizeof(cmd),
             "wpa_cli -i %s remove_network all 2>/dev/null", WIFI_IFACE);
    run_cmd(cmd, NULL, 0);

    /* Add a new network entry */
    snprintf(cmd, sizeof(cmd),
             "wpa_cli -i %s add_network 2>/dev/null", WIFI_IFACE);
    run_cmd(cmd, buf, sizeof(buf));

    /* The network id is the first token on the last meaningful line */
    int netid = 0;
    {
        char *p = buf;
        /* Skip "Selected interface" preamble if present */
        char *nl = strrchr(buf, '\n');
        if (nl && nl > buf) {
            /* Walk back past the trailing newline */
            *nl = '\0';
            char *prev_nl = strrchr(buf, '\n');
            p = prev_nl ? (prev_nl + 1) : buf;
        }
        netid = atoi(p);
    }

    /* Set SSID (must be double-quoted inside the shell argument) */
    snprintf(cmd, sizeof(cmd),
             "wpa_cli -i %s set_network %d ssid '\"%s\"' 2>/dev/null",
             WIFI_IFACE, netid, ssid);
    run_cmd(cmd, NULL, 0);

    if (password && password[0] != '\0') {
        /* WPA/WPA2 – set the pre-shared key */
        snprintf(cmd, sizeof(cmd),
                 "wpa_cli -i %s set_network %d psk '\"%s\"' 2>/dev/null",
                 WIFI_IFACE, netid, password);
        run_cmd(cmd, NULL, 0);
    } else {
        /* Open network */
        snprintf(cmd, sizeof(cmd),
                 "wpa_cli -i %s set_network %d key_mgmt NONE 2>/dev/null",
                 WIFI_IFACE, netid);
        run_cmd(cmd, NULL, 0);
    }

    /* Enable and select the network */
    snprintf(cmd, sizeof(cmd),
             "wpa_cli -i %s enable_network %d 2>/dev/null",
             WIFI_IFACE, netid);
    run_cmd(cmd, NULL, 0);

    snprintf(cmd, sizeof(cmd),
             "wpa_cli -i %s select_network %d 2>/dev/null",
             WIFI_IFACE, netid);
    run_cmd(cmd, NULL, 0);

    /* Wait up to 10 s for the association to complete */
    int connected = 0;
    for (int i = 0; i < 10 && !connected; i++) {
        sleep(1);
        snprintf(cmd, sizeof(cmd),
                 "wpa_cli -i %s status 2>/dev/null | grep -c 'wpa_state=COMPLETED'",
                 WIFI_IFACE);
        run_cmd(cmd, buf, sizeof(buf));
        if (atoi(buf) > 0)
            connected = 1;
    }

    if (!connected)
        return -1;

    /* Obtain IP address via DHCP (prefer udhcpc, fall back to dhclient) */
    snprintf(cmd, sizeof(cmd),
             "udhcpc -i %s -q -n 2>/dev/null || dhclient %s 2>/dev/null",
             WIFI_IFACE, WIFI_IFACE);
    system(cmd);

    /* Save the configuration so it survives reboot */
    snprintf(cmd, sizeof(cmd),
             "wpa_cli -i %s save_config 2>/dev/null", WIFI_IFACE);
    run_cmd(cmd, NULL, 0);

    return 0;
}

/* ------------------------------------------------------------------ */

int wifi_disconnect(void)
{
    char cmd[256];

    /* Release DHCP lease */
    snprintf(cmd, sizeof(cmd),
             "udhcpc -i %s -k 2>/dev/null; killall udhcpc 2>/dev/null",
             WIFI_IFACE);
    system(cmd);

    /* Disconnect via wpa_cli */
    snprintf(cmd, sizeof(cmd),
             "wpa_cli -i %s disconnect 2>/dev/null", WIFI_IFACE);
    run_cmd(cmd, NULL, 0);

    /* Flush the IP address */
    snprintf(cmd, sizeof(cmd),
             "ip addr flush dev %s 2>/dev/null", WIFI_IFACE);
    system(cmd);

    return 0;
}

/* ------------------------------------------------------------------ */

int wifi_get_status(wifi_info_t *info)
{
    if (!info)
        return -1;

    info->state = WIFI_STATE_UNKNOWN;
    info->connected_ssid[0] = '\0';
    info->ip_addr[0]        = '\0';

    char cmd[256];
    char buf[1024];

    snprintf(cmd, sizeof(cmd),
             "wpa_cli -i %s status 2>/dev/null", WIFI_IFACE);
    if (run_cmd(cmd, buf, sizeof(buf)) != 0)
        return -1;

    /* Parse key=value pairs */
    char *saveptr;
    char *line = strtok_r(buf, "\n", &saveptr);
    while (line) {
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            const char *key = line;
            const char *val = eq + 1;

            /* Strip trailing CR */
            size_t vlen = strlen(val);
            /* Use a mutable copy for stripping */
            char vbuf[256];
            strncpy(vbuf, val, sizeof(vbuf) - 1);
            vbuf[sizeof(vbuf) - 1] = '\0';
            while (vlen > 0 &&
                   (vbuf[vlen - 1] == '\r' || vbuf[vlen - 1] == '\n'))
                vbuf[--vlen] = '\0';

            if (strcmp(key, "wpa_state") == 0) {
                if (strcmp(vbuf, "COMPLETED") == 0)
                    info->state = WIFI_STATE_CONNECTED;
                else if (strcmp(vbuf, "ASSOCIATING")  == 0 ||
                         strcmp(vbuf, "AUTHENTICATING") == 0 ||
                         strcmp(vbuf, "4WAY_HANDSHAKE") == 0 ||
                         strcmp(vbuf, "GROUP_HANDSHAKE") == 0)
                    info->state = WIFI_STATE_CONNECTING;
                else if (strcmp(vbuf, "DISCONNECTED") == 0 ||
                         strcmp(vbuf, "INACTIVE")     == 0 ||
                         strcmp(vbuf, "SCANNING")     == 0)
                    info->state = WIFI_STATE_DISCONNECTED;
            } else if (strcmp(key, "ssid") == 0) {
                strncpy(info->connected_ssid, vbuf,
                        WIFI_MAX_SSID_LEN - 1);
                info->connected_ssid[WIFI_MAX_SSID_LEN - 1] = '\0';
            } else if (strcmp(key, "ip_address") == 0) {
                strncpy(info->ip_addr, vbuf, WIFI_MAX_IP_LEN - 1);
                info->ip_addr[WIFI_MAX_IP_LEN - 1] = '\0';
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    /* If wpa_state reported CONNECTED but no IP yet, it's still connecting */
    if (info->state == WIFI_STATE_CONNECTED && info->ip_addr[0] == '\0')
        info->state = WIFI_STATE_CONNECTING;

    return 0;
}

/* ------------------------------------------------------------------ */

const char *wifi_signal_str(int signal_dbm)
{
    if (signal_dbm >= -55) return "Excellent";
    if (signal_dbm >= -65) return "Good";
    if (signal_dbm >= -75) return "Fair";
    return "Weak";
}

int wifi_signal_bars(int signal_dbm)
{
    if (signal_dbm >= -55) return 4;
    if (signal_dbm >= -65) return 3;
    if (signal_dbm >= -75) return 2;
    return 1;
}
