// --------------------------------------------------------------------------
//
// Project       IoT - Internet of Things
//
// File          user_http.c
//
// Author        Axel Werner
//
// --------------------------------------------------------------------------
// Changelog
//
//    2018-01-19  AWe   replace httpd_printf() with ESP_LOG*()
//    2017-08-24  AWe   replace
//                      resetTimer      --> staCheckTimer
//                      resetTimerCb    --> staCheckConnStatus
//                      reassTimerCb    --> cgiWiFiDoConnectCb
//
//                      reformat some parts
//                      shrink some buffers for sprintf()
//    2017-08-23  AWe   take over changes from MightyPork/libesphttpd
//                        https:// github.com/MightyPork/libesphttpd/commit/3237c6f8fb9fd91b22980116b89768e1ca21cf66
//
// --------------------------------------------------------------------------

#ifdef ESP32

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http:// mozilla.org/MPL/2.0/. */

/*
Cgi/template routines for the /wifi url.
*/

#define LOG_LOCAL_LEVEL    ESP_LOG_DEBUG
#include "esp_log.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event_loop.h"
#include "esp_wifi.h"

#include "libesphttpd/esp.h"
#include "libesphttpd/cgiwifi.h"
#include "httpd-platform.h"

static const char *TAG = "esp32_cgiWifi";

// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------

extern EventGroupHandle_t wifi_event_group;
extern const int SCAN_DONE_BIT;
extern const int CONNECTED_BIT;
extern const int DISCONNECTED_BIT;


// Enable this to disallow any changes in AP settings
// #define DEMO_MODE

#define GOOD_IP2STR(ip) ((ip)>>0)&0xff, ((ip)>>8)&0xff,  ((ip)>>16)&0xff, ((ip)>>24)&0xff

// Scan result
typedef struct
{
   char scanInProgress; // if 1, don't access the underlying stuff from the webpage.
   wifi_ap_record_t *apData;
   int noAps;
} ScanResultData;

// Static scan status storage.
static ScanResultData cgiWifiAps  = { 0 };

#define CONNTRY_IDLE       0
#define CONNTRY_WORKING    1
#define CONNTRY_SUCCESS    2
#define CONNTRY_FAIL       3

// Connection result var
static int connTryStatus = CONNTRY_IDLE;
static HttpdPlatTimerHandle staCheckTimer;

// Temp store for new ap info.
static wifi_config_t wifi_config = { 0 };

// --------------------------------------------------------------------------
// taken from MightyPork/libesphttpd
// --------------------------------------------------------------------------

int ICACHE_FLASH_ATTR rssi2perc( int rssi )
{
   int r;

   if( rssi > 200 )
      r = 100;
   else if( rssi < 100 )
      r = 0;
   else
      r = 100 - 2 * ( 200 - rssi ); // approx.

   if( r > 100 ) r = 100;
   if( r < 0 ) r = 0;

   return r;
}

const ICACHE_FLASH_ATTR char *auth2str( int auth )
{
   switch( auth )
   {
      case WIFI_AUTH_OPEN:
         return "Open";
      case WIFI_AUTH_WEP:
         return "WEP";
      case WIFI_AUTH_WPA_PSK:
         return "WPA";
      case WIFI_AUTH_WPA2_PSK:
         return "WPA2";
      case WIFI_AUTH_WPA_WPA2_PSK:
         return "WPA/WPA2";
      default:
         return "Unknown";
   }
}

const ICACHE_FLASH_ATTR char *opmode2str( int opmode )
{
   switch( opmode )
   {
      case WIFI_MODE_NULL:
         return "Disabled";
      case WIFI_MODE_STA:
         return "Client";
      case WIFI_MODE_AP:
         return "AP only";
      case WIFI_MODE_APSTA:
         return "Client+AP";
      default:
         return "Unknown";
   }
}

// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------

// Callback the code calls when a wlan ap scan is done. Basically stores the result in
// the cgiWifiAps struct.

void ICACHE_FLASH_ATTR wifiScanDone( void )
{
   ESP_LOGD( TAG, "wifiScanDone" );

   // Clear prev ap data if needed.
   if( cgiWifiAps.apData != NULL )
      free( cgiWifiAps.apData );

   // get the list of APs found in the last scan
   uint16_t ap_num;             // amount of access points found.
   esp_wifi_scan_get_ap_num( &ap_num );

   // Allocate memory for access point data

   wifi_ap_record_t* ap_records = ( wifi_ap_record_t * )malloc( sizeof( wifi_ap_record_t ) * ap_num );
   cgiWifiAps.apData = ap_records;
   if( ap_records == NULL )
   {
      ESP_LOGE( TAG, "Out of memory allocating apData\n" );
      return;
   }
   cgiWifiAps.noAps = ap_num;
   ESP_LOGD( TAG, "Scan done: found %d APs", ap_num );

   // Copy access point data to the static struct
   esp_wifi_scan_get_ap_records( &ap_num, ap_records );

   // We're done.
   cgiWifiAps.scanInProgress = 0;

   // print the list
   ESP_LOGD( TAG, "Found %d access points:", ap_num );
   ESP_LOGD( TAG, "" );
   ESP_LOGD( TAG, "               SSID              | Channel | RSSI |   Auth Mode " );
   ESP_LOGD( TAG, "----------------------------------------------------------------" );
   for( int i = 0; i < ap_num; i++ )
      ESP_LOGD( TAG, "%32s | %7d | %4d | %12s",
                ( char * )ap_records[i].ssid,
                ap_records[i].primary,
                ap_records[i].rssi,
                auth2str( ap_records[i].authmode ) );
   ESP_LOGD( TAG, "----------------------------------------------------------------" );
}

// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------

// Routine to start a WiFi access point scan.
static void ICACHE_FLASH_ATTR wifiStartScan()
{
   if( cgiWifiAps.scanInProgress ) return;

   cgiWifiAps.scanInProgress = 1;

   // configure and run the scan process in nonblocking mode
   wifi_scan_config_t scan_config =
   {
      .ssid = 0,
      .bssid = 0,
      .channel = 0,
      .show_hidden = true,
      .scan_type = WIFI_SCAN_TYPE_ACTIVE,
      .scan_time.active =
      {
         .min = 0,
         .max = 0
      }
   };

   ESP_LOGD( TAG, "Start Scanning ..." );
   ESP_ERROR_CHECK( esp_wifi_scan_start( &scan_config, false ) );
}

// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------

// This CGI is called from the bit of AJAX-code in wifi.tpl. It will initiate a
// scan for access points and if available will return the result of an earlier scan.
// The result is embedded in a bit of JSON parsed by the javascript in wifi.tpl.

CgiStatus ICACHE_FLASH_ATTR cgiWiFiScan( HttpdConnData *connData )
{
   int pos = ( int )connData->cgiData;
   int len;
   char buff[256];

   if( !cgiWifiAps.scanInProgress && pos > 0 )
   {
//      ESP_LOGD( TAG, "Fill in json code for an access point" );
      // Fill in json code for an access point
      if( pos <= cgiWifiAps.noAps )
      {
         int rssi = cgiWifiAps.apData[pos - 1].rssi;

         len = sprintf( buff, "{\"essid\": \"%s\", \"bssid\": \"" MACSTR "\", \"rssi\": \"%d\", \"rssi_perc\": \"%d\", \"enc\": \"%d\", \"channel\": \"%d\"}%s\r\n",
                        cgiWifiAps.apData[pos - 1].ssid,
                        MAC2STR( cgiWifiAps.apData[pos - 1].bssid ),
                        rssi,
                        rssi2perc( rssi ),
                        cgiWifiAps.apData[pos - 1].authmode,
                        cgiWifiAps.apData[pos - 1].primary,
                        ( pos - 1 == cgiWifiAps.noAps - 1 ) ? "\r\n  " : ",\r\n   " ); // <-terminator

         httpdSend( connData, buff, len );
//         ESP_LOGD( TAG, "%s", buff );
      }

      if( pos >= cgiWifiAps.noAps )
      {
         // terminate datas of previous scan and start a new scan
         ESP_LOGD( TAG, "Start a new scan ..." );
         len = sprintf( buff, "]\n}\n}\n" ); // terminate the whole object

         httpdSend( connData, buff, len );
//         ESP_LOGD( TAG, "%s", buff );

         // Clear prev ap data
         if( cgiWifiAps.apData != NULL )
         {
            free( cgiWifiAps.apData );
            cgiWifiAps.apData = NULL;
         }

         // Also start a new scan.
         wifiStartScan();
         connData->cgiData = ( void* )0;
         return HTTPD_CGI_DONE;
      }
      else
      {
         pos++;
         connData->cgiData = ( void* )pos;
         return HTTPD_CGI_MORE;
      }
   }
   else  // scanInProgress || pos == 0
   {
      httpdStartResponse( connData, 200 );
      httpdHeader( connData, "Content-Type", "application/json" );
      httpdEndHeaders( connData );

      if( cgiWifiAps.scanInProgress == 1 )
      {
         if( xEventGroupGetBits( wifi_event_group ) & SCAN_DONE_BIT )
         {
            wifiScanDone();
            xEventGroupClearBits( wifi_event_group, SCAN_DONE_BIT );
         }
         else
         {
            ESP_LOGD( TAG, "We're still scanning" );
            // We're still scanning. Tell Javascript code that.
            len = sprintf( buff, "{\n \"result\": { \n\"inProgress\": \"1\"\n }\n}\n" );
            httpdSend( connData, buff, len );
//            ESP_LOGD( TAG, "%s", buff );
            return HTTPD_CGI_DONE;
         }
      }

      if( cgiWifiAps.scanInProgress == 0 )
      {
         ESP_LOGD( TAG, "We have a scan result" );
         // We have a scan result. Pass it on.
         len = sprintf( buff, "{\n \"result\": { \n\"inProgress\": \"0\", \n\"APs\": [\n" );
         httpdSend( connData, buff, len );
//         ESP_LOGD( TAG, "%s", buff );
         if( cgiWifiAps.apData == NULL )
            cgiWifiAps.noAps = 0;
         connData->cgiData = ( void * )1;  // => pos: starts the scan
      }
      return HTTPD_CGI_MORE;
   }
}

// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------

// This routine is ran some time after a connection attempt to an access point. If
// the connect succeeds, this gets the module in STA-only mode.

static void ICACHE_FLASH_ATTR staCheckConnStatus( void *arg )
{
   ESP_LOGD( TAG, "staCheckConnStatus" );
   int bits = xEventGroupWaitBits( wifi_event_group, CONNECTED_BIT, 0, 1, 0 );
   if( bits & CONNECTED_BIT )
   {
      // Go to STA mode.
      ESP_LOGD( TAG, "Got IP. Going into STA mode.." );
      esp_wifi_set_mode( WIFI_MODE_STA );
      esp_restart();
   }
   else
   {
      connTryStatus = CONNTRY_FAIL;
      ESP_LOGE( TAG, "Connect fail. Not going into STA-only mode." );
      // Maybe also pass this through on the webpage?
   }
}

// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------

// Actually connect to a station. This routine is timed because I had problems
// with immediate connections earlier. It probably was something else that caused it,
// but I can't be arsed to put the code back :P

static void ICACHE_FLASH_ATTR cgiWiFiDoConnectCb( void *arg )
{
   wifi_mode_t mode;
   esp_err_t err;

   ESP_LOGD( TAG, "Try to connect to AP %s pw %s\n", wifi_config.ap.ssid, wifi_config.ap.password );
   esp_wifi_disconnect();
   esp_wifi_set_config( WIFI_IF_STA, &wifi_config );

   ESP_LOGD( TAG, "Connect...." );
   err = esp_wifi_connect();
   if( err != ESP_OK )
   {
      ESP_LOGE( TAG, "Connect Error %d", err );
   }
   else
   {
      esp_wifi_get_mode( &mode );
      connTryStatus = CONNTRY_WORKING;
      if( mode != WIFI_MODE_STA )
      {
         // Schedule disconnect/connect
         // time out after 15 secs of trying to connect
         ESP_LOGD( TAG, "staCheckTimer...." );
         staCheckTimer = httpdPlatTimerCreate( "staCheckTimer", 15000, 0, staCheckConnStatus, NULL );
         httpdPlatTimerStart( staCheckTimer );
      }
   }
   ESP_LOGD( TAG, "cgiWiFiDoConnectCb leave" );
}

// This cgi uses the routines above to connect to a specific access point with the
// given ESSID using the given password.

CgiStatus ICACHE_FLASH_ATTR cgiWiFiConnect( HttpdConnData *connData )
{
   char essid[128];
   char passwd[128];
   static HttpdPlatTimerHandle reassTimer;

   if( connData->isConnectionClosed )
   {
      // Connection aborted. Clean up.
      return HTTPD_CGI_DONE;
   }

   int ssilen = httpdFindArg( connData->post.buff, "essid", essid, sizeof( essid ) );
   int passlen = httpdFindArg( connData->post.buff, "passwd", passwd, sizeof( passwd ) );

   if( ssilen == -1 || passlen == -1 )
   {
      ESP_LOGE( TAG, "Not rx needed args!" );
      httpdRedirect( connData, "/wifi" );
   }
   else
   {
      strncpy( ( char* )wifi_config.ap.ssid, essid, 32 );
      strncpy( ( char* )wifi_config.ap.password, passwd, 64 );
      ESP_LOGD( TAG, "Try to connect to AP %s pw %s", essid, passwd );

      // Schedule disconnect/connect

      // Set to 0 if you want to disable the actual reconnecting bit
#ifdef DEMO_MODE
      httpdRedirect( connData, "/wifi" );
#else
      // redirect & start connecting a little bit later
      reassTimer = httpdPlatTimerCreate( "reconnectTimer", 200, 0, cgiWiFiDoConnectCb, NULL );
      httpdPlatTimerStart( reassTimer );
      httpdRedirect( connData, "/wifi/connecting.html" );
#endif
   }
   return HTTPD_CGI_DONE;
}

// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------

// This cgi uses the routines above to connect to a specific access point with the
// given ESSID using the given password.

CgiStatus ICACHE_FLASH_ATTR cgiWiFiSetMode( HttpdConnData *connData )
{
   int len;
   char buff[128];

   if( connData->isConnectionClosed )
   {
      // Connection aborted. Clean up.
      return HTTPD_CGI_DONE;
   }

   len = httpdFindArg( connData->getArgs, "mode", buff, sizeof( buff ) );
   if( len > 0 )
   {
      ESP_LOGD( TAG, "cgiWifiSetMode: %s", buff );
#ifndef DEMO_MODE
      esp_wifi_set_mode( atoi( buff ) );
#endif
   }
   httpdRedirect( connData, "/wifi" );
   return HTTPD_CGI_DONE;
}

// --------------------------------------------------------------------------
// taken from MightyPork/libesphttpd
// --------------------------------------------------------------------------

// Set wifi channel for AP mode
CgiStatus ICACHE_FLASH_ATTR cgiWiFiSetChannel( HttpdConnData *connData )
{
   int len;
   char buff[64];

   if( connData->isConnectionClosed )
   {
      // Connection aborted. Clean up.
      return HTTPD_CGI_DONE;
   }

   len = httpdFindArg( connData->getArgs, "ch", buff, sizeof( buff ) );
   if( len > 0 )
   {
      ESP_LOGD( TAG, "cgiWifiSetChannel: %s", buff );
      int channel = atoi( buff );
      if( channel > 0 && channel < 15 )
      {
         ESP_LOGD( TAG, "Setting channel=%d", channel );

         // check if we are in softap mode
         esp_wifi_set_channel( channel, 0 );
      }
   }
   httpdRedirect( connData, "/wifi" );
   return HTTPD_CGI_DONE;
}

// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------

CgiStatus ICACHE_FLASH_ATTR cgiWiFiConnStatus( HttpdConnData *connData )
{
   char buff[128];
   int len;

   httpdStartResponse( connData, 200 );
   httpdHeader( connData, "Content-Type", "application/json" );
   httpdEndHeaders( connData );

   if( connTryStatus == CONNTRY_IDLE )
   {
      len = sprintf( buff, "{\n \"status\": \"idle\"\n }\n" );
   }
   else if( connTryStatus == CONNTRY_WORKING || connTryStatus == CONNTRY_SUCCESS )
   {
      int bits = xEventGroupWaitBits( wifi_event_group, CONNECTED_BIT, 0, 1, 0 );
      if( bits & CONNECTED_BIT )
      {
         tcpip_adapter_ip_info_t ipInfo;
         tcpip_adapter_get_ip_info( TCPIP_ADAPTER_IF_STA, &ipInfo );

         len = sprintf( buff, "{\"status\": \"success\", \"ip\": \""IPSTR"\"}", GOOD_IP2STR(ipInfo.ip.addr));

         // Go to STA mode. This needs a reset, so do that.
         ESP_LOGD( TAG, "staCheckTimer...." );
         staCheckTimer = httpdPlatTimerCreate( "staCheckTimer", 1000, 0, staCheckConnStatus, NULL );
         httpdPlatTimerStart( staCheckTimer );
      }
      else
      {
         len = sprintf( buff, "{\n \"status\": \"working\"\n }\n" );
      }
   }
   else
   {
      len = sprintf( buff, "{\n \"status\": \"fail\"\n }\n" );
   }

   httpdSend( connData, buff, len );
   return HTTPD_CGI_DONE;
}

// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------

// Template code for the WLAN page.

CgiStatus ICACHE_FLASH_ATTR tplWlan( HttpdConnData *connData, char *token, void **arg )
{
   char buff[1024];
   wifi_config_t wifi_config;    // ssid and password have the same location in .ap and .sta
   wifi_mode_t mode;
   esp_err_t err;

   if( token == NULL ) return HTTPD_CGI_DONE;
   strcpy( buff, "Unknown" );

   err = esp_wifi_get_mode( &mode );
   if( err != ESP_OK )   return HTTPD_CGI_DONE;

   // better to have two wifi_config varibales, on for ap, one for sta
   // to cover also  WiFi station + soft-AP mode
   if( WIFI_MODE_AP == mode )
   {
      esp_wifi_get_config( WIFI_IF_AP, &wifi_config );
      if( err != ESP_OK )   return HTTPD_CGI_DONE;

      ESP_LOGI( TAG, "AP mode, %s %s", wifi_config.ap.ssid, wifi_config.ap.password );
   }
   else if( WIFI_MODE_STA == mode || WIFI_MODE_APSTA == mode )
   {
      int bits = xEventGroupWaitBits( wifi_event_group, CONNECTED_BIT, 0, 1, 0 );
      if( bits & CONNECTED_BIT )
      {
         err = esp_wifi_get_config( WIFI_IF_STA, &wifi_config );
         if( err != ESP_OK )   return HTTPD_CGI_DONE;

         ESP_LOGI( TAG, "sta mode, connected %s", wifi_config.sta.ssid );
      }

      else
      {
         ESP_LOGI( TAG, "sta mode, disconnected" );
      }
   }
   else
   {
      ESP_LOGI( TAG, "NULL mode" );
   }

   if( strcmp( token, "WiFiMode" ) == 0 )
   {
      switch( mode )
      {
         case WIFI_MODE_NULL:  strcpy( buff, "none" ); break;
         case WIFI_MODE_STA:   strcpy( buff, "Client" ); break;
         case WIFI_MODE_AP:    strcpy( buff, "SoftAP" ); break;
         case WIFI_MODE_APSTA: strcpy( buff, "STA+AP" ); break;
         default: ;
      }
   }
   else if( strcmp( token, "currSsid" ) == 0 )
   {
      strcpy( buff, ( char* )wifi_config.sta.ssid );
   }
   else if( strcmp( token, "WiFiPasswd" ) == 0 )
   {
      strcpy( buff, ( char* )wifi_config.sta.password );
   }
   else if( strcmp( token, "WiFiapwarn" ) == 0 )
   {
      if( mode == WIFI_MODE_AP )
      {
         strcpy( buff, "<b>Can't scan in this mode.</b> Click <a href=\"setmode.cgi?mode=3\">here</a> to go to STA+AP mode." );
      }
      else
      {
         strcpy( buff, "Click <a href=\"setmode.cgi?mode=2\">here</a> to go to standalone AP mode." );
      }
   }
   httpdSend( connData, buff, -1 );

   return HTTPD_CGI_DONE;
}

// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
#endif // ESP32