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

/* format_mkv.c
 *
 * format plugin for MKV
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "refbuf.h"
#include "source.h"
#include "client.h"

#include "stats.h"
#include "format.h"
#include "format_mkv.h"

#include "logging.h"

#define CATMODULE "format-mkv"

static void mkv_free_plugin (format_plugin_t *plugin);
static refbuf_t *mkv_get_buffer (source_t *source);
static int  mkv_write_buf_to_client (client_t *client);
static void  mkv_write_buf_to_file (source_t *source, refbuf_t *refbuf);
static int  mkv_create_client_data (source_t *source, client_t *client);
static void mkv_free_client_data (client_t *client);

typedef struct mkv_client_data_St mkv_client_data_t;

struct mkv_client_data_St {

	refbuf_t *header;
	int header_pos;

};

int format_mkv_get_plugin (source_t *source)
{

	mkv_source_state_t *mkv_source_state = calloc(1, sizeof(mkv_source_state_t));
    format_plugin_t *plugin = calloc(1, sizeof(format_plugin_t));

	plugin->get_buffer = mkv_get_buffer;
	plugin->write_buf_to_client = mkv_write_buf_to_client;
	plugin->create_client_data = mkv_create_client_data;
	plugin->free_plugin = mkv_free_plugin;
	plugin->write_buf_to_file = mkv_write_buf_to_file;
	plugin->set_tag = NULL;
	plugin->apply_settings = NULL;

	plugin->contenttype = httpp_getvar (source->parser, "content-type");
	
	plugin->_state = mkv_source_state;
	source->format = plugin;
	
	mkv_source_state->header = refbuf_new(500000);
	
	mkv_source_state->slice_refbuf = refbuf_new(5000000);
	mkv_source_state->kradsource_receiver_client = kradsource_receiver_client_create();

	return 0;
}

static void mkv_free_plugin (format_plugin_t *plugin)
{

	mkv_source_state_t *mkv_source_state = plugin->_state;

	refbuf_release (mkv_source_state->slice_refbuf);
	kradsource_receiver_client_destroy(mkv_source_state->kradsource_receiver_client);

	refbuf_release (mkv_source_state->header);
	free (mkv_source_state);
    free (plugin);
    
}

static int send_mkv_header (client_t *client)
{

	mkv_client_data_t *mkv_client_data = client->format_data;
	int len = 4096;
	int ret;

	if (mkv_client_data->header->len - mkv_client_data->header_pos < len) 
	{
		len = mkv_client_data->header->len - mkv_client_data->header_pos;
	}
			printf("sending header to client %d\n", len);
	ret = client_send_bytes (client, mkv_client_data->header->data + mkv_client_data->header_pos, len);

    if (ret > 0)
    {
		mkv_client_data->header_pos += ret;
    }

	return ret;
	
}

static int mkv_write_buf_to_client (client_t *client)
{

    mkv_client_data_t *mkv_client_data = client->format_data;

	if (mkv_client_data->header_pos != mkv_client_data->header->len)
	{	
		printf("client needs header parts still\n");
		return send_mkv_header (client);
	}
	else
	{
		printf("client done with header\n");
    	client->write_to_client = format_generic_write_to_client;
		return client->write_to_client(client);
	}

}

static refbuf_t *mkv_get_buffer (source_t *source)
{
	
	mkv_source_state_t *mkv_source_state = source->format->_state;

	int len = 0;
	refbuf_t *refbuf;

	if (mkv_source_state->header_received == 0) {
		len = kradsource_receiver_client_header(source, mkv_source_state->header->data);
		if (len == 0) {
			return NULL;
		}
		mkv_source_state->header_received = 1;
		mkv_source_state->header->data = realloc(mkv_source_state->header->data, len);
		mkv_source_state->header->len = len;
		printf("got header data, length is %d\n", len);
	}
	
	if (mkv_source_state->slice_refbuf_len == 0) {
	
		mkv_source_state->slice_refbuf_len = kradsource_receiver_client_slice(source, mkv_source_state->slice_refbuf->data);
		if (mkv_source_state->slice_refbuf_len > 0) {
			source->format->read_bytes += mkv_source_state->slice_refbuf_len;
		} else {
			return NULL;
		}
	}

	len = 4096;

	if (mkv_source_state->slice_refbuf_len - mkv_source_state->slice_refbuf_pos < len) 
	{
		len = mkv_source_state->slice_refbuf_len - mkv_source_state->slice_refbuf_pos;
	}

	//printf("len is %d providing refbuf number %d slice refbuf pos is %d len is %d\n", len, mkv_source_state->refbufnum++, mkv_source_state->slice_refbuf_pos, mkv_source_state->slice_refbuf_len);

	refbuf = refbuf_new(len);
	if (mkv_source_state->slice_refbuf_pos == 0) {
		refbuf->sync_point = 1;
	}
	memcpy(refbuf->data, mkv_source_state->slice_refbuf->data + mkv_source_state->slice_refbuf_pos, len);
	mkv_source_state->slice_refbuf_pos += len;
	
	if (mkv_source_state->slice_refbuf_pos == mkv_source_state->slice_refbuf_len) {
		mkv_source_state->slice_refbuf_len = 0;
		mkv_source_state->slice_refbuf_pos = 0;
	}

	return refbuf;
}

static int mkv_create_client_data (source_t *source, client_t *client)
{
	
	mkv_client_data_t *mkv_client_data = calloc(1, sizeof(mkv_client_data_t));
	mkv_source_state_t *mkv_source_state = source->format->_state;
	
	int ret = -1;

	if (mkv_client_data)
	{
		mkv_client_data->header = mkv_source_state->header; 
		refbuf_addref (mkv_client_data->header);
		client->format_data = mkv_client_data;
		client->free_client_data = mkv_free_client_data;
		ret = 0;
	}
    
	return ret;

}


static void mkv_free_client_data (client_t *client)
{

    mkv_client_data_t *mkv_client_data = client->format_data;

	refbuf_release (mkv_client_data->header);
	free (client->format_data);
	client->format_data = NULL;
}


static void mkv_write_buf_to_file_fail (source_t *source)
{
	WARN0 ("Write to dump file failed, disabling");
	fclose (source->dumpfile);
	source->dumpfile = NULL;
}


static void mkv_write_buf_to_file (source_t *source, refbuf_t *refbuf)
{

	mkv_source_state_t *mkv_source_state = source->format->_state;

    if (mkv_source_state->file_header_written == 0)
    {
		if (fwrite (mkv_source_state->header->data, 1, mkv_source_state->header->len, source->dumpfile) != mkv_source_state->header->len)
			mkv_write_buf_to_file_fail(source);
        else
			mkv_source_state->file_header_written = 1;
    }

	if (fwrite (refbuf->data, 1, refbuf->len, source->dumpfile) != refbuf->len)
	{
		mkv_write_buf_to_file_fail(source);
	}

}


/* Begin Kludge */


int kradsource_receiver_client_header(source_t *source, char *header) {

	mkv_source_state_t *mkv_source_state = source->format->_state;
	kradsource_receiver_client_t *kradsource_receiver_client = mkv_source_state->kradsource_receiver_client;

	while ((kradsource_receiver_client->header_size == 0) || (kradsource_receiver_client->header_pos != kradsource_receiver_client->header_size)) {

		if (kradsource_receiver_client->buffer_pos == kradsource_receiver_client->ret) {
			usleep(1000000);
			kradsource_receiver_client->buffer_pos = 0;
			//kradsource_receiver_client->ret = read(kradsource_receiver_client->sd, kradsource_receiver_client->buffer, BUFSIZE);
			kradsource_receiver_client->ret = client_read_bytes (source->client, kradsource_receiver_client->buffer, 8192);
			printf("got %d bytes\n", kradsource_receiver_client->ret);
		}

		if (kradsource_receiver_client->header_size_len == 0) {


			kradsource_receiver_client->buffer_pos += strlen("KRADSOURCE");

			kradsource_receiver_client->header_size_len = strcspn(kradsource_receiver_client->buffer + kradsource_receiver_client->buffer_pos, "|");

			printf("Got header size len of: %d\n", kradsource_receiver_client->header_size_len);

			kradsource_receiver_client->buffer[kradsource_receiver_client->buffer_pos + kradsource_receiver_client->header_size_len] = '\0';

			kradsource_receiver_client->header_size = atoi(kradsource_receiver_client->buffer + kradsource_receiver_client->buffer_pos);

			printf("Got header size of: %d\n", kradsource_receiver_client->header_size);

			kradsource_receiver_client->buffer_pos += kradsource_receiver_client->header_size_len + 1;
		}

		if ((kradsource_receiver_client->header_size - kradsource_receiver_client->header_pos) >= (kradsource_receiver_client->ret - kradsource_receiver_client->buffer_pos)) {
			memcpy(kradsource_receiver_client->header + kradsource_receiver_client->header_pos, kradsource_receiver_client->buffer + kradsource_receiver_client->buffer_pos, kradsource_receiver_client->ret - kradsource_receiver_client->buffer_pos);
			kradsource_receiver_client->header_pos += kradsource_receiver_client->ret - kradsource_receiver_client->buffer_pos;
			printf("copied %d bytes to header now at pos %d\n", kradsource_receiver_client->ret - kradsource_receiver_client->buffer_pos, kradsource_receiver_client->header_pos);
			kradsource_receiver_client->buffer_pos += kradsource_receiver_client->ret - kradsource_receiver_client->buffer_pos;

		} else {	
			memcpy(kradsource_receiver_client->header + kradsource_receiver_client->header_pos, kradsource_receiver_client->buffer + kradsource_receiver_client->buffer_pos, kradsource_receiver_client->header_size - kradsource_receiver_client->header_pos);
			kradsource_receiver_client->buffer_pos += kradsource_receiver_client->header_size - kradsource_receiver_client->header_pos;
			printf("copied %d bytes to header", kradsource_receiver_client->header_size - kradsource_receiver_client->header_pos);
			kradsource_receiver_client->header_pos += kradsource_receiver_client->header_size - kradsource_receiver_client->header_pos;
			printf(" now at pos %d\n", kradsource_receiver_client->header_pos);


		}


	}

	memcpy(header, kradsource_receiver_client->header, kradsource_receiver_client->header_size);
	return kradsource_receiver_client->header_size;


}


int kradsource_receiver_client_slice(source_t *source, char *slice) {

	mkv_source_state_t *mkv_source_state = source->format->_state;
	kradsource_receiver_client_t *kradsource_receiver_client = mkv_source_state->kradsource_receiver_client;

	if (kradsource_receiver_client->slice_reset == 1) {
		kradsource_receiver_client->slice_pos = 0;
		kradsource_receiver_client->slice_size = 0;
		kradsource_receiver_client->slice_size_len = 0;
		kradsource_receiver_client->slice_reset = 0;
	}

	//printf("\nslice called\n");

	int busylooped = 0;

	while ((kradsource_receiver_client->slice_size == 0) || (kradsource_receiver_client->slice_pos != kradsource_receiver_client->slice_size)) {

		kradsource_receiver_client->buffered_bytes = kradsource_receiver_client->ret - kradsource_receiver_client->buffer_pos;
		//printf("buffered_bytes is: %d\n", kradsource_receiver_client->buffered_bytes);
		if (kradsource_receiver_client->buffered_bytes != 8192) {

			//printf("about to memmove %d bytes\n", kradsource_receiver_client->buffered_bytes);
			memmove(kradsource_receiver_client->buffer, kradsource_receiver_client->buffer + kradsource_receiver_client->buffer_pos, kradsource_receiver_client->buffered_bytes);

			kradsource_receiver_client->ret = kradsource_receiver_client->buffered_bytes;
			kradsource_receiver_client->buffer_pos = 0;

			while (kradsource_receiver_client->ret != 8192) {

				//kradsource_receiver_client->rret = read(kradsource_receiver_client->sd, kradsource_receiver_client->buffer + kradsource_receiver_client->ret, 8192 - kradsource_receiver_client->ret);
				kradsource_receiver_client->rret = client_read_bytes (source->client, kradsource_receiver_client->buffer + kradsource_receiver_client->ret, 8192 - kradsource_receiver_client->ret);
				//printf("\ngot  %d bytes \n", kradsource_receiver_client->rret);
				if (kradsource_receiver_client->rret == 0 || kradsource_receiver_client->rret == -1) {	 
					//printf("source kradsource_receiver_client buzylooped %d\r", busylooped++);
					//fflush(stdout);
					//kradsource_receiver_client_destroy(kradsource_receiver_client);
					return 0;
				} else {
					//printf("\nfreal got  %d bytes \n", kradsource_receiver_client->rret);
					kradsource_receiver_client->ret += kradsource_receiver_client->rret;
				}

			}

			kradsource_receiver_client->total_ret += kradsource_receiver_client->ret;
			//printf("got %d total %d\n", kradsource_receiver_client->ret, kradsource_receiver_client->total_ret);
		}

		if (kradsource_receiver_client->slice_size_len == 0) {

			//printf("buffer_pos is %d\n", kradsource_receiver_client->buffer_pos);

			kradsource_receiver_client->buffer_pos += strlen("KRADSLICE");

			//printf("buffer_pos is %d\n", kradsource_receiver_client->buffer_pos);

			kradsource_receiver_client->slice_size_len = strcspn(kradsource_receiver_client->buffer + kradsource_receiver_client->buffer_pos, "|");

			//printf("Got slice size len of: %d\n", kradsource_receiver_client->slice_size_len);

			kradsource_receiver_client->buffer[kradsource_receiver_client->buffer_pos + kradsource_receiver_client->slice_size_len] = '\0';

			kradsource_receiver_client->slice_size = atoi(kradsource_receiver_client->buffer + kradsource_receiver_client->buffer_pos);

			//printf("Got slice size of: %d\n", kradsource_receiver_client->slice_size);

			kradsource_receiver_client->buffer_pos += kradsource_receiver_client->slice_size_len + 1;

			//printf("buffer_pos is %d\n", kradsource_receiver_client->buffer_pos);
		}

		if ((kradsource_receiver_client->slice_size - kradsource_receiver_client->slice_pos) >= (kradsource_receiver_client->ret - kradsource_receiver_client->buffer_pos)) {
			memcpy(kradsource_receiver_client->slice + kradsource_receiver_client->slice_pos, kradsource_receiver_client->buffer + kradsource_receiver_client->buffer_pos, kradsource_receiver_client->ret - kradsource_receiver_client->buffer_pos);
			kradsource_receiver_client->slice_pos += kradsource_receiver_client->ret - kradsource_receiver_client->buffer_pos;
			//printf("xcopied %d bytes to slice now at pos %d\n", kradsource_receiver_client->ret - kradsource_receiver_client->buffer_pos, kradsource_receiver_client->slice_pos);
			kradsource_receiver_client->buffer_pos += kradsource_receiver_client->ret - kradsource_receiver_client->buffer_pos;

		} else {	
			memcpy(kradsource_receiver_client->slice + kradsource_receiver_client->slice_pos, kradsource_receiver_client->buffer + kradsource_receiver_client->buffer_pos, kradsource_receiver_client->slice_size - kradsource_receiver_client->slice_pos);
			kradsource_receiver_client->buffer_pos += kradsource_receiver_client->slice_size - kradsource_receiver_client->slice_pos;
			//printf("ycopied %d bytes to slice", kradsource_receiver_client->slice_size - kradsource_receiver_client->slice_pos);
			kradsource_receiver_client->slice_pos += kradsource_receiver_client->slice_size - kradsource_receiver_client->slice_pos;
			//printf(" now at pos %d\n", kradsource_receiver_client->slice_pos);

		}

		//usleep(10000);

	}

	//printf("got a slice\n");

	memcpy(slice, kradsource_receiver_client->slice, kradsource_receiver_client->slice_size);

	
	kradsource_receiver_client->slice_reset = 1;

	return kradsource_receiver_client->slice_size;

}


kradsource_receiver_client_t *kradsource_receiver_client_create() {

	return calloc(1, sizeof(kradsource_receiver_client_t));
	
}

void kradsource_receiver_client_destroy(kradsource_receiver_client_t *kradsource_receiver_client) {

	free(kradsource_receiver_client);

}

/* End Kludge */
