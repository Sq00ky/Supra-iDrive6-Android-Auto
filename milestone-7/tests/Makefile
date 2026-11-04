# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
# Host tests (Linux g++). From milestone-7: `make check` (or `make -C tests check`).
#   make -C tests check          build and run every test, then the TSAN run of test_session_lifecycle
#   make -C tests test_pcm       build one test (binary in tests/build/)
#   make -C tests run-test_pcm   build and run one test
#   make -C tests tsan           test_session_lifecycle under ThreadSanitizer only
# Tests run from milestone-7, so fixture paths are relative to it.

ROOT := ..
OUT  := build

HOST_CXX ?= g++
HOST_CC  ?= gcc
INC      := -I$(ROOT)/include -I$(ROOT)/src -I$(ROOT)/tests -I$(ROOT)/third_party/speexdsp
CXXFLAGS := -std=gnu++0x -O1 -g -Wall -DNBT_HOST_TEST $(INC)
CFLAGS   := -O1 -g -Wall $(INC)
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
SRC_test_bt_play           := src/audio/bt_play.cpp
SRC_test_car_config        := src/sdr_builder.cpp
SRC_test_car_gps           := src/car/gps.cpp src/most/client.cpp tests/fake_dsi_client.cpp
SRC_test_ccs_config        :=
SRC_test_ccs_decoder       :=
SRC_test_ccs_input         := src/car/ccs_input.cpp src/most/client.cpp tests/fake_dsi_client.cpp
SRC_test_ccs_push          :=
SRC_test_cds_parse         :=
SRC_test_ctl_wizard        :=
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
SRC_test_session_lifecycle := src/aa_cryptor.cpp src/aa_session.cpp src/audio/audio_acks.cpp src/crash_handler.cpp src/sdr_builder.cpp
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

TESTS := $(patsubst SRC_%,%,$(filter SRC_test_%,$(.VARIABLES)))

.PHONY: all check tsan clean $(TESTS) $(addprefix run-,$(TESTS)) run-test_media_payload
all: $(TESTS)

check: $(addprefix run-,$(TESTS)) run-test_media_payload tsan
	@echo "[CHECK] all host tests passed"

$(OUT):
	@mkdir -p $(OUT)

# speexdsp object, shared by the tests that need it (plain and TSAN builds).
$(OUT)/speex.o: $(SPEEX) | $(OUT)
	$(HOST_CC) $(CFLAGS) -MMD -MP -c $< -o $@

srcs = $(addprefix $(ROOT)/,$(filter-out $(SPEEX),$(SRC_$(1))))
objs = $(if $(filter $(SPEEX),$(SRC_$(1))),$(OUT)/speex.o)

define TEST_RULES
$(1): $(OUT)/$(1)
$(OUT)/$(1): $(1).cpp $$(call srcs,$(1)) $$(call objs,$(1)) | $(OUT)
	$$(HOST_CXX) $$(CXXFLAGS) -MMD -MP -MF $$@.d -o $$@ $(1).cpp $$(call srcs,$(1)) $$(call objs,$(1)) $$(LDLIBS)
run-$(1): $(OUT)/$(1)
	@cd $(ROOT) && tests/$(OUT)/$(1) $$(ARGS_$(1)) > tests/$(OUT)/$(1).log 2>&1 \
	  && echo "[PASS] $(1)" || { echo "[FAIL] $(1) (tests/$(OUT)/$(1).log)"; tail -20 tests/$(OUT)/$(1).log; exit 1; }
endef
$(foreach t,$(TESTS),$(eval $(call TEST_RULES,$(t))))

# This one #includes aa_session.cpp, so it is not in its SRC list, and with several sources -MF keeps only the last one's
# dependencies: without this line, a change to the session would not rebuild it.
$(OUT)/test_session_sensors: $(ROOT)/src/aa_session.cpp $(ROOT)/src/aa_session.hpp $(ROOT)/src/aa_sensor_msg.hpp stub_cryptor.hpp

# Not here: rhmiapp_host_test, which dlopens a host build of rhmiapp/libNbtAad.so linked against
# rhmiapp_fake_appframework.cpp.

# Compiles the payload-boundary block out of aa_session.cpp itself.
run-test_media_payload:
	@cd $(ROOT) && python3 tests/test_media_payload.py > tests/$(OUT)/test_media_payload.log 2>&1 \
	  && echo "[PASS] test_media_payload.py" || { echo "[FAIL] test_media_payload.py"; tail -20 tests/$(OUT)/test_media_payload.log; exit 1; }

# 400 session cycles under ThreadSanitizer.
TSAN_SRCS := test_session_lifecycle.cpp $(call srcs,test_session_lifecycle)
$(OUT)/test_session_lifecycle.tsan: $(TSAN_SRCS) | $(OUT)
	$(HOST_CXX) $(CXXFLAGS) -fsanitize=thread -MMD -MP -MF $@.d -o $@ $(TSAN_SRCS) $(LDLIBS)
tsan: $(OUT)/test_session_lifecycle.tsan
	@cd $(ROOT) && TSAN_OPTIONS=halt_on_error=1 setarch -R tests/$(OUT)/test_session_lifecycle.tsan \
	  > tests/$(OUT)/test_session_lifecycle.tsan.log 2>&1 \
	  && echo "[PASS] test_session_lifecycle (TSAN)" \
	  || { echo "[FAIL] test_session_lifecycle (TSAN)"; tail -30 tests/$(OUT)/test_session_lifecycle.tsan.log; exit 1; }

clean:
	rm -rf $(OUT)

-include $(wildcard $(OUT)/*.d)
