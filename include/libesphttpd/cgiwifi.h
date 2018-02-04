#ifndef CGIWIFI_H
#define CGIWIFI_H

#include "httpd.h"

CgiStatus cgiWiFiScan( HttpdConnData *connData );
CgiStatus tplWlan( HttpdConnData *connData, char *token, void **arg );
CgiStatus cgiWiFi( HttpdConnData *connData );
CgiStatus cgiWiFiConnect( HttpdConnData *connData );
CgiStatus cgiWiFiSetMode( HttpdConnData *connData );
CgiStatus cgiWiFiSetChannel( HttpdConnData *connData );
CgiStatus cgiWiFiConnStatus( HttpdConnData *connData );

#endif
