// zplayer on Nintendo Switch: presentation through EGL and OpenGL ES 3, see
// gles_present_switch.h. The game's picture is one textured quad; Dear ImGui's
// draw lists go through the same program. third_party/imgui has only the SDL3
// and Vulkan backends, so the small renderer ImGui needs is here.
//
// Written from scratch for zcport.

#if defined(__SWITCH__)

#include "gles_present_switch.h"

#include <cstddef>
#include <cstdio>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <imgui.h>

namespace
{

const char *const kVertexShader =
    "#version 300 es\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "layout(location = 2) in vec4 a_col;\n"
    "uniform vec2 u_size;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_col;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    v_col = a_col;\n"
    "    gl_Position = vec4(a_pos.x * 2.0 / u_size.x - 1.0, 1.0 - a_pos.y * 2.0 / u_size.y, 0.0, 1.0);\n"
    "}\n";

const char *const kFragmentShader =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "in vec2 v_uv;\n"
    "in vec4 v_col;\n"
    "out vec4 o_col;\n"
    "void main() {\n"
    "    o_col = v_col * texture(u_tex, v_uv);\n"
    "}\n";

EGLDisplay g_display = EGL_NO_DISPLAY;
EGLContext g_context = EGL_NO_CONTEXT;
EGLSurface g_surface = EGL_NO_SURFACE;

int g_width = 0, g_height = 0;
GLuint g_program = 0, g_vao = 0, g_vbo = 0, g_ebo = 0;
GLint g_u_size = -1;
GLuint g_picture = 0;
int g_picture_w = 0, g_picture_h = 0;
bool g_picture_shown = false;

std::string gl_log(GLuint object, bool program)
{
    char buf[1024];
    GLsizei n = 0;
    buf[0] = 0;
    if (program)
        glGetProgramInfoLog(object, sizeof(buf), &n, buf);
    else
        glGetShaderInfoLog(object, sizeof(buf), &n, buf);
    return buf;
}

GLuint compile(GLenum type, const char *source, std::string *error)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        *error = "shader: " + gl_log(s, false);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool make_program(std::string *error)
{
    GLuint vs = compile(GL_VERTEX_SHADER, kVertexShader, error);
    if (!vs)
        return false;
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFragmentShader, error);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }
    g_program = glCreateProgram();
    glAttachShader(g_program, vs);
    glAttachShader(g_program, fs);
    glLinkProgram(g_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(g_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        *error = "program: " + gl_log(g_program, true);
        return false;
    }
    g_u_size = glGetUniformLocation(g_program, "u_size");
    glUseProgram(g_program);
    glUniform1i(glGetUniformLocation(g_program, "u_tex"), 0);
    return true;
}

void bind_vertex_format()
{
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ebo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (void *)offsetof(ImDrawVert, pos));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (void *)offsetof(ImDrawVert, uv));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (void *)offsetof(ImDrawVert, col));
}

void make_texture_defaults(GLenum filter)
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// What ImGui asks of a renderer for its font atlas and the dialogs' pictures
// (ImGuiBackendFlags_RendererHasTextures).
void update_texture(ImTextureData *tex)
{
    if (tex->Status == ImTextureStatus_WantCreate) {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        make_texture_defaults(GL_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tex->Width, tex->Height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     tex->GetPixels());
        tex->SetTexID((ImTextureID)(intptr_t)id);
        tex->SetStatus(ImTextureStatus_OK);
    }
    else if (tex->Status == ImTextureStatus_WantUpdates) {
        glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)tex->GetTexID());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, tex->Width);
        for (int i = 0; i < tex->Updates.Size; i++) {
            const ImTextureRect &r = tex->Updates[i];
            glTexSubImage2D(GL_TEXTURE_2D, 0, r.x, r.y, r.w, r.h, GL_RGBA, GL_UNSIGNED_BYTE,
                            tex->GetPixelsAt(r.x, r.y));
        }
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        tex->SetStatus(ImTextureStatus_OK);
    }
    else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
        GLuint id = (GLuint)(intptr_t)tex->GetTexID();
        glDeleteTextures(1, &id);
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
    }
}

void draw_picture(int x, int y, int w, int h, bool smooth)
{
    const float x0 = (float)x, y0 = (float)y, x1 = (float)(x + w), y1 = (float)(y + h);
    const ImU32 white = IM_COL32_WHITE;
    const ImDrawVert quad[4] = {
        { ImVec2(x0, y0), ImVec2(0, 0), white },
        { ImVec2(x1, y0), ImVec2(1, 0), white },
        { ImVec2(x1, y1), ImVec2(1, 1), white },
        { ImVec2(x0, y1), ImVec2(0, 1), white },
    };
    static const unsigned short kIndices[6] = { 0, 1, 2, 0, 2, 3 };
    glBindTexture(GL_TEXTURE_2D, g_picture);
    const GLenum filter = smooth ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STREAM_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kIndices), kIndices, GL_STREAM_DRAW);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
}

void draw_ui(ImDrawData *dd)
{
    if (dd->Textures) {
        for (int i = 0; i < dd->Textures->Size; i++) {
            ImTextureData *tex = (*dd->Textures)[i];
            if (tex->Status != ImTextureStatus_OK)
                update_texture(tex);
        }
    }

    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);

    const ImVec2 off = dd->DisplayPos;
    for (int n = 0; n < dd->CmdListsCount; n++) {
        const ImDrawList *list = dd->CmdLists[n];
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)list->VtxBuffer.Size * sizeof(ImDrawVert),
                     list->VtxBuffer.Data, GL_STREAM_DRAW);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)list->IdxBuffer.Size * sizeof(ImDrawIdx),
                     list->IdxBuffer.Data, GL_STREAM_DRAW);
        for (int c = 0; c < list->CmdBuffer.Size; c++) {
            const ImDrawCmd &cmd = list->CmdBuffer[c];
            if (cmd.UserCallback) {
                if (cmd.UserCallback != ImDrawCallback_ResetRenderState)
                    cmd.UserCallback(list, &cmd);
                continue;
            }
            const float x0 = cmd.ClipRect.x - off.x, y0 = cmd.ClipRect.y - off.y;
            const float x1 = cmd.ClipRect.z - off.x, y1 = cmd.ClipRect.w - off.y;
            if (x1 <= x0 || y1 <= y0)
                continue;
            glScissor((GLint)x0, (GLint)(g_height - y1), (GLsizei)(x1 - x0), (GLsizei)(y1 - y0));
            glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)cmd.GetTexID());
            glDrawElements(GL_TRIANGLES, (GLsizei)cmd.ElemCount,
                           sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                           (void *)(intptr_t)(cmd.IdxOffset * sizeof(ImDrawIdx)));
        }
    }

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
}

std::string egl_error(const char *what)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "%s failed (EGL error 0x%x)", what, (unsigned)eglGetError());
    return buf;
}

} // namespace

namespace zcgles
{

bool init(void *native_window, int width, int height, std::string *error)
{
    g_width = width;
    g_height = height;
    g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_display == EGL_NO_DISPLAY || !eglInitialize(g_display, NULL, NULL)) {
        *error = egl_error("eglInitialize");
        return false;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        *error = egl_error("eglBindAPI");
        return false;
    }
    static const EGLint kConfig[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };
    EGLConfig config;
    EGLint count = 0;
    if (!eglChooseConfig(g_display, kConfig, &config, 1, &count) || count < 1) {
        *error = egl_error("eglChooseConfig");
        return false;
    }
    g_surface = eglCreateWindowSurface(g_display, config, (EGLNativeWindowType)native_window, NULL);
    if (g_surface == EGL_NO_SURFACE) {
        *error = egl_error("eglCreateWindowSurface");
        return false;
    }
    static const EGLint kContext[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    g_context = eglCreateContext(g_display, config, EGL_NO_CONTEXT, kContext);
    if (g_context == EGL_NO_CONTEXT) {
        *error = egl_error("eglCreateContext");
        return false;
    }
    if (!eglMakeCurrent(g_display, g_surface, g_surface, g_context)) {
        *error = egl_error("eglMakeCurrent");
        return false;
    }
    eglSwapInterval(g_display, 1);

    if (!make_program(error))
        return false;
    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
    glGenBuffers(1, &g_ebo);
    glGenTextures(1, &g_picture);
    glBindTexture(GL_TEXTURE_2D, g_picture);
    make_texture_defaults(GL_NEAREST);
    bind_vertex_format();
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(g_program);
    glUniform2f(g_u_size, (GLfloat)width, (GLfloat)height);
    return true;
}

void shutdown()
{
    if (g_display == EGL_NO_DISPLAY)
        return;
    if (g_context != EGL_NO_CONTEXT) {
        if (g_picture) {
            glDeleteTextures(1, &g_picture);
            glDeleteBuffers(1, &g_vbo);
            glDeleteBuffers(1, &g_ebo);
            glDeleteVertexArrays(1, &g_vao);
            glDeleteProgram(g_program);
        }
        g_picture = g_vbo = g_ebo = g_vao = g_program = 0;
        g_picture_w = g_picture_h = 0;
        g_picture_shown = false;
    }
    eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g_context != EGL_NO_CONTEXT)
        eglDestroyContext(g_display, g_context);
    if (g_surface != EGL_NO_SURFACE)
        eglDestroySurface(g_display, g_surface);
    eglTerminate(g_display);
    g_display = EGL_NO_DISPLAY;
    g_context = EGL_NO_CONTEXT;
    g_surface = EGL_NO_SURFACE;
}

void set_picture(const uint32_t *rgba, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    glBindTexture(GL_TEXTURE_2D, g_picture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    if (w != g_picture_w || h != g_picture_h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        g_picture_w = w;
        g_picture_h = h;
    }
    else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    g_picture_shown = true;
}

void present(ImDrawData *ui, int dst_x, int dst_y, int dst_w, int dst_h, bool smooth)
{
    glViewport(0, 0, g_width, g_height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (g_picture_shown && dst_w > 0 && dst_h > 0)
        draw_picture(dst_x, dst_y, dst_w, dst_h, smooth);
    if (ui)
        draw_ui(ui);
    eglSwapBuffers(g_display, g_surface);
}

} // namespace zcgles

#endif // __SWITCH__
