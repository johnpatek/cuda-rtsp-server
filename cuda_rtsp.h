#ifndef CUDA_RTSP_H
#define CUDA_RTSP_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <cuda.h>

#ifdef CU_RTSP_EXPOSE_GMAIN
#include <glib/gmain.h>
#endif

    typedef struct CUrtsp_server_st *CUrtsp_server;
    typedef struct CUrtsp_session_st *CUrtsp_session;

#define CU_RTSP_DEFAULT_PORT 8554

    typedef enum CUrtsp_format_enum
    {
        CU_RTSP_FORMAT_NV12,
        CU_RTSP_FORMAT_YV12,
        CU_RTSP_FORMAT_I420,
        CU_RTSP_FORMAT_BGRA,
        CU_RTSP_FORMAT_RGBA,
        CU_RTSP_FORMAT_Y444,
        CU_RTSP_FORMAT_VUYA,
    } CUrtsp_format;

    typedef void(CUDA_CB *CUrtspWriteCallback)(CUdeviceptr, size_t, void *);

    typedef struct CUDA_RTSP_SESSION_st
    {
        CUdevice device;
        CUcontext context;
        size_t width;
        size_t height;
        CUrtsp_format format;
        size_t fpsNum;
        size_t fpsDen;
        CUrtspWriteCallback writeCallback;
        void *userData;
    } CUDA_RTSP_SESSION;

    void cuRTSPInit();

    void cuRTSPDeinit();

    const char *cuRTSPGetError();

    CUresult cuRTSPServerCreate(CUrtsp_server *pServer, uint16_t port);

    void cuRTSPServerDestroy(CUrtsp_server hServer);

    CUresult cuRTSPServerAttach(CUrtsp_server hServer);

    void cuRTSPServerDispatch(CUrtsp_server hServer);

    void cuRTSPServerShutdown(CUrtsp_server hServer);

#ifdef CU_RTSP_EXPOSE_GMAIN
    CUresult cuRTSPServerAttachGMain(CUrtsp_server hServer, GMainContext *context);
#endif

    CUresult cuRTSPSessionCreate(CUrtsp_session *pSession, CUDA_RTSP_SESSION *pCreateSession);

    CUresult cuRTSPSessionMount(CUrtsp_session hSession, CUrtsp_server hServer, const char *path);

#ifdef __cplusplus
}
#endif

#endif