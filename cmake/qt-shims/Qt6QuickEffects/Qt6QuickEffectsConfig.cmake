# Some Qt 6 distributions (notably the online installer's default Desktop kit) ship
# Qt6QuickEffectsPrivate but no public Qt6QuickEffects config, so
# find_package(Qt6 ... COMPONENTS QuickEffects) fails even though the private module — everything
# QuickEffects is a thin public wrapper around — is right there. Point Qt6QuickEffects_DIR at this
# directory (see docs/BUILDING.md) to alias the private target as Qt6::QuickEffects instead of
# building the real module from source.
include(CMakeFindDependencyMacro)

find_dependency(Qt6QuickEffectsPrivate)

if(TARGET Qt6::QuickEffectsPrivate AND NOT TARGET Qt6::QuickEffects)
    add_library(Qt6::QuickEffects INTERFACE IMPORTED)
    set_target_properties(Qt6::QuickEffects PROPERTIES
        INTERFACE_LINK_LIBRARIES Qt6::QuickEffectsPrivate)
endif()

set(Qt6QuickEffects_FOUND TRUE)
