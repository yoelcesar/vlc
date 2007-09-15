/*****************************************************************************
 * vlc_interface.h: interface access for other threads
 * This library provides basic functions for threads to interact with user
 * interface, such as message output.
 *****************************************************************************
 * Copyright (C) 1999, 2000 the VideoLAN team
 * $Id$
 *
 * Authors: Vincent Seguin <seguin@via.ecp.fr>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#if !defined( __LIBVLC__ )
  #error You are not libvlc or one of its plugins. You cannot include this file
#endif

#ifndef _VLC_INTF_H_
#define _VLC_INTF_H_

# ifdef __cplusplus
extern "C" {
# endif

typedef struct intf_dialog_args_t intf_dialog_args_t;

/**
 * \file
 * This file contains structures and function prototypes for
 * interface management in vlc
 */

/**
 * \defgroup vlc_interface Interface
 * These functions and structures are for interface management
 * @{
 */

/** Describe all interface-specific data of the interface thread */
struct intf_thread_t
{
    VLC_COMMON_MEMBERS

    /* Thread properties and locks */
    vlc_bool_t          b_block;
    vlc_bool_t          b_play;

    /* Specific interfaces */
    intf_console_t *    p_console;                               /** console */
    intf_sys_t *        p_sys;                          /** system interface */

    /** Interface module */
    module_t *   p_module;
    void      ( *pf_run )    ( intf_thread_t * ); /** Run function */

    /** Specific for dialogs providers */
    void ( *pf_show_dialog ) ( intf_thread_t *, int, int,
                               intf_dialog_args_t * );

    /** Interaction stuff */
    vlc_bool_t b_interaction;

    /** Video window callbacks */
    void * ( *pf_request_window ) ( intf_thread_t *, vout_thread_t *,
                                    int *, int *,
                                    unsigned int *, unsigned int * );
    void   ( *pf_release_window ) ( intf_thread_t *, void * );
    int    ( *pf_control_window ) ( intf_thread_t *, void *, int, va_list );

    /* XXX: new message passing stuff will go here */
    vlc_mutex_t  change_lock;
    vlc_bool_t   b_menu_change;
    vlc_bool_t   b_menu;

    /* Provides the ability to switch an interface on the fly */
    char *psz_switch_intf;
};

/** \brief Arguments passed to a dialogs provider
 *  This describes the arguments passed to the dialogs provider. They are
 *  mainly used with INTF_DIALOG_FILE_GENERIC.
 */
struct intf_dialog_args_t
{
    intf_thread_t *p_intf;
    char *psz_title;

    char **psz_results;
    int  i_results;

    void (*pf_callback) ( intf_dialog_args_t * );
    void *p_arg;

    /* Specifically for INTF_DIALOG_FILE_GENERIC */
    char *psz_extensions;
    vlc_bool_t b_save;
    vlc_bool_t b_multiple;

    /* Specific to INTF_DIALOG_INTERACTION */
    interaction_dialog_t *p_dialog;
};

/*****************************************************************************
 * Prototypes
 *****************************************************************************/
#define intf_Create(a,b,c,d) __intf_Create(VLC_OBJECT(a),b,c,d)
VLC_EXPORT( intf_thread_t *, __intf_Create,     ( vlc_object_t *, const char *, int, const char *const * ) );
VLC_EXPORT( int,               intf_RunThread,  ( intf_thread_t * ) );
VLC_EXPORT( void,              intf_StopThread, ( intf_thread_t * ) );
VLC_EXPORT( void,              intf_Destroy,    ( intf_thread_t * ) );

/* If the interface is in the main thread, it should listen both to
 * p_intf->b_die and p_libvlc->b_die */
#define intf_ShouldDie( p_intf ) (p_intf->b_die || p_intf->p_libvlc->b_die )

#define intf_Eject(a,b) __intf_Eject(VLC_OBJECT(a),b)
VLC_EXPORT( int, __intf_Eject, ( vlc_object_t *, const char * ) );

/*@}*/

/*****************************************************************************
 * Macros
 *****************************************************************************/
#if defined( WIN32 ) && !defined( UNDER_CE )
#    define CONSOLE_INTRO_MSG \
         if( !getenv( "PWD" ) || !getenv( "PS1" ) ) /* detect cygwin shell */ \
         { \
         AllocConsole(); \
         freopen( "CONOUT$", "w", stdout ); \
         freopen( "CONOUT$", "w", stderr ); \
         freopen( "CONIN$", "r", stdin ); \
         } \
         msg_Info( p_intf, COPYRIGHT_MESSAGE ); \
         msg_Info( p_intf, _("\nWarning: if you can't access the GUI " \
                             "anymore, open a command-line window, go to the " \
                             "directory where you installed VLC and run " \
                             "\"vlc -I wx\"\n") )
#else
#    define CONSOLE_INTRO_MSG
#endif

/* Interface dialog ids for dialog providers */
#define INTF_DIALOG_FILE_SIMPLE 1
#define INTF_DIALOG_FILE        2
#define INTF_DIALOG_DISC        3
#define INTF_DIALOG_NET         4
#define INTF_DIALOG_CAPTURE     5
#define INTF_DIALOG_SAT         6
#define INTF_DIALOG_DIRECTORY   7

#define INTF_DIALOG_STREAMWIZARD 8
#define INTF_DIALOG_WIZARD 9

#define INTF_DIALOG_PLAYLIST   10
#define INTF_DIALOG_MESSAGES   11
#define INTF_DIALOG_FILEINFO   12
#define INTF_DIALOG_PREFS      13
#define INTF_DIALOG_BOOKMARKS  14
#define INTF_DIALOG_EXTENDED   15

#define INTF_DIALOG_POPUPMENU  20
#define INTF_DIALOG_AUDIOPOPUPMENU  21
#define INTF_DIALOG_VIDEOPOPUPMENU  22
#define INTF_DIALOG_MISCPOPUPMENU  23

#define INTF_DIALOG_FILE_GENERIC 30
#define INTF_DIALOG_INTERACTION 50

#define INTF_DIALOG_UPDATEVLC   90
#define INTF_DIALOG_VLM   91

#define INTF_DIALOG_EXIT       99

/* Useful text messages shared by interfaces */
#define INTF_ABOUT_MSG LICENSE_MSG

#define EXTENSIONS_AUDIO "*.a52;*.aac;*.ac3;*.dts;*.flac;*.m4a;*.m4p;*.mka;" \
                         "*.mod;*.mp1;*.mp2;*.mp3;*.ogg;*.spx;*.wav;*.wma;*.xm"

#define EXTENSIONS_VIDEO "*.asf;*.avi;*.divx;*.dv;*.flv;*.gxf;*.m1v;*.m2v;" \
                         "*.m4v;*.mkv;*.mov;*.mp2;*.mp4;*.mpeg;*.mpeg1;" \
                         "*.mpeg2;*.mpeg4;*.mpg;*.mxf;*.ogg;*.ogm;" \
                         "*.ps;*.ts;*.vob;*.wmv"

#define EXTENSIONS_PLAYLIST "*.asx;*.b4s;*.m3u;*.pls;*.vlc;*.xspf"

#define EXTENSIONS_MEDIA EXTENSIONS_VIDEO ";" EXTENSIONS_AUDIO ";" \
                          EXTENSIONS_PLAYLIST

#define EXTENSIONS_SUBTITLE "*.idx;*.srt;*.sub;*.utf"

/** \defgroup vlc_interaction Interaction
 * \ingroup vlc_interface
 * Interaction between user and modules
 * @{
 */

/**
 * This structure describes a piece of interaction with the user
 */
struct interaction_dialog_t
{
    int             i_id;               ///< Unique ID
    int             i_type;             ///< Type identifier
    char           *psz_title;          ///< Title
    char           *psz_description;    ///< Descriptor string
    char           *psz_default_button;  ///< default button title (~OK)
    char           *psz_alternate_button;///< alternate button title (~NO)
    /// other button title (optional,~Cancel)
    char           *psz_other_button;

    char           *psz_returned[1];    ///< returned responses from the user

    vlc_value_t     val;                ///< value coming from core for dialogue
    int             i_timeToGo;         ///< time (in sec) until shown progress is finished
    vlc_bool_t      b_cancelled;        ///< was the dialogue cancelled ?

    void *          p_private;          ///< Private interface data

    int             i_status;           ///< Dialog status;
    int             i_action;           ///< Action to perform;
    int             i_flags;            ///< Misc flags
    int             i_return;           ///< Return status

    interaction_t  *p_interaction;      ///< Parent interaction object
    vlc_object_t   *p_parent;           ///< The vlc object that asked
                                        //for interaction
};
/**
 * Possible flags . Dialog types
 */
#define DIALOG_GOT_ANSWER           0x01
#define DIALOG_YES_NO_CANCEL        0x02
#define DIALOG_LOGIN_PW_OK_CANCEL   0x04
#define DIALOG_PSZ_INPUT_OK_CANCEL  0x08
#define DIALOG_BLOCKING_ERROR       0x10
#define DIALOG_NONBLOCKING_ERROR    0x20
#define DIALOG_WARNING              0x40
#define DIALOG_USER_PROGRESS        0x80
#define DIALOG_INTF_PROGRESS        0x100

/** Possible return codes */
enum
{
    DIALOG_DEFAULT,
    DIALOG_OK_YES,
    DIALOG_NO,
    DIALOG_CANCELLED
};

/** Possible status  */
enum
{
    NEW_DIALOG,                 ///< Just created
    SENT_DIALOG,                ///< Sent to interface
    UPDATED_DIALOG,             ///< Update to send
    ANSWERED_DIALOG,            ///< Got "answer"
    HIDING_DIALOG,              ///< Hiding requested
    HIDDEN_DIALOG,              ///< Now hidden. Requesting destruction
    DESTROYED_DIALOG,           ///< Interface has destroyed it
};

/** Possible interaction types */
enum
{
    INTERACT_DIALOG_ONEWAY,     ///< Dialog box without feedback
    INTERACT_DIALOG_TWOWAY,     ///< Dialog box with feedback
};

/** Possible actions */
enum
{
    INTERACT_NEW,
    INTERACT_UPDATE,
    INTERACT_HIDE,
    INTERACT_DESTROY
};

/**
 * This structure contains the active interaction dialogs, and is
 * used by the manager
 */
struct interaction_t
{
    VLC_COMMON_MEMBERS

    int                         i_dialogs;      ///< Number of dialogs
    interaction_dialog_t      **pp_dialogs;     ///< Dialogs
    intf_thread_t              *p_intf;         ///< Interface to use
    int                         i_last_id;      ///< Last attributed ID
};

/***************************************************************************
 * Exported symbols
 ***************************************************************************/

#define intf_UserFatal( a, b, c, d, e... ) __intf_UserFatal( VLC_OBJECT(a),b,c,d, ## e )
VLC_EXPORT( int, __intf_UserFatal,( vlc_object_t*, vlc_bool_t, const char*, const char*, ...) ATTRIBUTE_FORMAT( 4, 5 ) );
#define intf_UserWarn( a, c, d, e... ) __intf_UserWarn( VLC_OBJECT(a),c,d, ## e )
VLC_EXPORT( int, __intf_UserWarn,( vlc_object_t*, const char*, const char*, ...) ATTRIBUTE_FORMAT( 3, 4 ) );
#define intf_UserLoginPassword( a, b, c, d, e... ) __intf_UserLoginPassword( VLC_OBJECT(a),b,c,d,e)
VLC_EXPORT( int, __intf_UserLoginPassword,( vlc_object_t*, const char*, const char*, char **, char **) );
#define intf_UserYesNo( a, b, c, d, e, f ) __intf_UserYesNo( VLC_OBJECT(a),b,c, d, e, f )
VLC_EXPORT( int, __intf_UserYesNo,( vlc_object_t*, const char*, const char*, const char*, const char*, const char*) );
#define intf_UserStringInput( a, b, c, d ) __intf_UserStringInput( VLC_OBJECT(a),b,c,d )
VLC_EXPORT( int, __intf_UserStringInput,(vlc_object_t*, const char*, const char*, char **) );

#define intf_IntfProgress( a, b, c ) __intf_Progress( VLC_OBJECT(a), NULL, b,c, -1 )
#define intf_UserProgress( a, b, c, d, e ) __intf_Progress( VLC_OBJECT(a),b,c,d,e )
VLC_EXPORT( int, __intf_Progress,( vlc_object_t*, const char*, const char*, float, int) );
#define intf_ProgressUpdate( a, b, c, d, e ) __intf_ProgressUpdate( VLC_OBJECT(a),b,c,d,e )
VLC_EXPORT( void, __intf_ProgressUpdate,( vlc_object_t*, int, const char*, float, int) );
#define intf_ProgressIsCancelled( a, b ) __intf_UserProgressIsCancelled( VLC_OBJECT(a),b )
VLC_EXPORT( vlc_bool_t, __intf_UserProgressIsCancelled,( vlc_object_t*, int ) );
#define intf_UserHide( a, b ) __intf_UserHide( VLC_OBJECT(a), b )
VLC_EXPORT( void, __intf_UserHide,( vlc_object_t *, int ));

/** @} */
/** @} */

# ifdef __cplusplus
}
# endif
#endif
