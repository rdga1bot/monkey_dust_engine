#pragma once
// Include this header (instead of md_camera.h) in USE_GLM builds that need
// Raylib adapter methods: ToRaylib(), FromRaylib(), GetViewProjRaylib(), FrustumPlanes().
// In non-USE_GLM builds math_types.h already pulls raylib.h, so adapters are
// available from md_camera.h directly — this header is still safe to include.
#include "raylib.h"
#include <monkey_dust/render/md_camera.h>
