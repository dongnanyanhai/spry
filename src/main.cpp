#include "api.h"
#include "app.h"
#include "array.h"
#include "assets.h"
#include "concurrency.h"
#include "deps/sokol_app.h"
#include "deps/sokol_gfx.h"
#include "deps/sokol_gl.h"
#include "deps/sokol_glue.h"
#include "deps/sokol_log.h"
#include "deps/sokol_time.h"
#include "draw.h"
#include "font.h"
#include "luax.h"
#include "microui.h"
#include "os.h"
#include "prelude.h"
#include "profile.h"
#include "sync.h"
#include "vfs.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
}

static Mutex g_init_mtx;
static sgl_pipeline g_pipeline;

static void init() {
  PROFILE_FUNC();
  LockGuard lock(&g_init_mtx);

  {
    PROFILE_BLOCK("sokol");

    sg_desc sg = {};
    sg.logger.func = slog_func;
    sg.context = sapp_sgcontext();
    sg_setup(sg);

    sgl_desc_t sgl = {};
    sgl.logger.func = slog_func;
    sgl_setup(sgl);

    sg_pipeline_desc sg_pipline = {};
    sg_pipline.depth.write_enabled = true;
    sg_pipline.colors[0].blend.enabled = true;
    sg_pipline.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    sg_pipline.colors[0].blend.dst_factor_rgb =
        SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    g_pipeline = sgl_make_pipeline(sg_pipline);
  }

  {
    PROFILE_BLOCK("miniaudio");

    g_app->miniaudio_vfs = vfs_for_miniaudio();

    ma_engine_config ma_config = ma_engine_config_init();
    ma_config.channels = 2;
    ma_config.sampleRate = 44100;
    ma_config.pResourceManagerVFS = g_app->miniaudio_vfs;
    ma_result res = ma_engine_init(&ma_config, &g_app->audio_engine);
    if (res != MA_SUCCESS) {
      fatal_error("failed to initialize audio engine");
    }
  }

  microui_init();

  renderer_reset();

  g_app->time.startup = stm_now();
  g_app->time.last = stm_now();

  {
    PROFILE_BLOCK("spry.start");

    lua_State *L = g_app->L;

    if (!g_app->error_mode.load()) {
      luax_spry_get(L, "start");

      Slice<String> args = g_app->args;
      lua_createtable(L, args.len - 1, 0);
      for (u64 i = 1; i < args.len; i++) {
        lua_pushlstring(L, args[i].data, args[i].len);
        lua_rawseti(L, -2, i);
      }

      luax_pcall(L, 1, 0);
    }
  }

  g_app->gpu_mtx.lock();

  lua_channels_setup();
  assets_start_hot_reload();

#ifndef NDEBUG
  printf("end of init\n");
#endif
}

static void event(const sapp_event *e) {
  microui_sokol_event(e);

  switch (e->type) {
  case SAPP_EVENTTYPE_KEY_DOWN: g_app->key_state[e->key_code] = true; break;
  case SAPP_EVENTTYPE_KEY_UP: g_app->key_state[e->key_code] = false; break;
  case SAPP_EVENTTYPE_MOUSE_DOWN:
    g_app->mouse_state[e->mouse_button] = true;
    break;
  case SAPP_EVENTTYPE_MOUSE_UP:
    g_app->mouse_state[e->mouse_button] = false;
    break;
  case SAPP_EVENTTYPE_MOUSE_MOVE:
    g_app->mouse_x = e->mouse_x;
    g_app->mouse_y = e->mouse_y;
    break;
  case SAPP_EVENTTYPE_MOUSE_SCROLL:
    g_app->scroll_x = e->scroll_x;
    g_app->scroll_y = e->scroll_y;
    break;
  default: break;
  }
}

static void render() {
  PROFILE_FUNC();

  {
    PROFILE_BLOCK("begin render pass");

    sg_pass_action pass = {};
    pass.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.colors[0].store_action = SG_STOREACTION_STORE;
    if (g_app->error_mode.load()) {
      pass.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};
    } else {
      float rgba[4];
      renderer_get_clear_color(rgba);
      pass.colors[0].clear_value.r = rgba[0];
      pass.colors[0].clear_value.g = rgba[1];
      pass.colors[0].clear_value.b = rgba[2];
      pass.colors[0].clear_value.a = rgba[3];
    }

    {
      LockGuard lock{&g_app->gpu_mtx};
      sg_begin_default_pass(pass, sapp_width(), sapp_height());
    }

    sgl_defaults();
    sgl_load_pipeline(g_pipeline);

    sgl_viewport(0, 0, sapp_width(), sapp_height(), true);
    sgl_ortho(0, sapp_widthf(), sapp_heightf(), 0, -1, 1);
  }

  if (g_app->error_mode.load()) {
    if (g_app->default_font == nullptr) {
      g_app->default_font = (FontFamily *)mem_alloc(sizeof(FontFamily));
      g_app->default_font->load_default();
    }

    renderer_reset();

    float x = 10;
    float y = 10;
    u64 font_size = 16;

    if (LockGuard lock{&g_app->error_mtx}) {
      y = draw_font(g_app->default_font, font_size, x, y,
                    "-- ! Spry Error ! --");
      y += font_size;

      y = draw_font_wrapped(g_app->default_font, font_size, x, y,
                            g_app->fatal_error, sapp_widthf() - x);
      y += font_size;

      if (g_app->traceback.data) {
        draw_font(g_app->default_font, font_size, x, y, g_app->traceback);
      }
    }
  } else {
    microui_begin();

    lua_State *L = g_app->L;

    luax_spry_get(L, "_timer_update");
    lua_pushnumber(L, g_app->time.delta);
    luax_pcall(L, 1, 0);

    {
      PROFILE_BLOCK("spry.frame");

      luax_spry_get(L, "frame");
      lua_pushnumber(L, g_app->time.delta);
      luax_pcall(L, 1, 0);
    }

    assert(lua_gettop(L) == 1);

    microui_end_and_present();
  }

  {
    PROFILE_BLOCK("end render pass");
    LockGuard lock{&g_app->gpu_mtx};

    sgl_draw();

    sgl_error_t sgl_err = sgl_error();
    if (sgl_err != SGL_NO_ERROR) {
      panic("a draw error occurred: %d", sgl_err);
    }

    sg_end_pass();
    sg_commit();
  }
}

static void frame() {
  PROFILE_FUNC();

  {
    AppTime *time = &g_app->time;
    u64 lap = stm_laptime(&time->last);
    time->delta = stm_sec(lap);
    time->accumulator += lap;

#ifndef __EMSCRIPTEN__
    if (time->target_ticks > 0) {
      u64 TICK_MS = 1000000;
      u64 TICK_US = 1000;

      u64 target = time->target_ticks;

      if (time->accumulator < target) {
        u64 ms = (target - time->accumulator) / TICK_MS;
        if (ms > 0) {
          PROFILE_BLOCK("sleep");
          os_sleep(ms - 1);
        }

        {
          PROFILE_BLOCK("spin loop");

          u64 lap = stm_laptime(&time->last);
          time->delta += stm_sec(lap);
          time->accumulator += lap;

          while (time->accumulator < target) {
            os_yield();

            u64 lap = stm_laptime(&time->last);
            time->delta += stm_sec(lap);
            time->accumulator += lap;
          }
        }
      }

      u64 fuzz = TICK_US * 100;
      while (time->accumulator >= target - fuzz) {
        if (time->accumulator < target + fuzz) {
          time->accumulator = 0;
        } else {
          time->accumulator -= target + fuzz;
        }
      }
    }
#endif
  }

  g_app->gpu_mtx.unlock();
  render();
  assets_perform_hot_reload_changes();
  g_app->gpu_mtx.lock();

  memcpy(g_app->prev_key_state, g_app->key_state, sizeof(g_app->key_state));
  memcpy(g_app->prev_mouse_state, g_app->mouse_state,
         sizeof(g_app->mouse_state));
  g_app->prev_mouse_x = g_app->mouse_x;
  g_app->prev_mouse_y = g_app->mouse_y;
  g_app->scroll_x = 0;
  g_app->scroll_y = 0;

  Array<Sound *> &sounds = g_app->garbage_sounds;
  for (u64 i = 0; i < sounds.len;) {
    Sound *sound = sounds[i];

    if (sound->dead_end) {
      assert(sound->zombie);
      sound->trash();
      mem_free(sound);

      sounds[i] = sounds[sounds.len - 1];
      sounds.len--;
    } else {
      i++;
    }
  }
}

static void actually_cleanup() {
  PROFILE_FUNC();

  g_app->gpu_mtx.unlock();

  lua_State *L = g_app->L;

  {
    PROFILE_BLOCK("before quit");

    luax_spry_get(L, "before_quit");
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
      String err = luax_check_string(L, -1);
      panic("%s", err.data);
    }
  }

  microui_trash();

  {
    PROFILE_BLOCK("lua close");
    lua_close(L);
    luaalloc_delete(g_app->LA);
  }

  {
    PROFILE_BLOCK("destroy assets");

    lua_channels_shutdown();

    if (g_app->default_font != nullptr) {
      g_app->default_font->trash();
      mem_free(g_app->default_font);
    }

    for (Sound *sound : g_app->garbage_sounds) {
      sound->trash();
    }
    g_app->garbage_sounds.trash();

    assets_shutdown();
  }

  {
    PROFILE_BLOCK("audio uninit");
    ma_engine_uninit(&g_app->audio_engine);
    mem_free(g_app->miniaudio_vfs);
  }

  {
    PROFILE_BLOCK("destory sokol");
    sgl_destroy_pipeline(g_pipeline);
    sgl_shutdown();
    sg_shutdown();
  }

  vfs_trash();

  mem_free(g_app->fatal_error.data);
  mem_free(g_app->traceback.data);

  for (String arg : g_app->args) {
    mem_free(arg.data);
  }
  mem_free(g_app->args.data);

  mem_free(g_app);

  g_init_mtx.trash();
}

static void cleanup() {
  actually_cleanup();

#ifdef USE_PROFILER
  profile_shutdown();
#endif

#ifndef NDEBUG
  DebugAllocator *allocator = dynamic_cast<DebugAllocator *>(g_allocator);
  if (allocator != nullptr) {
    allocator->dump_allocs();
  }
#endif

  allocator->trash();
  operator delete(g_allocator);

#ifndef NDEBUG
  printf("bye\n");
#endif
}

static void setup_lua() {
  PROFILE_FUNC();

  LuaAlloc *LA = luaalloc_create(nullptr, nullptr);
  lua_State *L = lua_newstate(luaalloc, LA);

  g_app->LA = LA;
  g_app->L = L;

  luaL_openlibs(L);
  open_spry_api(L);
  open_luasocket(L);
  luax_run_bootstrap(L);

  // add error message handler. always at the bottom of stack.
  lua_pushcfunction(L, luax_msgh);

  luax_spry_get(L, "_define_default_callbacks");
  luax_pcall(L, 0, 0);
}

static void load_all_lua_scripts(lua_State *L) {
  PROFILE_FUNC();

  Array<String> files = {};
  defer({
    for (String str : files) {
      mem_free(str.data);
    }
    files.trash();
  });

  bool ok = vfs_list_all_files(&files);
  if (!ok) {
    panic("failed to list all files");
  }
  qsort(files.data, files.len, sizeof(String),
        [](const void *a, const void *b) -> int {
          String *lhs = (String *)a;
          String *rhs = (String *)b;
          return strcmp(lhs->data, rhs->data);
        });

  for (String file : files) {
    if (file != "main.lua" && file.ends_with(".lua")) {
      asset_load_kind(AssetKind_LuaRef, file, nullptr);
    }
  }
}

/* extern(app.h) */ App *g_app;
/* extern(prelude.h) */ Allocator *g_allocator;

sapp_desc sokol_main(int argc, char **argv) {
  g_init_mtx.make();
  LockGuard lock(&g_init_mtx);

#ifndef NDEBUG
  g_allocator = new DebugAllocator();
#else
  g_allocator = new HeapAllocator();
#endif

  g_allocator->make();

  os_high_timer_resolution();
  stm_setup();

  profile_setup();
  PROFILE_FUNC();

  const char *mount_path = nullptr;

  for (i32 i = 1; i < argc; i++) {
    if (argv[i][0] != '-') {
      mount_path = argv[i];
      break;
    }
  }

  g_app = (App *)mem_alloc(sizeof(App));
  memset(g_app, 0, sizeof(App));

  g_app->args.resize(argc);
  for (i32 i = 0; i < argc; i++) {
    g_app->args[i] = to_cstr(argv[i]);
  }

  setup_lua();
  lua_State *L = g_app->L;

  MountResult mount = vfs_mount(mount_path);

  g_app->is_fused.store(mount.is_fused);

  if (!g_app->error_mode.load() && mount.ok) {
    asset_load_kind(AssetKind_LuaRef, "main.lua", nullptr);
  }

  if (!g_app->error_mode.load()) {
    luax_spry_get(L, "arg");

    lua_createtable(L, argc - 1, 0);
    for (i32 i = 1; i < argc; i++) {
      lua_pushstring(L, argv[i]);
      lua_rawseti(L, -2, i);
    }

    if (lua_pcall(L, 1, 0, 1) != LUA_OK) {
      lua_pop(L, 1);
    }
  }

  lua_newtable(L);
  i32 conf_table = lua_gettop(L);

  if (!g_app->error_mode.load()) {
    luax_spry_get(L, "conf");
    lua_pushvalue(L, conf_table);
    luax_pcall(L, 1, 0);
  }

  g_app->win_console = g_app->win_console || luax_boolean_field(L, -1, "win_console", false);

  bool hot_reload = luax_boolean_field(L, -1, "hot_reload", true);
  bool startup_load_scripts =
      luax_boolean_field(L, -1, "startup_load_scripts", true);
  bool fullscreen = luax_boolean_field(L, -1, "fullscreen", false);
  lua_Number reload_interval =
      luax_opt_number_field(L, -1, "reload_interval", 0.1);
  lua_Number swap_interval = luax_opt_number_field(L, -1, "swap_interval", 1);
  lua_Number target_fps = luax_opt_number_field(L, -1, "target_fps", 0);
  lua_Number width = luax_opt_number_field(L, -1, "window_width", 800);
  lua_Number height = luax_opt_number_field(L, -1, "window_height", 600);
  String title = luax_opt_string_field(L, -1, "window_title", "Spry");

  lua_pop(L, 1); // conf table

  if (!g_app->error_mode.load() && startup_load_scripts && mount.ok) {
    load_all_lua_scripts(L);
  }

  g_app->hot_reload_enabled.store(mount.can_hot_reload && hot_reload);
  g_app->reload_interval.store((u32)(reload_interval * 1000));

  if (target_fps != 0) {
    g_app->time.target_ticks = 1000000000 / target_fps;
  }

#ifdef IS_WIN32
  if (!g_app->win_console) {
    FreeConsole();
  }
#endif

  sapp_desc sapp = {};
  sapp.init_cb = init;
  sapp.frame_cb = frame;
  sapp.cleanup_cb = cleanup;
  sapp.event_cb = event;
  sapp.width = (i32)width;
  sapp.height = (i32)height;
  sapp.window_title = title.data;
  sapp.logger.func = slog_func;
  sapp.swap_interval = (i32)swap_interval;
  sapp.fullscreen = fullscreen;

#ifndef NDEBUG
  printf("debug build\n");
#endif
  return sapp;
}
