#include <cuda_rtsp.h>
#include <eglext_loader.h>
#include <cudaEGL.h>

#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glu.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>

struct egl_application
{
    EGLDisplay egl_display;
    EGLSurface egl_surface;
    EGLStreamKHR egl_stream;
    EGLContext egl_context;
    CUeglStreamConnection cu_egl_stream_connection;

    int degrees;
};

struct egl_application *egl_application_new();

void egl_application_free();

void egl_application_render(struct egl_application *application);

void egl_application_copy_frame(struct egl_application *application, CUdeviceptr buffer, size_t size);
