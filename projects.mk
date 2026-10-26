# One catalogue for local selection and future project additions.
PROJECTS := toy-piano physical-modeling physical-modeling-mod granular looper looper-mod reverb-fx granular-fx mod-bench
BUILDABLE_PROJECTS := toy-piano reverb-fx physical-modeling physical-modeling-mod granular looper looper-mod mod-bench
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
# Separate Mod on Daisy build of the looper (own image and build/mod directory).
PROJECT_DIR_looper-mod := instruments/looper
PROJECT_ARGS_looper-mod := MOD=1
PROJECT_DIR_reverb-fx := effects/reverb
PROJECT_DIR_granular-fx := effects/granular
PROJECT_DIR_mod-bench := modules/mod-on-daisy/bench

PROJECT_STATUS_toy-piano := buildable
PROJECT_STATUS_physical-modeling := buildable
PROJECT_STATUS_physical-modeling-mod := buildable
PROJECT_STATUS_granular := buildable
PROJECT_STATUS_looper := buildable
PROJECT_STATUS_looper-mod := buildable
PROJECT_STATUS_reverb-fx := buildable
PROJECT_STATUS_granular-fx := planned
PROJECT_STATUS_mod-bench := buildable
# Tape FX (TapeVibe/MixForge port): standalone stereo effect and Toy Piano test product.
PROJECTS += tape-fx toy-piano-tape
BUILDABLE_PROJECTS += tape-fx toy-piano-tape
PROJECT_DIR_tape-fx := effects/tape-fx
PROJECT_STATUS_tape-fx := buildable
PROJECT_DIR_toy-piano-tape := instruments/toy-piano
PROJECT_ARGS_toy-piano-tape := TAPE=1
PROJECT_STATUS_toy-piano-tape := buildable
# Acid Seq (docs/acid-seq/ARBEITSAUFTRAG.md): components with standalone test firmwares
# and the composed product. Each joins BUILDABLE_PROJECTS with its first validated build.
PROJECTS += step-seq acid-303 drive-fx delay-fx acid-seq
PROJECT_DIR_step-seq := sequencers/step-seq
PROJECT_STATUS_step-seq := buildable
BUILDABLE_PROJECTS += step-seq
PROJECT_DIR_acid-303 := instruments/acid-303
PROJECT_STATUS_acid-303 := buildable
BUILDABLE_PROJECTS += acid-303
PROJECT_DIR_drive-fx := effects/drive
PROJECT_STATUS_drive-fx := planned
PROJECT_DIR_delay-fx := effects/stereo-delay
PROJECT_STATUS_delay-fx := planned
PROJECT_DIR_acid-seq := sequencers/acid-seq
PROJECT_STATUS_acid-seq := planned
