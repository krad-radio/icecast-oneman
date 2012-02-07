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

/* format_ebml.c
 *
 * format plugin for EBML
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
#include "format_ebml.h"

#include "logging.h"

#define CATMODULE "format-ebml"

static void ebml_free_plugin (format_plugin_t *plugin);
static refbuf_t *ebml_get_buffer (source_t *source);
static int  ebml_write_buf_to_client (client_t *client);
static void  ebml_write_buf_to_file (source_t *source, refbuf_t *refbuf);
static int  ebml_create_client_data (source_t *source, client_t *client);
static void ebml_free_client_data (client_t *client);

typedef struct ebml_client_data_St ebml_client_data_t;

struct ebml_client_data_St {

	refbuf_t *header;
	int header_pos;

};

int format_ebml_get_plugin (source_t *source)
{

	ebml_source_state_t *ebml_source_state = calloc(1, sizeof(ebml_source_state_t));
    format_plugin_t *plugin = calloc(1, sizeof(format_plugin_t));

	plugin->get_buffer = ebml_get_buffer;
	plugin->write_buf_to_client = ebml_write_buf_to_client;
	plugin->create_client_data = ebml_create_client_data;
	plugin->free_plugin = ebml_free_plugin;
	plugin->write_buf_to_file = ebml_write_buf_to_file;
	plugin->set_tag = NULL;
	plugin->apply_settings = NULL;

	plugin->contenttype = httpp_getvar (source->parser, "content-type");
	
	plugin->_state = ebml_source_state;
	source->format = plugin;

	ebml_source_state->ebml = krad_ebml_create_feedbuffer();
	return 0;
}

static void ebml_free_plugin (format_plugin_t *plugin)
{

	ebml_source_state_t *ebml_source_state = plugin->_state;

	refbuf_release (ebml_source_state->header);
	krad_ebml_destroy(ebml_source_state->ebml);
	free (ebml_source_state);
    free (plugin);
    
}

static int send_ebml_header (client_t *client)
{

	ebml_client_data_t *ebml_client_data = client->format_data;
	int len = 4096;
	int ret;

	if (ebml_client_data->header->len - ebml_client_data->header_pos < len) 
	{
		len = ebml_client_data->header->len - ebml_client_data->header_pos;
	}
	
	ret = client_send_bytes (client, ebml_client_data->header->data + ebml_client_data->header_pos, len);

    if (ret > 0)
    {
		ebml_client_data->header_pos += ret;
    }

	return ret;
	
}

static int ebml_write_buf_to_client (client_t *client)
{

    ebml_client_data_t *ebml_client_data = client->format_data;

	if (ebml_client_data->header_pos != ebml_client_data->header->len)
	{	
		return send_ebml_header (client);
	}
	else
	{
    	client->write_to_client = format_generic_write_to_client;
		return client->write_to_client(client);
	}

}

static refbuf_t *ebml_get_buffer (source_t *source)
{
	
	ebml_source_state_t *ebml_source_state = source->format->_state;
    format_plugin_t *format = source->format;
    char *data = NULL;
    int bytes = 0;
	refbuf_t *refbuf;
	
	while (1) 
	{

		if ((bytes = krad_ebml_read_space(ebml_source_state->ebml)) > 0) 
		{
			refbuf = refbuf_new(bytes);
			krad_ebml_read(ebml_source_state->ebml, refbuf->data, bytes);
			
			if (ebml_source_state->header == NULL) 
			{
				ebml_source_state->header = refbuf;
				continue;
			}
			
			if (krad_ebml_last_was_sync(ebml_source_state->ebml)) 
			{
				refbuf->sync_point = 1;
			}
			return refbuf;
			
		}
		else
		{
		
		    data = krad_ebml_write_buffer(ebml_source_state->ebml, 4096);
		    bytes = client_read_bytes (source->client, data, 4096);
			if (bytes <= 0) 
			{
				krad_ebml_wrote (ebml_source_state->ebml, 0);
				return NULL;
			}
			format->read_bytes += bytes;
	        krad_ebml_wrote (ebml_source_state->ebml, bytes);
		}	
	}
}

static int ebml_create_client_data (source_t *source, client_t *client)
{
	
	ebml_client_data_t *ebml_client_data = calloc(1, sizeof(ebml_client_data_t));
	ebml_source_state_t *ebml_source_state = source->format->_state;
	
	int ret = -1;

	if (ebml_client_data)
	{
		ebml_client_data->header = ebml_source_state->header; 
		refbuf_addref (ebml_client_data->header);
		client->format_data = ebml_client_data;
		client->free_client_data = ebml_free_client_data;
		ret = 0;
	}
    
	return ret;

}


static void ebml_free_client_data (client_t *client)
{

    ebml_client_data_t *ebml_client_data = client->format_data;

	refbuf_release (ebml_client_data->header);
	free (client->format_data);
	client->format_data = NULL;
}


static void ebml_write_buf_to_file_fail (source_t *source)
{
	WARN0 ("Write to dump file failed, disabling");
	fclose (source->dumpfile);
	source->dumpfile = NULL;
}


static void ebml_write_buf_to_file (source_t *source, refbuf_t *refbuf)
{

	ebml_source_state_t *ebml_source_state = source->format->_state;

    if (ebml_source_state->file_headers_written == 0)
    {
		if (fwrite (ebml_source_state->header->data, 1, ebml_source_state->header->len, source->dumpfile) != ebml_source_state->header->len)
			ebml_write_buf_to_file_fail(source);
        else
			ebml_source_state->file_headers_written = 1;
    }

	if (fwrite (refbuf->data, 1, refbuf->len, source->dumpfile) != refbuf->len)
	{
		ebml_write_buf_to_file_fail(source);
	}

}


/* internal mini krad ebml parsing */

unsigned char krad_ebml_get_next_match_byte(unsigned char match_byte, uint64_t position, uint64_t *matched_byte_num, uint64_t *winner) {

	if (winner != NULL) {
		*winner = 0;
	}

	if (matched_byte_num != NULL) {
		if (match_byte == KRAD_EBML_CLUSTER_BYTE1) {
			if (matched_byte_num != NULL) {
				*matched_byte_num = position;
			}
			return KRAD_EBML_CLUSTER_BYTE2;
		}
	
		if ((*matched_byte_num == position - 1) && (match_byte == KRAD_EBML_CLUSTER_BYTE2)) {
			return KRAD_EBML_CLUSTER_BYTE3;
		}
	
		if ((*matched_byte_num == position - 2) && (match_byte == KRAD_EBML_CLUSTER_BYTE3)) {
			return KRAD_EBML_CLUSTER_BYTE4;
		}	

		if ((*matched_byte_num == position - 3) && (match_byte == KRAD_EBML_CLUSTER_BYTE4)) {
			if (winner != NULL) {
				*winner = *matched_byte_num;
			}
			*matched_byte_num = 0;
			return KRAD_EBML_CLUSTER_BYTE1;
		}

		*matched_byte_num = 0;
	}
	
	return KRAD_EBML_CLUSTER_BYTE1;

}

void krad_ebml_destroy(krad_ebml_t *krad_ebml) {

	free(krad_ebml->buffer);
	free(krad_ebml->header);
	free(krad_ebml);

}

krad_ebml_t *krad_ebml_create_feedbuffer() {

	krad_ebml_t *krad_ebml = calloc(1, sizeof(krad_ebml_t));

	krad_ebml->buffer = calloc(1, KRAD_EBML_BUFFER_SIZE);
	krad_ebml->header = calloc(1, KRAD_EBML_HEADER_MAX_SIZE);

	
	krad_ebml->cluster_mark[0] = KRAD_EBML_CLUSTER_BYTE1;
	krad_ebml->cluster_mark[1] = KRAD_EBML_CLUSTER_BYTE2;
	krad_ebml->cluster_mark[2] = KRAD_EBML_CLUSTER_BYTE3;
	krad_ebml->cluster_mark[3] = KRAD_EBML_CLUSTER_BYTE4;

	return krad_ebml;

}

size_t krad_ebml_read_space(krad_ebml_t *krad_ebml) {

	size_t read_space;
	
	if (krad_ebml->header_read == 1) {
		read_space = (krad_ebml->position - krad_ebml->header_size) - krad_ebml->read_position;
	
		return read_space;
	} else {
		if (krad_ebml->header_size != 0) {
			return krad_ebml->header_size - krad_ebml->header_read_position;
		} else {
			return 0;
		}
	}


}

int krad_ebml_read(krad_ebml_t *krad_ebml, char *buffer, int len) {

	size_t read_space;
	size_t read_space_to_cluster;
	int to_read;
	
	read_space_to_cluster = 0;
	
	if (len < 1) {
		return 0;
	}
	
	if (krad_ebml->header_read == 1) {
		read_space = (krad_ebml->position - krad_ebml->header_size) - krad_ebml->read_position;
	
		if (read_space < 1) {
			return 0;
		}
		
		if (read_space >= len ) {
			to_read = len;
		} else {
			to_read = read_space;
		}
		
		if (krad_ebml->cluster_position != 0) {
			read_space_to_cluster = (krad_ebml->cluster_position - krad_ebml->header_size) - krad_ebml->read_position;
			if ((read_space_to_cluster != 0) && (read_space_to_cluster <= to_read)) {
				to_read = read_space_to_cluster;
				krad_ebml->cluster_position = 0;
				krad_ebml->last_was_cluster_end = 1;
			} else {
				if (read_space_to_cluster == 0) {
					krad_ebml->this_was_cluster_start = 1;
				}
			}
		}
	
		memcpy(buffer, krad_ebml->buffer, to_read);
		krad_ebml->read_position += to_read;
		

		memmove(krad_ebml->buffer, krad_ebml->buffer + to_read, krad_ebml->buffer_position_internal - to_read);
		krad_ebml->buffer_position_internal -= to_read;
		
	} else {
		if (krad_ebml->header_size != 0) {
			
			read_space = krad_ebml->header_size - krad_ebml->header_read_position;
	
			if (read_space >= len ) {
				to_read = len;
			} else {
				to_read = read_space;
			}
	
			memcpy(buffer, krad_ebml->header, to_read);
			krad_ebml->header_read_position += to_read;		
			
			if (krad_ebml->header_read_position == krad_ebml->header_size) {
				krad_ebml->header_read = 1;
			}
			
		} else {
			return 0;
		}
	}
	
	
	return to_read;	

}

int krad_ebml_last_was_sync(krad_ebml_t *krad_ebml) {

	if (krad_ebml->last_was_cluster_end == 1) {
		krad_ebml->last_was_cluster_end = 0;
		krad_ebml->this_was_cluster_start = 1;
	}
	
	if (krad_ebml->this_was_cluster_start == 1) {
		krad_ebml->this_was_cluster_start = 0;
		return 1;
	}

	return 0;

}

char *krad_ebml_write_buffer(krad_ebml_t *krad_ebml, int len) {

	krad_ebml->input_buffer = malloc(len);

	return (char *)krad_ebml->input_buffer;

}


int krad_ebml_wrote(krad_ebml_t *krad_ebml, int len) {
	
	int b;

	for (b = 0; b < len; b++) {
		if ((krad_ebml->input_buffer[b] == krad_ebml->match_byte) || (krad_ebml->matched_byte_num > 0)) {
			krad_ebml->match_byte = krad_ebml_get_next_match_byte(krad_ebml->input_buffer[b], krad_ebml->position + b, 
																  &krad_ebml->matched_byte_num, &krad_ebml->found);
			if (krad_ebml->found > 0) {
				if (krad_ebml->header_size == 0) {
					if (b > 0) {
						memcpy(krad_ebml->header + krad_ebml->header_position, krad_ebml->input_buffer, b);
						krad_ebml->header_position += b;
					}
					krad_ebml->header_size = (krad_ebml->header_position - 4) + 1;
					if (KRAD_EBML_DEBUG) {
						printf("\ngot header, size is %d\n", krad_ebml->header_size);
					}
					/* first cluster */
					memcpy(krad_ebml->buffer, krad_ebml->cluster_mark, 4);
					krad_ebml->buffer_position_internal += 4;
					if ((b + 1) < len) {
						memcpy(krad_ebml->buffer + krad_ebml->buffer_position_internal, 
							   krad_ebml->input_buffer + (b + 1),
							   len - (b + 1));
						krad_ebml->buffer_position_internal += len - (b + 1);
					}
					if (KRAD_EBML_DEBUG) {
						printf("\nfound first cluster starting at %zu\n", krad_ebml->found);
					}
					krad_ebml->cluster_position = krad_ebml->found;
					krad_ebml->position += len;
					free(krad_ebml->input_buffer);
					return len;

				}
				if (KRAD_EBML_DEBUG) {
					printf("\nfound cluster starting at %zu\n", krad_ebml->found);
				}
				krad_ebml->cluster_position = krad_ebml->found;
			}
		}
	}
	
	if (krad_ebml->header_size == 0) {
		if (KRAD_EBML_DEBUG) {
			printf("\nadding to header header, pos is %d size is %d adding %d\n", 
				   krad_ebml->header_size, krad_ebml->header_position, len);
		}
		memcpy(krad_ebml->header + krad_ebml->header_position, krad_ebml->input_buffer, len);
		krad_ebml->header_position += len;
	} else {
	
		memcpy(krad_ebml->buffer + krad_ebml->buffer_position_internal, krad_ebml->input_buffer, len);
		krad_ebml->buffer_position_internal += len;
	}
	
	krad_ebml->position += len;

	free(krad_ebml->input_buffer);

	return len;
}


