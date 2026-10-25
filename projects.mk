# One catalogue for local selection and future project additions.
PROJECTS := toy-piano physical-modeling physical-modeling-mod granular looper reverb-fx granular-fx mod-bench
BUILDABLE_PROJECTS := toy-piano reverb-fx physical-modeling physical-modeling-mod granular looper mod-bench
PROJECTS += physical-modeling-fx-torus physical-modeling-fx-ambient
# Added to BUILDABLE_PROJECTS only after cross-compilation is validated.
BUILDABLE_PROJECTS += physical-modeling-fx-torus physical-modeling-fx-ambient
PROJECT_DIR_physical-modeling-fx-torus := instruments/physical-modeling
PROJECT_ARGS_physical-modeling-fx-torus := MOD=1 COMPOSED=1 FX=torus
PROJECT_STATUS_physical-modeling-fx-torus := buildable
PROJECT_DIR_physical-modeling-fx-ambient := instruments/physical-modeling
PROJECT_ARGS_physical-modeling-fx-ambient := MOD=1 COMPOSED=1 FX=ambient
PROJECT_STATUS_physical-modeling-fx-ambient := buildable

PROJECT_DIR_toy-piano := instruments/toy-piano
PROJECTS += toy-piano-ambient
BUILDABLE_PROJECTS += toy-piano-ambient
PROJECT_DIR_toy-piano-ambient := instruments/toy-piano
PROJECT_ARGS_toy-piano-ambient := AMBIENT=1
PROJECT_STATUS_toy-piano-ambient := buildable
PROJECT_DIR_physical-modeling := instruments/physical-modeling
# Separate Mod on Daisy build of the same sources (own image and build/mod directory).
PROJECT_DIR_physical-modeling-mod := instruments/physical-modeling
PROJECT_ARGS_physical-modeling-mod := MOD=1
PROJECT_DIR_granular := instruments/granular
PROJECT_DIR_looper := instruments/looper
PROJECT_DIR_reverb-fx := effects/reverb
PROJECT_DIR_granular-fx := effects/granular
PROJECT_DIR_mod-bench := modules/mod-on-daisy/bench

PROJECT_STATUS_toy-piano := buildable
PROJECT_STATUS_physical-modeling := buildable
PROJECT_STATUS_physical-modeling-mod := buildable
PROJECT_STATUS_granular := buildable
PROJECT_STATUS_looper := buildable
PROJECT_STATUS_reverb-fx := buildable
PROJECT_STATUS_granular-fx := planned
PROJECT_STATUS_mod-bench := buildable
