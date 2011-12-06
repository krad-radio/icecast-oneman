/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2000-2004, Jack Moffitt <jack@xiph.org, 
 *                      Michael Smith <msmith@xiph.org>,
 *                      oddsock <oddsock@xiph.org>,
 *                      Karl Heyes <karl@xiph.org>
 *                      and others (see AUTHORS for details).
 */

/* format_mkv.h
**
** mkv format plugin header
**
*/
#ifndef __FORMAT_MKV_H__
#define __FORMAT_MKV_H__

#include "format.h"

typedef struct mkv_source_state_St mkv_source_state_t;

struct mkv_source_state_St {

	refbuf_t *header;
	int file_headers_written;
	
};

int format_mkv_get_plugin (source_t *source);

#endif  /* __FORMAT_MKV_H__ */
