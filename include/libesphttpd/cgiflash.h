#ifndef CGIFLASH_H
#define CGIFLASH_H

#include "httpd.h"

#define CGIFLASH_TYPE_FW 0
#define CGIFLASH_TYPE_ESPFS 1

typedef struct {
	int type;
	int fw1Pos;
	int fw2Pos;
	int fwSize;
	char *tagName;
} CgiUploadFlashDef;

CgiStatus cgiGetFirmwareNext(HttpdConnData *connData);
CgiStatus cgiUploadFirmware(HttpdConnData *connData);
CgiStatus cgiRebootFirmware(HttpdConnData *connData);

#endif
