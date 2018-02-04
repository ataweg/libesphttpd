#ifndef __HTTPDESPFS_H__
#define __HTTPDESPFS_H__

#include "httpd.h"

#define FILE_CHUNK_LEN 1024

/**
 * The template substitution callback.
 * Returns CGI_MORE if more should be sent within the token, CGI_DONE otherwise.
 */
typedef CgiStatus( * TplCallback )( HttpdConnData *connData, char *token, void **arg );

CgiStatus cgiEspFsHook( HttpdConnData *connData );
CgiStatus ICACHE_FLASH_ATTR cgiEspFsTemplate( HttpdConnData *connData );
CgiStatus serveStaticFile( HttpdConnData *connData, const char* filepath, int responseCode );
CgiStatus cgiTemplateSendContent( HttpdConnData *connData );

/**
 * @return 1 upon success, 0 upon failure
 */
int tplSend( HttpdConnData *conn, const char *str, int len );

#endif // __HTTPDESPFS_H__