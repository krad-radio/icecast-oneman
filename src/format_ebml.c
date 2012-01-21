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
