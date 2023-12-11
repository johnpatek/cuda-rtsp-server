
#include "cuda_rtsp.h"

#include <assert.h>
#include <stdio.h>

#define GST_USE_UNSTABLE_API 1
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/cuda/cuda-gst.h>
#include <gst/cuda/gstcudabufferpool.h>
#include <gst/cuda/gstcudamemory.h>
#include <gst/rtsp-server/rtsp-server.h>

const char *FORMATS[] = {
    "NV12",
    "YV12",
    "I420",
    "BGRA",
    "RGBA",
    "Y444",
    "VUYA",
    "ARGB",
    "ABGR",
    "BGR",
    "RGB",
};

typedef struct CUrtsp_server_st
{
    GMainContext *context;
    GMainLoop *loop;
    GstRTSPServer *gst_rtsp_server;
    int server_id;
} CUrtsp_server_st;

typedef struct CUrtsp_session_st
{
    CUDA_RTSP_SESSION session_info;
    GstRTSPMediaFactory *gst_rtsp_media_factory;
    GstContext *gst_context;
    GstCudaContext *gst_cuda_context;
    GstBufferPool *cu_buffer_pool;
    GstClockTime timestamp;
} CUrtsp_session_st;

// error buffer
char current_error[256];

// set contents of error buffer
static void cuRTSPSetError(const char *format, ...);

// check if video format requires cudaconvert element
static bool cuRTSPSessionNeedsConvert(CUrtsp_format format);

static void cuRTSPSessionConfigure(
    GstRTSPMediaFactory *factory,
    GstRTSPMedia *media,
    CUrtsp_session hSession);

static void cuRTSPSessionPushBuffer(
    GstElement *appsrc,
    guint unused,
    CUrtsp_session hSession);

static void cuRTSPSessionDestroy(
    CUrtsp_session hSession);

void cuRTSPInit()
{
    assert(cuInit(0) == CUDA_SUCCESS);
    gst_init(NULL, NULL);
    assert(gst_cuda_load_library() == TRUE);
    gst_cuda_memory_init_once();
}

void cuRTSPDeinit()
{
    gst_deinit();
}

const char *cuRTSPGetError()
{
    const char *result;
    result = NULL;
    if (strlen(current_error) > 0)
    {
        result = &current_error[0];
    }
    return result;
}

CUresult cuRTSPServerCreate(CUrtsp_server *pServer, const CUDA_RTSP_SERVER *pCreateServer)
{
    CUresult result;
    char service[7];

    result = CUDA_SUCCESS;

    if (pServer == NULL)
    {
        result = CUDA_ERROR_INVALID_VALUE;
        cuRTSPSetError("cuRTSPServerCreate: pServer cannot be NULL");
        goto error;
    }

    (*pServer) = calloc(1, sizeof(struct CUrtsp_server_st));
    (*pServer)->loop = NULL;
    (*pServer)->gst_rtsp_server = gst_rtsp_server_new();
    if (pCreateServer != NULL && pCreateServer->port != CU_RTSP_DEFAULT_PORT)
    {
        if (pCreateServer->port < 1 || pCreateServer->port > 65535)
        {
            result = CUDA_ERROR_INVALID_VALUE;
            cuRTSPSetError("cuRTSPServerCreate: port value must be [1,65535]");
            goto error;
        }
        snprintf(service, sizeof(service) / sizeof(service[0]), "%d", (int)pCreateServer->port);
        gst_rtsp_server_set_service((*pServer)->gst_rtsp_server, service);
    }

    goto done;
error:
    if ((*pServer) != NULL)
    {
        cuRTSPServerDestroy(*pServer);
    }
done:
    return result;
}

void cuRTSPServerDestroy(CUrtsp_server hServer)
{
    if (hServer != NULL)
    {
        if (hServer->gst_rtsp_server != NULL)
        {
            gst_object_unref(hServer->gst_rtsp_server);
        }
        free(hServer);
    }
}

CUresult cuRTSPServerAttachGMain(CUrtsp_server hServer, GMainContext *context)
{
    CUresult result;

    result = CUDA_SUCCESS;

    if (hServer != NULL)
    {
        hServer->context = context;
        hServer->server_id = gst_rtsp_server_attach(hServer->gst_rtsp_server, context);
    }
    else
    {
        result = CUDA_ERROR_INVALID_VALUE;
        cuRTSPSetError("cuRTSPServerAttachGMain: hServer cannot be NULL");
    }

    return result;
}

CUresult cuRTSPServerAttach(CUrtsp_server hServer)
{
    return cuRTSPServerAttachGMain(hServer, NULL);
}

void cuRTSPServerDispatch(CUrtsp_server hServer)
{
    hServer->loop = g_main_loop_new(hServer->context, FALSE);
    g_main_loop_run(hServer->loop);
}

void cuRTSPServerShutdown(CUrtsp_server hServer)
{
    g_main_loop_quit(hServer->loop);
}

CUresult cuRTSPSessionCreate(CUrtsp_session *pSession, const CUDA_RTSP_SESSION *pCreateSession)
{
    const char *launch_string_no_convert = "( appsrc name=source ! nvh264enc ! rtph264pay name=pay0 pt=96 )";
    const char *launch_string_with_convert = "( appsrc name=source ! cudaconvert ! nvh264enc ! rtph264pay name=pay0 pt=96 )";
    CUresult result;
    GstStructure *s;
    gint device_id;
    const char *launch_string;

    result = CUDA_SUCCESS;

    if (pSession == NULL)
    {
        cuRTSPSetError("cuRTSPSessionCreate: pSession cannot be NULL");
        goto error;
    }

    if (pCreateSession == NULL)
    {
        cuRTSPSetError("cuRTSPSessionCreate: pCreateSession cannot be NULL");
        goto error;
    }

    if (cuRTSPSessionNeedsConvert(pCreateSession->format))
    {
        launch_string = launch_string_with_convert;
    }
    else
    {
        launch_string = launch_string_no_convert;
    }

    *pSession = calloc(1, sizeof(CUrtsp_session_st));
    (*pSession)->session_info.device = pCreateSession->device;
    (*pSession)->session_info.context = pCreateSession->context;
    (*pSession)->session_info.width = pCreateSession->width;
    (*pSession)->session_info.height = pCreateSession->height;
    (*pSession)->session_info.format = pCreateSession->format;
    (*pSession)->session_info.fpsNum = pCreateSession->fpsNum;
    (*pSession)->session_info.fpsDen = pCreateSession->fpsDen;
    (*pSession)->session_info.writeCallback = pCreateSession->writeCallback;
    (*pSession)->session_info.userData = pCreateSession->userData;
    (*pSession)->gst_rtsp_media_factory = gst_rtsp_media_factory_new();
    (*pSession)->gst_cuda_context = gst_cuda_context_new_wrapped((*pSession)->session_info.context, (*pSession)->session_info.device);
    (*pSession)->gst_context = gst_context_new(GST_CUDA_CONTEXT_TYPE, TRUE);
    g_object_get(G_OBJECT((*pSession)->gst_cuda_context), "cuda-device-id", &device_id, NULL);
    s = gst_context_writable_structure((*pSession)->gst_context);
    gst_structure_set(s, GST_CUDA_CONTEXT_TYPE, GST_TYPE_CUDA_CONTEXT,
                      (*pSession)->gst_cuda_context, "cuda-device-id", G_TYPE_INT, device_id, NULL);
    (*pSession)->cu_buffer_pool = gst_cuda_buffer_pool_new((*pSession)->gst_cuda_context);
    gst_rtsp_media_factory_set_launch((*pSession)->gst_rtsp_media_factory, launch_string);
    gst_rtsp_media_factory_set_enable_rtcp((*pSession)->gst_rtsp_media_factory, (pCreateSession->live) ? FALSE : TRUE);
    g_signal_connect((*pSession)->gst_rtsp_media_factory, "media-configure", (GCallback)cuRTSPSessionConfigure, *pSession);
    goto done;
error:
    result = CUDA_ERROR_INVALID_VALUE;
done:
    return result;
}

CUresult cuRTSPSessionMount(CUrtsp_session hSession, CUrtsp_server hServer, const char *path)
{
    CUresult result;
    GstRTSPMountPoints *mount_points;

    result = CUDA_SUCCESS;
    mount_points = NULL;

    if (hSession == NULL)
    {
        cuRTSPSetError("cuRTSPSessionMount: hSession cannot be NULL");
        goto error;
    }

    if (hServer == NULL)
    {
        cuRTSPSetError("cuRTSPSessionMount: hServer cannot be NULL");
        goto error;
    }
    mount_points = gst_rtsp_server_get_mount_points(hServer->gst_rtsp_server);
    gst_rtsp_mount_points_add_factory(mount_points, path, hSession->gst_rtsp_media_factory);
    goto done;
error:
    result = CUDA_ERROR_INVALID_VALUE;
done:
    if (mount_points != NULL)
    {
        g_object_unref(mount_points);
    }
    return result;
}

static void cuRTSPSetError(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(current_error, sizeof(current_error) / sizeof(current_error[0]), format, args);
    va_end(args);
}

static void cuRTSPSessionConfigure(
    GstRTSPMediaFactory *factory,
    GstRTSPMedia *media,
    CUrtsp_session hSession)
{
    const char *caps_format = "video/x-raw(memory:CUDAMemory),format=%s,width=%d,height=%d,framerate=%d/%d";
    char caps_string[256];
    GstElement *pipeline;
    GstElement *appsrc;
    GstCaps *caps;
    GstVideoInfo video_info;
    GstStructure *config;

    snprintf(
        &caps_string[0],
        sizeof(caps_string) / sizeof(caps_string[0]),
        caps_format,
        FORMATS[hSession->session_info.format],
        (int)hSession->session_info.width,
        (int)hSession->session_info.height,
        (int)hSession->session_info.fpsNum,
        (int)hSession->session_info.fpsDen);

    caps = gst_caps_from_string(&caps_string[0]);
    pipeline = gst_rtsp_media_get_element(media);
    gst_element_set_context(pipeline, GST_CONTEXT(hSession->gst_context));
    appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(pipeline), "source");
    gst_util_set_object_arg(G_OBJECT(appsrc), "format", "time");
    g_object_set(G_OBJECT(appsrc), "caps",
                 caps,
                 NULL);
    gst_video_info_from_caps(&video_info, caps);
    config = gst_buffer_pool_get_config(hSession->cu_buffer_pool);
    gst_buffer_pool_config_set_params(config, caps, video_info.size, 2, 0);
    gst_buffer_pool_set_config(hSession->cu_buffer_pool, config);
    gst_buffer_pool_set_active(hSession->cu_buffer_pool, TRUE);
    g_signal_connect(appsrc, "need-data", (GCallback)cuRTSPSessionPushBuffer, hSession);
    gst_caps_unref(caps);
    gst_object_unref(appsrc);
    gst_object_unref(pipeline);
}

static void cuRTSPSessionPushBuffer(
    GstElement *appsrc,
    guint unused,
    CUrtsp_session hSession)
{
    GstBuffer *buffer;
    GstMapInfo map_info;
    GstFlowReturn ret;
    assert(hSession != NULL);
    assert(cuCtxPushCurrent(hSession->session_info.context) == CUDA_SUCCESS);
    gst_buffer_pool_acquire_buffer(hSession->cu_buffer_pool, &buffer, NULL);
    gst_buffer_map(buffer, &map_info, GST_MAP_WRITE);
    hSession->session_info.writeCallback(
        (CUdeviceptr)map_info.data,
        map_info.size,
        hSession->session_info.userData);
    gst_buffer_unmap(buffer, &map_info);
    GST_BUFFER_PTS(buffer) = hSession->timestamp;
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int(
        hSession->session_info.fpsDen,
        GST_SECOND,
        hSession->session_info.fpsNum);
    hSession->timestamp += GST_BUFFER_DURATION(buffer);
    g_signal_emit_by_name(appsrc, "push-buffer", buffer, &ret);
    gst_buffer_unref(buffer);
    assert(cuCtxPopCurrent(&hSession->session_info.context) == CUDA_SUCCESS);
}

static void cuRTSPSessionDestroy(
    CUrtsp_session hSession)
{
}

bool cuRTSPSessionNeedsConvert(CUrtsp_format format)
{
    bool result;

    switch (format)
    {
    case CU_RTSP_FORMAT_NV12:
    case CU_RTSP_FORMAT_YV12:
    case CU_RTSP_FORMAT_I420:
    case CU_RTSP_FORMAT_BGRA:
    case CU_RTSP_FORMAT_RGBA:
    case CU_RTSP_FORMAT_Y444:
    case CU_RTSP_FORMAT_VUYA:
        result = false;
        break;
    default:
        result = true;
        break;
    }

    return result;
}