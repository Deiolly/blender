/* SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup asset_system
 */

#include "BKE_blender.h"
#include "BKE_preferences.h"

#include "BLI_fileops.h" /* For PATH_MAX (at least on Windows). */
#include "BLI_path_util.h"
#include "BLI_string_ref.hh"

#include "DNA_asset_types.h"
#include "DNA_userdef_types.h"

#include "CLG_log.h"

#include "AS_asset_library.hh"
#include "asset_library_service.hh"
#include "utils.hh"

/* When enabled, use a pre file load handler (#BKE_CB_EVT_LOAD_PRE) callback to destroy the asset
 * library service. Without this an explicit call from the file loading code is needed to do this,
 * which is not as nice.
 *
 * TODO Currently disabled because UI data depends on asset library data, so we have to make sure
 * it's freed in the right order (UI first). Pre-load handlers don't give us this order.
 * Should be addressed with a proper ownership model for the asset system:
 * https://wiki.blender.org/wiki/Source/Architecture/Asset_System/Back_End#Ownership_Model
 */
//#define WITH_DESTROY_VIA_LOAD_HANDLER

static CLG_LogRef LOG = {"asset_system.asset_library_service"};

namespace blender::asset_system {

std::unique_ptr<AssetLibraryService> AssetLibraryService::instance_;
bool AssetLibraryService::atexit_handler_registered_ = false;

AssetLibraryService *AssetLibraryService::get()
{
  if (!instance_) {
    allocate_service_instance();
  }
  return instance_.get();
}

void AssetLibraryService::destroy()
{
  if (!instance_) {
    return;
  }
  instance_->app_handler_unregister();
  instance_.reset();
}

AssetLibrary *AssetLibraryService::get_asset_library(
    const Main *bmain, const AssetLibraryReference &library_reference)
{
  if (library_reference.type == ASSET_LIBRARY_LOCAL) {
    /* For the "Current File" library  we get the asset library root path based on main. */
    std::string root_path = bmain ? AS_asset_library_find_suitable_root_path_from_main(bmain) : "";

    if (root_path.empty()) {
      /* File wasn't saved yet. */
      return get_asset_library_current_file();
    }

    return get_asset_library_on_disk(root_path);
  }
  if (library_reference.type == ASSET_LIBRARY_CUSTOM) {
    bUserAssetLibrary *user_library = BKE_preferences_asset_library_find_from_index(
        &U, library_reference.custom_library_index);

    if (user_library) {
      return get_asset_library_on_disk(user_library->path);
    }
  }

  return nullptr;
}

AssetLibrary *AssetLibraryService::get_asset_library_on_disk(StringRefNull root_path)
{
  BLI_assert_msg(!root_path.is_empty(),
                 "top level directory must be given for on-disk asset library");

  std::string normalized_root_path = utils::normalize_directory_path(root_path);

  std::unique_ptr<AssetLibrary> *lib_uptr_ptr = on_disk_libraries_.lookup_ptr(
      normalized_root_path);
  if (lib_uptr_ptr != nullptr) {
    CLOG_INFO(&LOG, 2, "get \"%s\" (cached)", normalized_root_path.c_str());
    AssetLibrary *lib = lib_uptr_ptr->get();
    lib->refresh();
    return lib;
  }

  std::unique_ptr lib_uptr = std::make_unique<AssetLibrary>(normalized_root_path);
  AssetLibrary *lib = lib_uptr.get();

  lib->on_blend_save_handler_register();
  lib->load_catalogs();

  on_disk_libraries_.add_new(normalized_root_path, std::move(lib_uptr));
  CLOG_INFO(&LOG, 2, "get \"%s\" (loaded)", normalized_root_path.c_str());
  return lib;
}

AssetLibrary *AssetLibraryService::get_asset_library_current_file()
{
  if (current_file_library_) {
    CLOG_INFO(&LOG, 2, "get current file lib (cached)");
  }
  else {
    CLOG_INFO(&LOG, 2, "get current file lib (loaded)");
    current_file_library_ = std::make_unique<AssetLibrary>();
    current_file_library_->on_blend_save_handler_register();
  }

  AssetLibrary *lib = current_file_library_.get();
  return lib;
}

void AssetLibraryService::allocate_service_instance()
{
  instance_ = std::make_unique<AssetLibraryService>();
  instance_->app_handler_register();

  if (!atexit_handler_registered_) {
    /* Ensure the instance gets freed before Blender's memory leak detector runs. */
    BKE_blender_atexit_register([](void * /*user_data*/) { AssetLibraryService::destroy(); },
                                nullptr);
    atexit_handler_registered_ = true;
  }
}

static void on_blendfile_load(struct Main * /*bMain*/,
                              struct PointerRNA ** /*pointers*/,
                              const int /*num_pointers*/,
                              void * /*arg*/)
{
#ifdef WITH_DESTROY_VIA_LOAD_HANDLER
  AssetLibraryService::destroy();
#endif
}

void AssetLibraryService::app_handler_register()
{
  /* The callback system doesn't own `on_load_callback_store_`. */
  on_load_callback_store_.alloc = false;

  on_load_callback_store_.func = &on_blendfile_load;
  on_load_callback_store_.arg = this;

  BKE_callback_add(&on_load_callback_store_, BKE_CB_EVT_LOAD_PRE);
}

void AssetLibraryService::app_handler_unregister()
{
  BKE_callback_remove(&on_load_callback_store_, BKE_CB_EVT_LOAD_PRE);
  on_load_callback_store_.func = nullptr;
  on_load_callback_store_.arg = nullptr;
}

bool AssetLibraryService::has_any_unsaved_catalogs() const
{
  if (current_file_library_ && current_file_library_->catalog_service->has_unsaved_changes()) {
    return true;
  }

  for (const auto &asset_lib_uptr : on_disk_libraries_.values()) {
    if (asset_lib_uptr->catalog_service->has_unsaved_changes()) {
      return true;
    }
  }

  return false;
}

void AssetLibraryService::foreach_loaded_asset_library(FunctionRef<void(AssetLibrary &)> fn) const
{
  if (current_file_library_) {
    fn(*current_file_library_);
  }

  for (const auto &asset_lib_uptr : on_disk_libraries_.values()) {
    fn(*asset_lib_uptr);
  }
}

}  // namespace blender::asset_system
