# SPDX-License-Identifier: GPL-2.0-or-later

set(INC
  .
  intern
  ../blenkernel
  ../blenlib
  ../makesdna
  ../../../intern/clog
  ../../../intern/guardedalloc
)

set(INC_SYS
)

set(SRC
  intern/asset_catalog.cc
  intern/asset_catalog_path.cc
  intern/asset_catalog_tree.cc
  intern/asset_identifier.cc
  intern/asset_library.cc
  intern/asset_library_service.cc
  intern/asset_representation.cc
  intern/asset_storage.cc
  intern/utils.cc

  AS_asset_catalog.hh
  AS_asset_catalog_path.hh
  AS_asset_catalog_tree.hh
  AS_asset_identifier.hh
  AS_asset_library.hh
  AS_asset_representation.hh
  intern/asset_library_service.hh
  intern/asset_storage.hh
  intern/utils.hh

  AS_asset_library.h
  AS_asset_representation.h
)

set(LIB
)


blender_add_lib(bf_asset_system "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")

if(WITH_GTESTS)
  set(TEST_SRC
    tests/asset_catalog_test.cc
    tests/asset_catalog_path_test.cc
    tests/asset_library_service_test.cc
    tests/asset_library_test.cc
  )
  set(TEST_LIB
    bf_asset_system
  )
  include(GTestTesting)
  blender_add_test_lib(bf_asset_system_tests "${TEST_SRC}" "${INC};${TEST_INC}" "${INC_SYS}" "${LIB};${TEST_LIB}")
endif()
