// Núcleo libretro de prueba que dibuja con OpenGL (no es un emulador).
// Sirve para comprobar el camino de "render por hardware" del Arcade sin necesitar juegos:
// pinta cuatro cuadrantes de colores, con el ROJO abajo a la izquierda en coordenadas de OpenGL,
// y una franja blanca que se mueve de izquierda a derecha.
//   cmake --build build --config Release --target testgl_libretro
// Copia la DLL a cores/, define un sistema en cores/sistemas.ini que la use y dale cualquier archivo.
#include "../src/libretro/libretro.h"
#include <cstring>

#ifdef _WIN32
#define GLAPI __stdcall
#else
#define GLAPI
#endif

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_hw_render_callback hw;
static unsigned frame_count = 0;

typedef void (GLAPI *glClearColor_t)(float, float, float, float);
typedef void (GLAPI *glClear_t)(unsigned);
typedef void (GLAPI *glEnable_t)(unsigned);
typedef void (GLAPI *glScissor_t)(int, int, int, int);
typedef void (GLAPI *glViewport_t)(int, int, int, int);
typedef void (GLAPI *glBindFramebuffer_t)(unsigned, unsigned);
static glClearColor_t pglClearColor;
static glClear_t pglClear;
static glEnable_t pglEnable, pglDisable;
static glScissor_t pglScissor;
static glViewport_t pglViewport;
static glBindFramebuffer_t pglBindFramebuffer;

static const unsigned W = 320, H = 240;

static void context_reset()
{
    pglClearColor = (glClearColor_t)hw.get_proc_address("glClearColor");
    pglClear = (glClear_t)hw.get_proc_address("glClear");
    pglEnable = (glEnable_t)hw.get_proc_address("glEnable");
    pglDisable = (glEnable_t)hw.get_proc_address("glDisable");
    pglScissor = (glScissor_t)hw.get_proc_address("glScissor");
    pglViewport = (glViewport_t)hw.get_proc_address("glViewport");
    pglBindFramebuffer = (glBindFramebuffer_t)hw.get_proc_address("glBindFramebuffer");
}
static void context_destroy() {}

extern "C" {

RETRO_API void retro_set_environment(retro_environment_t cb) { environ_cb = cb; }
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t) {}
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t) {}
RETRO_API void retro_set_input_poll(retro_input_poll_t) {}
RETRO_API void retro_set_input_state(retro_input_state_t) {}
RETRO_API void retro_init() {}
RETRO_API void retro_deinit() {}
RETRO_API unsigned retro_api_version() { return RETRO_API_VERSION; }
RETRO_API void retro_set_controller_port_device(unsigned, unsigned) {}
RETRO_API void retro_reset() { frame_count = 0; }
RETRO_API size_t retro_serialize_size() { return sizeof(frame_count); }
RETRO_API bool retro_serialize(void *data, size_t) { memcpy(data, &frame_count, sizeof(frame_count)); return true; }
RETRO_API bool retro_unserialize(const void *data, size_t) { memcpy(&frame_count, data, sizeof(frame_count)); return true; }
RETRO_API void retro_cheat_reset() {}
RETRO_API void retro_cheat_set(unsigned, bool, const char *) {}
RETRO_API bool retro_load_game_special(unsigned, const retro_game_info *, size_t) { return false; }
RETRO_API void retro_unload_game() {}
RETRO_API unsigned retro_get_region() { return RETRO_REGION_NTSC; }
RETRO_API void *retro_get_memory_data(unsigned) { return nullptr; }
RETRO_API size_t retro_get_memory_size(unsigned) { return 0; }

RETRO_API void retro_get_system_info(retro_system_info *info)
{
    memset(info, 0, sizeof(*info));
    info->library_name = "Prueba OpenGL";
    info->library_version = "1";
    info->valid_extensions = "gltest";
    info->need_fullpath = true;
}

RETRO_API void retro_get_system_av_info(retro_system_av_info *info)
{
    memset(info, 0, sizeof(*info));
    info->geometry.base_width = W;   info->geometry.base_height = H;
    info->geometry.max_width = W;    info->geometry.max_height = H;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps = 60.0;
    info->timing.sample_rate = 48000.0;
}

RETRO_API bool retro_load_game(const retro_game_info *)
{
    retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
    memset(&hw, 0, sizeof(hw));
    hw.context_type = RETRO_HW_CONTEXT_OPENGL;
    hw.context_reset = context_reset;
    hw.context_destroy = context_destroy;
    hw.depth = true;
    hw.bottom_left_origin = true;
    return environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw);
}

RETRO_API void retro_run()
{
    if (!pglClear) return;
    pglBindFramebuffer(0x8D40 /* GL_FRAMEBUFFER */, unsigned(hw.get_current_framebuffer()));
    pglViewport(0, 0, W, H);
    pglEnable(0x0C11 /* GL_SCISSOR_TEST */);
    const float colors[4][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1, 1, 0 } }; // abajo-izq, abajo-der, arriba-izq, arriba-der
    for (int q = 0; q < 4; ++q) {
        pglScissor((q & 1) ? W / 2 : 0, (q & 2) ? H / 2 : 0, W / 2, H / 2);
        pglClearColor(colors[q][0], colors[q][1], colors[q][2], 1);
        pglClear(0x4000 /* GL_COLOR_BUFFER_BIT */);
    }
    pglScissor(int(frame_count % W), 0, 6, H); // franja blanca en movimiento
    pglClearColor(1, 1, 1, 1);
    pglClear(0x4000);
    pglDisable(0x0C11);
    ++frame_count;
    video_cb(RETRO_HW_FRAME_BUFFER_VALID, W, H, 0);
}

} // extern "C"
