#include <cuda_rtsp.h>
#include "egl.h"

#include <sigfn.h>

static void copy_frame(CUdeviceptr buffer, size_t size, void *user_data);

static void handle_signal(int signum, void *user_data);

int main(int argc, const char **argv)
{
    CUdevice cu_device;
    CUcontext cu_context;
    CUrtsp_server cu_server;
    CUrtsp_session cu_session;
    struct egl_application *application;
    bool running;

    eglext_load();

    cuRTSPInit(0);

    cuDeviceGet(&cu_device, 0);

    cuCtxCreate(&cu_context, 0, cu_device);

    application = egl_application_new();

    CUDA_RTSP_SESSION create_session = {
        .device = cu_device,
        .context = cu_context,
        .width = 640,
        .height = 480,
        .format = CU_RTSP_FORMAT_BGRA,
        .fpsNum = 30,
        .fpsDen = 1,
        .live = true,
        .writeCallback = copy_frame,
        .userData = application,
    };

    assert(cuRTSPServerCreate(&cu_server, NULL) == CUDA_SUCCESS);
    assert(cuRTSPSessionCreate(&cu_session, &create_session) == CUDA_SUCCESS);
    assert(cuRTSPSessionMount(cu_session, cu_server, "/test") == CUDA_SUCCESS);
    assert(cuRTSPServerAttach(cu_server) == CUDA_SUCCESS);

    sigfn_handle(SIGINT, handle_signal, &running);
    sigfn_handle(SIGTERM, handle_signal, &running);

    running = true;
    while(running)
    {
        egl_application_render(application);
    }

    cuRTSPServerDestroy(cu_server);

    cuRTSPDeinit();
    
    return 0;
}

void copy_frame(CUdeviceptr buffer, size_t size, void *user_data)
{
    struct egl_application *const application = (struct egl_application *)user_data;
    egl_application_copy_frame(application, buffer, size);
}

void handle_signal(int signum, void *user_data)
{
    *(bool*)user_data = false;
}