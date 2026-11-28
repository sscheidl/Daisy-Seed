# One catalogue for local selection and future project additions.
PROJECTS := toy-piano kekule kekule-mod messier messier-mod ferrum ferrum-mod fourier-fx granular-fx mod-bench
BUILDABLE_PROJECTS := toy-piano fourier-fx kekule kekule-mod messier messier-mod ferrum ferrum-mod mod-bench
PROJECTS += kekule-fx-torus kekule-fx-ambient
# Added to BUILDABLE_PROJECTS only after cross-compilation is validated.
BUILDABLE_PROJECTS += kekule-fx-torus kekule-fx-ambient
PROJECT_DIR_kekule-fx-torus := instruments/kekule
PROJECT_ARGS_kekule-fx-torus := MOD=1 COMPOSED=1 FX=torus
PROJECT_STATUS_kekule-fx-torus := buildable
PROJECT_DIR_kekule-fx-ambient := instruments/kekule
PROJECT_ARGS_kekule-fx-ambient := MOD=1 COMPOSED=1 FX=ambient
PROJECT_STATUS_kekule-fx-ambient := buildable

PROJECT_DIR_toy-piano := instruments/toy-piano
PROJECTS += toy-piano-ambient
BUILDABLE_PROJECTS += toy-piano-ambient
PROJECT_DIR_toy-piano-ambient := instruments/toy-piano
PROJECT_ARGS_toy-piano-ambient := AMBIENT=1
PROJECT_STATUS_toy-piano-ambient := buildable
PROJECT_DIR_kekule := instruments/kekule
# Separate Mod on Daisy build of the same sources (own image and build/mod directory).
PROJECT_DIR_kekule-mod := instruments/kekule
PROJECT_ARGS_kekule-mod := MOD=1
PROJECT_DIR_messier := instruments/messier
# Separate Mod on Daisy build of Nebulae (own image and build/mod directory).
PROJECT_DIR_messier-mod := instruments/messier
PROJECT_ARGS_messier-mod := MOD=1
PROJECT_DIR_ferrum := instruments/ferrum
# Separate Mod on Daisy build of the looper (own image and build/mod directory).
PROJECT_DIR_ferrum-mod := instruments/ferrum
PROJECT_ARGS_ferrum-mod := MOD=1
PROJECT_DIR_fourier-fx := effects/fourier
PROJECT_DIR_granular-fx := effects/granular
PROJECT_DIR_mod-bench := modules/mod-on-daisy/bench

PROJECT_STATUS_toy-piano := buildable
PROJECT_STATUS_kekule := buildable
PROJECT_STATUS_kekule-mod := buildable
PROJECT_STATUS_messier := buildable
PROJECT_STATUS_messier-mod := buildable
PROJECT_STATUS_ferrum := buildable
PROJECT_STATUS_ferrum-mod := buildable
PROJECT_STATUS_fourier-fx := buildable
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
# BABBAGE, formerly BABBAGE (docs/acid-seq/ARBEITSAUFTRAG.md): components with standalone test firmwares
# and the composed product. Each joins BUILDABLE_PROJECTS with its first validated build.
PROJECTS += step-seq acid-303 drive-fx delay-fx babbage
# Audrey 3: Audrey II port; also needs the DaisySP-LGPL submodule (instruments/moreau/README.md).
PROJECTS += moreau
BUILDABLE_PROJECTS += moreau
PROJECT_DIR_moreau := instruments/moreau
PROJECT_STATUS_moreau := buildable
# The two effects of Audrey 3 as components with standalone firmwares.
PROJECTS += echo-delay-fx sc-reverb-fx
BUILDABLE_PROJECTS += echo-delay-fx sc-reverb-fx
PROJECT_DIR_echo-delay-fx := effects/echo-delay
PROJECT_STATUS_echo-delay-fx := buildable
PROJECT_DIR_sc-reverb-fx := effects/sc-reverb
PROJECT_STATUS_sc-reverb-fx := buildable
PROJECT_DIR_step-seq := sequencers/step-seq
PROJECT_STATUS_step-seq := buildable
BUILDABLE_PROJECTS += step-seq
PROJECT_DIR_acid-303 := instruments/acid-303
PROJECT_STATUS_acid-303 := buildable
BUILDABLE_PROJECTS += acid-303
PROJECT_DIR_drive-fx := effects/drive
PROJECT_STATUS_drive-fx := buildable
BUILDABLE_PROJECTS += drive-fx
PROJECT_DIR_delay-fx := effects/stereo-delay
PROJECT_STATUS_delay-fx := buildable
BUILDABLE_PROJECTS += delay-fx
PROJECT_DIR_babbage := instruments/babbage
PROJECT_STATUS_babbage := buildable
BUILDABLE_PROJECTS += babbage

# Streichwurst: instrument + separate FX + shared MIDI/control; ARM build verified 2026-11-01.
PROJECTS += streichwurst
BUILDABLE_PROJECTS += streichwurst
PROJECT_DIR_streichwurst := instruments/streichwurst
PROJECT_STATUS_streichwurst := buildable
# Standalone Streichwurst FX ("Modular FX first"): ARM build verified 2026-11-01.
PROJECTS += streichwurst-fx
BUILDABLE_PROJECTS += streichwurst-fx
PROJECT_DIR_streichwurst-fx := effects/streichwurst-fx
PROJECT_STATUS_streichwurst-fx := buildable
# Grain Cloud: new independent granular engine, host core only (instruments/grain-cloud/README.md).
# Joins BUILDABLE_PROJECTS with its first validated firmware build (stage 3).
PROJECTS += grain-cloud
PROJECT_DIR_grain-cloud := instruments/grain-cloud
PROJECT_STATUS_grain-cloud := planned
# FV-1 interpreter FX (research / feasibility study): standalone
# firmware with own demo programs; ARM build verified 2026-11-05. PROGRAMS=local images are local only.
PROJECTS += fv1-vm-fx
BUILDABLE_PROJECTS += fv1-vm-fx
PROJECT_DIR_fv1-vm-fx := effects/fv1-vm
PROJECT_STATUS_fv1-vm-fx := buildable
# Toy Piano -> fv1-vm (composed test product, owner decision 2026-11-05); ARM build verified 2026-11-05.
# FV1_PROGRAMS=local images are local only.
PROJECTS += toy-piano-fv1
BUILDABLE_PROJECTS += toy-piano-fv1
PROJECT_DIR_toy-piano-fv1 := instruments/toy-piano
PROJECT_ARGS_toy-piano-fv1 := FV1=1
PROJECT_STATUS_toy-piano-fv1 := buildable
# FV-1 FX Suite: fv1-vm-fx with the MIT bank effects/fv1-banks/otherworld (owner decision 2026-11-22);
# ARM build verified 2026-11-22.
PROJECTS += fv1-fx-suite
BUILDABLE_PROJECTS += fv1-fx-suite
PROJECT_DIR_fv1-fx-suite := effects/fv1-vm
PROJECT_ARGS_fv1-fx-suite := PROGRAMS=otherworld
PROJECT_STATUS_fv1-fx-suite := buildable
# Toy Piano with a filter per voice (test product for the filter models, owner decision 2026-11-06);
# ARM build verified 2026-11-06. No effects.
PROJECTS += toy-piano-filter
BUILDABLE_PROJECTS += toy-piano-filter
PROJECT_DIR_toy-piano-filter := instruments/toy-piano
PROJECT_ARGS_toy-piano-filter := FILTER=1
PROJECT_STATUS_toy-piano-filter := buildable
# Cumulonimbus (Clouds granular path, effects/cumulonimbus): standalone diagnostic FX at the 48 kHz and
# the 32 kHz internal grain rate, and Toy Piano on Cumulonimbus, playable and profiling, at both rates.
# ARM builds verified 2026-11-09; hardware NOT MEASURED.
PROJECTS += cumulonimbus-fx cumulonimbus-fx-32
BUILDABLE_PROJECTS += cumulonimbus-fx cumulonimbus-fx-32
PROJECT_DIR_cumulonimbus-fx := effects/cumulonimbus
PROJECT_ARGS_cumulonimbus-fx := RATE=48
PROJECT_STATUS_cumulonimbus-fx := buildable
PROJECT_DIR_cumulonimbus-fx-32 := effects/cumulonimbus
PROJECT_ARGS_cumulonimbus-fx-32 := RATE=32
PROJECT_STATUS_cumulonimbus-fx-32 := buildable
PROJECTS += toy-piano-cumulonimbus toy-piano-cumulonimbus-32 toy-piano-cumulonimbus-profile toy-piano-cumulonimbus-32-profile
BUILDABLE_PROJECTS += toy-piano-cumulonimbus toy-piano-cumulonimbus-32 toy-piano-cumulonimbus-profile toy-piano-cumulonimbus-32-profile
PROJECT_DIR_toy-piano-cumulonimbus := instruments/toy-piano
PROJECT_ARGS_toy-piano-cumulonimbus := CUMULO=1 CUMULO_RATE=48 CUMULO_PROFILE=0
PROJECT_STATUS_toy-piano-cumulonimbus := buildable
PROJECT_DIR_toy-piano-cumulonimbus-32 := instruments/toy-piano
PROJECT_ARGS_toy-piano-cumulonimbus-32 := CUMULO=1 CUMULO_RATE=32 CUMULO_PROFILE=0
PROJECT_STATUS_toy-piano-cumulonimbus-32 := buildable
PROJECT_DIR_toy-piano-cumulonimbus-profile := instruments/toy-piano
PROJECT_ARGS_toy-piano-cumulonimbus-profile := CUMULO=1 CUMULO_RATE=48 CUMULO_PROFILE=1
PROJECT_STATUS_toy-piano-cumulonimbus-profile := buildable
PROJECT_DIR_toy-piano-cumulonimbus-32-profile := instruments/toy-piano
PROJECT_ARGS_toy-piano-cumulonimbus-32-profile := CUMULO=1 CUMULO_RATE=32 CUMULO_PROFILE=1
PROJECT_STATUS_toy-piano-cumulonimbus-32-profile := buildable
