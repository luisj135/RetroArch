/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2013 - Hans-Kristian Arntzen
 * 
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "dynamic.h"
#include "general.h"
#include "compat/strl.h"
#include "compat/posix_string.h"
#include "file.h"
#include <string.h>
#include <ctype.h>

#ifdef RARCH_CONSOLE
#include "console/rarch_console.h"
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "boolean.h"
#include "libretro.h"

#ifdef NEED_DYNAMIC
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

#ifdef HAVE_DYNAMIC
#define SYM(x) do { \
   function_t func = dylib_proc(lib_handle, #x); \
   memcpy(&p##x, &func, sizeof(func)); \
   if (p##x == NULL) { RARCH_ERR("Failed to load symbol: \"%s\"\n", #x); rarch_fail(1, "init_libretro_sym()"); } \
} while (0)

static dylib_t lib_handle = NULL;
#else
#define SYM(x) p##x = x
#endif

void (*pretro_init)(void);
void (*pretro_deinit)(void);

unsigned (*pretro_api_version)(void);

void (*pretro_get_system_info)(struct retro_system_info*);
void (*pretro_get_system_av_info)(struct retro_system_av_info*);

void (*pretro_set_environment)(retro_environment_t);
void (*pretro_set_video_refresh)(retro_video_refresh_t);
void (*pretro_set_audio_sample)(retro_audio_sample_t);
void (*pretro_set_audio_sample_batch)(retro_audio_sample_batch_t);
void (*pretro_set_input_poll)(retro_input_poll_t);
void (*pretro_set_input_state)(retro_input_state_t);

void (*pretro_set_controller_port_device)(unsigned, unsigned);

void (*pretro_reset)(void);
void (*pretro_run)(void);

size_t (*pretro_serialize_size)(void);
bool (*pretro_serialize)(void*, size_t);
bool (*pretro_unserialize)(const void*, size_t);

void (*pretro_cheat_reset)(void);
void (*pretro_cheat_set)(unsigned, bool, const char*);

bool (*pretro_load_game)(const struct retro_game_info*);
bool (*pretro_load_game_special)(unsigned, const struct retro_game_info*, size_t);

void (*pretro_unload_game)(void);

unsigned (*pretro_get_region)(void);

void *(*pretro_get_memory_data)(unsigned);
size_t (*pretro_get_memory_size)(unsigned);

static void set_environment(void);
static void set_environment_defaults(void);

#ifdef HAVE_DYNAMIC
#if defined(__APPLE__)
#define DYNAMIC_EXT "dylib"
#elif defined(_WIN32)
#define DYNAMIC_EXT "dll"
#else
#define DYNAMIC_EXT "so"
#endif

static dylib_t libretro_get_system_info_lib(const char *path, struct retro_system_info *info)
{
   dylib_t lib = dylib_load(path);
   if (!lib)
      return NULL;

   void (*proc)(struct retro_system_info*) = 
      (void (*)(struct retro_system_info*))dylib_proc(lib, "retro_get_system_info");

   if (!proc)
   {
      dylib_close(lib);
      return NULL;
   }

   proc(info);
   return lib;
}

bool libretro_get_system_info(const char *path, struct retro_system_info *info)
{
   struct retro_system_info dummy_info = {0};
   dylib_t lib = libretro_get_system_info_lib(path, &dummy_info);
   if (!lib)
      return false;

   memcpy(info, &dummy_info, sizeof(*info));
   info->library_name     = strdup(dummy_info.library_name);
   info->library_version  = strdup(dummy_info.library_version);
   info->valid_extensions = strdup(dummy_info.valid_extensions);
   dylib_close(lib);
   return true;
}

void libretro_free_system_info(struct retro_system_info *info)
{
   free((void*)info->library_name);
   free((void*)info->library_version);
   free((void*)info->valid_extensions);
   memset(info, 0, sizeof(*info));
}

static bool find_first_libretro(char *path, size_t size,
      const char *dir, const char *rom_path)
{
   bool ret = false;
   const char *ext = path_get_extension(rom_path);
   if (!ext || !*ext)
   {
      RARCH_ERR("Path has no extension. Cannot infer libretro implementation.\n");
      return false;
   }

   RARCH_LOG("Searching for valid libretro implementation in: \"%s\".\n", dir);

   struct string_list *list = dir_list_new(dir, DYNAMIC_EXT, false);
   if (!list)
   {
      RARCH_ERR("Couldn't open directory: \"%s\".\n", dir);
      return false;
   }

   for (size_t i = 0; i < list->size && !ret; i++)
   {
      RARCH_LOG("Checking library: \"%s\".\n", list->elems[i].data);

      struct retro_system_info info = {0};
      dylib_t lib = libretro_get_system_info_lib(list->elems[i].data, &info);
      if (!lib)
         continue;

      if (!info.valid_extensions)
      {
         dylib_close(lib);
         continue;
      }

      struct string_list *supported_ext = string_split(info.valid_extensions, "|"); 

      if (string_list_find_elem(supported_ext, ext))
      {
         strlcpy(path, list->elems[i].data, size);
         ret = true;
      }

      string_list_free(supported_ext);
      dylib_close(lib);
   }

   dir_list_free(list);
   return ret;
}
#endif

static void load_symbols(void)
{
#ifdef HAVE_DYNAMIC
   const char *libretro_path = g_settings.libretro;
   char libretro_core_buffer[PATH_MAX];

   if (path_is_directory(g_settings.libretro))
   {
      if (!find_first_libretro(libretro_core_buffer, sizeof(libretro_core_buffer),
               g_settings.libretro, g_extern.fullpath))
      {
         RARCH_ERR("libretro_path is a directory, but no valid libretro implementation was found.\n");
         rarch_fail(1, "load_dynamic()");
      }

      libretro_path = libretro_core_buffer;
   }

   RARCH_LOG("Loading dynamic libretro from: \"%s\"\n", libretro_path);
   lib_handle = dylib_load(libretro_path);
   if (!lib_handle)
   {
      RARCH_ERR("Failed to open dynamic library: \"%s\"\n", libretro_path);
      rarch_fail(1, "load_dynamic()");
   }
#endif

   SYM(retro_init);
   SYM(retro_deinit);

   SYM(retro_api_version);
   SYM(retro_get_system_info);
   SYM(retro_get_system_av_info);

   SYM(retro_set_environment);
   SYM(retro_set_video_refresh);
   SYM(retro_set_audio_sample);
   SYM(retro_set_audio_sample_batch);
   SYM(retro_set_input_poll);
   SYM(retro_set_input_state);

   SYM(retro_set_controller_port_device);

   SYM(retro_reset);
   SYM(retro_run);

   SYM(retro_serialize_size);
   SYM(retro_serialize);
   SYM(retro_unserialize);

   SYM(retro_cheat_reset);
   SYM(retro_cheat_set);

   SYM(retro_load_game);
   SYM(retro_load_game_special);

   SYM(retro_unload_game);
   SYM(retro_get_region);
   SYM(retro_get_memory_data);
   SYM(retro_get_memory_size);
}

void libretro_get_current_core_pathname(char *name, size_t size)
{
   if (size == 0)
      return;

   struct retro_system_info info = {0};
   pretro_get_system_info(&info);
   const char *id = info.library_name ? info.library_name : "Unknown";

   if (!id || strlen(id) >= size)
   {
      name[0] = '\0';
      return;
   }

   name[strlen(id)] = '\0';

   for (size_t i = 0; id[i] != '\0'; i++)
   {
      char c = id[i];
      if (isspace(c) || isblank(c))
         name[i] = '_';
      else
         name[i] = tolower(c);
   }
}

void init_libretro_sym(void)
{
   // Guarantee that we can do "dirty" casting.
   // Every OS that this program supports should pass this ...
   rarch_assert(sizeof(void*) == sizeof(void (*)(void)));

#ifdef HAVE_DYNAMIC
   // Try to verify that -lretro was not linked in from other modules
   // since loading it dynamically and with -l will fail hard.
   function_t sym = dylib_proc(NULL, "retro_init");
   if (sym)
   {
      RARCH_ERR("Serious problem. RetroArch wants to load libretro dyamically, but it is already linked.\n"); 
      RARCH_ERR("This could happen if other modules RetroArch depends on link against libretro directly.\n");
      RARCH_ERR("Proceeding could cause a crash. Aborting ...\n");
      rarch_fail(1, "init_libretro_sym()");
   }

   if (!*g_settings.libretro)
   {
      RARCH_ERR("RetroArch is built for dynamic libretro, but libretro_path is not set. Cannot continue.\n");
      rarch_fail(1, "init_libretro_sym()");
   }
#endif

   load_symbols();

   set_environment_defaults();
   set_environment();
}

void uninit_libretro_sym(void)
{
#ifdef HAVE_DYNAMIC
   if (lib_handle)
      dylib_close(lib_handle);
#endif
}

#ifdef NEED_DYNAMIC
// Platform independent dylib loading.
dylib_t dylib_load(const char *path)
{
#ifdef _WIN32
   dylib_t lib = LoadLibrary(path);
   if (!lib)
      RARCH_ERR("Failed to load library, error code: 0x%x\n", (unsigned)GetLastError());
   return lib;
#else
   dylib_t lib = dlopen(path, RTLD_LAZY);
   if (!lib)
      RARCH_ERR("dylib_load() failed: \"%s\".\n", dlerror());
   return lib;
#endif
}

function_t dylib_proc(dylib_t lib, const char *proc)
{
#ifdef _WIN32
   function_t sym = (function_t)GetProcAddress(lib ? (HMODULE)lib : GetModuleHandle(NULL), proc);
#else
   void *ptr_sym = NULL;
   if (lib)
      ptr_sym = dlsym(lib, proc); 
   else
   {
      void *handle = dlopen(NULL, RTLD_LAZY);
      if (handle)
      {
         ptr_sym = dlsym(handle, proc);
         dlclose(handle);
      }
   }

   // Dirty hack to workaround the non-legality of (void*) -> fn-pointer casts.
   function_t sym;
   memcpy(&sym, &ptr_sym, sizeof(void*));
#endif

   return sym;
}

void dylib_close(dylib_t lib)
{
#ifdef _WIN32
   FreeLibrary((HMODULE)lib);
#else
   dlclose(lib);
#endif
}
#endif

static bool environment_cb(unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_GET_OVERSCAN:
         *(bool*)data = !g_settings.video.crop_overscan;
         RARCH_LOG("Environ GET_OVERSCAN: %u\n", (unsigned)!g_settings.video.crop_overscan);
         break;

      case RETRO_ENVIRONMENT_GET_CAN_DUPE:
         *(bool*)data = true;
         RARCH_LOG("Environ GET_CAN_DUPE: true\n");
         break;

      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *var = (struct retro_variable*)data;
         if (var->key)
         {
            // Split string has '\0' delimiters so we have to find the position in original string,
            // then pass the corresponding offset into the split string.
            const char *key = strstr(g_extern.system.environment, var->key);
            size_t key_len = strlen(var->key);
            if (key && key[key_len] == '=')
            {
               ptrdiff_t offset = key - g_extern.system.environment;
               var->value = &g_extern.system.environment_split[offset + key_len + 1];
            }
            else
               var->value = NULL;
         }
         else
            var->value = g_extern.system.environment;

         RARCH_LOG("Environ GET_VARIABLE: %s=%s\n",
               var->key ? var->key : "null",
               var->value ? var->value : "null");

         break;
      }

      case RETRO_ENVIRONMENT_SET_VARIABLES:
      {
         RARCH_LOG("Environ SET_VARIABLES:\n");
         RARCH_LOG("=======================\n");
         const struct retro_variable *vars = (const struct retro_variable*)data;
         while (vars->key)
         {
            RARCH_LOG("\t%s :: %s\n",
                  vars->key,
                  vars->value ? vars->value : "N/A");

            vars++;
         }
         RARCH_LOG("=======================\n");
         break;
      }

      case RETRO_ENVIRONMENT_SET_MESSAGE:
      {
         const struct retro_message *msg = (const struct retro_message*)data;
         RARCH_LOG("Environ SET_MESSAGE: %s\n", msg->msg);
         if (g_extern.msg_queue)
            msg_queue_push(g_extern.msg_queue, msg->msg, 1, msg->frames);
         break;
      }

      case RETRO_ENVIRONMENT_SET_ROTATION:
      {
         unsigned rotation = *(const unsigned*)data;
         RARCH_LOG("Environ SET_ROTATION: %u\n", rotation);
         if (!g_settings.video.allow_rotate)
            break;

         g_extern.system.rotation = rotation;

         if (driver.video && driver.video->set_rotation)
         {
            if (driver.video_data)
               video_set_rotation_func(rotation);
         }
         else
            return false;
         break;
      }

      case RETRO_ENVIRONMENT_SHUTDOWN:
         RARCH_LOG("Environ SHUTDOWN.\n");
         g_extern.system.shutdown = true;
         break;

      case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
         g_extern.system.performance_level = *(const unsigned*)data;
         RARCH_LOG("Environ PERFORMANCE_LEVEL: %u.\n", g_extern.system.performance_level);
         break;

      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
         *(const char **)data = *g_settings.system_directory ? g_settings.system_directory : NULL;
         RARCH_LOG("Environ SYSTEM_DIRECTORY: \"%s\".\n", g_settings.system_directory);
         break;

      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      {
         enum retro_pixel_format pix_fmt = *(const enum retro_pixel_format*)data;
         switch (pix_fmt)
         {
            case RETRO_PIXEL_FORMAT_0RGB1555:
               RARCH_LOG("Environ SET_PIXEL_FORMAT: 0RGB1555.\n");
               break;

            case RETRO_PIXEL_FORMAT_RGB565:
               RARCH_LOG("Environ SET_PIXEL_FORMAT: RGB565.\n");
               break;
            case RETRO_PIXEL_FORMAT_XRGB8888:
               RARCH_LOG("Environ SET_PIXEL_FORMAT: XRGB8888.\n");
               break;
            default:
               return false;
         }
         
         g_extern.system.pix_fmt = pix_fmt;
         break;
      }

      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      {
         memset(g_extern.system.input_desc_btn, 0, sizeof(g_extern.system.input_desc_btn));

         const struct retro_input_descriptor *desc = (const struct retro_input_descriptor*)data;
         for (; desc->description; desc++)
         {
            if (desc->port >= MAX_PLAYERS)
               continue;

            if (desc->device != RETRO_DEVICE_JOYPAD) // Ignore all others for now.
               continue;

            if (desc->id >= RARCH_FIRST_CUSTOM_BIND)
               continue;

            g_extern.system.input_desc_btn[desc->port][desc->id] = desc->description;
         }

         static const char *libretro_btn_desc[] = {
            "B (bottom)", "Y (left)", "Select", "Start",
            "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right",
            "A (right)", "X (up)",
            "L", "R", "L2", "R2", "L3", "R3",
         };

         RARCH_LOG("Environ SET_INPUT_DESCRIPTORS:\n");
         for (unsigned p = 0; p < MAX_PLAYERS; p++)
         {
            for (unsigned id = 0; id < RARCH_FIRST_CUSTOM_BIND; id++)
            {
               const char *desc = g_extern.system.input_desc_btn[p][id];
               if (desc)
               {
                  RARCH_LOG("\tRetroPad, Player %u, Button \"%s\" => \"%s\"\n",
                        p + 1, libretro_btn_desc[id], desc);
               }
            }
         }

         break;
      }
      
      case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
      {
         RARCH_LOG("Environ SET_KEYBOARD_CALLBACK.\n");
         const struct retro_keyboard_callback *info = (const struct retro_keyboard_callback*)data;
         g_extern.system.key_event = info->callback;
         break;
      }

      case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
         RARCH_LOG("Environ SET_DISK_CONTROL_INTERFACE.\n");
         g_extern.system.disk_control = *(const struct retro_disk_control_callback*)data;
         break;

      case RETRO_ENVIRONMENT_SET_HW_RENDER:
      {
         RARCH_LOG("Environ SET_HW_RENDER.\n");
         struct retro_hw_render_callback *cb = (struct retro_hw_render_callback*)data;
         switch (cb->context_type)
         {
            case RETRO_HW_CONTEXT_NONE:
               RARCH_LOG("Requesting no HW context.\n");
               break;

#if defined(HAVE_OPENGLES2)
            case RETRO_HW_CONTEXT_OPENGLES2:
               RARCH_LOG("Requesting OpenGLES2 context.\n");
               driver.video = &video_gl;
               break;

            case RETRO_HW_CONTEXT_OPENGL:
               RARCH_ERR("Requesting OpenGL context, but RetroArch is compiled against OpenGLES2. Cannot use HW context.\n");
               return false;
#elif defined(HAVE_OPENGL)
            case RETRO_HW_CONTEXT_OPENGLES2:
               RARCH_ERR("Requesting OpenGLES2 context, but RetroArch is compiled against OpenGL. Cannot use HW context.\n");
               return false;

            case RETRO_HW_CONTEXT_OPENGL:
               RARCH_LOG("Requesting OpenGL context.\n");
               driver.video = &video_gl;
               break;
#endif

            default:
               RARCH_LOG("Requesting unknown context.\n");
               return false;
         }
         cb->get_current_framebuffer = driver_get_current_framebuffer;
         cb->get_proc_address = driver_get_proc_address;
         memcpy(&g_extern.system.hw_render_callback, cb, sizeof(*cb));
         break;
      }

      default:
         RARCH_LOG("Environ UNSUPPORTED (#%u).\n", cmd);
         return false;
   }

   return true;
}

static void set_environment(void)
{
   pretro_set_environment(environment_cb);
}

static void set_environment_defaults(void)
{
   char *save;

   // Split up environment variables beforehand.
   if (g_extern.system.environment_split &&
         strtok_r(g_extern.system.environment_split, ";", &save))
      while (strtok_r(NULL, ";", &save));
}

