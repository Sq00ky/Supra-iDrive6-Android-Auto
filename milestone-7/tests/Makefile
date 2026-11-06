# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
# Host tests (Linux g++). From milestone-7: `make check` (or `make -C tests check`).
#   make -C tests check          build and run host checks, including TSAN when supported
#   make -C tests test_pcm       build one test (binary in tests/build/)
#   make -C tests run-test_pcm   build and run one test
#   make -C tests tsan           test_session_lifecycle under ThreadSanitizer only
# Tests run from milestone-7, so fixture paths are relative to it.

ROOT := ..
OUT  := build

HOST_CXX ?= g++
HOST_CC  ?= gcc
INC      := -I$(ROOT)/include -I$(ROOT)/src -I$(ROOT)/tests -I$(ROOT)/third_party/speexdsp
CXXFLAGS := -std=gnu++98 -O1 -g -Wall -Wextra -UNDEBUG -DNBT_HOST_TEST $(INC)
CFLAGS   := -O1 -g -Wall -Wextra -UNDEBUG -DNBT_HOST_TEST $(INC)
LDLIBS   := -lpthread -lssl -lcrypto -lm

# The vendored speexdsp resampler, built as C the way the daemon builds it.
SPEEX := $(ROOT)/src/audio/speex.c

# Each test and the daemon sources it links (beyond its own .cpp).
SRC_test_aa_sensor_msg     :=
SRC_test_alt_wifi          := src/wireless_setup.cpp
SRC_test_ap_candidates     := src/wireless_setup.cpp
SRC_test_audio_acks        := src/audio/audio_acks.cpp
SRC_test_audio_channels    :=
SRC_test_audio_config      := src/audio/config.cpp
SRC_test_audio_format      := src/sdr_builder.cpp
SRC_test_audio_mixer       := src/audio/mixer.cpp src/audio/playout.cpp src/audio/drift.cpp src/audio/stream_rate.cpp
SRC_test_audio_route       := src/audio/audio_route.cpp
SRC_test_bcl_detect        := src/wireless_setup.cpp
SRC_test_boot_guard        :=
SRC_test_overlay_watch     :=
SRC_test_pf_aa             :=
SRC_test_bt_play           := src/audio/bt_play.cpp
SRC_test_car_config        := src/sdr_builder.cpp
SRC_test_car_gps           := src/car/geoid.cpp src/car/gps.cpp src/most/client.cpp tests/fake_dsi_client.cpp
SRC_test_ccs_config        :=
SRC_test_ccs_decoder       :=
SRC_test_ccs_input         := src/car/ccs_input.cpp src/most/client.cpp tests/fake_dsi_client.cpp
SRC_test_ccs_push          :=
SRC_test_cds_parse         :=
SRC_test_ctl_wizard        :=
SRC_test_entry_key         :=
SRC_test_input_activity    := src/car/input_activity.cpp src/car/ccs_input.cpp src/most/client.cpp tests/fake_dsi_client.cpp
SRC_test_kombi_graph       :=
SRC_test_kombi_nav         := src/kombi_client.cpp src/kombi_nav.cpp
SRC_test_kombi_nav_msg     :=
SRC_test_lm_geometry       :=
SRC_test_media_ack         :=
SRC_test_media_msgs        :=
SRC_test_mmr_launch        :=
SRC_test_mmr_pps           :=
SRC_test_most_client       := src/most/client.cpp tests/fake_dsi_client.cpp
SRC_test_most_msg          :=
SRC_test_most_reassembler  :=
SRC_test_most_registry     :=
SRC_test_most_router       :=
SRC_test_most_trace        := src/most/client.cpp src/most/trace.cpp tests/fake_dsi_client.cpp
SRC_test_neutral_resume    :=
SRC_test_pcm               := src/audio/pcm_ring.cpp src/audio/resampler.cpp $(SPEEX)
SRC_test_resampler         := src/audio/resampler.cpp $(SPEEX)
SRC_test_ptt               :=
SRC_test_route_policy      :=
SRC_test_sdr               := src/sdr_builder.cpp
SRC_test_sdr_drop          := src/sdr_builder.cpp
SRC_test_session_lifecycle := src/aa_session.cpp src/audio/audio_acks.cpp src/crash_handler.cpp src/sdr_builder.cpp
SRC_test_session_owner     :=
SRC_test_session_sensors   := src/audio/audio_acks.cpp src/crash_handler.cpp src/sdr_builder.cpp
SRC_test_touch             := src/touch_probe.cpp
SRC_test_touch_frame       :=
SRC_test_usb_accessory     := src/tcp_server.cpp src/usb_accessory.cpp src/usb_direct.cpp src/wireless_setup.cpp
SRC_test_usb_log           :=
SRC_test_voice_scenario    := src/audio/voice_scenario.cpp
SRC_test_volume_input      := src/car/volume_input.cpp src/car/ccs_input.cpp src/most/client.cpp tests/fake_dsi_client.cpp
SRC_test_volume_level      :=
SRC_test_volume            := src/audio/mixer.cpp src/audio/playout.cpp src/audio/drift.cpp src/audio/stream_rate.cpp
SRC_test_wpp               := src/wireless_setup.cpp

# Arguments some tests take (paths relative to milestone-7).
ARGS_test_ccs_decoder := tests/fixtures/controls_2026-10-22_e5.txt
ARGS_test_ccs_push    := tests/fixtures/controls_2026-10-23_push.txt
ARGS_test_ccs_input   := tests/fixtures/controls_2026-10-23_push.txt

TESTS := $(basename $(notdir $(wildcard test_*.cpp)))
PY_TESTS := $(basename $(notdir $(wildcard test_*.py)))
MISSING_SRCS := $(foreach t,$(TESTS),$(if $(filter undefined,$(origin SRC_$(t))),$(t)))
ifneq ($(strip $(MISSING_SRCS)),)
$(error Define SRC_<test> for: $(MISSING_SRCS))
endif

# Match the host source compilation check, excluding target-only and separately built sources.
HOST_SRCS := $(shell find $(ROOT)/src -type f \( -name '*.cpp' -o -name '*.c' \) \
	! -name main.cpp ! -name aa_cryptor.cpp ! -name aa_session.cpp \
	! -name video_manager.cpp ! -name asound_device.cpp ! -name mmr_launch.cpp)
HOST_OBJS := $(patsubst $(ROOT)/src/%.cpp,$(OUT)/host/%.o,$(filter %.cpp,$(HOST_SRCS))) \
	$(patsubst $(ROOT)/src/%.c,$(OUT)/host/%.o,$(filter %.c,$(HOST_SRCS)))

NBT_HOST_TSAN ?= 1
ifeq ($(NBT_HOST_TSAN),1)
HAS_TSAN := $(shell printf 'int main() { return 0; }\n' | $(HOST_CXX) -fsanitize=thread -x c++ - -o /dev/null >/dev/null 2>&1 && echo 1)
ifeq ($(HAS_TSAN),1)
TSAN_CHECK := tsan
else
$(info ThreadSanitizer lifecycle check unavailable: compiler/linker probe failed)
endif
else ifneq ($(NBT_HOST_TSAN),0)
$(error NBT_HOST_TSAN must be 0 or 1)
endif

.PHONY: all check tsan clean host-sources $(TESTS) $(addprefix run-,$(TESTS) $(PY_TESTS)) run-test_touch_split
all: $(TESTS)

check: host-sources $(OUT)/libNbtAad.so $(addprefix run-,$(TESTS) $(PY_TESTS)) run-test_touch_split $(TSAN_CHECK)
	@echo "[CHECK] all host tests passed"

$(OUT):
	@mkdir -p $(OUT)

# speexdsp object, shared by the tests that need it (plain and TSAN builds).
$(OUT)/speex.o: $(SPEEX) | $(OUT)
	$(HOST_CC) $(CFLAGS) -MMD -MP -c $< -o $@

host-sources: $(HOST_OBJS)

$(OUT)/host/%.o: $(ROOT)/src/%.cpp
	@mkdir -p $(@D)
	$(HOST_CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(OUT)/host/%.o: $(ROOT)/src/%.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/libNbtAad.so: $(ROOT)/rhmiapp/nbt_aad_app.cpp | $(OUT)
	$(HOST_CXX) $(CXXFLAGS) -fPIC -shared -MMD -MP -MF $@.d -o $@ $<

srcs = $(addprefix $(ROOT)/,$(filter-out $(SPEEX),$(SRC_$(1))))
objs = $(if $(filter $(SPEEX),$(SRC_$(1))),$(OUT)/speex.o)
FLAGS_test_session_lifecycle := -include $(CURDIR)/stub_cryptor.hpp
WORKDIR_test_wpp := $(CURDIR)/$(OUT)/wpp
ENV_test_touch := NBT_TOUCH_CONNECT=0

define TEST_RULES
$(1): $(OUT)/$(1)
$(OUT)/$(1): $(1).cpp $$(call srcs,$(1)) $$(call objs,$(1)) | $(OUT)
	$$(HOST_CXX) $$(CXXFLAGS) $$(FLAGS_$(1)) -MM -MP -MT $$@ $(1).cpp $$(call srcs,$(1)) > $$@.d
	$$(HOST_CXX) $$(CXXFLAGS) $$(FLAGS_$(1)) -o $$@ $(1).cpp $$(call srcs,$(1)) $$(call objs,$(1)) $$(LDLIBS)
run-$(1): $(OUT)/$(1)
	@mkdir -p $$(if $$(WORKDIR_$(1)),$$(WORKDIR_$(1)),$(ROOT))
	@cd $$(if $$(WORKDIR_$(1)),$$(WORKDIR_$(1)),$(ROOT)) && $$(ENV_$(1)) timeout 120s $(CURDIR)/$(OUT)/$(1) $$(ARGS_$(1)) > $(CURDIR)/$(OUT)/$(1).log 2>&1 \
	  && echo "[PASS] $(1)" || { echo "[FAIL] $(1) ($(OUT)/$(1).log)"; tail -20 $(CURDIR)/$(OUT)/$(1).log; exit 1; }
endef
$(foreach t,$(TESTS),$(eval $(call TEST_RULES,$(t))))

run-test_touch_split: run-test_touch
	@cd $(ROOT) && NBT_TOUCH_CONNECT=0 TOUCH_SPLIT=1 timeout 120s tests/$(OUT)/test_touch > tests/$(OUT)/test_touch_split.log 2>&1 \
	  && echo "[PASS] test_touch_split" || { echo "[FAIL] test_touch_split"; tail -20 tests/$(OUT)/test_touch_split.log; exit 1; }

# This test includes aa_session.cpp rather than listing it among its sources.
$(OUT)/test_session_sensors: $(ROOT)/src/aa_session.cpp $(ROOT)/src/aa_session.hpp $(ROOT)/src/aa_sensor_msg.hpp stub_cryptor.hpp

# Not here: rhmiapp_host_test, which dlopens a host build of rhmiapp/libNbtAad.so linked against
# rhmiapp_fake_appframework.cpp.

$(addprefix run-,$(PY_TESTS)): run-%: | $(OUT)
	@cd $(ROOT) && timeout 120s python3 tests/$*.py > tests/$(OUT)/$*.log 2>&1 \
	  && echo "[PASS] $*.py" || { echo "[FAIL] $*.py"; tail -20 tests/$(OUT)/$*.log; exit 1; }

# 400 session cycles under ThreadSanitizer.
TSAN_SRCS := test_session_lifecycle.cpp $(call srcs,test_session_lifecycle)
$(OUT)/test_session_lifecycle.tsan: $(TSAN_SRCS) | $(OUT)
	$(HOST_CXX) $(CXXFLAGS) $(FLAGS_test_session_lifecycle) -MM -MP -MT $@ $(TSAN_SRCS) > $@.d
	$(HOST_CXX) $(CXXFLAGS) $(FLAGS_test_session_lifecycle) -fsanitize=thread -o $@ $(TSAN_SRCS) $(LDLIBS)
tsan: $(OUT)/test_session_lifecycle.tsan
	@cd $(ROOT) && TSAN_OPTIONS=halt_on_error=1 timeout 120s tests/$(OUT)/test_session_lifecycle.tsan \
	  > tests/$(OUT)/test_session_lifecycle.tsan.log 2>&1 \
	  && echo "[PASS] test_session_lifecycle (TSAN)" \
	  || { echo "[FAIL] test_session_lifecycle (TSAN)"; tail -30 tests/$(OUT)/test_session_lifecycle.tsan.log; exit 1; }

clean:
	rm -rf $(OUT)

-include $(wildcard $(OUT)/*.d $(OUT)/host/*.d $(OUT)/host/*/*.d)
