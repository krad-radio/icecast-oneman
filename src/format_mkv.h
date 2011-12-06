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

/* Begin Kludge */

#define BUFSIZE 5000000

typedef struct {

	int slice_reset;

	char header[BUFSIZE];
	char buffer[BUFSIZE];
	int header_pos;
	int header_size;
	int header_size_len;

	int slice_pos;
	int slice_size;
	int slice_size_len;
	char slice[BUFSIZE];

	int buffered_bytes;
	int buffer_pos;
	int ret;
	int rret;
	int total_ret;


} kradsource_receiver_client_t;


int kradsource_receiver_client_header(source_t *source, char *header);
int kradsource_receiver_client_slice(source_t *source, char *slice);
kradsource_receiver_client_t *kradsource_receiver_client_create();
void kradsource_receiver_client_destroy(kradsource_receiver_client_t *kradsource_receiver_client);

/* End Kludge */

typedef struct mkv_source_state_St mkv_source_state_t;

struct mkv_source_state_St {

	refbuf_t *header;
	int file_header_written;
	int header_received;
	
	/* Kludge Vars */
	int refbufnum;
	refbuf_t *slice_refbuf;
	int slice_refbuf_pos;
	int slice_refbuf_len;
	kradsource_receiver_client_t *kradsource_receiver_client;
	
};

int format_mkv_get_plugin (source_t *source);

#endif  /* __FORMAT_MKV_H__ */
