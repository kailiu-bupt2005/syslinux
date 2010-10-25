#include <dprintf.h>
#include <stdio.h>
#include <string.h>
#include <core.h>
#include <fs.h>

/*
 * Common implementation of load_config
 *
 * This searches for a specified set of filenames in a specified set
 * of directories.  If found, return the opened file descriptor
 */
FILE * search_open_config(const char *search_directories[], const char *filenames[])
{
    char confignamebuf[FILENAME_MAX];
    const char *sd, **sdp;
    const char *sf, **sfp;
    FILE *f;

    for (sdp = search_directories; (sd = *sdp); sdp++) {
	for (sfp = filenames; (sf = *sfp); sfp++) {
	    snprintf(confignamebuf, sizeof confignamebuf,
		     "%s%s%s",
		     sd, (*sd && sd[strlen(sd)-1] == '/') ? "" : "/",
		     sf);
	    realpath(ConfigName, confignamebuf, FILENAME_MAX);
	    mp("Config search: %s", ConfigName);
	    f = fopen(ConfigName, "r");
	    if (f)
		return f;
	}
    }

    return NULL;
}


