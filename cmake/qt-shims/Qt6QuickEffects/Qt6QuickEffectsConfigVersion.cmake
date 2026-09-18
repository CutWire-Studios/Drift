# Always compatible: this shim just aliases Qt6QuickEffectsPrivate (see Qt6QuickEffectsConfig.cmake),
# so it tracks whatever Qt6 version is actually being configured rather than one pinned number.
if(DEFINED Qt6_VERSION)
    set(PACKAGE_VERSION "${Qt6_VERSION}")
else()
    set(PACKAGE_VERSION "0.0.0")
endif()
set(PACKAGE_VERSION_COMPATIBLE TRUE)
set(PACKAGE_VERSION_EXACT TRUE)
