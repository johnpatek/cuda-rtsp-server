#include "egl.h"

struct egl_application *egl_application_new()
{
    const EGLint config_attributes[] = {
        EGL_SURFACE_TYPE,
        EGL_STREAM_BIT_KHR,
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,
        1,
        EGL_GREEN_SIZE,
        1,
        EGL_BLUE_SIZE,
        1,
        EGL_ALPHA_SIZE,
        1,
        EGL_DEPTH_SIZE,
        1,
        EGL_NONE,
    };
    const EGLint stream_attributes[] = {
        EGL_NONE};
    const EGLint surface_attributes[] = {
        EGL_WIDTH, 640,
        EGL_HEIGHT, 480,
        EGL_NONE};
    EGLint context_attributes[] = {
        EGL_NONE,
    };
    struct egl_application *application;
    EGLConfig config;
    EGLint config_count;

    application = calloc(1, sizeof(struct egl_application));
    application->egl_display = eglGetDisplay((EGLNativeDisplayType)0);
    assert(eglInitialize(application->egl_display, NULL, NULL) == EGL_TRUE);
    assert(eglChooseConfig(application->egl_display, &config_attributes[0], &config, 1, &config_count) == EGL_TRUE);
    eglBindAPI(EGL_OPENGL_API);
    application->egl_stream = eglCreateStreamKHR(application->egl_display, &stream_attributes[0]);
    assert(application->egl_stream != EGL_NO_STREAM_KHR);
    assert(cuEGLStreamConsumerConnect(&application->cu_egl_stream_connection, application->egl_stream) == CUDA_SUCCESS);
    application->egl_surface = eglCreateStreamProducerSurfaceKHR(application->egl_display, config, application->egl_stream, &surface_attributes[0]);
    assert(application->egl_surface != EGL_NO_SURFACE);
    application->egl_context = eglCreateContext(application->egl_display, config, NULL, &context_attributes[0]);
    assert(application->egl_context != EGL_NO_CONTEXT);

    return application;
}

void egl_application_free()
{
}

void egl_application_render(struct egl_application *application)
{
    const GLfloat vertices[] =
        {
            -1, -1, -1, -1, -1, 1, -1, 1, 1, -1, 1, -1,
            1, -1, -1, 1, -1, 1, 1, 1, 1, 1, 1, -1,
            -1, -1, -1, -1, -1, 1, 1, -1, 1, 1, -1, -1,
            -1, 1, -1, -1, 1, 1, 1, 1, 1, 1, 1, -1,
            -1, -1, -1, -1, 1, -1, 1, 1, -1, 1, -1, -1,
            -1, -1, 1, -1, 1, 1, 1, 1, 1, 1, -1, 1};

    const GLfloat colors[] =
        {
            0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 0,
            1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0,
            0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0,
            0, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0,
            0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0,
            0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1};
    const float rads = (3.14f * (float)application->degrees) / 180.0f;
    const float r = fabsf(sin(rads));
    const float g = fabsf(cos(rads));
    const float b = sqrtf(fabsf(sin(rads) * cos(rads)));
    assert(eglMakeCurrent(application->egl_display, application->egl_surface, application->egl_surface, application->egl_context) == EGL_TRUE);
    glViewport(0, 0, 640, 480);
    glClearColor(1.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION_MATRIX);
    glLoadIdentity();
    gluPerspective(60, (double)640 / (double)480, 0.1, 100);

    glMatrixMode(GL_MODELVIEW_MATRIX);
    glTranslatef(0, 0, -5);

    static float alpha = 0;
    // attempt to rotate cube
    glRotatef(alpha, 0, 1, 0);

    /* We have a color array and a vertex array */
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, vertices);
    glColorPointer(3, GL_FLOAT, 0, colors);

    /* Send data : 24 vertices */
    glDrawArrays(GL_QUADS, 0, 24);

    /* Cleanup states */
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    alpha += 1;
    assert(eglSwapBuffers(application->egl_display, application->egl_surface) == EGL_TRUE);
    application->degrees = (application->degrees + 1) % 360;
}

void egl_application_copy_frame(struct egl_application *application, CUdeviceptr buffer, size_t size)
{
    EGLint stream_state;
    CUgraphicsResource graphics_resource;
    CUeglFrame egl_frame;
    assert(eglQueryStreamKHR(application->egl_display, application->egl_stream, EGL_STREAM_STATE_KHR, &stream_state) == EGL_TRUE);
    if (stream_state == EGL_STREAM_STATE_NEW_FRAME_AVAILABLE_KHR)
    {
        assert(cuEGLStreamConsumerAcquireFrame(&application->cu_egl_stream_connection, &graphics_resource, NULL, 16000) == CUDA_SUCCESS);
        assert(cuGraphicsResourceGetMappedEglFrame(&egl_frame, graphics_resource, 0, 0) == CUDA_SUCCESS);
        const CUDA_MEMCPY2D copy_info = {
            .srcMemoryType = CU_MEMORYTYPE_ARRAY,
            .srcArray = egl_frame.frame.pArray[0],
            .srcPitch = 2560,
            .dstXInBytes = 0,
            .dstY = 0,
            .dstMemoryType = CU_MEMORYTYPE_DEVICE,
            .dstDevice = buffer,
            .WidthInBytes = 2560,
            .Height = 480,
        };
        cuMemcpy2D(&copy_info);
        assert(cuEGLStreamConsumerReleaseFrame(&application->cu_egl_stream_connection, graphics_resource, NULL) == CUDA_SUCCESS);
    }
}