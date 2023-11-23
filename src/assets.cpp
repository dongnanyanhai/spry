#include "assets.h"
#include "app.h"
#include "deps/lua/lauxlib.h"
#include "luax.h"
#include "os.h"
#include "profile.h"
#include "strings.h"
#include "sync.h"

struct FileChange {
  u64 key;
  u64 modtime;
};

struct Assets {
  HashMap<Asset> table;
  RWLock rw_lock;

  Mutex mtx;
  Cond notify;
  bool shutdown;

  Thread *reload_thread;
  Array<FileChange> changes;
};

static Assets g_assets = {};

static void hot_reload_thread(void *) {
  u32 reload_interval = atomic_int_load(&g_app->reload_interval);

  while (true) {
    PROFILE_BLOCK("hot reload");

    {
      mutex_lock(&g_assets.mtx);
      defer(mutex_unlock(&g_assets.mtx));

      if (g_assets.shutdown) {
        return;
      }

      bool signaled =
          cond_timed_wait(&g_assets.notify, &g_assets.mtx, reload_interval);
      if (signaled) {
        return;
      }
    }

    {
      PROFILE_BLOCK("check for updates");

      rw_shared_lock(&g_assets.rw_lock);
      defer(rw_shared_unlock(&g_assets.rw_lock));

      g_assets.changes.len = 0;
      for (auto [k, v] : g_assets.table) {
        PROFILE_BLOCK("read modtime");

        u64 modtime = os_file_modtime(v->name.data);
        if (modtime > v->modtime) {
          FileChange change = {};
          change.key = v->hash;
          change.modtime = modtime;
          array_push(&g_assets.changes, change);
        }
      }
    }

    if (g_assets.changes.len > 0) {
      PROFILE_BLOCK("perform hot reload");

      mutex_lock(&g_app->frame_mtx);
      defer(mutex_unlock(&g_app->frame_mtx));

      for (FileChange change : g_assets.changes) {
        Asset a = {};
        bool exists = asset_read(change.key, &a);
        assert(exists);

        a.modtime = change.modtime;

        bool ok = false;
        switch (a.kind) {
        case AssetKind_LuaRef: {
          luaL_unref(g_app->L, LUA_REGISTRYINDEX, a.lua_ref);
          a.lua_ref = luax_require_script(g_app->L, a.name);
          ok = true;
          break;
        }
        case AssetKind_Image: {
          image_trash(&a.image);
          ok = image_load(&a.image, a.name);
          break;
        }
        case AssetKind_Sprite: {
          sprite_data_trash(&a.sprite);
          ok = sprite_data_load(&a.sprite, a.name);
          break;
        }
        case AssetKind_Tilemap: {
          tilemap_trash(&a.tilemap);
          ok = tilemap_load(&a.tilemap, a.name);
          break;
        }
        default: continue; break;
        }

        if (!ok) {
          fatal_error(tmp_fmt("failed to hot reload: %s", a.name.data));
          return;
        }

        asset_write(a);
        printf("reloaded: %s\n", a.name.data);
      }
    }
  }
}

void assets_setup() {
  g_assets.rw_lock = rw_make();
  g_assets.mtx = mutex_make();
  g_assets.notify = cond_make();
}

void assets_shutdown() {
  if (g_assets.reload_thread != nullptr) {
    mutex_lock(&g_assets.mtx);
    g_assets.shutdown = true;
    mutex_unlock(&g_assets.mtx);

    cond_signal(&g_assets.notify);
    thread_join(g_assets.reload_thread);
  }
  array_trash(&g_assets.changes);

  cond_trash(&g_assets.notify);
  mutex_trash(&g_assets.mtx);
  rw_trash(&g_assets.rw_lock);

  for (auto [k, v] : g_assets.table) {
    mem_free(v->name.data);

    switch (v->kind) {
    case AssetKind_Image: image_trash(&v->image); break;
    case AssetKind_Sprite: sprite_data_trash(&v->sprite); break;
    case AssetKind_Tilemap: tilemap_trash(&v->tilemap); break;
    default: break;
    }
  }
  hashmap_trash(&g_assets.table);
}

void assets_start_hot_reload() {
  if (atomic_int_load(&g_app->hot_reload_enabled) != 0) {
    g_assets.reload_thread = thread_make(hot_reload_thread, nullptr);
  }
}

bool asset_load(AssetKind kind, String filepath, Asset *out) {
  PROFILE_FUNC();

  u64 key = fnv1a(filepath);

  {
    Asset asset = {};
    if (asset_read(key, &asset)) {
      if (out != nullptr) {
        *out = asset;
      }
      return true;
    }
  }

  {
    PROFILE_BLOCK("load new asset");

    Asset asset = {};
    asset.name = to_cstr(filepath);
    asset.hash = key;
    {
      PROFILE_BLOCK("asset modtime")
      asset.modtime = os_file_modtime(asset.name.data);
    }
    asset.kind = kind;

    bool ok = false;
    switch (kind) {
    case AssetKind_LuaRef: {
      asset.lua_ref = LUA_REFNIL;
      asset_write(asset);
      asset.lua_ref = luax_require_script(g_app->L, filepath);
      ok = true;
      break;
    }
    case AssetKind_Image: {
      ok = image_load(&asset.image, filepath);
      break;
    }
    case AssetKind_Sprite: {
      ok = sprite_data_load(&asset.sprite, filepath);
      break;
    }
    case AssetKind_Tilemap: {
      ok = tilemap_load(&asset.tilemap, filepath);
      break;
    }
    default: break;
    }

    if (!ok) {
      mem_free(asset.name.data);
      return false;
    }

    asset_write(asset);

    if (out != nullptr) {
      *out = asset;
    }
    return true;
  }
}

bool asset_read(u64 key, Asset *out) {
  rw_shared_lock(&g_assets.rw_lock);
  defer(rw_shared_unlock(&g_assets.rw_lock));

  const Asset *asset = hashmap_get(&g_assets.table, key);
  if (asset == nullptr) {
    return false;
  }

  *out = *asset;
  return true;
}

void asset_write(Asset asset) {
  rw_unique_lock(&g_assets.rw_lock);
  defer(rw_unique_unlock(&g_assets.rw_lock));

  g_assets.table[asset.hash] = asset;
}

Asset check_asset(lua_State *L, u64 key) {
  Asset asset = {};
  if (!asset_read(key, &asset)) {
    luaL_error(L, "cannot read asset");
  }

  return asset;
}

Asset check_asset_mt(lua_State *L, i32 arg, const char *mt) {
  u64 *udata = (u64 *)luaL_checkudata(L, arg, mt);

  Asset asset = {};
  bool ok = asset_read(*udata, &asset);
  if (!ok) {
    luaL_error(L, "cannot read asset");
  }

  return asset;
}
