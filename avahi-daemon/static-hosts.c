/* $Id$ */

/***
  This file is part of avahi.
 
  avahi is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.
 
  avahi is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General
  Public License for more details.
 
  You should have received a copy of the GNU Lesser General Public
  License along with avahi; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <errno.h>
#include <stdio.h>

#include <avahi-common/llist.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-core/log.h>
#include <avahi-core/publish.h>

#include "main.h"
#include "static-hosts.h"

typedef struct StaticHost StaticHost;

struct StaticHost {
    AvahiSEntryGroup *group;

    char *host, *ip;

    AVAHI_LLIST_FIELDS(StaticHost, hosts);
};

static AVAHI_LLIST_HEAD(StaticHost, hosts) = NULL;

static void add_static_host_to_server(StaticHost *h);
static void remove_static_host_from_server(StaticHost *h);

static void entry_group_callback(AvahiServer *s, AVAHI_GCC_UNUSED AvahiSEntryGroup *eg, AvahiEntryGroupState state, void* userdata) {
    StaticHost *h;

    assert(s);
    assert(eg);

    h = userdata;

    switch (state) {

        case AVAHI_ENTRY_GROUP_COLLISION:
            avahi_log_error("Host name conflict for \"%s\", not established.", h->host);
            break;

        case AVAHI_ENTRY_GROUP_ESTABLISHED:
            avahi_log_notice ("Static Host \"%s\" successfully established.", h->host);
            break;

        case AVAHI_ENTRY_GROUP_FAILURE:
            avahi_log_notice ("Failed to establish Static Host \"%s\": %s.", h->host, avahi_strerror (avahi_server_errno (s)));
            break;
        
        case AVAHI_ENTRY_GROUP_UNCOMMITED:
        case AVAHI_ENTRY_GROUP_REGISTERING:
            ;
    }
}

static StaticHost *static_host_new(void) {
    StaticHost *s;
    
    s = avahi_new(StaticHost, 1);

    s->group = NULL;
    s->host = NULL;
    s->ip = NULL;

    AVAHI_LLIST_PREPEND(StaticHost, hosts, hosts, s);

    return s;
}

static void static_host_free(StaticHost *s) {
    assert(s);

    AVAHI_LLIST_REMOVE(StaticHost, hosts, hosts, s);

    avahi_s_entry_group_free (s->group);

    avahi_free(s->host);
    avahi_free(s->ip);
    
    avahi_free(s);
}

static void add_static_host_to_server(StaticHost *h)
{
    AvahiAddress a;
    int err;

    if (!h->group)
        h->group = avahi_s_entry_group_new (avahi_server, entry_group_callback, h);

    if (!avahi_address_parse (h->ip, AVAHI_PROTO_UNSPEC, &a)) {
        avahi_log_error("Static host %s: avahi_address_parse failed", h->host);
        return;
    }

    if ((err = avahi_server_add_address(avahi_server, h->group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0, h->host, &a))) {
        avahi_log_error ("Static host %s: avahi_server_add_address failure: %s", h->host, avahi_strerror(err));
        return;
    }

    avahi_s_entry_group_commit (h->group);
}

static void remove_static_host_from_server(StaticHost *h)
{
    avahi_s_entry_group_reset (h->group);
}
 
void static_hosts_add_to_server(void) {
    StaticHost *h;

    for (h = hosts; h; h = h->hosts_next) {
        add_static_host_to_server(h);
    }
}

void static_hosts_remove_from_server(void) {
    StaticHost *h;

    for (h = hosts; h; h = h->hosts_next) {
        remove_static_host_from_server(h);
    }
}

void static_hosts_load(int in_chroot) {
    FILE *f;
    unsigned int line = 0;
    const char *filename = (in_chroot ? "/hosts" : AVAHI_CONFIG_DIR "/hosts");

    if (!(f = fopen(filename, "r")))
    {
        if (errno != ENOENT)
            avahi_log_error ("Failed to open static hosts file: %s", strerror (errno));
        return;
    }

    while (!feof(f)) {
        unsigned int len;
        char ln[256], *s;
        char *host, *ip;
        StaticHost *h;

        if (!fgets(ln, sizeof (ln), f))
            break;

        line++;

		/* Find the start of the line, ignore whitespace */
		s = ln + strspn(ln, " \t");
		/* Set the end of the string to NULL */
		s[strcspn(s, "#\r\n")] = 0;

		/* Ignore comment (#) and blank lines (*/
		if (*s == '#' || *s == 0)
			continue;

		/* Read the first string (ip) up to the next whitespace */
		len = strcspn(s, " \t");
		ip = avahi_strndup(s, len);

		/* Skip past it */
		s += len;

		/* Find the next token */
		s += strspn(s, " \t");
		len = strcspn(s, " \t");
		host = avahi_strndup(s, len);

		if (*host == 0)
		{
			avahi_log_error ("%s:%d: Error, unexpected end of line!", filename, line);
            break;
		}

        /* Skip past any more spaces */
		s += strspn(s+len, " \t");
        
        /* Anything left? */
		if (*(s+len) != 0) {
			avahi_log_error ("%s:%d: Junk on the end of the line!", filename, line);
            break;
		}

        h = static_host_new();
        h->host = host;
        h->ip = ip;
    }
}

void static_hosts_free_all (void)
{
    StaticHost *h;

    for (h = hosts; h; h = hosts->hosts_next)
    {
        static_host_free (h);
    }
}
