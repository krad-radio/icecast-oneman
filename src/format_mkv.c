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
	
	mkv_source_state->header = refbuf_new(30000);

	return 0;
}

static void mkv_free_plugin (format_plugin_t *plugin)
{

	mkv_source_state_t *mkv_source_state = plugin->_state;

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
		return send_mkv_header (client);
	}
	else
	{
    	client->write_to_client = format_generic_write_to_client;
		return client->write_to_client(client);
	}

}

static refbuf_t *mkv_get_buffer (source_t *source)
{
	
	mkv_source_state_t *mkv_source_state = source->format->_state;

	/* Insert Fantatistic MKV Codes here */	

	return NULL;

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

    if (mkv_source_state->file_headers_written == 0)
    {
		if (fwrite (mkv_source_state->header->data, 1, mkv_source_state->header->len, source->dumpfile) != mkv_source_state->header->len)
			mkv_write_buf_to_file_fail(source);
        else
			mkv_source_state->file_headers_written = 1;
    }

	if (fwrite (refbuf->data, 1, refbuf->len, source->dumpfile) != refbuf->len)
	{
		mkv_write_buf_to_file_fail(source);
	}

}
