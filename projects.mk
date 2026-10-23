# One catalogue for local selection and future project additions.
PROJECTS := toy-piano physical-modeling physical-modeling-mod granular looper reverb-fx granular-fx mod-bench
BUILDABLE_PROJECTS := toy-piano reverb-fx physical-modeling physical-modeling-mod granular looper mod-bench

PROJECT_DIR_toy-piano := instruments/toy-piano
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
