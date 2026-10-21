// Excluded actual-input preparation for adopted groups02--06 only.
// Fresh installed C++ and Python runs are required; source completeness gives
// no runtime credit. Exact numeric expectations are pinned capture002 values.
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace imagejxx::desktop {
namespace {
constexpr const char *contractSha="2652bcf1c0665e997fb1bbe55c7f616071359e6b99293b875b2b8555ff045255";
constexpr const char *referenceSha="8885c41ded23f51e56b1c42e28245bf4b9320108dacfa09da631e14fbe9bf762";
Json adoptedVariants() {
  Json result=Json::array();
  result.push_back(Json::parse(R"ADOPTED({"assertions":["valid_typed_sigma","exact_owned_pixels","changed_one_history","exact_undo_redo","preserved_nonpixel_state"],"id":"P02a.shared_ui.02.sigma-valid-typing.sigma-0p5","output_oracle":"u8-full-s0p5","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"fresh_host_session":true,"input_oracle":"u8-full-s2","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"replace_sigma","text":"0.5"},{"label":"valid_draft","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"condition":"gaussian_apply_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"applied","op":"observe"},{"op":"actual_undo"},{"label":"undone","op":"observe"},{"op":"actual_redo"},{"label":"redone","op":"observe"}],"typed_sigma":"0.5"})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"assertions":["valid_typed_sigma","exact_owned_pixels","changed_one_history","exact_undo_redo","preserved_nonpixel_state"],"id":"P02a.shared_ui.02.sigma-valid-typing.sigma-12","output_oracle":"u8-full-s12","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"fresh_host_session":true,"input_oracle":"u8-full-s2","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"replace_sigma","text":"12"},{"label":"valid_draft","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"condition":"gaussian_apply_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"applied","op":"observe"},{"op":"actual_undo"},{"label":"undone","op":"observe"},{"op":"actual_redo"},{"label":"redone","op":"observe"}],"typed_sigma":"12"})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"assertions":["invalid_draft_error","ok_and_preview_disabled","no_gaussian_job","unchanged_document","remembered_unchanged"],"id":"P02a.shared_ui.03.sigma-invalid-rejection.empty","reason":"missing number","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"fresh_host_session":true,"input_oracle":"u8-full-s2","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"replace_sigma","text":""},{"label":"invalid_draft","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"name":"Enter","op":"key_pulse"},{"label":"attempted_disabled_accept","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"typed_sigma":""})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"assertions":["invalid_draft_error","ok_and_preview_disabled","no_gaussian_job","unchanged_document","remembered_unchanged"],"id":"P02a.shared_ui.03.sigma-invalid-rejection.malformed","reason":"malformed token","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"fresh_host_session":true,"input_oracle":"u8-full-s2","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"replace_sigma","text":"abc"},{"label":"invalid_draft","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"name":"Enter","op":"key_pulse"},{"label":"attempted_disabled_accept","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"typed_sigma":"abc"})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"assertions":["invalid_draft_error","ok_and_preview_disabled","no_gaussian_job","unchanged_document","remembered_unchanged"],"id":"P02a.shared_ui.03.sigma-invalid-rejection.negative","reason":"negative sigma","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"fresh_host_session":true,"input_oracle":"u8-full-s2","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"replace_sigma","text":"-1"},{"label":"invalid_draft","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"name":"Enter","op":"key_pulse"},{"label":"attempted_disabled_accept","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"typed_sigma":"-1"})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"assertions":["invalid_draft_error","ok_and_preview_disabled","no_gaussian_job","unchanged_document","remembered_unchanged"],"id":"P02a.shared_ui.03.sigma-invalid-rejection.nan","reason":"nonfinite sigma","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"fresh_host_session":true,"input_oracle":"u8-full-s2","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"replace_sigma","text":"NaN"},{"label":"invalid_draft","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"name":"Enter","op":"key_pulse"},{"label":"attempted_disabled_accept","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"typed_sigma":"NaN"})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"assertions":["invalid_draft_error","ok_and_preview_disabled","no_gaussian_job","unchanged_document","remembered_unchanged"],"id":"P02a.shared_ui.03.sigma-invalid-rejection.infinity","reason":"nonfinite sigma","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"fresh_host_session":true,"input_oracle":"u8-full-s2","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"replace_sigma","text":"Infinity"},{"label":"invalid_draft","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"name":"Enter","op":"key_pulse"},{"label":"attempted_disabled_accept","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"typed_sigma":"Infinity"})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"assertions":["invalid_draft_error","ok_and_preview_disabled","no_gaussian_job","unchanged_document","remembered_unchanged"],"id":"P02a.shared_ui.03.sigma-invalid-rejection.negative-infinity","reason":"nonfinite sigma","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"fresh_host_session":true,"input_oracle":"u8-full-s2","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"replace_sigma","text":"-Infinity"},{"label":"invalid_draft","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"name":"Enter","op":"key_pulse"},{"label":"attempted_disabled_accept","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"typed_sigma":"-Infinity"})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"assertions":["invalid_draft_error","ok_and_preview_disabled","no_gaussian_job","unchanged_document","remembered_unchanged"],"id":"P02a.shared_ui.03.sigma-invalid-rejection.overflow","reason":"unrepresentable number","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"fresh_host_session":true,"input_oracle":"u8-full-s2","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"replace_sigma","text":"1e999"},{"label":"invalid_draft","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"name":"Enter","op":"key_pulse"},{"label":"attempted_disabled_accept","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"typed_sigma":"1e999"})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"assertions":["invalid_draft_error","ok_and_preview_disabled","no_gaussian_job","unchanged_document","remembered_unchanged"],"id":"P02a.shared_ui.03.sigma-invalid-rejection.unsafe-finite-halo","reason":"finite sigma with safely unrepresentable kernel halo","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"fresh_host_session":true,"input_oracle":"u8-full-s2","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"replace_sigma","text":"1e20"},{"label":"invalid_draft","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"name":"Enter","op":"key_pulse"},{"label":"attempted_disabled_accept","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"typed_sigma":"1e20"})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"assertions":["exact_owned_pixels","identity_apply_delta_zero","remembered_sigma_zero","preserved_nonpixel_state","recording_off_workflow_unchanged"],"id":"P02a.shared_ui.04.zero-sigma-history.u8","output_oracle":"u8-full-s0","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"fresh_host_session":true,"input_oracle":"u8-full-s0","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"replace_sigma","text":"0"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"condition":"gaussian_apply_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"applied","op":"observe"},{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"label":"reopened","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.cancel"}]})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"assertions":["exact_owned_pixels","identity_apply_delta_zero","remembered_sigma_zero","preserved_nonpixel_state","recording_off_workflow_unchanged"],"id":"P02a.shared_ui.04.zero-sigma-history.u16","output_oracle":"u16-full-s0","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"fresh_host_session":true,"input_oracle":"u16-full-s0","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"replace_sigma","text":"0"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"condition":"gaussian_apply_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"applied","op":"observe"},{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"label":"reopened","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.cancel"}]})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"assertions":["exact_owned_pixels","identity_apply_delta_zero","remembered_sigma_zero","preserved_nonpixel_state","recording_off_workflow_unchanged"],"id":"P02a.shared_ui.04.zero-sigma-history.f32","output_oracle":"f32-full-s0","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"fresh_host_session":true,"input_oracle":"f32-full-s0","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"replace_sigma","text":"0"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"condition":"gaussian_apply_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"applied","op":"observe"},{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"label":"reopened","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.cancel"}]})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"assertions":["exact_owned_pixels","identity_apply_delta_zero","remembered_sigma_zero","preserved_nonpixel_state","recording_off_workflow_unchanged"],"id":"P02a.shared_ui.04.zero-sigma-history.rgb","output_oracle":"rgb-full-s0","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"fresh_host_session":true,"input_oracle":"rgb-full-s0","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"replace_sigma","text":"0"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"condition":"gaussian_apply_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"applied","op":"observe"},{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"label":"reopened","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.cancel"}]})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"assertions":["scaled_available_true","scaled_true_effective_xy_4_1","exact_owned_pixels","changed_one_history","calibration_unchanged"],"id":"P02a.shared_ui.05.scaled-anisotropy.u8","output_oracle":"u8-scaled-s2-px0p5-py2-run-scaled-harness","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"calibration":{"spacing_xy":[0.5,2],"unit":"um"},"fresh_host_session":true,"input_oracle":"u8-scaled-s2-px0p5-py2-run-scaled-harness","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"click","selector":"gaussian_panel.controls.scaled"},{"label":"scaled_draft","op":"observe"},{"op":"replace_sigma","text":"2"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"condition":"gaussian_apply_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"applied","op":"observe"}]})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"assertions":["scaled_available_true","scaled_true_effective_xy_4_1","exact_owned_pixels","changed_one_history","calibration_unchanged"],"id":"P02a.shared_ui.05.scaled-anisotropy.f32","output_oracle":"f32-scaled-s2-px0p5-py2-run-scaled-harness","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"calibration":{"spacing_xy":[0.5,2],"unit":"um"},"fresh_host_session":true,"input_oracle":"f32-scaled-s2-px0p5-py2-run-scaled-harness","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"click","selector":"gaussian_panel.controls.scaled"},{"label":"scaled_draft","op":"observe"},{"op":"replace_sigma","text":"2"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"condition":"gaussian_apply_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"applied","op":"observe"}]})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"assertions":["initial_scaled_accept_remembered_true","pixel_alias_scaled_absent_false","exact_owned_pixels","second_document_one_history","remembered_scaled_false_on_accept"],"id":"P02a.shared_ui.06.pixel-unit-aliases.px","output_oracle":"u8-full-s2","owned_policy_difference":"Native aliases px/pixel/pixels; upstream source compares only exact pixels","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"calibration":{"spacing_xy":[0.5,2],"unit":"um"},"fresh_host_session":true,"input_oracle":"u8-scaled-s2-px0p5-py2-run-scaled-harness","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"click","selector":"gaussian_panel.controls.scaled"},{"op":"replace_sigma","text":"2"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"condition":"gaussian_apply_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"applied","op":"observe"},{"calibration":{"spacing_xy":[0.5,2],"unit":"px"},"input_oracle":"u8-full-s2","name":"create_second_document","op":"api_preparation"},{"op":"click","selector":"document_tabs.new_document"},{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"label":"pixel_units","op":"observe"},{"op":"replace_sigma","text":"2"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"condition":"gaussian_apply_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"applied","op":"observe"}]})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"assertions":["initial_scaled_accept_remembered_true","pixel_alias_scaled_absent_false","exact_owned_pixels","second_document_one_history","remembered_scaled_false_on_accept"],"id":"P02a.shared_ui.06.pixel-unit-aliases.pixel","output_oracle":"u8-full-s2","owned_policy_difference":"Native aliases px/pixel/pixels; upstream source compares only exact pixels","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"calibration":{"spacing_xy":[0.5,2],"unit":"um"},"fresh_host_session":true,"input_oracle":"u8-scaled-s2-px0p5-py2-run-scaled-harness","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"click","selector":"gaussian_panel.controls.scaled"},{"op":"replace_sigma","text":"2"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"condition":"gaussian_apply_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"applied","op":"observe"},{"calibration":{"spacing_xy":[0.5,2],"unit":"pixel"},"input_oracle":"u8-full-s2","name":"create_second_document","op":"api_preparation"},{"op":"click","selector":"document_tabs.new_document"},{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"label":"pixel_units","op":"observe"},{"op":"replace_sigma","text":"2"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"condition":"gaussian_apply_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"applied","op":"observe"}]})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"assertions":["initial_scaled_accept_remembered_true","pixel_alias_scaled_absent_false","exact_owned_pixels","second_document_one_history","remembered_scaled_false_on_accept"],"id":"P02a.shared_ui.06.pixel-unit-aliases.pixels","output_oracle":"u8-full-s2","owned_policy_difference":"Native aliases px/pixel/pixels; upstream source compares only exact pixels","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"calibration":{"spacing_xy":[0.5,2],"unit":"um"},"fresh_host_session":true,"input_oracle":"u8-scaled-s2-px0p5-py2-run-scaled-harness","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"click","selector":"gaussian_panel.controls.scaled"},{"op":"replace_sigma","text":"2"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"condition":"gaussian_apply_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"applied","op":"observe"},{"calibration":{"spacing_xy":[0.5,2],"unit":"pixels"},"input_oracle":"u8-full-s2","name":"create_second_document","op":"api_preparation"},{"op":"click","selector":"document_tabs.new_document"},{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"label":"pixel_units","op":"observe"},{"op":"replace_sigma","text":"2"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"condition":"gaussian_apply_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"applied","op":"observe"}]})ADOPTED"));
  return result;
}
Json numericOracles() { return Json::parse(R"ORACLES({"f32-full-s0":{"calibration_unit":"pixels","dtype":"F32","id":"f32-full-s0","input_sha256":"7b334761d36148554ea3ab8743b4df82453d1b639d3a2fc8a8d20d1174baffd6","method":"direct","output_sha256":"7b334761d36148554ea3ab8743b4df82453d1b639d3a2fc8a8d20d1174baffd6","owned_expected_output_sha256":"7b334761d36148554ea3ab8743b4df82453d1b639d3a2fc8a8d20d1174baffd6","pixel_spacing_xy":[1,1],"raw_reference_full_output_equals_owned_expected":true,"raw_reference_outside_changed_pixels":0,"roi":{"is_area":false,"kind":"none","mask_pixels":[],"mask_shape":null,"processor_bounds":[0,0,23,17],"requested_bounds":null,"requested_type":null},"scalar_sigma":0,"scaled_units":false,"shape":[17,23],"sigma_xy":[0,0]},"f32-scaled-s2-px0p5-py2-run-scaled-harness":{"calibration_unit":"um","dtype":"F32","id":"f32-scaled-s2-px0p5-py2-run-scaled-harness","input_sha256":"7b334761d36148554ea3ab8743b4df82453d1b639d3a2fc8a8d20d1174baffd6","method":"run-scaled-harness","output_sha256":"2dfde4306041c87bd233e00d60bf0a4b34d3a0fd0780950d434742e0174e1e96","owned_expected_output_sha256":"2dfde4306041c87bd233e00d60bf0a4b34d3a0fd0780950d434742e0174e1e96","pixel_spacing_xy":[0.5,2],"raw_reference_full_output_equals_owned_expected":true,"raw_reference_outside_changed_pixels":0,"roi":{"is_area":false,"kind":"none","mask_pixels":[],"mask_shape":null,"processor_bounds":[0,0,23,17],"requested_bounds":null,"requested_type":null},"scalar_sigma":2,"scaled_units":true,"shape":[17,23],"sigma_xy":[4,1]},"rgb-full-s0":{"calibration_unit":"pixels","dtype":"RGB","id":"rgb-full-s0","input_sha256":"f783a5242a27c90c40dcc59916cb52b1c1a2458dd23f9372cab7a9932137eebb","method":"direct","output_sha256":"f783a5242a27c90c40dcc59916cb52b1c1a2458dd23f9372cab7a9932137eebb","owned_expected_output_sha256":"f783a5242a27c90c40dcc59916cb52b1c1a2458dd23f9372cab7a9932137eebb","pixel_spacing_xy":[1,1],"raw_reference_full_output_equals_owned_expected":true,"raw_reference_outside_changed_pixels":0,"roi":{"is_area":false,"kind":"none","mask_pixels":[],"mask_shape":null,"processor_bounds":[0,0,23,17],"requested_bounds":null,"requested_type":null},"scalar_sigma":0,"scaled_units":false,"shape":[17,23,3],"sigma_xy":[0,0]},"u16-full-s0":{"calibration_unit":"pixels","dtype":"U16","id":"u16-full-s0","input_sha256":"133aeaee8e454bcc4fc6d27be46034f670bf9fe8127e6217a4324722555e5323","method":"direct","output_sha256":"133aeaee8e454bcc4fc6d27be46034f670bf9fe8127e6217a4324722555e5323","owned_expected_output_sha256":"133aeaee8e454bcc4fc6d27be46034f670bf9fe8127e6217a4324722555e5323","pixel_spacing_xy":[1,1],"raw_reference_full_output_equals_owned_expected":true,"raw_reference_outside_changed_pixels":0,"roi":{"is_area":false,"kind":"none","mask_pixels":[],"mask_shape":null,"processor_bounds":[0,0,23,17],"requested_bounds":null,"requested_type":null},"scalar_sigma":0,"scaled_units":false,"shape":[17,23],"sigma_xy":[0,0]},"u8-full-s0":{"calibration_unit":"pixels","dtype":"U8","id":"u8-full-s0","input_sha256":"f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4","method":"direct","output_sha256":"f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4","owned_expected_output_sha256":"f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4","pixel_spacing_xy":[1,1],"raw_reference_full_output_equals_owned_expected":true,"raw_reference_outside_changed_pixels":0,"roi":{"is_area":false,"kind":"none","mask_pixels":[],"mask_shape":null,"processor_bounds":[0,0,23,17],"requested_bounds":null,"requested_type":null},"scalar_sigma":0,"scaled_units":false,"shape":[17,23],"sigma_xy":[0,0]},"u8-full-s0p5":{"calibration_unit":"pixels","dtype":"U8","id":"u8-full-s0p5","input_sha256":"f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4","method":"direct","output_sha256":"c8e467e214f036adec4f1ff169bfa41091acbc17d30e922974b751feec632a53","owned_expected_output_sha256":"c8e467e214f036adec4f1ff169bfa41091acbc17d30e922974b751feec632a53","pixel_spacing_xy":[1,1],"raw_reference_full_output_equals_owned_expected":true,"raw_reference_outside_changed_pixels":0,"roi":{"is_area":false,"kind":"none","mask_pixels":[],"mask_shape":null,"processor_bounds":[0,0,23,17],"requested_bounds":null,"requested_type":null},"scalar_sigma":0.5,"scaled_units":false,"shape":[17,23],"sigma_xy":[0.5,0.5]},"u8-full-s12":{"calibration_unit":"pixels","dtype":"U8","id":"u8-full-s12","input_sha256":"f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4","method":"direct","output_sha256":"0693443fe207a6841cd12caf9a97ea811d3995193904dd0464993041119cfc76","owned_expected_output_sha256":"0693443fe207a6841cd12caf9a97ea811d3995193904dd0464993041119cfc76","pixel_spacing_xy":[1,1],"raw_reference_full_output_equals_owned_expected":true,"raw_reference_outside_changed_pixels":0,"roi":{"is_area":false,"kind":"none","mask_pixels":[],"mask_shape":null,"processor_bounds":[0,0,23,17],"requested_bounds":null,"requested_type":null},"scalar_sigma":12,"scaled_units":false,"shape":[17,23],"sigma_xy":[12,12]},"u8-full-s2":{"calibration_unit":"pixels","dtype":"U8","id":"u8-full-s2","input_sha256":"f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4","method":"direct","output_sha256":"3d21e18911f89681c4f49f0a8d5ddba1664826c562600e1a5600d9491a9c7e2b","owned_expected_output_sha256":"3d21e18911f89681c4f49f0a8d5ddba1664826c562600e1a5600d9491a9c7e2b","pixel_spacing_xy":[1,1],"raw_reference_full_output_equals_owned_expected":true,"raw_reference_outside_changed_pixels":0,"roi":{"is_area":false,"kind":"none","mask_pixels":[],"mask_shape":null,"processor_bounds":[0,0,23,17],"requested_bounds":null,"requested_type":null},"scalar_sigma":2,"scaled_units":false,"shape":[17,23],"sigma_xy":[2,2]},"u8-scaled-s2-px0p5-py2-run-scaled-harness":{"calibration_unit":"um","dtype":"U8","id":"u8-scaled-s2-px0p5-py2-run-scaled-harness","input_sha256":"f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4","method":"run-scaled-harness","output_sha256":"9c5c2ba837d156d0b25f8f73ddb7f7da3291037cd522f74b8d9b56f8f0e253ad","owned_expected_output_sha256":"9c5c2ba837d156d0b25f8f73ddb7f7da3291037cd522f74b8d9b56f8f0e253ad","pixel_spacing_xy":[0.5,2],"raw_reference_full_output_equals_owned_expected":true,"raw_reference_outside_changed_pixels":0,"roi":{"is_area":false,"kind":"none","mask_pixels":[],"mask_shape":null,"processor_bounds":[0,0,23,17],"requested_bounds":null,"requested_type":null},"scalar_sigma":2,"scaled_units":true,"shape":[17,23],"sigma_xy":[4,1]}})ORACLES"); }
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
std::filesystem::path freshDirectory(const std::string &directory) {
  require(!directory.empty(), "Sigma output directory is required");
  const auto path = std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path != path.root_path(), "Sigma output cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path), "Sigma output directory must be fresh");
  return path;
}
std::string address(const void *value) {
  std::ostringstream out; out << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(value); return out.str();
}
// Observation identity only; this never computes an expected numerical result.
std::string sha256(const std::vector<unsigned char> &input) {
  constexpr std::array<std::uint32_t,64> k{
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
  std::array<std::uint32_t,8> hash{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  auto bytes=input; bytes.push_back(0x80); while (bytes.size()%64!=56) bytes.push_back(0);
  const auto bits=std::uint64_t(input.size())*8;
  for (int n=56;n>=0;n-=8) bytes.push_back(static_cast<unsigned char>(bits>>n));
  auto rotate=[](std::uint32_t value,int n) { return (value>>n)|(value<<(32-n)); };
  for (std::size_t offset=0;offset<bytes.size();offset+=64) {
    std::array<std::uint32_t,64> words{};
    for (std::size_t i=0;i<16;++i) for (std::size_t j=0;j<4;++j) words[i]=(words[i]<<8)|bytes[offset+4*i+j];
    for (std::size_t i=16;i<64;++i) {
      const auto a=words[i-15],b=words[i-2];
      words[i]=words[i-16]+(rotate(a,7)^rotate(a,18)^(a>>3))+words[i-7]+(rotate(b,17)^rotate(b,19)^(b>>10));
    }
    auto work=hash;
    for (std::size_t i=0;i<64;++i) {
      const auto first=work[7]+(rotate(work[4],6)^rotate(work[4],11)^rotate(work[4],25))+
          ((work[4]&work[5])^(~work[4]&work[6]))+k[i]+words[i];
      const auto second=(rotate(work[0],2)^rotate(work[0],13)^rotate(work[0],22))+
          ((work[0]&work[1])^(work[0]&work[2])^(work[1]&work[2]));
      for (std::size_t j=7;j>0;--j) work[j]=work[j-1]; work[4]+=first; work[0]=first+second;
    }
    for (std::size_t i=0;i<8;++i) hash[i]+=work[i];
  }
  std::ostringstream out; out<<std::hex<<std::setfill('0'); for (auto value:hash) out<<std::setw(8)<<value; return out.str();
}
std::vector<unsigned char> rawPixels(const std::vector<double> &pixels,const std::string &dtype) {
  static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"Sigma evidence requires IEEE754 Float32");
  const int size=dtype=="float32"?4:dtype=="uint16"?2:1;
  require(dtype=="float32" || dtype=="uint16" || dtype=="uint8","Unsupported Sigma observation dtype");
  std::vector<unsigned char> bytes; bytes.reserve(pixels.size()*static_cast<std::size_t>(size));
  for (double value:pixels) {
    require(std::isfinite(value),"Sigma source/readback contains nonfinite values");
    std::uint32_t bits=0;
    if (dtype=="float32") {
      const float sample=static_cast<float>(value);
      require(static_cast<double>(sample)==value,"Sigma Float32 observation has noncanonical values");
      std::memcpy(&bits,&sample,sizeof(bits));
    } else {
      require(value>=0 && value<=(size==2?65535:255) && std::floor(value)==value,"Sigma integer observation has noncanonical values");
      bits=static_cast<std::uint32_t>(value);
    }
    for (int n=0;n<size;++n) bytes.push_back(static_cast<unsigned char>(bits>>(8*n)));
  }
  return bytes;
}
Json calibrationJson(const SpatialCalibration &value) {
  return {{"spacing",value.spacing},{"origin",value.origin},{"direction",value.direction},{"unit",value.unit},
          {"time_unit",value.time_unit},{"frame_interval",value.frame_interval},{"time_origin",value.time_origin}};
}
Json displayJson(const DisplayState &value) {
  return {{"t",value.t},{"c",value.c},{"crosshair",value.crosshair},{"orthogonal",value.orthogonal},
          {"linked_zoom",value.linkedZoom},{"zoom",value.zoom},{"plane_zoom",value.planeZoom},{"pan",value.pan},
          {"revision",value.revision},{"properties",metadataJson(value.properties)}};
}
Json usageJson(const JobUsage &value) {
  return {{"running",value.running},{"queued",value.queued},{"running_bytes",value.running_bytes},
          {"retained_bytes",value.retained_bytes},{"reserved_bytes",value.reserved_bytes}};
}


std::string jobState(JobState value) {
  switch (value) {
  case JobState::Queued:return "QUEUED"; case JobState::Running:return "RUNNING";
  case JobState::Ready:return "READY"; case JobState::Publishing:return "PUBLISHING";
  case JobState::Completed:return "COMPLETED"; case JobState::Failed:return "FAILED";
  case JobState::Canceled:return "CANCELED";
  }
  throw std::logic_error("Unknown actual scheduler state");
}
Json jobJson(const JobInfo &value) {
  return {{"id",value.id},{"document_id",value.document_id},{"label",value.label},
      {"state",jobState(value.state)},{"error",value.error},{"cancel_requested",value.cancel_requested},
      {"resource_bytes",value.resource_bytes},{"completed",value.completed},{"total",value.total},
      {"checkpoints",value.checkpoints},{"stage",value.stage},
      {"stage_completed",value.stage_completed},{"stage_total",value.stage_total}};
}

Image fixtureImage(const std::string &dtype) {
  const auto type=dtype=="U16"?PixelType::UInt16:dtype=="F32"?PixelType::Float32:PixelType::UInt8;
  Image image(23,17,dtype=="RGB"?3:1,type);
  for (int y=0;y<17;++y) for (int x=0;x<23;++x) {
    const int b=(37*x+61*y+13*x*y+19*(x%3)+23*(y%5))%256;
    if (dtype=="RGB") {
      image.at(x,y,0)=(b+3*x)%256; image.at(x,y,1)=(5*b+11*y+17)%256;
      image.at(x,y,2)=(255-b+7*x+19*y)%256;
    } else if (dtype=="U16") image.at(x,y)=(257*b+97*x+211*y+331*(x%7))%65536;
    else if (dtype=="F32") image.at(x,y)=static_cast<float>((b-127)/8.+(x-2*y)/16.+((x*y)%17)/256.);
    else image.at(x,y)=(b+(x==0?47:0)+(y==16?83:0))%256;
  }
  return image;
}

class SigmaDriver {
public:
  SigmaDriver(Window &window,Application &application,Json variant,std::string context,
              std::filesystem::path directory)
      :window_(window),application_(application),scheduler_(application.job_scheduler()),
       variant_(std::move(variant)),oracles_(numericOracles()),directory_(std::move(directory)) {
    const auto id=variant_.at("id").get<std::string>(); groupId_=id.substr(0,id.rfind('.'));
    invalid_=groupId_=="P02a.shared_ui.03.sigma-invalid-rejection";
    identity_=groupId_=="P02a.shared_ui.04.zero-sigma-history";
    aliases_=groupId_=="P02a.shared_ui.06.pixel-unit-aliases";
    scaled_=aliases_ || groupId_=="P02a.shared_ui.05.scaled-anisotropy";
    report_={{"schema_version",1},{"group_id",groupId_},{"case_id",id},{"context",std::move(context)},
      {"status","unverified"},{"acceptance_claim",false},{"contract_sha256",contractSha},
      {"adopted_variant",variant_},{"reference_capture_sha256",referenceSha},
      {"oracles",Json::object()},{"api_inputs",Json::array()},{"steps",Json::array()},{"frames",Json::array()},
      {"control_observations",Json::array()},{"checkpoints",Json::array()},{"lifecycle",Json::array()},
      {"comparison_scope","Exact capture002 raw integer/F32 bits; no native primitive/scipy oracle"},
      {"observation_scope","Actual native planes and CPU upload-input/cache; no GPU readback"},
      {"full61_verified",false},{"full122_verified",false},{"family_complete",false},{"release_accepted",false},
      {"nonfinite_image_workload_qualified",false}};
    report_["fixture_formula"]="owned asymmetric-analytic-v1: b=(37*x+61*y+13*x*y+19*(x%3)+23*(y%5))%256; UInt8 edge offsets47/83; UInt16=(257*b+97*x+211*y+331*(x%7))%65536; Float32=(b-127)/8+(x-2*y)/16+((x*y)%17)/256; RGB=(b+3*x,5*b+11*y+17,255-b+7*x+19*y)%256";
    createDocument("source",variant_.at("setup").at("input_oracle").get<std::string>(),scaled_?"um":"pixels",scaled_);
    report_["fixture_input_raw"]=rawImage("fixture-input.raw",*originals_.at("source").data().dataset.plane({0,0,0}));
  }
  ~SigmaDriver() { if (!cleaned_) try { cleanupJobs(); } catch (...) {} }
  Json run() {
    HeadlessInputSession session(window_); session_=&session;
    deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    report_["lifecycle"].push_back({{"label","opened"},{"observed",session.lifecycle()}});
    try {
      frame("warmup-1"); frame("warmup-2"); checkpoint("baseline","baseline",0); verifyBaseline("source",checkpoints_.at("baseline"));
      for (const auto &step:variant_.at("steps")) {
        ++stepOrdinal_; const auto before=frames_;
        report_["steps"].push_back({{"ordinal",stepOrdinal_},{"adopted_step",step},
          {"first_frame_ordinal",before},{"last_frame_ordinal",before},{"before_frame_ordinal",before},
          {"checkpoint_name",nullptr},{"completed",false}});
        const auto checkpointName=runStep(step);
        auto &trace=report_["steps"].back(); trace["last_frame_ordinal"]=frames_;
        trace["checkpoint_name"]=checkpointName.empty()?Json():Json(checkpointName); trace["completed"]=true;
      }
      report_["after"]=nativeState("after"); report_["original_after_raw"]=Json::object();
      for (const auto &entry:originals_) report_["original_after_raw"][entry.first]=
          rawImage(entry.first+"-original-after.raw",*entry.second.data().dataset.plane({0,0,0}));
      writeMember("case-unverified.json",report_); verifyFinal();
      cleanupJobs(); frame("after_native_cleanup"); checkpoint("cleanup","cleanup",0);
      report_["lifecycle"].push_back({{"label","before_close"},{"observed",session.lifecycle()}});
      session.close(); session_=nullptr;
      report_["lifecycle"].push_back({{"label","closed"},{"observed",session.lifecycle()}});
      report_["final_cleanup"]={{"usage",usageJson(scheduler_->usage())},{"actual_native_jobs",actualJobs()},
          {"lifecycle",session.lifecycle()},{"observer_retained_result_aliases",0},{"renderer_owns_native_jobs",false}};
      const auto usage=scheduler_->usage();
      require(usage.running==0 && usage.queued==0 && usage.reserved_bytes==0 && usage.retained_bytes==0 &&
          session.lifecycle().at("open")==false && session.lifecycle().at("context_id")==0,
          "Sigma driver leaked actual native reservations or renderer context");
      report_["status"]="passed"; writeMember("case.json",report_); return report_;
    } catch (const std::exception &error) {
      report_["status"]="failed"; report_["failure"]={{"message",error.what()},{"step_ordinal",stepOrdinal_}};
      try { report_["failure_native_state"]=nativeState("failure"); report_["failure_rendered"]=last_;
        report_["failure_actual_native_jobs"]=actualJobs(); writeMember("case-failed-before-cleanup.json",report_); } catch (...) {}
      try { cleanupJobs(); session.close(); session_=nullptr;
        report_["failure_cleanup"]={{"usage",usageJson(scheduler_->usage())},{"lifecycle",session.lifecycle()}};
        writeMember("case-failed.json",report_); } catch (...) { session_=nullptr; }
      throw;
    }
  }
private:
  Window &window_; Application &application_; std::shared_ptr<JobScheduler> scheduler_;
  Json variant_,oracles_,report_,last_; std::filesystem::path directory_; std::string groupId_;
  bool invalid_=false,identity_=false,aliases_=false,scaled_=false,cleaned_=false;
  std::map<std::string,std::shared_ptr<imagejxx::Document>> owners_;
  std::map<std::string,DocumentSnapshot> originals_; std::map<std::string,Json> checkpoints_,cacheRaw_;
  std::map<std::string,int> occurrences_; HeadlessInputSession *session_=nullptr;
  std::size_t frames_=0,rawOrdinal_=0,stepOrdinal_=0; std::chrono::steady_clock::time_point deadline_;
  void writeMember(const std::string &name,const Json &value) const {
    const auto path=directory_/name; require(!std::filesystem::exists(path),"Sigma JSON member already exists"); writeJson(path.u8string(),value);
  }
  Json rawMember(const std::string &name,const std::vector<unsigned char> &bytes,const Json &shape,const std::string &dtype) {
    const auto path=directory_/name; require(!std::filesystem::exists(path),"Sigma raw member already exists");
    writeBytes(path.u8string(),bytes); std::size_t samples=1;
    for (const auto &axis:shape) samples*=axis.get<std::size_t>();
    return {{"member_id",name},{"path",path.u8string()},{"shape",shape},{"dtype",dtype},
      {"encoding",dtype=="float32"?"IEEE754 Float32LE":dtype=="uint16"?"UInt16LE":"UInt8"},
      {"sample_count",samples},{"byte_count",bytes.size()},{"sha256",sha256(bytes)}};
  }
  Json rawImage(const std::string &name,const Image &image) {
    return rawMember(name,rawPixels(image.pixels,typeName(image.type)),image.channels==1?
        Json::array({image.height,image.width}):Json::array({image.height,image.width,image.channels}),typeName(image.type));
  }
  void compactImage(Json &image) {
    if (image.is_null()) return;
    const auto dtype=image.at("dtype").get<std::string>(); const auto shape=image.at("shape");
    const auto bytes=rawPixels(image.at("pixels").get<std::vector<double>>(),dtype);
    const auto key=dtype+shape.dump()+sha256(bytes); auto found=cacheRaw_.find(key);
    if (found==cacheRaw_.end()) found=cacheRaw_.emplace(key,rawMember("cache-"+std::to_string(++rawOrdinal_)+".raw",bytes,shape,dtype)).first;
    image.erase("pixels"); image["raw"]=found->second; image["observation_scope"]="actual CPU upload-input/cache; no GPU readback";
  }
  Json actualJobs() const { Json result=Json::array(); for (const auto &job:scheduler_->jobs()) result.push_back(jobJson(job)); return result; }
  const Json &frame(const std::string &label,const Json &events=Json::array()) {
    require(session_ && frames_<1200 && std::chrono::steady_clock::now()<deadline_,"Sigma watchdog exceeded30 seconds/1200 frames");
    auto value=session_->frame(events);
    if (value.contains("ordinary_pixels") && value.at("ordinary_pixels").contains("readback")) {
      auto &readback=value["ordinary_pixels"]["readback"]; compactImage(readback["source"]); compactImage(readback["rendered_rgb"]);
    }
    if (value.contains("orthogonal_planes")) for (auto &plane:value["orthogonal_planes"])
      if (plane.contains("readback")) { compactImage(plane["readback"]["source"]); compactImage(plane["readback"]["rendered_rgb"]); }
    Json observed{{"ordinal",++frames_},{"step_ordinal",stepOrdinal_},{"label",label},{"requested_events",events},
      {"rendered",value},{"usage",usageJson(scheduler_->usage())},{"actual_native_jobs",actualJobs()}};
    writeMember("frame-"+std::to_string(frames_)+".json",observed);
    report_["frames"].push_back(std::move(observed)); last_=std::move(value);
    require(last_.at("input_frame").at("raw_inputs")==events && last_.at("session").at("open")==true &&
      last_.at("session").at("context_id")==last_.at("session").at("current_context_id"),"Sigma input lost actual order or persistent context");
    return last_;
  }
  template<class Predicate> void await(const char *label,Predicate done) {
    do { frame(label); std::this_thread::yield(); } while (!done());
  }
  void createDocument(const std::string &role,const std::string &inputOracle,const std::string &unit,bool anisotropic) {
    require(owners_.count(role)==0,"Sigma source role must be fresh"); const auto &oracle=oracles_.at(inputOracle);
    const auto dtype=oracle.at("dtype").get<std::string>();
    SpatialCalibration calibration; calibration.spacing=anisotropic?std::array<double,3>{.5,2,1}:std::array<double,3>{1,1,1};
    calibration.origin={0,0,0}; calibration.direction={1,0,0,0,1,0,0,0,1}; calibration.unit=unit;
    calibration.frame_interval=1; calibration.time_origin=0; calibration.time_unit="s";
    DocumentData data{Dataset::from_stack({fixtureImage(dtype)},calibration)};
    data.metadata.title="Owned Gaussian sigma "+role;
    auto owner=std::make_shared<imagejxx::Document>(std::move(data)); window_.addNativeDocument(owner);
    auto display=owner->snapshot().display(); display.crosshair={11,8,0};
    display.properties["display_range"]=metadata_array({dtype=="F32"?-32.:0.,dtype=="U16"?65535.:dtype=="F32"?64.:255.});
    window_.setNativeDisplay(display,owner->id); owner->mark_saved();
    originals_.emplace(role,owner->snapshot()); owners_.emplace(role,std::move(owner));
    report_["oracles"][role]={{"input",oracle},{"output",invalid_?Json():oracles_.at(role=="source" && aliases_?
        inputOracle:variant_.at("output_oracle").get<std::string>())}};
    report_["api_inputs"].push_back({{"step_ordinal",stepOrdinal_},{"purpose","explicit fresh UNBLURRED fixture/calibration preparation"},
      {"role",role},{"methods",{"addNativeDocument","setNativeDisplay","mark_saved"}},{"document_id",owners_.at(role)->id},
      {"input_oracle",inputOracle},{"calibration",calibrationJson(calibration)},{"tested_changes_use_API_setters",false}});
    if (session_) { frame("prepare_"+role+"_1"); frame("prepare_"+role+"_2");
      checkpoint(role+"_baseline",role+"_baseline",0); verifyBaseline(role,checkpoints_.at(role+"_baseline")); }
  }
  Json control(const std::string &selector) const {
    if (selector.compare(0,24,"gaussian_panel.controls.")==0) {
      const auto name=selector.substr(24); auto result=last_.at("gaussian_panel").at("control_states").at(name);
      result["rect"]=last_.at("gaussian_panel").at("control_rects").at(name);
      result["point"]=last_.at("gaussian_panel").at("controls").at(name); return result;
    }
    if (selector=="document_tabs.new_document") {
      for (const auto &item:last_.at("document_tabs")) if (item.at("document_id")==owners_.at("new_document")->id) return item;
      throw std::runtime_error("Alias destination lacks an actual tab");
    }
    return last_.at("host_controls").at(selector);
  }
  void click(const std::string &selector) {
    const auto value=control(selector); const bool disabled=value.at("enabled")==false;
    require(!disabled || (invalid_ && selector=="gaussian_panel.controls.ok"),"Only adopted invalid-sigma OK may be clicked while disabled");
    const auto &point=value.at("point"),&rect=value.at("rect");
    require(value.at("id").get<std::uint64_t>()!=0 && rect.is_array() && rect.size()==4 && point.is_array() && point.size()==2,
        "Sigma control lacks actual item ID/geometry");
    const double x=std::floor(point[0].get<double>()),y=std::floor(point[1].get<double>());
    require(std::isfinite(x) && std::isfinite(y) && x>=rect[0].get<double>() && x<rect[2].get<double>() &&
      y>=rect[1].get<double>() && y<rect[3].get<double>(),"Sigma physical point lies outside actual control");
    report_["control_observations"].push_back({{"selector",selector},{"step_ordinal",stepOrdinal_},{"control",value},
      {"position",{x,y}},{"before_frame_ordinal",frames_},{"selected_live_document_id",application_.active_document_id()},
      {"attempted_disabled",disabled}});
    frame(selector+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    const auto current=control(selector); const auto &currentRect=current.at("rect");
    require(current.at("id")==value.at("id") && current.at("enabled")==value.at("enabled") &&
      x>=currentRect[0].get<double>() && x<currentRect[2].get<double>() && y>=currentRect[1].get<double>() && y<currentRect[3].get<double>(),
      "Sigma control changed before actual left down");
    frame(selector+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(selector+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}})); frame(selector+".settled");
  }
  void keyPulse(const std::string &name,const std::string &label) {
    frame(label+".down",Json::array({{{"type","key"},{"name",name},{"down",true}}}));
    frame(label+".up",Json::array({{{"type","key"},{"name",name},{"down",false}}})); frame(label+".settled");
  }
  void openGaussian() {
    report_["api_inputs"].push_back({{"step_ordinal",stepOrdinal_},{"purpose","adopted labelled_action_preparation opening only"},
      {"action","show_operation"},{"parameters",{{"operation","gaussian_blur_imagej"}}}});
    window_.action("show_operation",{{"operation","gaussian_blur_imagej"}}); frame("gaussian_open_initial_focus");
    require(last_.at("gaussian_panel").at("open")==true && last_.at("gaussian_panel").at("source_id")==application_.active_document_id(),
        "Sigma dialog opening bound the wrong actual native owner");
  }
  void replaceSigma(const std::string &text) {
    click("gaussian_panel.controls.sigma"); const auto id=control("gaussian_panel.controls.sigma").at("id");
    require(last_.at("input_frame").at("focus").at("active_id")==id && last_.at("input_frame").at("input_text").at("id")==id,
        "Physical Sigma typing did not focus the actual InputText item");
    frame("gaussian.sigma.select_all.down",Json::array({{{"type","keys"},{"keys",Json::array({
      {{"name","Ctrl"},{"down",true}},{{"name","A"},{"down",true}}})}}}));
    frame("gaussian.sigma.select_all.up",Json::array({{{"type","keys"},{"keys",Json::array({
      {{"name","Ctrl"},{"down",false}},{{"name","A"},{"down",false}}})}}}));
    if (text.empty()) keyPulse("Backspace","gaussian.sigma.delete_selection");
    else frame("gaussian.sigma.text",Json::array({{{"type","text"},{"value",text}}}));
    frame("gaussian.sigma.text_settled");
    require(last_.at("gaussian_panel").at("sigma_text")==text,"Actual Sigma text differs from the physically injected token");
  }
  void actualHistory(const std::string &name) {
    click("menu.Edit"); frame("Edit_popup_visible"); click("menu.Edit."+name); frame("history_"+name+"_settled");
  }
  Json documentState(const std::string &label,const std::shared_ptr<imagejxx::Document> &owner) {
    const auto source=owner->snapshot(); const auto &data=source.data(); const auto shape=data.dataset.shape();
    const auto revisions=source.revisions(); const auto history=owner->history();
    Json planes=Json::array(),rois=Json::array(),overlays=Json::array();
    for (int t=0;t<shape.t;++t) for (int c=0;c<shape.c;++c) for (int z=0;z<shape.z;++z) {
      const auto plane=data.dataset.plane({t,c,z});
      const auto member=label+"-plane-"+std::to_string(t)+"-"+std::to_string(c)+"-"+std::to_string(z)+".raw";
      planes.push_back({{"position",{t,c,z}},{"raw",rawImage(member,*plane)},
                        {"image_address",address(plane.get())},{"buffer_address",address(plane->pixels.data())}});
    }
    for (const auto &roi:data.rois) rois.push_back({{"name",roi.name},{"selection",selectionJson(roi.selection)},
        {"attributes",metadataJson(roi.attributes)},{"t",roi.t?Json(*roi.t):Json()},
        {"c",roi.c?Json(*roi.c):Json()},{"z",roi.z?Json(*roi.z):Json()}});
    for (const auto &overlay:data.overlays) overlays.push_back(metadataJson(overlay));
    return {{"document_id",source.id()},{"data_identity",source.data_identity()?Json(*source.data_identity()):Json()},
      {"shape_tcz_yxs",{shape.t,shape.c,shape.z,shape.y,shape.x,shape.s}},{"dtype",typeName(data.dataset.pixel_type())},
      {"dirty",source.dirty()},{"undo_steps",owner->undo_steps()},{"redo_steps",owner->redo_steps()},
      {"history",{{"undo_steps",history.undo_steps},{"redo_steps",history.redo_steps},
          {"step_limit",history.step_limit},{"pixel_budget",history.pixel_budget},
          {"pixels",{{"logical_bytes",history.pixels.logical_bytes},{"current_bytes",history.pixels.current_bytes},
          {"retained_bytes",history.pixels.retained_bytes},{"obsolete_bytes",history.pixels.obsolete_bytes},
          {"retained_planes",history.pixels.retained_planes},{"exact",history.pixels.exact}}}}},
      {"revisions",{{"pixels",revisions.pixels},{"metadata",revisions.metadata},{"selection",revisions.selection},{"display",revisions.display}}},
      {"calibration",calibrationJson(data.dataset.calibration())},{"planes",planes},
      {"selection",data.selection?selectionJson(*data.selection):Json()},
      {"previous_selection",data.previous_selection?selectionJson(*data.previous_selection):Json()},
      {"rois",rois},{"overlays",overlays},{"spectrum_present",data.spectrum.has_value()},
      {"metadata",{{"title",data.metadata.title},{"properties",metadataJson(data.metadata.properties)},
          {"extensions",metadataJson(data.metadata.extensions)},{"record_layout",metadataJson(data.metadata.record_layout)}}},
      {"display",displayJson(source.display())},{"document_info",window_.documentInfo(owner->id)}};
  }

  Json nativeState(const std::string &label) {
    Json documents=Json::object(),desktopIds=Json::array();
    for (const auto &entry:owners_) documents[entry.first]=documentState(label+"-"+entry.first,entry.second);
    for (const auto &document:window_.documents()) desktopIds.push_back(document->id);
    return {{"active_document_id",application_.active_document_id()},
      {"application_document_ids",application_.document_ids()},{"desktop_document_ids",desktopIds},
      {"workflow",window_.workflow},{"results",window_.results},{"recording",window_.recording},{"documents",documents}};
  }
  Json checkpoint(const std::string &name,const std::string &label,int occurrence) {
    require(checkpoints_.count(name)==0,"Sigma checkpoint must not overwrite an earlier observation");
    Json value{{"name",name},{"label",label},{"occurrence",occurrence},{"step_ordinal",stepOrdinal_},
      {"frame_ordinal",frames_},{"rendered",last_},{"native_state",nativeState(name)},
      {"usage",usageJson(scheduler_->usage())},{"actual_native_jobs",actualJobs()}};
    writeMember("checkpoint-"+name+".json",value); report_["checkpoints"].push_back(value); checkpoints_.emplace(name,value); return value;
  }

  const Json &doc(const Json &value,const std::string &role) const { return value.at("native_state").at("documents").at(role); }
  const Json &baseline(const std::string &role) const { return checkpoints_.at(role=="source"?"baseline":"new_document_baseline"); }
  void checkRaw(const Json &value,const std::string &expected) const {
    require(value.at("planes").size()==1 && value.at("planes").at(0).at("position")==Json::array({0,0,0}) &&
      value.at("planes").at(0).at("raw").at("sha256")==expected,"Actual sigma output/source raw bits differ from pinned capture002");
  }
  void verifyPreserved(const Json &value,const std::string &role) const {
    const auto &state=doc(value,role),&before=doc(baseline(role),role);
    for (const auto *field:{"document_id","shape_tcz_yxs","dtype","calibration","selection","previous_selection",
        "rois","overlays","spectrum_present","metadata"})
      require(state.at(field)==before.at(field),"Gaussian sigma changed preserved native nonpixel state");
    for (const auto *field:{"t","c","crosshair","orthogonal","properties"})
      require(state.at("display").at(field)==before.at("display").at(field),"Gaussian sigma changed scientific display/navigation policy");
    require(value.at("native_state").at("workflow")==Json::array() && value.at("native_state").at("results")==Json::array() &&
      value.at("native_state").at("recording")==false,"Gaussian sigma produced workflow/results with recording off");
  }
  void unchangedDocument(const Json &actual,const Json &before) const {
    for (const auto *field:{"document_id","data_identity","shape_tcz_yxs","dtype","dirty","undo_steps","redo_steps",
        "history","revisions","calibration","selection","previous_selection","rois","overlays","spectrum_present","metadata","display","document_info"})
      require(actual.at(field)==before.at(field),"Invalid or identity sigma changed actual native document state");
    require(actual.at("planes").size()==before.at("planes").size(),"Sigma changed original source plane count");
    for (std::size_t i=0;i<actual.at("planes").size();++i) {
      const auto &a=actual.at("planes").at(i),&b=before.at("planes").at(i);
      for (const auto *field:{"position","image_address","buffer_address"})
        require(a.at(field)==b.at(field),"Invalid/identity sigma replaced original source storage");
      for (const auto *field:{"shape","dtype","sha256","byte_count"})
        require(a.at("raw").at(field)==b.at("raw").at(field),"Invalid/identity sigma changed original scientific bytes");
    }
  }
  void verifyBaseline(const std::string &role,const Json &value) const {
    const auto &state=doc(value,role); checkRaw(state,report_.at("oracles").at(role).at("input").at("input_sha256").get<std::string>());
    require(state.at("undo_steps")==0 && state.at("redo_steps")==0 && state.at("dirty")==false &&
        state.at("selection").is_null() && state.at("rois")==Json::array() && state.at("overlays")==Json::array(),
        "Sigma fixture did not begin as a fresh saved full-plane input/history0");
  }
  void verifyInvalid(const Json &value) const {
    const auto &panel=value.at("rendered").at("gaussian_panel");
    require(panel.at("open")==true && panel.at("draft_valid")==false && !panel.at("error").get<std::string>().empty() &&
      panel.at("control_states").at("ok").at("enabled")==false &&
      panel.at("control_states").at("preview").at("enabled")==false,
      "Invalid sigma did not show its actual error and disable OK/Preview");
    unchangedDocument(doc(value,"source"),doc(checkpoints_.at("baseline"),"source")); verifyPreserved(value,"source");
    require(value.at("actual_native_jobs")==checkpoints_.at("baseline").at("actual_native_jobs") && !window_.busy() &&
      value.at("usage").at("reserved_bytes")==0 &&
      panel.at("remembered")==checkpoints_.at("baseline").at("rendered").at("gaussian_panel").at("remembered"),
      "Invalid sigma admitted work or changed interactive remembered settings");
  }
  void verifyApplied(const Json &value,const std::string &role) const {
    const auto &state=doc(value,role),&oracle=report_.at("oracles").at(role).at("output");
    checkRaw(state,oracle.at("owned_expected_output_sha256").get<std::string>()); verifyPreserved(value,role);
    require(value.at("rendered").at("ordinary_pixels").at("pixels_source")=="live" &&
        value.at("rendered").at("ordinary_pixels").at("readback").at("source").at("raw").at("sha256")==
          oracle.at("owned_expected_output_sha256"),"Gaussian sigma actual selected live renderer cache differs from pinned result");
    const auto &apply=value.at("rendered").at("gaussian_jobs").at("apply");
    const auto &record=apply.at("command_record");
    const bool effectiveScaled=role=="source" && scaled_;
    require(apply.at("exit")=="published" && apply.at("published_document_id")==owners_.at(role)->id &&
      record.at("schema_version")==1 && record.at("operation")=="gaussian_blur_imagej" &&
      record.at("options").at("sigma")==oracle.at("scalar_sigma") && record.at("options").at("scaled_units")==effectiveScaled &&
      record.at("effective_sigma_xy")==oracle.at("sigma_xy") && record.at("calibration")==state.at("calibration") &&
      record.at("target").at("document_id")==owners_.at(role)->id,
      "Actual sigma Apply lost raw/effective/calibration/native source lineage");
    require(apply.at("job").at("state")=="completed","Sigma terminal observation lacks actual completed native job");
    if (identity_) unchangedDocument(state,doc(checkpoints_.at("baseline"),"source"));
    else require(state.at("undo_steps")==1 && state.at("redo_steps")==0 && state.at("dirty")==true,
        "Changed sigma Apply failed to publish exactly one native history step");
    if (role=="new_document") unchangedDocument(doc(value,"source"),doc(checkpoints_.at("source_applied"),"source"));
    const auto &remembered=value.at("rendered").at("gaussian_panel").at("remembered");
    require(remembered.at("sigma")==record.at("options").at("sigma") && remembered.at("scaled_units")==effectiveScaled,
        "Actual Gaussian acceptance did not remember its interactive options");
  }
  void verifyObservation(const std::string &name,const Json &value) const {
    const auto &panel=value.at("rendered").at("gaussian_panel");
    if (name=="invalid_draft" || name=="attempted_disabled_accept") verifyInvalid(value);
    else if (name=="valid_draft") {
      require(panel.at("draft_valid")==true && panel.at("error")=="" && panel.at("sigma_text")==variant_.at("typed_sigma") &&
        panel.at("sigma")==report_.at("oracles").at("source").at("output").at("scalar_sigma") &&
        panel.at("control_states").at("ok").at("enabled")==true,"Physically typed valid sigma did not yield actual valid draft");
      unchangedDocument(doc(value,"source"),doc(checkpoints_.at("baseline"),"source"));
    } else if (name=="scaled_draft") {
      require(panel.at("scaled_available")==true && panel.at("scaled_units")==true && panel.at("draft_valid")==true &&
        panel.at("control_states").contains("scaled"),"Physical Scaled Units gesture failed on calibrated anisotropic source");
      unchangedDocument(doc(value,"source"),doc(checkpoints_.at("baseline"),"source"));
    } else if (name=="source_applied") verifyApplied(value,"source");
    else if (name=="new_document_applied") verifyApplied(value,"new_document");
    else if (name=="undone" || name=="redone") {
      const auto &state=doc(value,"source"); checkRaw(state,report_.at("oracles").at("source").at(name=="undone"?"input":"output").
          at(name=="undone"?"input_sha256":"owned_expected_output_sha256").get<std::string>());
      verifyPreserved(value,"source");
      require(state.at("undo_steps")==(name=="undone"?0:1) && state.at("redo_steps")==(name=="undone"?1:0),
          "Physical Undo/Redo failed exactly one native history transition");
    } else if (name=="reopened") {
      require(identity_ && panel.at("open")==true && panel.at("sigma")==0 && panel.at("sigma_text")=="0" &&
          panel.at("remembered").at("sigma")==0,"Identity Apply did not retain/reopen remembered sigma0");
      unchangedDocument(doc(value,"source"),doc(checkpoints_.at("baseline"),"source"));
    } else if (name=="pixel_units") {
      require(panel.at("open")==true && panel.at("source_id")==owners_.at("new_document")->id &&
        panel.at("scaled_available")==false && panel.at("scaled_units")==false &&
        !panel.at("controls").contains("scaled") && !panel.at("control_states").contains("scaled") &&
        panel.at("remembered").at("scaled_units")==true,
        "Pixel alias did not hide Scaled Units and normalize draftfalse while preserving prior rememberedtrue");
      unchangedDocument(doc(value,"new_document"),doc(checkpoints_.at("new_document_baseline"),"new_document"));
      unchangedDocument(doc(value,"source"),doc(checkpoints_.at("source_applied"),"source"));
    } else throw std::logic_error("Unknown adopted sigma observation");
  }
  std::string runStep(const Json &step) {
    const auto op=step.at("op").get<std::string>();
    if (op=="open_command") openGaussian();
    else if (op=="replace_sigma") replaceSigma(step.at("text").get<std::string>());
    else if (op=="click") click(step.at("selector").get<std::string>());
    else if (op=="key_pulse") keyPulse(step.at("name").get<std::string>(),"gaussian.invalid.accept_Enter");
    else if (op=="actual_undo") actualHistory("Undo");
    else if (op=="actual_redo") actualHistory("Redo");
    else if (op=="api_preparation") {
      require(aliases_ && step.at("name")=="create_second_document","Unknown adopted sigma fixture preparation");
      createDocument("new_document",step.at("input_oracle").get<std::string>(),step.at("calibration").at("unit").get<std::string>(),true);
    } else if (op=="await_native") {
      require(step.at("condition")=="gaussian_apply_terminal","Unknown adopted sigma native wait");
      await("gaussian_apply_terminal",[&] {
        const auto &apply=last_.at("gaussian_jobs").at("apply");
        require(apply.at("exit")!="publication_failed" && apply.at("exit")!="job_rejected" &&
            apply.at("exit")!="source_invalid" && apply.at("exit")!="callback_failed_after_publication", "Actual sigma Apply failed its native operation/publication");
        return !window_.busy() && !last_.at("gaussian_jobs").at("apply_busy").get<bool>() && apply.at("exit")=="published";
      });
    } else if (op=="observe") {
      const auto label=step.at("label").get<std::string>(); const int occurrence=++occurrences_[label];
      const auto name=label=="applied"?(occurrence==1?"source_applied":"new_document_applied"):label;
      const auto value=checkpoint(name,label,occurrence); verifyObservation(name,value); return name;
    } else throw std::logic_error("Unknown literal adopted sigma step");
    return {};
  }
  void verifyFinal() const {
    require(report_.at("steps").size()==variant_.at("steps").size(),"Sigma driver omitted an original adopted step");
    for (const auto &step:report_.at("steps")) require(step.at("completed")==true,"Sigma driver did not finish every original step");
    for (const auto &entry:report_.at("original_after_raw").items())
      require(entry.value().at("sha256")==report_.at("oracles").at(entry.key()).at("input").at("input_sha256"),
          "Sigma Apply/Undo/Redo mutated retained original scientific buffers");
    require(window_.workflow==Json::array() && window_.results==Json::array() && !window_.recording,
        "Sigma driver changed workflow/results with recordingoff");
    if (invalid_) {
      unchangedDocument(report_.at("after").at("documents").at("source"),doc(checkpoints_.at("baseline"),"source"));
      require(actualJobs()==checkpoints_.at("baseline").at("actual_native_jobs"),"Disabled sigma acceptance created a native job");
      require(last_.at("gaussian_panel").at("remembered")==checkpoints_.at("baseline").at("rendered").at("gaussian_panel").at("remembered") &&
          last_.at("gaussian_panel").at("open")==false && last_.at("gaussian_panel").at("exit_reason")=="cancel",
          "Physical invalid-sigma Cancel changed remembered options or failed to close the actual panel");
    }
  }
  void cleanupJobs() {
    report_["api_inputs"].push_back({{"step_ordinal",stepOrdinal_},{"purpose","bounded final/failure native job cleanup only"},
      {"methods",{"Window.cancel","action(wait_jobs,timeout_ms=5000)","Window.poll"}}});
    window_.cancel(); window_.action("wait_jobs",{{"timeout_ms",5000}}); window_.poll(); cleaned_=true;
  }
};
} // namespace

Json Window::gaussianSigmaCase(const std::string &variant,const std::string &context,const std::string &directory) {
  checkThread();
  if (context!="imgui_cpp" && context!="imgui_python") throw std::invalid_argument("Sigma context must be imgui_cpp or imgui_python");
  const auto variants=adoptedVariants();
  const auto found=std::find_if(variants.begin(),variants.end(),[&](const Json &value) { return value.at("id")==variant; });
  if (found==variants.end()) throw std::invalid_argument("Unknown full adopted Gaussian sigma variant ID");
  require(documents_.empty() && workflow.empty() && results.empty() && ImGui::GetCurrentContext()==nullptr,
      "Sigma case requires a fresh creator-owned Window without an ImGui context");
  SigmaDriver driver(*this,*controller_,*found,context,freshDirectory(directory)); return driver.run();
}
int Window::gaussianSigmaConsumerTest(const std::string &directory) {
  checkThread(); require(documents_.empty() && ImGui::GetCurrentContext()==nullptr,"Sigma standalone requires a fresh Window");
  const auto root=freshDirectory(directory);
  Json report{{"schema_version",1},{"context","imgui_cpp"},{"status","unverified"},{"acceptance_claim",false},
      {"contract_sha256",contractSha},{"cases",Json::array()},{"groups",Json::array({
      "P02a.shared_ui.02.sigma-valid-typing","P02a.shared_ui.03.sigma-invalid-rejection",
      "P02a.shared_ui.04.zero-sigma-history","P02a.shared_ui.05.scaled-anisotropy","P02a.shared_ui.06.pixel-unit-aliases"})},
      {"full61_verified",false},{"full122_verified",false},{"family_complete",false},{"release_accepted",false},
      {"nonfinite_image_workload_qualified",false}};
  try {
    std::size_t caseOrdinal=0;
    for (const auto &variant:adoptedVariants()) {
      const auto id=variant.at("id").get<std::string>();
      std::ostringstream caseName; caseName<<"case"<<std::setfill('0')<<std::setw(2)<<++caseOrdinal;
      const auto directory=root/caseName.str();
      Window owner((directory/"preferences.json").u8string());
      report["cases"].push_back(owner.gaussianSigmaCase(id,"imgui_cpp",directory.u8string()));
      std::cout<<"PASS "<<id<<" imgui_cpp\n";
    }
    report["status"]="passed"; writeJson((root/"imgui-cpp.json").u8string(),report); return 0;
  } catch (const std::exception &error) {
    report["status"]="failed"; report["failure"]={{"message",error.what()}};
    writeJson((root/"imgui-cpp-failed.json").u8string(),report); throw;
  }
}
} // namespace imagejxx::desktop
