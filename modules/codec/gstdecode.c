/*****************************************************************************
 * gstdecode.c: Decoder module making use of gstreamer
 *****************************************************************************
 * Copyright (C) 2014 VLC authors and VideoLAN
 * $Id:
 *
 * Author: Vikram Fugro <vikram.fugro@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_codec.h>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/app/gstappsrc.h>
#include <gst/gstatomicqueue.h>

struct decoder_sys_t
{
    GstElement *p_decoder;
    GstElement *p_decode_src;
    GstElement *p_decode_in;
    GstElement *p_decode_out;

    GstBus *p_bus;

    GstVideoInfo vinfo;
    GstAtomicQueue *p_que;
    bool b_prerolled;
    bool b_running;
};

typedef struct
{
    GstCaps *p_sinkcaps;
    GstCaps *p_srccaps;
} sink_src_caps_t;

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int  OpenDecoder( vlc_object_t* );
static void CloseDecoder( vlc_object_t* );
static picture_t *DecodeBlock( decoder_t*, block_t** );

#define MODULE_DESCRIPTION N_( "Uses GStreamer framework's plugins " \
        "to decode the media codecs" )

#define USEDECODEBIN_TEXT N_( "Use DecodeBin" )
#define USEDECODEBIN_LONGTEXT N_( \
    "DecodeBin is a container element, that can add and " \
    "manage multiple elements. Apart from adding the decoders, " \
    "decodebin also adds elementary stream parsers which can provide " \
    "more info such as codec profile, level and other attributes, " \
    "in the form of GstCaps (Stream Capabilities) to decoder." )

vlc_module_begin( )
    set_shortname( "GstDecode" )
    add_shortcut( "gstdecode" )
    set_category( CAT_INPUT )
    set_subcategory( SUBCAT_INPUT_VCODEC )
    /* decoder main module */
    set_description( N_( "GStreamer Based Decoder" ) )
    set_help( MODULE_DESCRIPTION )
    set_capability( "decoder", 50 )
    set_section( N_( "Decoding" ) , NULL )
    set_callbacks( OpenDecoder, CloseDecoder )
    add_bool( "use-decodebin", false, USEDECODEBIN_TEXT,
        USEDECODEBIN_LONGTEXT, false )
vlc_module_end( )

/* gst_init( ) is not thread-safe, hence a thread-safe wrapper */
static void vlc_gst_init( void )
{
    static vlc_mutex_t init_lock = VLC_STATIC_MUTEX;

    vlc_mutex_lock( &init_lock );
    gst_init( NULL, NULL );
    vlc_mutex_unlock( &init_lock );
}

static GstStructure* vlc_to_gst_fmt( const es_format_t *p_fmt )
{
    const video_format_t *p_vfmt = &p_fmt->video;
    GstStructure *p_str = NULL;

    switch( p_fmt->i_codec ){
    case VLC_CODEC_H264:
        p_str = gst_structure_new_empty( "video/x-h264" );
        gst_structure_set( p_str, "alignment", G_TYPE_STRING, "au", NULL );
        break;
    case VLC_CODEC_MP4V:
        p_str = gst_structure_new_empty( "video/mpeg" );
        gst_structure_set( p_str, "mpegversion", G_TYPE_INT, 4,
                "systemstream", G_TYPE_BOOLEAN, FALSE, NULL );
        break;
    case VLC_CODEC_VP8:
        p_str = gst_structure_new_empty( "video/x-vp8" );
        break;
    case VLC_CODEC_MPGV:
        p_str = gst_structure_new_empty( "video/mpeg" );
        gst_structure_set( p_str, "mpegversion", G_TYPE_INT, 2,
                "systemstream", G_TYPE_BOOLEAN, FALSE, NULL );
        break;
    case VLC_CODEC_FLV1:
        p_str = gst_structure_new_empty( "video/x-flash-video" );
        gst_structure_set( p_str, "flvversion", G_TYPE_INT, 1, NULL );
        break;
    case VLC_CODEC_WMV1:
        p_str = gst_structure_new_empty( "video/x-wmv" );
        gst_structure_set( p_str, "wmvversion", G_TYPE_INT, 1,
                "format", G_TYPE_STRING, "WMV1", NULL );
        break;
    case VLC_CODEC_WMV2:
        p_str = gst_structure_new_empty( "video/x-wmv" );
        gst_structure_set( p_str, "wmvversion", G_TYPE_INT, 2,
                "format", G_TYPE_STRING, "WMV2", NULL );
        break;
    case VLC_CODEC_WMV3:
        p_str = gst_structure_new_empty( "video/x-wmv" );
        gst_structure_set( p_str, "wmvversion", G_TYPE_INT, 3,
                "format", G_TYPE_STRING, "WMV3", NULL );
        break;
    case VLC_CODEC_VC1:
        p_str = gst_structure_new_empty( "video/x-wmv" );
        gst_structure_set( p_str, "wmvversion", G_TYPE_INT, 3,
                "format", G_TYPE_STRING, "WVC1", NULL );
        break;
    default:
        /* unsupported codec */
        return NULL;
    }

    if( p_vfmt->i_width && p_vfmt->i_height )
        gst_structure_set( p_str,
                "width", G_TYPE_INT, p_vfmt->i_width,
                "height", G_TYPE_INT, p_vfmt->i_height, NULL );

    if( p_vfmt->i_frame_rate && p_vfmt->i_frame_rate_base )
        gst_structure_set( p_str, "framerate", GST_TYPE_FRACTION,
                p_vfmt->i_frame_rate,
                p_vfmt->i_frame_rate_base, NULL );

    if( p_vfmt->i_sar_num && p_vfmt->i_sar_den )
        gst_structure_set( p_str, "pixel-aspect-ratio", GST_TYPE_FRACTION,
                p_vfmt->i_sar_num,
                p_vfmt->i_sar_den, NULL );

    if( p_fmt->i_extra )
    {
        GstBuffer *p_buf;

        p_buf = gst_buffer_new_wrapped_full( GST_MEMORY_FLAG_READONLY,
                p_fmt->p_extra, p_fmt->i_extra, 0,
                p_fmt->i_extra, NULL, NULL );
        if( p_buf == NULL )
        {
            gst_structure_free( p_str );
            return NULL;
        }

        gst_structure_set( p_str, "codec_data", GST_TYPE_BUFFER, p_buf, NULL );
        gst_buffer_unref( p_buf );
    }

    return p_str;
}

/* Emitted by appsrc when serving a seek request.
 * Seek over here is only used for flushing the buffers.
 * Returns TRUE always, as the 'real' seek will be
 * done by VLC framework */
static gboolean seek_data_cb( GstAppSrc *p_src, guint64 l_offset,
        gpointer p_data )
{
    VLC_UNUSED( p_src );
    decoder_t *p_dec = p_data;
    msg_Dbg( p_dec, "appsrc seeking to %"G_GUINT64_FORMAT, l_offset );
    return TRUE;
}

/* Emitted by decodebin when there are no more
 * outputs.This signal is not really necessary
 * to be connected. It is connected here for sanity
 * check only, just in-case something unexpected
 * happens inside decodebin in finding the appropriate
 * decoder, and it fails to emit PAD_ADDED signal */
static void no_more_pads_cb( GstElement *p_ele, gpointer p_data )
{
    VLC_UNUSED( p_ele );
    decoder_t *p_dec = p_data;
    decoder_sys_t *p_sys = p_dec->p_sys;
    GstPad *p_pad;

    msg_Dbg( p_dec, "no more pads" );

    p_pad = gst_element_get_static_pad( p_sys->p_decode_out,
            "sink" );
    if( !gst_pad_is_linked( p_pad ) )
    {
        msg_Err( p_dec, "failed to link decode out pad" );
        GST_ELEMENT_ERROR( p_sys->p_decoder, STREAM, FAILED,
                ( "vlc stream error" ), NULL );
    }

    gst_object_unref( p_pad );
}

/* Sets the video output format */
static bool set_vout_format( GstStructure* p_str,
        const es_format_t *restrict p_infmt, es_format_t *restrict p_outfmt )
{
    video_format_t *p_voutfmt = &p_outfmt->video;
    const video_format_t *p_vinfmt = &p_infmt->video;
    gboolean b_ret;

    /* We are interested in system memory raw buffers for now,
     * but support for opaque data formats can also be added.
     * For eg. when using HW decoders for zero-copy */
    p_outfmt->i_codec = vlc_fourcc_GetCodecFromString(
            VIDEO_ES,
            gst_structure_get_string( p_str, "format" ) );
    if( !p_outfmt->i_codec )
        return false;

    gst_structure_get_int( p_str, "width", &p_voutfmt->i_width );
    gst_structure_get_int( p_str, "height", &p_voutfmt->i_height );

    b_ret = gst_structure_get_fraction( p_str,
            "pixel-aspect-ratio",
            &p_voutfmt->i_sar_num,
            &p_voutfmt->i_sar_den );

    if( !b_ret || !p_voutfmt->i_sar_num ||
            !p_voutfmt->i_sar_den )
    {
        p_voutfmt->i_sar_num = 1;
        p_voutfmt->i_sar_den = 1;
    }

    b_ret = gst_structure_get_fraction( p_str, "framerate",
            &p_voutfmt->i_frame_rate,
            &p_voutfmt->i_frame_rate_base );

    if( !b_ret || !p_voutfmt->i_frame_rate ||
            !p_voutfmt->i_frame_rate_base )
    {
        p_voutfmt->i_frame_rate = p_vinfmt->i_frame_rate;
        p_voutfmt->i_frame_rate_base = p_vinfmt->i_frame_rate_base;
    }

    return true;
}

static bool set_out_fmt( decoder_t *p_dec, GstPad *p_pad )
{
    GstCaps *p_caps = gst_pad_get_current_caps( p_pad );
    decoder_sys_t *p_sys = p_dec->p_sys;
    GstStructure *p_str;

    if( !gst_video_info_from_caps( &p_sys->vinfo,
                p_caps ) )
    {
        msg_Err( p_dec, "failed to get video info from caps" );
        gst_caps_unref( p_caps );
        GST_ELEMENT_ERROR( p_sys->p_decoder, STREAM, FAILED,
                ( "vlc stream error" ), NULL );
        return false;
    }

    p_str = gst_caps_get_structure( p_caps, 0 );

    if( !set_vout_format( p_str, &p_dec->fmt_in, &p_dec->fmt_out ) )
    {
        msg_Err( p_dec, "failed to set out format" );
        gst_caps_unref( p_caps );
        GST_ELEMENT_ERROR( p_sys->p_decoder, STREAM, FAILED,
                ( "vlc stream error" ), NULL );
        return false;
    }

    gst_caps_unref( p_caps );
    return true;
}

/* Emitted by decodebin and links decodebin to fakesink.
 * Since only one elementary codec stream is fed to decodebin,
 * this signal cannot be emitted more than once. */
static void pad_added_cb( GstElement *p_ele, GstPad *p_pad, gpointer p_data )
{
    VLC_UNUSED( p_ele );
    decoder_t *p_dec = p_data;
    decoder_sys_t *p_sys = p_dec->p_sys;

    if( likely( gst_pad_has_current_caps( p_pad ) ) )
    {
        GstPad *p_sinkpad;

        if( !set_out_fmt( p_dec, p_pad ) )
            return;

        p_sinkpad = gst_element_get_static_pad(
                p_sys->p_decode_out, "sink" );
        gst_pad_link( p_pad, p_sinkpad );
        gst_object_unref( p_sinkpad );
    }
    else
    {
        msg_Err( p_dec, "decodebin src pad has no caps" );
        GST_ELEMENT_ERROR( p_sys->p_decoder, STREAM, FAILED,
                ( "vlc stream error" ), NULL );
    }
}

/* Emitted by fakesink for every buffer and sets the
 * output format (if not set). Adds the buffer to the queue */
static void frame_handoff_cb( GstElement *p_ele, GstBuffer *p_buf,
        GstPad *p_pad, gpointer p_data )
{
    VLC_UNUSED( p_ele );
    decoder_t *p_dec = p_data;
    decoder_sys_t *p_sys = p_dec->p_sys;

    if( unlikely( p_dec->fmt_out.i_codec == 0 ) )
    {
        if( !gst_pad_has_current_caps( p_pad ) )
        {
            msg_Err( p_dec, "fakesink pad has no caps" );
            GST_ELEMENT_ERROR( p_sys->p_decoder, STREAM, FAILED,
                    ( "vlc stream error" ), NULL );
            return;
        }

        if( !set_out_fmt( p_dec, p_pad ) )
            return;
    }

    /* Push the buffer to the queue */
    gst_atomic_queue_push( p_sys->p_que, gst_buffer_ref( p_buf ) );
}

/* Copy the frame data from the GstBuffer (from decoder)
 * to the picture obtained from downstream in VLC.
 * TODO(Zero-Copy): This function should be avoided as much
 * as possible, since it involves a complete frame copy. */
static void gst_CopyPicture( picture_t *p_pic, GstVideoFrame *p_frame )
{
    int i_plane, i_planes, i_line, i_dst_stride, i_src_stride;
    uint8_t *p_dst, *p_src;
    int i_w, i_h;

    i_planes = p_pic->i_planes;
    for( i_plane = 0; i_plane < i_planes; i_plane++ )
    {
        p_dst = p_pic->p[i_plane].p_pixels;
        p_src = GST_VIDEO_FRAME_PLANE_DATA( p_frame, i_plane );
        i_dst_stride = p_pic->p[i_plane].i_pitch;
        i_src_stride = GST_VIDEO_FRAME_PLANE_STRIDE( p_frame, i_plane );

        i_w = GST_VIDEO_FRAME_COMP_WIDTH( p_frame,
                i_plane ) * GST_VIDEO_FRAME_COMP_PSTRIDE( p_frame, i_plane );
        i_h = GST_VIDEO_FRAME_COMP_HEIGHT( p_frame, i_plane );

        for( i_line = 0;
                i_line < __MIN( p_pic->p[i_plane].i_lines, i_h );
                i_line++ )
        {
            memcpy( p_dst, p_src, i_w );
            p_src += i_src_stride;
            p_dst += i_dst_stride;
        }
    }
}

/* Check if the element can use this caps */
static gint find_decoder_func( gconstpointer p_p1, gconstpointer p_p2 )
{
    GstElementFactory *p_factory;
    sink_src_caps_t *p_caps;

    p_factory = ( GstElementFactory* )p_p1;
    p_caps = ( sink_src_caps_t* )p_p2;

    return !( gst_element_factory_can_sink_any_caps( p_factory,
                p_caps->p_sinkcaps ) &&
            gst_element_factory_can_src_any_caps( p_factory,
                p_caps->p_srccaps ) );
}

static bool default_msg_handler( decoder_t *p_dec, GstMessage *p_msg )
{
    bool err = false;

    switch( GST_MESSAGE_TYPE( p_msg ) ){
    case GST_MESSAGE_ERROR:
        {
            gchar  *psz_debug;
            GError *p_error;

            gst_message_parse_error( p_msg, &p_error, &psz_debug );
            g_free( psz_debug );

            msg_Err( p_dec, "Error from %s: %s",
                    GST_ELEMENT_NAME( GST_MESSAGE_SRC( p_msg ) ),
                    p_error->message );
            g_error_free( p_error );
            err = true;
        }
        break;
    case GST_MESSAGE_WARNING:
        {
            gchar  *psz_debug;
            GError *p_error;

            gst_message_parse_warning( p_msg, &p_error, &psz_debug );
            g_free( psz_debug );

            msg_Warn( p_dec, "Warning from %s: %s",
                    GST_ELEMENT_NAME( GST_MESSAGE_SRC( p_msg ) ),
                    p_error->message );
            g_error_free( p_error );
        }
        break;
    case GST_MESSAGE_INFO:
        {
            gchar  *psz_debug;
            GError *p_error;

            gst_message_parse_info( p_msg, &p_error, &psz_debug );
            g_free( psz_debug );

            msg_Info( p_dec, "Info from %s: %s",
                    GST_ELEMENT_NAME( GST_MESSAGE_SRC( p_msg ) ),
                    p_error->message );
            g_error_free( p_error );
        }
        break;
    default:
        break;
    }

    return err;
}

/*****************************************************************************
 * OpenDecoder: probe the decoder and return score
 *****************************************************************************/
static int OpenDecoder( vlc_object_t *p_this )
{
    decoder_t *p_dec = ( decoder_t* )p_this;
    decoder_sys_t *p_sys;
    GstStateChangeReturn i_ret;
    gboolean b_ret;
    sink_src_caps_t caps = { NULL, NULL };
    GstStructure *p_str;
    GstAppSrcCallbacks cb;
    int i_rval = VLC_SUCCESS;
    GList *p_list;
    bool dbin;

#define VLC_GST_CHECK( r, v, s, t ) \
    { if( r == v ){ msg_Err( p_dec, s ); i_rval = t; goto fail; } }

    vlc_gst_init( );

    p_str = vlc_to_gst_fmt( &p_dec->fmt_in );
    if( !p_str )
        return VLC_EGENERIC;

    /* Allocate the memory needed to store the decoder's structure */
    p_sys = p_dec->p_sys = calloc( 1, sizeof( *p_sys ) );
    if( p_sys == NULL )
    {
        gst_structure_free( p_str );
        return VLC_ENOMEM;
    }

    dbin = var_CreateGetBool( p_dec, "use-decodebin" );
    msg_Dbg( p_dec, "Using decodebin? %s", dbin ? "yes ":"no" );

    caps.p_sinkcaps = gst_caps_new_empty( );
    gst_caps_append_structure( caps.p_sinkcaps, p_str );
    /* Currently supports only system memory raw output format */
    caps.p_srccaps = gst_caps_new_empty_simple( "video/x-raw" );

    /* Get the list of all the available gstreamer decoders */
    p_list = gst_element_factory_list_get_elements(
            GST_ELEMENT_FACTORY_TYPE_DECODER, GST_RANK_MARGINAL );
    VLC_GST_CHECK( p_list, NULL, "no decoder list found", VLC_ENOMOD );
    if( !dbin )
    {
        GList *p_l;
        /* Sort them as per ranks */
        p_list = g_list_sort( p_list, gst_plugin_feature_rank_compare_func );
        VLC_GST_CHECK( p_list, NULL, "failed to sort decoders list",
                VLC_ENOMOD );
        p_l = g_list_find_custom( p_list, &caps, find_decoder_func );
        VLC_GST_CHECK( p_l, NULL, "no suitable decoder found",
                VLC_ENOMOD );
        /* create the decoder with highest rank */
        p_sys->p_decode_in = gst_element_factory_create(
                ( GstElementFactory* )p_l->data, NULL );
        VLC_GST_CHECK( p_sys->p_decode_in, NULL,
                "failed to create decoder", VLC_ENOMOD );
    }
    else
    {
        GList *p_l;
        /* Just check if any suitable decoder exists, rest will be
         * handled by decodebin */
        p_l = g_list_find_custom( p_list, &caps, find_decoder_func );
        VLC_GST_CHECK( p_l, NULL, "no suitable decoder found",
                VLC_ENOMOD );
    }
    gst_plugin_feature_list_free( p_list );
    p_list = NULL;
    gst_caps_unref( caps.p_srccaps );
    caps.p_srccaps = NULL;

    p_sys->b_prerolled = false;
    p_sys->b_running = false;

    /* Queue: GStreamer thread will dump buffers into this queue,
     * DecodeBlock() will pop out the buffers from the queue */
    p_sys->p_que = gst_atomic_queue_new( 0 );
    VLC_GST_CHECK( p_sys->p_que, NULL, "failed to create queue",
            VLC_ENOMEM );

    p_sys->p_decode_src = gst_element_factory_make( "appsrc", NULL );
    VLC_GST_CHECK( p_sys->p_decode_src, NULL, "appsrc not found",
            VLC_ENOMOD );
    g_object_set( G_OBJECT( p_sys->p_decode_src ), "caps", caps.p_sinkcaps,
            "block", FALSE, "emit-signals", TRUE, "format", GST_FORMAT_BYTES,
            "stream-type", GST_APP_STREAM_TYPE_SEEKABLE,
            /* Making DecodeBlock() to block on appsrc with max queue size of 1 byte.
             * This will make the push_buffer() tightly coupled with the buffer
             * flow from appsrc -> decoder. push_buffer() will only return when
             * the same buffer it just fed to appsrc has also been fed to the
             * decoder element as well */
            "block", TRUE, "max-bytes", ( guint64 )1, NULL );
    gst_caps_unref( caps.p_sinkcaps );
    caps.p_sinkcaps = NULL;
    cb.enough_data = cb.need_data = NULL;
    cb.seek_data = seek_data_cb;
    gst_app_src_set_callbacks( GST_APP_SRC( p_sys->p_decode_src ),
            &cb, p_dec, NULL );

    if( dbin )
    {
        p_sys->p_decode_in = gst_element_factory_make( "decodebin", NULL );
        VLC_GST_CHECK( p_sys->p_decode_in, NULL, "decodebin not found",
                VLC_ENOMOD );
        //g_object_set( G_OBJECT( p_sys->p_decode_in ),
        //"max-size-buffers", 2, NULL );
        g_signal_connect( G_OBJECT( p_sys->p_decode_in ), "pad-added",
                G_CALLBACK( pad_added_cb ), p_dec );
        g_signal_connect( G_OBJECT( p_sys->p_decode_in ), "no-more-pads",
                G_CALLBACK( no_more_pads_cb ), p_dec );
    }

    /* fakesink: will emit signal for every available buffer */
    p_sys->p_decode_out = gst_element_factory_make( "fakesink", NULL );
    VLC_GST_CHECK( p_sys->p_decode_out, NULL, "fakesink not found",
            VLC_ENOMOD );
    /* connect to the signal with the callback */
    g_object_set( G_OBJECT( p_sys->p_decode_out ), "sync", FALSE,
            "enable-last-sample", FALSE, "signal-handoffs", TRUE, NULL );
    g_signal_connect( G_OBJECT( p_sys->p_decode_out ), "handoff",
            G_CALLBACK( frame_handoff_cb ), p_dec );

    p_sys->p_decoder = GST_ELEMENT( gst_bin_new( "decoder" ) );
    VLC_GST_CHECK( p_sys->p_decoder, NULL, "bin not found", VLC_ENOMOD );
    p_sys->p_bus = gst_bus_new( );
    VLC_GST_CHECK( p_sys->p_bus, NULL, "failed to create bus",
            VLC_ENOMOD );
    gst_element_set_bus( p_sys->p_decoder, p_sys->p_bus );

    gst_bin_add_many( GST_BIN( p_sys->p_decoder ),
            p_sys->p_decode_src, p_sys->p_decode_in,
            p_sys->p_decode_out, NULL );
    gst_object_ref( p_sys->p_decode_src );
    gst_object_ref( p_sys->p_decode_in );
    gst_object_ref( p_sys->p_decode_out );

    b_ret = gst_element_link( p_sys->p_decode_src, p_sys->p_decode_in );
    VLC_GST_CHECK( b_ret, FALSE, "failed to link src <-> in",
            VLC_EGENERIC );

    if( !dbin )
    {
        b_ret = gst_element_link( p_sys->p_decode_in, p_sys->p_decode_out );
        VLC_GST_CHECK( b_ret, FALSE, "failed to link in <-> out",
                VLC_EGENERIC );
    }

    p_dec->fmt_out.i_cat = p_dec->fmt_in.i_cat;

    /* set the pipeline to playing */
    i_ret = gst_element_set_state( p_sys->p_decoder, GST_STATE_PLAYING );
    VLC_GST_CHECK( i_ret, GST_STATE_CHANGE_FAILURE,
            "set state failure", VLC_EGENERIC );
    p_sys->b_running = true;

    /* Force packetized for now */
    p_dec->b_need_packetized = true;
    /* Set callbacks */
    p_dec->pf_decode_video = DecodeBlock;

    return VLC_SUCCESS;

fail:
    if( caps.p_sinkcaps )
        gst_caps_unref( caps.p_sinkcaps );
    if( caps.p_srccaps )
        gst_caps_unref( caps.p_srccaps );
    if( p_list )
        gst_plugin_feature_list_free( p_list );
    CloseDecoder( ( vlc_object_t* )p_dec );
    return i_rval;
}

/* Decode */
static picture_t *DecodeBlock( decoder_t *p_dec, block_t **pp_block )
{
    block_t *p_block;
    picture_t *p_pic = NULL;
    decoder_sys_t *p_sys = p_dec->p_sys;
    GstMessage *p_msg;
    gboolean b_ret;
    GstBuffer *p_buf;

    if( !pp_block )
        return NULL;

    p_block = *pp_block;

    if( !p_block )
        goto check_messages;

    if( unlikely( p_block->i_flags & (BLOCK_FLAG_DISCONTINUITY |
                    BLOCK_FLAG_CORRUPTED ) ) )
    {
        if( p_block->i_flags & BLOCK_FLAG_DISCONTINUITY )
        {
            GstBuffer *p_buffer;
            /* Send a new segment event. Seeking position is
             * irrelevant in this case, as the main motive for a
             * seek here, is to tell the elements to start flushing
             * and start accepting buffers from a new time segment */
            b_ret = gst_element_seek_simple( p_sys->p_decoder,
                    GST_FORMAT_BYTES, GST_SEEK_FLAG_FLUSH, 0 );
            msg_Dbg( p_dec, "new segment event : %d", b_ret );

            /* flush the output buffers from the queue */
            while( ( p_buffer = gst_atomic_queue_pop( p_sys->p_que ) ) )
                gst_buffer_unref( p_buffer );

            p_sys->b_prerolled = false;
        }

        block_Release( p_block );
        goto done;
    }

    if( likely( p_block->i_buffer ) )
    {
        p_buf = gst_buffer_new_wrapped_full( GST_MEMORY_FLAG_READONLY,
                p_block->p_start, p_block->i_size,
                p_block->p_buffer - p_block->p_start, p_block->i_buffer,
                p_block, ( GDestroyNotify )block_Release );
        if( unlikely( p_buf == NULL ) )
        {
            msg_Err( p_dec, "failed to create input gstbuffer" );
            p_dec->b_error = true;
            block_Release( p_block );
            goto done;
        }

        if( p_block->i_dts > VLC_TS_INVALID )
            GST_BUFFER_DTS( p_buf ) = gst_util_uint64_scale( p_block->i_dts,
                    GST_SECOND, GST_MSECOND );

        if( p_block->i_pts <= VLC_TS_INVALID )
            GST_BUFFER_PTS( p_buf ) = GST_BUFFER_DTS( p_buf );
        else
            GST_BUFFER_PTS( p_buf ) = gst_util_uint64_scale( p_block->i_pts,
                    GST_SECOND, GST_MSECOND );

        if( p_block->i_length > VLC_TS_INVALID )
            GST_BUFFER_DURATION( p_buf ) = gst_util_uint64_scale(
                    p_block->i_length, GST_SECOND, GST_MSECOND );

        if( p_dec->fmt_in.video.i_frame_rate  &&
                p_dec->fmt_in.video.i_frame_rate_base )
            GST_BUFFER_DURATION( p_buf ) = gst_util_uint64_scale( GST_SECOND,
                    p_dec->fmt_in.video.i_frame_rate_base,
                    p_dec->fmt_in.video.i_frame_rate );

        /* Give the input buffer to GStreamer Bin.
         *
         *  libvlc                      libvlc
         *    \ (i/p)              (o/p) ^
         *     \                        /
         *   ___v____GSTREAMER BIN_____/____
         *  |                               |
         *  |   appsrc-->decode-->fakesink  |
         *  |_______________________________|
         *
         * * * * * * * * * * * * * * * * * * * * */
        if( unlikely( gst_app_src_push_buffer(
                        GST_APP_SRC_CAST( p_sys->p_decode_src ), p_buf )
                    != GST_FLOW_OK ) )
        {
            /* block will be released internally,
             * when gst_buffer_unref() is called */
            p_dec->b_error = true;
            msg_Err( p_dec, "failed to push buffer" );
            goto done;
        }
    }
    else
        block_Release( p_block );

check_messages:
    /* Poll for any messages, errors */
    p_msg = gst_bus_pop_filtered( p_sys->p_bus,
            GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_ERROR |
            GST_MESSAGE_EOS | GST_MESSAGE_WARNING |
            GST_MESSAGE_INFO );
    if( p_msg )
    {
        switch( GST_MESSAGE_TYPE( p_msg ) ){
        case GST_MESSAGE_EOS:
            /* for debugging purpose */
            msg_Warn( p_dec, "got unexpected eos" );
            break;
        /* First buffer received */
        case GST_MESSAGE_ASYNC_DONE:
            /* for debugging purpose */
            p_sys->b_prerolled = true;
            msg_Dbg( p_dec, "Pipeline is prerolled" );
            break;
        default:
            p_dec->b_error = default_msg_handler( p_dec, p_msg );
            if( p_dec->b_error )
            {
                gst_message_unref( p_msg );
                goto done;
            }
            break;
        }
        gst_message_unref( p_msg );
    }

    /* Look for any output buffers in the queue */
    if( gst_atomic_queue_peek( p_sys->p_que ) )
    {
        GstVideoFrame frame;

        /* Get a new picture */
        p_pic = decoder_NewPicture( p_dec );
        if( !p_pic )
            goto done;

        p_buf = GST_BUFFER_CAST(
                gst_atomic_queue_pop( p_sys->p_que ) );

        if( likely( GST_BUFFER_PTS_IS_VALID( p_buf ) ) )
            p_pic->date = gst_util_uint64_scale(
                    GST_BUFFER_PTS( p_buf ), GST_MSECOND, GST_SECOND );
        else
            msg_Warn( p_dec, "Gst Buffer has no timestamp" );

        if( unlikely( !gst_video_frame_map( &frame,
                        &p_sys->vinfo, p_buf, GST_MAP_READ ) ) )
        {
            msg_Err( p_dec, "failed to map gst video frame" );
            gst_buffer_unref( p_buf );
            p_dec->b_error = true;
            goto done;
        }

        gst_CopyPicture( p_pic, &frame );
        gst_video_frame_unmap( &frame );
        gst_buffer_unref( p_buf );
    }

done:
    *pp_block = NULL;
    return p_pic;
}

/* Close the decoder instance */
static void CloseDecoder( vlc_object_t *p_this )
{
    decoder_t *p_dec = ( decoder_t* )p_this;
    decoder_sys_t *p_sys = p_dec->p_sys;

    if( p_sys->b_running )
    {
        GstMessage *p_msg;
        GstFlowReturn i_ret;

        /* Send EOS to the pipeline */
        i_ret = gst_app_src_end_of_stream(
                GST_APP_SRC_CAST( p_sys->p_decode_src ) );
        msg_Dbg( p_dec, "app src eos: %s", gst_flow_get_name( i_ret ) );

        /* and catch it on the bus with a timeout */
        p_msg = gst_bus_timed_pop_filtered( p_sys->p_bus,
                2000000000ULL, GST_MESSAGE_EOS | GST_MESSAGE_ERROR );

        if( p_msg )
        {
            switch( GST_MESSAGE_TYPE( p_msg ) ){
            case GST_MESSAGE_EOS:
                msg_Dbg( p_dec, "got eos" );
                break;
            default:
                p_dec->b_error = default_msg_handler( p_dec, p_msg );
                if( p_dec->b_error )
                    msg_Warn( p_dec, "pipeline may not close gracefully" );
                break;
            }

            gst_message_unref( p_msg );
        }
        else
            msg_Warn( p_dec,
                    "no message, pipeline may not close gracefully" );
    }

    /* Remove any left-over buffers from the queue */
    if( p_sys->p_que )
    {
        GstBuffer *p_buf;
        while( ( p_buf = gst_atomic_queue_pop( p_sys->p_que ) ) )
            gst_buffer_unref( p_buf );
        gst_atomic_queue_unref( p_sys->p_que );
    }

    if( p_sys->b_running &&
            gst_element_set_state( p_sys->p_decoder, GST_STATE_NULL )
            != GST_STATE_CHANGE_SUCCESS )
        msg_Warn( p_dec,
                "failed to change the state to NULL," \
                "pipeline may not close gracefully" );

    if( p_sys->p_bus )
        gst_object_unref( p_sys->p_bus );
    if( p_sys->p_decode_src )
        gst_object_unref( p_sys->p_decode_src );
    if( p_sys->p_decode_in )
        gst_object_unref( p_sys->p_decode_in );
    if( p_sys->p_decode_out )
        gst_object_unref( p_sys->p_decode_out );
    if( p_sys->p_decoder )
        gst_object_unref( p_sys->p_decoder );

    free( p_sys );
}