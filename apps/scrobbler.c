/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2006-2008 Robert Keevil
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
/*
Audioscrobbler spec at:
http://www.audioscrobbler.net/wiki/Portable_Player_Logging
*/

#include <stdio.h>
#include <config.h>
#include "file.h"
#include "logf.h"
#include "metadata.h"
#include "kernel.h"
#include "audio.h"
#include "core_alloc.h"
#include "settings.h"
#include "ata_idle_notify.h"
#include "filefuncs.h"
#include "appevents.h"

#if CONFIG_RTC
#include "time.h"
#include "timefuncs.h"
#endif

#include "scrobbler.h"

#define SCROBBLER_VERSION "1.1"

/* increment this on any code change that effects output */
#define SCROBBLER_REVISION " 1"

#define SCROBBLER_MAX_CACHE 32
/* longest entry I've had is 323, add a safety margin */
#define SCROBBLER_CACHE_LEN 512

static int scrobbler_cache;

static int cache_pos;
static struct mp3entry scrobbler_entry;
static bool pending = false;
static bool scrobbler_initialised = false;
#if CONFIG_RTC
static time_t timestamp;
#else
static unsigned long timestamp;
#endif

/* Crude work-around for Archos Sims - return a set amount */
#if (CONFIG_CODEC != SWCODEC) && (CONFIG_PLATFORM & PLATFORM_HOSTED)
unsigned long audio_prev_elapsed(void)
{
    return 120000;
}
#endif

static void get_scrobbler_filename(char *path, size_t size)
{
    int used;

#if CONFIG_RTC
    const char *base_filename = ".scrobbler.log";
#else
    const char *base_filename = ".scrobbler-timeless.log";
#endif

/* Get location of USB mass storage area */
#ifdef APPLICATION
#if (CONFIG_PLATFORM & PLATFORM_MAEMO)
    used = snprintf(path, size, "/home/user/MyDocs/%s", base_filename);
#elif (CONFIG_PLATFORM & PLATFORM_ANDROID)
    used = snprintf(path, size, "/sdcard/%s", base_filename);
#elif defined (SAMSUNG_YPR0)
    used = snprintf(path, size, "%s/%s", HOME_DIR, base_filename);
#else /* SDL/unknown RaaA build */
    used = snprintf(path, size, "%s/%s", ROCKBOX_DIR, base_filename);
#endif /* (CONFIG_PLATFORM & PLATFORM_MAEMO) */

#else
    used = snprintf(path, size, "/%s", base_filename);
#endif

    if (used >= (int)size)
    {
        logf("SCROBBLER: not enough buffer space for log file");
        memset(path, 0, size);
    }
}

static void write_cache(void)
{
    int i;
    int fd;

    char scrobbler_file[MAX_PATH];
    get_scrobbler_filename(scrobbler_file, MAX_PATH);

    /* If the file doesn't exist, create it.
    Check at each write since file may be deleted at any time */
    if(!file_exists(scrobbler_file))
    {
        fd = open(scrobbler_file, O_RDWR | O_CREAT, 0666);
        if(fd >= 0)
        {
            fdprintf(fd, "#AUDIOSCROBBLER/" SCROBBLER_VERSION "\n"
                         "#TZ/UNKNOWN\n"
#if CONFIG_RTC
                         "#CLIENT/Rockbox " TARGET_NAME SCROBBLER_REVISION "\n");
#else
                         "#CLIENT/Rockbox " TARGET_NAME SCROBBLER_REVISION " Timeless\n");
#endif

            close(fd);
        }
        else
        {
            logf("SCROBBLER: cannot create log file");
        }
    }

    /* write the cache entries */
    fd = open(scrobbler_file, O_WRONLY | O_APPEND);
    if(fd >= 0)
    {
        logf("SCROBBLER: writing %d entries", cache_pos); 
        /* copy data to temporary storage in case data moves during I/O */
        char temp_buf[SCROBBLER_CACHE_LEN];
        for ( i=0; i < cache_pos; i++ )
        {
            logf("SCROBBLER: write %d", i);
            char* scrobbler_buf = core_get_data(scrobbler_cache);
            ssize_t len = strlcpy(temp_buf, scrobbler_buf+(SCROBBLER_CACHE_LEN*i),
                                        sizeof(temp_buf));
            if (write(fd, temp_buf, len) != len)
                break;
        }
        close(fd);
    }
    else
    {
        logf("SCROBBLER: error writing file");
    }

    /* clear even if unsuccessful - don't want to overflow the buffer */
    cache_pos = 0;
}

static void scrobbler_flush_callback(void *data)
{
    (void)data;
    if (scrobbler_initialised && cache_pos)
        write_cache();
}

static int fgetc(int fd)
{
    unsigned char cb;
    if (read(fd, &cb, 1) != 1)
        return EOF;
    return cb;
}

static void read_last_line(char* last_line, size_t size)
{
    char scrobbler_file[MAX_PATH];
    get_scrobbler_filename(scrobbler_file, MAX_PATH);
    static char buf[SCROBBLER_CACHE_LEN];
    int fd;
    size_t offset = 0;
    int c;

    if(file_exists(scrobbler_file))
    {
        fd = open(scrobbler_file, O_RDONLY, 0666);
        if(fd >= 0)
        {
            offset = -1;
            lseek(fd, offset, SEEK_END);
            c = fgetc(fd);
            if (c == '\n')
                offset = -2;
            c = 0;
            buf[size-1] = '\0';
            size_t i = size-2;
            while (c != '\n')
            {
                int ret = lseek(fd, offset, SEEK_END);
                if (ret != 0)
                    break;
                c = fgetc(fd);
                offset--;
                buf[i] = (char)c;
                i--;
            }
            strcpy(last_line, buf+i+2);

            close(fd);
        }
    }
}

static bool scrobbler_entry_match(struct mp3entry* entry, char* last_line, size_t size)
{
    static char artist[SCROBBLER_CACHE_LEN];
    static char album[SCROBBLER_CACHE_LEN];
    static char title[SCROBBLER_CACHE_LEN];
    //sscanf(last_line, "%s\t%s\t%s\t", artist, album, title);
    size_t i = 0;
    size_t offset = 0;
    while (last_line[offset] != '\t' && last_line[offset] != '\0')
    {
        artist[i++] = last_line[offset];
        offset++;
    }
    artist[i] = '\0';
    offset++;

    i = 0;
    while (last_line[offset] != '\t' && last_line[offset] != '\0')
    {
        album[i++] = last_line[offset];
        offset++;
    }
    album[i] = '\0';
    offset++;

    i = 0;
    while (last_line[offset] != '\t' && last_line[offset] != '\0')
    {
        title[i++] = last_line[offset];
        offset++;
    }
    title[i] = '\0';

    if (strncmp(entry->artist, artist, size) != 0 ||
        strncmp(entry->album, album, size) != 0 ||
        strncmp(entry->title, title, size) != 0)
    {
        return false;
    }

    return true;
}

static void add_to_cache(unsigned long play_length)
{
    if ( cache_pos >= SCROBBLER_MAX_CACHE )
        write_cache();

    int ret;
    char rating = 'S'; /* Skipped */
    char* scrobbler_buf = core_get_data(scrobbler_cache);

    logf("SCROBBLER: add_to_cache[%d]", cache_pos);

    if ( play_length > (scrobbler_entry.length/2) )
        rating = 'L'; /* Listened */

    if (scrobbler_entry.tracknum > 0)
    {
        ret = snprintf(scrobbler_buf+(SCROBBLER_CACHE_LEN*cache_pos),
                SCROBBLER_CACHE_LEN,
                "%s\t%s\t%s\t%d\t%d\t%c\t%ld\t%s\n",
                scrobbler_entry.artist,
                scrobbler_entry.album?scrobbler_entry.album:"",
                scrobbler_entry.title,
                scrobbler_entry.tracknum,
                (int)scrobbler_entry.length/1000,
                rating,
                (long)timestamp,
                scrobbler_entry.mb_track_id?scrobbler_entry.mb_track_id:"");
    } else {
        ret = snprintf(scrobbler_buf+(SCROBBLER_CACHE_LEN*cache_pos),
                SCROBBLER_CACHE_LEN,
                "%s\t%s\t%s\t\t%d\t%c\t%ld\t%s\n",
                scrobbler_entry.artist,
                scrobbler_entry.album?scrobbler_entry.album:"",
                scrobbler_entry.title,
                (int)scrobbler_entry.length/1000,
                rating,
                (long)timestamp,
                scrobbler_entry.mb_track_id?scrobbler_entry.mb_track_id:"");
    }

    static char last_line[SCROBBLER_CACHE_LEN];
    read_last_line(last_line, SCROBBLER_CACHE_LEN);
    if (scrobbler_entry_match(&scrobbler_entry, last_line, SCROBBLER_CACHE_LEN))
        rating = 'S';

    if ( rating == 'S' )
    {
        logf("SCROBBLER: skipped a song; won't add to cache");
    }
    else if ( ret >= SCROBBLER_CACHE_LEN )
    {
        logf("SCROBBLER: entry too long:");
        logf("SCROBBLER: %s", scrobbler_entry.path);
    }
    else
    {
        cache_pos++;
        register_storage_idle_func(scrobbler_flush_callback);
    }

}

static void scrobbler_change_event(void *data)
{
    struct mp3entry *id = (struct mp3entry*)data;
    /* add entry using the previous scrobbler_entry and timestamp */
    if (pending)
        add_to_cache(audio_prev_elapsed());

    /*  check if track was resumed > %50 played
        check for blank artist or track name */
    if ((id->elapsed > (id->length/2)) ||
        (!id->artist ) || (!id->title ) )
    {
        pending = false;
        logf("SCROBBLER: skipping file %s", id->path);
    }
    else
    {
        logf("SCROBBLER: add pending");
        copy_mp3entry(&scrobbler_entry, id);
#if CONFIG_RTC
        timestamp = mktime(get_time());
#else
        timestamp = 0;
#endif
        pending = true;
    }
}

int scrobbler_init(void)
{
    logf("SCROBBLER: init %d", global_settings.audioscrobbler);

    if(!global_settings.audioscrobbler)
        return -1;

    scrobbler_cache = core_alloc("scrobbler", SCROBBLER_MAX_CACHE*SCROBBLER_CACHE_LEN);
    if (scrobbler_cache <= 0)
    {
        logf("SCROOBLER: OOM");
        return -1;
    }

    add_event(PLAYBACK_EVENT_TRACK_CHANGE, false, scrobbler_change_event);
    cache_pos = 0;
    pending = false;
    scrobbler_initialised = true;

    return 1;
}

static void scrobbler_flush_cache(void)
{
    if (scrobbler_initialised)
    {
        /* Add any pending entries to the cache */
        if(pending)
            add_to_cache(audio_prev_elapsed());

        /* Write the cache to disk if needed */
        if (cache_pos)
            write_cache();

        pending = false;
    }
}

void scrobbler_shutdown(void)
{
    scrobbler_flush_cache();

    if (scrobbler_initialised)
    {
        remove_event(PLAYBACK_EVENT_TRACK_CHANGE, scrobbler_change_event);
        scrobbler_initialised = false;
        /* get rid of the buffer */
        core_free(scrobbler_cache);
        scrobbler_cache = 0;
    }
}

void scrobbler_poweroff(void)
{
    if (scrobbler_initialised && pending)
    {
        if ( audio_status() )
            add_to_cache(audio_current_track()->elapsed);
        else
            add_to_cache(audio_prev_elapsed());

        /* scrobbler_shutdown is called later, the cache will be written
        *  make sure the final track isn't added twice when that happens */
        pending = false;
    }
}

bool scrobbler_is_enabled(void)
{
    return scrobbler_initialised;
}
