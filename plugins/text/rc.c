/*****************************************************************************
 * rc.c : remote control stdin/stdout plugin for vlc
 *****************************************************************************
 * Copyright (C) 2001 VideoLAN
 * $Id: rc.c,v 1.16 2002/06/01 18:04:49 sam Exp $
 *
 * Authors: Peter Surda <shurdeek@panorama.sth.ac.at>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#include <stdlib.h>                                      /* malloc(), free() */
#include <string.h>

#include <errno.h>                                                 /* ENOMEM */
#include <stdio.h>
#include <ctype.h>

#include <vlc/vlc.h>
#include <vlc/intf.h>
#include <vlc/vout.h>

#ifdef HAVE_UNISTD_H
#    include <unistd.h>
#endif

#ifdef HAVE_SYS_TIME_H
#    include <sys/time.h>
#endif
#include <sys/types.h>

#if defined( WIN32 )
#include <winsock2.h>                                            /* select() */
#endif

/*****************************************************************************
 * intf_sys_t: description and status of rc interface
 *****************************************************************************/
struct intf_sys_s
{
    vlc_mutex_t         change_lock;
};

#define MAX_LINE_LENGTH 256

/*****************************************************************************
 * Local prototypes.
 *****************************************************************************/
static void intf_getfunctions ( function_list_t * p_function_list );
static int  intf_Open         ( intf_thread_t *p_intf );
static void intf_Close        ( intf_thread_t *p_intf );
static void intf_Run          ( intf_thread_t *p_intf );

/*****************************************************************************
 * Build configuration tree.
 *****************************************************************************/
MODULE_CONFIG_START
MODULE_CONFIG_STOP

MODULE_INIT_START
    SET_DESCRIPTION( _("remote control interface module") )
    ADD_CAPABILITY( INTF, 20 )
MODULE_INIT_STOP

MODULE_ACTIVATE_START
    intf_getfunctions( &p_module->p_functions->intf );
MODULE_ACTIVATE_STOP

MODULE_DEACTIVATE_START
MODULE_DEACTIVATE_STOP

/*****************************************************************************
 * Functions exported as capabilities. They are declared as static so that
 * we don't pollute the namespace too much.
 *****************************************************************************/
static void intf_getfunctions( function_list_t * p_function_list )
{
    p_function_list->functions.intf.pf_open  = intf_Open;
    p_function_list->functions.intf.pf_close = intf_Close;
    p_function_list->functions.intf.pf_run   = intf_Run;
}

/*****************************************************************************
 * intf_Open: initialize and create stuff
 *****************************************************************************/
static int intf_Open( intf_thread_t *p_intf )
{
    /* Non-buffered stdout */
    setvbuf( stdout, (char *)NULL, _IOLBF, 0 );

    /* Allocate instance and initialize some members */
    p_intf->p_sys = (intf_sys_t *)malloc( sizeof( intf_sys_t ) );
    if( p_intf->p_sys == NULL )
    {
        msg_Err( p_intf, "out of memory" );
        return( 1 );
    }

#ifdef WIN32
    AllocConsole();
    freopen( "CONOUT$", "w", stdout );
    freopen( "CONOUT$", "w", stderr );
    freopen( "CONIN$", "r", stdin );
    printf( VERSION_MESSAGE "\n" );
#endif

    printf( "remote control interface initialized, `h' for help\n" );
    return( 0 );
}

/*****************************************************************************
 * intf_Close: destroy interface stuff
 *****************************************************************************/
static void intf_Close( intf_thread_t *p_intf )
{
    /* Destroy structure */
    free( p_intf->p_sys );
}

/*****************************************************************************
 * intf_Run: rc thread
 *****************************************************************************
 * This part of the interface is in a separate thread so that we can call
 * exec() from within it without annoying the rest of the program.
 *****************************************************************************/
static void intf_Run( intf_thread_t *p_intf )
{
    char       p_buffer[ MAX_LINE_LENGTH + 1 ];
    vlc_bool_t b_complete = 0;

    int        i_dummy;
    off_t      i_oldpos = 0;
    off_t      i_newpos;
    fd_set     fds;                                        /* stdin changed? */
    struct timeval tv;                                   /* how long to wait */

    double     f_ratio = 1;

    input_thread_t *p_input;

    while( !p_intf->b_die )
    {
        b_complete = 0;

        /* Check stdin */
        tv.tv_sec = 0;
        tv.tv_usec = 50000;
        FD_ZERO( &fds );
        FD_SET( STDIN_FILENO, &fds );

        i_dummy = select( 32, &fds, NULL, NULL, &tv );
        if( i_dummy > 0 )
        {
            int i_size = 0;

            while( !p_intf->b_die
                    && i_size < MAX_LINE_LENGTH
                    && read( STDIN_FILENO, p_buffer + i_size, 1 ) > 0
                    && p_buffer[ i_size ] != '\r'
                    && p_buffer[ i_size ] != '\n' )
            {
                i_size++;
            }

            if( i_size == MAX_LINE_LENGTH
                 || p_buffer[ i_size ] == '\r'
                 || p_buffer[ i_size ] == '\n' )
            {
                p_buffer[ i_size ] = 0;
                b_complete = 1;
            }
        }

        /* Manage the input part */
        p_input = vlc_object_find( p_intf->p_vlc,
                                   VLC_OBJECT_INPUT, FIND_CHILD );

        if( p_input )
        {
            /* Get position */
            vlc_mutex_lock( &p_input->stream.stream_lock );
            if( !p_input->b_die && p_input->stream.i_mux_rate )
            {
#define A p_input->stream.p_selected_area
                f_ratio = 1.0 / ( 50 * p_input->stream.i_mux_rate );
                i_newpos = A->i_tell * f_ratio;

                if( i_oldpos != i_newpos )
                {
                    i_oldpos = i_newpos;
                    printf( "pos: %li s / %li s\n", (long int)i_newpos,
                            (long int)(f_ratio * A->i_size) );
                }
#undef S
            }
            vlc_mutex_unlock( &p_input->stream.stream_lock );
        }

        /* Is there something to do? */
        if( b_complete == 1 )
        {
            char *p_cmd = p_buffer;

            switch( p_cmd[0] )
            {
            case 'a':
            case 'A':
                if( p_cmd[1] == ' ' )
                {
//                    playlist_Add( p_intf, PLAYLIST_END, p_cmd + 2 );
//                    playlist_Jumpto( p_intf->p_vlc->p_playlist,
//                                     p_intf->p_vlc->p_playlist->i_size-2 );
                }
                break;

            case 'd':
            case 'D':
                vlc_dumpstructure( p_intf->p_vlc );
                break;

            case 'p':
            case 'P':
                if( p_input )
                {
                    input_SetStatus( p_input, INPUT_STATUS_PAUSE );
                }
                break;

            case 'f':
            case 'F':
                if( p_input )
                {
                    vout_thread_t *p_vout;
                    p_vout = vlc_object_find( p_input,
                                              VLC_OBJECT_VOUT, FIND_CHILD );

                    if( p_vout )
                    {
                        p_vout->i_changes |= VOUT_FULLSCREEN_CHANGE;
                        vlc_object_release( p_vout );
                    }
                }
                break;

            case 's':
            case 'S':
                ;
                break;

            case 'q':
            case 'Q':
                p_intf->p_vlc->b_die = 1;
                break;

            case 'r':
            case 'R':
                if( p_input )
                {
                    for( i_dummy = 1;
                         i_dummy < MAX_LINE_LENGTH && p_cmd[ i_dummy ] >= '0'
                                                   && p_cmd[ i_dummy ] <= '9';
                         i_dummy++ )
                    {
                        ;
                    }

                    p_cmd[ i_dummy ] = 0;
                    input_Seek( p_input, (off_t)atoi( p_cmd + 1 ),
                                INPUT_SEEK_SECONDS | INPUT_SEEK_SET );
                    /* rcreseek(f_cpos); */
                }
                break;

            case '?':
            case 'h':
            case 'H':
                printf( "help for remote control commands\n" );
                printf( "h . . . . . . . . . . . . . . . . . . . . . help\n" );
                printf( "a XYZ . . . . . . . . . . append XYZ to playlist\n" );
                printf( "p . . . . . . . . . . . . . . . . . toggle pause\n" );
                printf( "f . . . . . . . . . . . . . . toggle  fullscreen\n" );
                printf( "r X . . . seek in seconds,  for instance `r 3.5'\n" );
                printf( "q . . . . . . . . . . . . . . . . . . . . . quit\n" );
                printf( "end of help\n" );
                break;

            default:
                printf( "unknown command `%s'\n", p_cmd );
                break;
            }
        }

        if( p_input )
        {
            vlc_object_release( p_input );
        }

        msleep( INTF_IDLE_SLEEP );
    }
}

