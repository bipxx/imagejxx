# CFG1: Settings consumer audit and wand-preference delivery packet

Reviewed 2026-10-24. **Source/document audit only: no build, test execution or family closure is claimed.** This subsequent packet is proposed while C closes P13i. It does not authorize edits to held product inputs; C must release named shared-file seams before implementation.

The current native schema contains **61 keys in 13 groups**, not the historical 37-definition fixture. Every current key has a schema entry, a generic/specialized Preferences widget and a consumer in inspected source. This audit does not establish complete actual-input/persistence/consumer qualification for every key. Do not add placeholder settings without consumers.

The smallest complete next packet is **Wand tolerance/connectivity: real Preferences → real canvas selection → persisted fresh-host consumption**. Validation, persistence and the algorithm already exist. The missing deliverable is independent four-context qualification and the narrow observation support required to drive those existing widgets. No new dependency, kernel, settings version or global-default behavior is needed.

## Authority and evidence language

- Parent families G001/G002/G006, with G013 preservation, remain in progress. Full wand modes, scientific/planar tool support, platform accessibility and upstream ImageJ option parity remain separate requirements.
- Exactly A/B/C: A native contract/tests; B shared ImGui controls/probe; C independent readers, builds, receipts and cleanup. No nested agents, worker builds, new environments or cache generations.
- `I`=implemented in inspected source; `AP`=an API/component test exists; `UI`=an actual-input producer/reader source exists. None means executed against current products. Retired `build/` locators are unavailable evidence unless C verifies retained original members; never recreate or repin them.
- [Native schema](../../src/native/settings.cpp), [public Settings](../../include/imagejxx/settings.hpp), [PreferencesDocument](../../include/imagejxx/preferences.hpp), [panel](../../src/desktop/settings_panel.cpp), [Window](../../src/desktop/window.cpp), [Python persistence facade](../../src/imagejxx/ui/persistence.py).
- Matrix abbreviations: **S**=`src/native/settings.cpp`, **W**=`src/desktop/window.cpp`. Line numbers identify reviewed source; hashes below bind the audit inputs.

## Shared validation, widgets and persistence

Ranges are inclusive. `DBL_MAX` is `std::numeric_limits<double>::max()`. Number requires finite values; integral values may normalize to double. Integer rejects booleans/floats. Choice requires an exact registered string. Color accepts only `#RRGGBB`/`#RRGGBBAA`, normalized lowercase. Direct Settings mutation rejects unknown keys; preference JSON preserves unknown compartments separately.

Widgets use the schema label under `PushID(setting key)`: Checkbox, InputInt, InputDouble, Combo/Selectable, color text+ColorEdit4, Text InputText, measurements checklist or ShortcutEditor. They edit a draft. Apply validates the **whole draft**, including hidden fields. Defaults stages the whole selected group (or all groups), independent of Search. Revert copies applied settings. Close/titlebar close discard drafts. Search matches key/group/label/description. Preserve these semantics.

**Observation gap:** generic Number/Color fields lack per-key control observations. Integer has draft/point, with full item identity only for processing/history memory fields; Choice observes only `conversion_unsigned16_range` and `startup_mode`; Boolean retains only its legacy first/auto-DPI toggle. Text and shortcut controls have dedicated observations. This is not evidence that the other widgets are absent.

**P1 — explicit shared persistence:** all 61 keys serialize into `format=imagejxx-preferences`, `version=1`, `values`. Native PreferencesDocument owns the codec/file policy; desktop Settings IO and Python Preferences delegate to it. Known keys override matching unknown values; unknown values/root fields/extras survive. Recovery/write blocking, exclusive sibling temporary publication and non-overwriting `.bak`/`.bak.N` backups already exist. Settings validates a proposed map before publication. Window prepares scheduler/history/frontend consumers before explicit save, then publishes without partial owner state. Settings snapshots are detached: native Settings mutation alone does not mutate an image or desktop.

**P-H:** P1 plus successful histogram publication remembers validated defaults; source-specific target ranges are not global defaults. Draft/cancel/failure does not save accepted options; protected files are not overwritten implicitly.

**P-T:** P1 plus successful threshold Auto remembers seven options. Toggling alone does not save. Preserve the display/settings transaction and protected-file policy.

**P-L:** P1 plus Window::settings reads live FPS, colors, brush width, show-ROIs and history limit. Do not infer that every live control automatically persists; qualify its publication action. Gaussian sigma and Rank radii are **session acceptance memory**, not schema keys or persisted preferences.

## Exact key → widget → consumer → persistence → evidence/gap matrix

All rows are `I`; defaults/domains/groups/labels below come from current definitions. Evidence codes identify reusable sources and gaps, not passes.


### Appearance

| Key / widget label | Default; accepted domain | Widget | Native/ImGui consumer | Persistence | Evidence/gap |
| --- | --- | --- | --- | --- | --- |
| `theme` / Theme (S:105) | `"dark"`; dark, light, classic | Combo | W setupGui/appearance:12642,12688 → actual ImGui theme | P1 | E-A: selected actual controls; remaining field-to-effect matrix unqualified |
| `ui_scale` / UI and font scale (S:114) | `1.`; [.5, 3] | InputDouble | W appearance:12688 → guiScale/fonts/style; multiply by selected DPI | P1 | E-A: selected actual controls; remaining field-to-effect matrix unqualified |
| `auto_dpi` / Follow display DPI (S:117) | `true`; bool | Checkbox | W appearance:12688 → select supplied DPI or 1 | P1 | E-A: selected actual controls; remaining field-to-effect matrix unqualified |
| `canvas_background` / Canvas background (S:120) | `"#181b21"`; #RRGGBB or #RRGGBBAA | text+ColorEdit4 | W ordinary/linked canvas:10474,10675 → background color | P1 | E-A: selected actual controls; remaining field-to-effect matrix unqualified |
| `default_zoom` / Initial image zoom (S:123) | `0.`; [0, 64] | InputDouble | W new/adopted owner:1432,3485,5478 → zero fits; positive initial X scale | P1 | E-A: selected actual controls; remaining field-to-effect matrix unqualified |
| `interpolation` / Image interpolation (S:127) | `"nearest"`; nearest, linear | Combo | W ordinary texture:4560 and linked adapter:10484 → display sampling only | P1 | E-A: selected actual controls; remaining field-to-effect matrix unqualified |

### Drawing

| Key / widget label | Default; accepted domain | Widget | Native/ImGui consumer | Persistence | Evidence/gap |
| --- | --- | --- | --- | --- | --- |
| `foreground` / Paint color (S:136) | `"#ffffff"`; #RRGGBB or #RRGGBBAA | text+ColorEdit4 | W applyPreferences:1375 → paintColor:1448 → fill/brush/text/annotation defaults | P-L | E-D: API/tool inputs exist; complete Preferences-to-tool chain unqualified |
| `background` / Erase color (S:139) | `"#000000"`; #RRGGBB or #RRGGBBAA | text+ColorEdit4 | W applyPreferences:1376 → paintColor(false):1448 → clear/erase | P-L | E-D: API/tool inputs exist; complete Preferences-to-tool chain unqualified |
| `brush_width` / Brush width (S:142) | `8.`; [1, 256] | InputDouble | W applyPreferences:1373 → live brush/eraser gesture diameter | P-L | E-D: API/tool inputs exist; complete Preferences-to-tool chain unqualified |
| `rounded_corner_diameter` / Rounded rectangle corner diameter (S:145) | `8`; [0, 2147483647.] | InputInt | W creation:2831, toolbar:9306 → rounded candidate geometry | P1 | E-D: API/tool inputs exist; complete Preferences-to-tool chain unqualified |
| `line_width` / Line selection width (S:148) | `1.`; [1, 256] | InputDouble | W creation:2830, line/polyline finish:4508, style:10756 | P1 | E-D: API/tool inputs exist; complete Preferences-to-tool chain unqualified |
| `point_size` / Point size (S:151) | `8.`; [1, 256] | InputDouble | W multipoint finish:4507, actual point gesture:11141 | P1 | E-D: API/tool inputs exist; complete Preferences-to-tool chain unqualified |
| `text_size` / Text size (S:154) | `16`; [1, 256] | InputInt | W text defaults:7961,9040, actual canvas dialog:11127 | P1 | E-D: API/tool inputs exist; complete Preferences-to-tool chain unqualified |

### Selection

| Key / widget label | Default; accepted domain | Widget | Native/ImGui consumer | Persistence | Evidence/gap |
| --- | --- | --- | --- | --- | --- |
| `show_rois` / Show ROIs (S:157) | `true`; bool | Checkbox | W applyPreferences:1374 → live showRois → ordinary/linked ROI render | P-L | E-S: API/command tests; wand actual Preferences chain missing |
| `selection_enlargement_distance` / Enlargement default (pixels) (S:159) | `15.`; [0, 1000000] | InputDouble | W enlargement open:6425 → captured native request; command edits do not replace preference | P1 | E-S: API/command tests; wand actual Preferences chain missing |
| `selection_enlargement_units` / Enlargement default units (S:162) | `"calibrated"`; pixels, calibrated | Combo | W enlargement open:6424 → pixels/calibrated request using existing X-spacing rule | P1 | E-S: API/command tests; wand actual Preferences chain missing |
| `wand_tolerance` / Wand tolerance (S:165) | `0.`; [0, DBL_MAX] | InputDouble | W action(wand):7663 → gray(frame) → native segmentation.cpp:11 wand_mask; canvas:11131 supplies no override | P1 | E-S AP; real Preferences+Wand chain missing; CFG1 selected |
| `wand_connectivity` / Wand connectivity (S:168) | `4`; {4,8};5/6/7 rejected | InputInt | W action(wand):7663 → native 4/8 traversal; same actual canvas route | P1 | E-S AP; real Preferences+Wand chain missing; CFG1 selected |

### Playback

| Key / widget label | Default; accepted domain | Widget | Native/ImGui consumer | Persistence | Evidence/gap |
| --- | --- | --- | --- | --- | --- |
| `animation_fps` / Frames per second (S:171) | `8.`; [.1, 120] | InputDouble | W applyPreferences:1372 → live playback fps | P-L | E-V: playback inputs exist; persisted FPS-to-timing chain unqualified |

### Conversion

| Key / widget label | Default; accepted domain | Widget | Native/ImGui consumer | Persistence | Evidence/gap |
| --- | --- | --- | --- | --- | --- |
| `conversion_scale` / Scale when converting (S:173) | `true`; bool | Checkbox | S conversion_options:417 → W captured operation defaults:4157/4240 → native ConversionOptions | P1 | E-C: API/reference + selected actual preferences controls |
| `conversion_weighted_rgb` / Weighted RGB conversion (S:177) | `false`; bool | Checkbox | S conversion_options:417 → W captured operation defaults:4157/4240 → native ConversionOptions | P1 | E-C: API/reference + selected actual preferences controls |
| `conversion_red_weight` / Red weight (S:180) | `.299`; [-DBL_MAX, DBL_MAX] | InputDouble | S conversion_options:417 → W captured operation defaults:4157/4240 → native ConversionOptions | P1 | E-C: API/reference + selected actual preferences controls |
| `conversion_green_weight` / Green weight (S:184) | `.587`; [-DBL_MAX, DBL_MAX] | InputDouble | S conversion_options:417 → W captured operation defaults:4157/4240 → native ConversionOptions | P1 | E-C: API/reference + selected actual preferences controls |
| `conversion_blue_weight` / Blue weight (S:188) | `.114`; [-DBL_MAX, DBL_MAX] | InputDouble | S conversion_options:417 → W captured operation defaults:4157/4240 → native ConversionOptions | P1 | E-C: API/reference + selected actual preferences controls |
| `conversion_range_policy` / Conversion source range (S:192) | `"display"`; display, data, dtype | Combo | S conversion_options:417 → W captured operation defaults:4157/4240 → native ConversionOptions | P1 | E-C: API/reference + selected actual preferences controls |
| `conversion_full_range_inversions` / Full-range 16-bit inversion (S:203) | `false`; bool | Checkbox | S conversion_options:417 → W captured operation defaults:4157/4240 → native ConversionOptions | P1 | E-C: API/reference + selected actual preferences controls |
| `conversion_unsigned16_range` / Unsigned 16-bit range (bits) (S:208) | `"auto"`; auto, 8, 10, 12, 14, 15, 16 | Combo | S conversion_options:417 → W captured operation defaults:4157/4240 → native ConversionOptions | P1 | E-C: API/reference + selected actual preferences controls |

### Histogram

| Key / widget label | Default; accepted domain | Widget | Native/ImGui consumer | Persistence | Evidence/gap |
| --- | --- | --- | --- | --- | --- |
| `histogram_custom` / Custom bins and range (S:218) | `false`; bool | Checkbox | HistogramPanel::show:35/defaults:204 → W rememberHistogram:3272 after successful result | P-H | E-H: accepted-default/reopen and histogram inputs exist |
| `histogram_bins` / Custom bin count (S:221) | `256`; [1, 65536] | InputInt | HistogramPanel::show:35/defaults:204 → W rememberHistogram:3272 after successful result | P-H | E-H: accepted-default/reopen and histogram inputs exist |
| `histogram_lower` / Custom lower bound (S:224) | `0.`; [-DBL_MAX, DBL_MAX]; coupled lower < upper | InputDouble | HistogramPanel::show:35/defaults:204 → W rememberHistogram:3272 after successful result | P-H | E-H: accepted-default/reopen and histogram inputs exist |
| `histogram_upper` / Custom upper bound (S:227) | `1.`; [-DBL_MAX, DBL_MAX]; coupled lower < upper | InputDouble | HistogramPanel::show:35/defaults:204 → W rememberHistogram:3272 after successful result | P-H | E-H: accepted-default/reopen and histogram inputs exist |
| `histogram_log` / Log count display (S:230) | `false`; bool | Checkbox | HistogramPanel::show:35/defaults:204 → W rememberHistogram:3272 after successful result | P-H | E-H: accepted-default/reopen and histogram inputs exist |
| `histogram_use_roi` / Single-plane active area ROI (S:233) | `true`; bool | Checkbox | HistogramPanel::show:35/defaults:204 → W rememberHistogram:3272 after successful result | P-H | E-H: accepted-default/reopen and histogram inputs exist |
| `histogram_scientific` / Scientific target mode (S:236) | `false`; bool | Checkbox | HistogramPanel::show:35/defaults:204 → W rememberHistogram:3272 after successful result | P-H | E-H: accepted-default/reopen and histogram inputs exist |
| `histogram_roi` / Scientific area ROI policy (S:239) | `"active_selection"`; whole_plane, active_selection, positioned_rois | Combo | HistogramPanel::show:35/defaults:204 → W rememberHistogram:3272 after successful result | P-H | E-H: accepted-default/reopen and histogram inputs exist |

### Threshold

| Key / widget label | Default; accepted domain | Widget | Native/ImGui consumer | Persistence | Evidence/gap |
| --- | --- | --- | --- | --- | --- |
| `threshold_method` / Auto method (S:243) | `"default"`; 18 registered methods; exact list below | Combo | ThresholdPanel::show:170 → W thresholdAutomatic:3276; successful Auto remembers validated options | P-T | E-T: Auto/restart actual controls; historical receipt not a current run |
| `threshold_histogram_roi` / Use target ROI for Auto histogram (S:253) | `false`; bool | Checkbox | ThresholdPanel::show:170 → W thresholdAutomatic:3276; successful Auto remembers validated options | P-T | E-T: Auto/restart actual controls; historical receipt not a current run |
| `threshold_apply_roi` / Scientific Apply ROI (S:256) | `"active_selection"`; whole_plane, active_selection, positioned_rois | Combo | ThresholdPanel::show:170 → W thresholdAutomatic:3276; successful Auto remembers validated options | P-T | E-T: Auto/restart actual controls; historical receipt not a current run |
| `threshold_stack_histogram` / Use all Z slices (S:260) | `false`; bool | Checkbox | ThresholdPanel::show:170 → W thresholdAutomatic:3276; successful Auto remembers validated options | P-T | E-T: Auto/restart actual controls; historical receipt not a current run |
| `threshold_bright` / Bright objects (S:263) | `true`; bool | Checkbox | ThresholdPanel::show:170 → W thresholdAutomatic:3276; successful Auto remembers validated options | P-T | E-T: Auto/restart actual controls; historical receipt not a current run |
| `threshold_ignore_black` / Ignore black (S:266) | `false`; bool | Checkbox | ThresholdPanel::show:170 → W thresholdAutomatic:3276; successful Auto remembers validated options | P-T | E-T: Auto/restart actual controls; historical receipt not a current run |
| `threshold_ignore_white` / Ignore white (S:269) | `false`; bool | Checkbox | ThresholdPanel::show:170 → W thresholdAutomatic:3276; successful Auto remembers validated options | P-T | E-T: Auto/restart actual controls; historical receipt not a current run |

### Binary processing

| Key / widget label | Default; accepted domain | Widget | Native/ImGui consumer | Persistence | Evidence/gap |
| --- | --- | --- | --- | --- | --- |
| `binary_iterations` / Morphology iterations (S:272) | `1`; [1, 100] | InputInt | S binary_options:408 → W operation defaults:4150/4236 → native BinaryMorphologyOptions | P1 | E-B: iterations/pad paired consumers; broader option matrix separate |
| `binary_neighbors` / Neighbor count (S:276) | `1`; [1, 8] | InputInt | S binary_options:408 → W operation defaults:4150/4236 → native BinaryMorphologyOptions | P1 | E-B: iterations/pad paired consumers; broader option matrix separate |
| `binary_black_background` / Black background (S:280) | `true`; bool | Checkbox | S binary_options:408 → W operation defaults:4150/4236 → native BinaryMorphologyOptions | P1 | E-B: iterations/pad paired consumers; broader option matrix separate |
| `binary_pad_edges` / Pad edges when eroding (S:284) | `false`; bool | Checkbox | S binary_options:408 → W operation defaults:4150/4236 → native BinaryMorphologyOptions | P1 | E-B: iterations/pad paired consumers; broader option matrix separate |

### Resources

| Key / widget label | Default; accepted domain | Widget | Native/ImGui consumer | Persistence | Evidence/gap |
| --- | --- | --- | --- | --- | --- |
| `history_limit` / Undo steps (S:288) | `8`; [1, 128] | InputInt | W historyPolicy:1332 → prepared per-owner history policy; new/adopted owners | P-L | E-R HU01–HU04 paired UI + native policy test sources |
| `history_memory_mb` / Undo pixel budget (MiB) (S:291) | `0`; [0, 1048576] | InputInt | W historyPolicy:1332 → obsolete-pixel bytes; zero retains step cap | P1 | E-R HU01–HU04 paired UI + native policy test sources |
| `async_viewport` / Background virtual views (S:295) | `false`; bool | Checkbox | W refreshTexture:4557, view:10526, release:12150; virtual XYZ always uses workers | P1 | E-R: selected policy/budget UI sources; no global RSS/platform closure |
| `viewport_cache_mb` / Virtual XY cache (MiB) (S:298) | `128`; [1, 1048576] | InputInt | W applyPreferences:1369/1411 → shared VirtualViewCache budget | P1 | E-R: selected policy/budget UI sources; no global RSS/platform closure |
| `render_memory_mb` / Render surfaces (MiB) (S:303) | `1024`; [1, 1048576] | InputInt | W applyPreferences:1370/1410 → RenderMemory validation/live limit | P1 | E-R: selected policy/budget UI sources; no global RSS/platform closure |
| `processing_workers` / Processing workers (S:308) | `2`; [1, 64] | InputInt | W applyPreferences:1366 → native JobScheduler worker limit | P1 | E-R: selected policy/budget UI sources; no global RSS/platform closure |
| `processing_memory_mb` / Processing reservations (MiB) (S:311) | `512`; [0, 1048576] | InputInt | W applyPreferences:1368 → scheduler plus named operation admission; not total RSS | P1 | E-R P13i actual budget/refusal/retry routes; qualification belongs to C receipt |

### Measurements

| Key / widget label | Default; accepted domain | Widget | Native/ImGui consumer | Persistence | Evidence/gap |
| --- | --- | --- | --- | --- | --- |
| `measurement_precision` / Decimal places (S:316) | `-1`; [-1, 15] | InputInt | W formattedResults:804 → view/CSV formatting; raw results untouched | P1 | E-M: API output consumers; full preference-to-result/profile UI missing |
| `measurements` / Results columns (S:320) | `[]`; ≤1024 unique nonblank names, each≤256 chars; custom names accepted; [] means all | column checkboxes | W result columns:790, panel:9744 → view/exported columns | P1 | E-M: API output consumers; full preference-to-result/profile UI missing |
| `profile_samples` / Profile samples per segment (S:358) | `0`; {0} or integer[2,1000000] | InputInt | W profile:8232 → per-segment samples; zero retains automatic ceil(length)+1 | P1 | E-M: API output consumers; full preference-to-result/profile UI missing |

### Files

| Key / widget label | Default; accepted domain | Widget | Native/ImGui consumer | Persistence | Evidence/gap |
| --- | --- | --- | --- | --- | --- |
| `print_dpi` / PDF resolution (DPI) (S:362) | `150.`; [1, 9600] | InputDouble | W export_pdf defaults:7646/8858 → physical PDF resolution; explicit override wins | P1 | E-F: API PDF defaults/override; full actual preference/export chain unqualified |
| `print_landscape` / Landscape PDF (S:365) | `false`; bool | Checkbox | W export_pdf defaults:7648/8860 → PDF orientation; explicit override wins | P1 | E-F: API PDF defaults/override; full actual preference/export chain unqualified |

### Startup

| Key / widget label | Default; accepted domain | Widget | Native/ImGui consumer | Persistence | Evidence/gap |
| --- | --- | --- | --- | --- | --- |
| `startup_mode` / At startup (S:368) | `"empty"`; empty, workspace | Combo | native startup.cpp:204 + W startup:12551 → next-launch plan | P1 | E-U: native + paired startup source tests |
| `startup_workspace` / Saved workspace (S:372) | `""`; strict UTF-8 ≤4096 bytes, no NUL; workspace mode requires nonempty path | InputText | native startup.cpp:211 + W startup:12552; Browse edits draft only | P1 | E-U: native + paired startup source tests |

### Shortcuts

| Key / widget label | Default; accepted domain | Widget | Native/ImGui consumer | Persistence | Evidence/gap |
| --- | --- | --- | --- | --- | --- |
| `shortcut_bindings` / Keyboard shortcuts (S:375) | `[]`; native portable grammar/caps/conflicts; [] inherits registry defaults | ShortcutEditor | S validate:66 → compile_shortcuts; W applyPreferences:1349 → dispatch/labels + ShortcutEditor | P1 | E-K: compiled grammar + paired actual control sources |


The 18 `threshold_method` strings are `otsu`, `mean`, `isodata`, `li`, `triangle`, `yen`, `max_entropy`, `percentile`, `intermodes`, `minimum`, `moments`, `min_error`, `default`, `ij_isodata`, `huang`, `huang2`, `renyi_entropy`, `shanbhag` ([native catalog](../../src/native/operation_catalog.inc)). Its preference default is `default`, not the generic operation's `otsu`. Enumeration does not imply identical reference semantics across methods.

The measurements widget initially offers Image, Slice, count, minimum, maximum, mean, stddev, median, sum, integrated_density, Area, Unit, ROI, Bounds, Length, Angle, label, x, y, width, height, pixels, area, centroid_x, centroid_y, perimeter, eccentricity, circularity and touches_border. Previously configured custom names are appended. Native validation accepts additional valid names; the widget has no arbitrary-name text editor. Keep that UI limitation explicit for measurement-specific work rather than narrowing the native API.

## Reusable evidence sources and precise limits

| Code | Existing sources | Reusable predicates and remaining limit |
| --- | --- | --- |
| Common | [test_settings.py](../../tests/python/test_settings.py): schema defaults, rejected values, native/Python sharing and file recovery; [test_native_preferences.py](../../tests/python/test_native_preferences.py); [native settings tests](../../tests/native/test_settings.cpp) and [native preferences tests](../../tests/native/test_preferences.cpp) | Schema/migration/detached data/atomic save predicates exist. No current all-key four-context pass is asserted. |
| E-A | test_settings.py `test_real_preferences_controls_search_scaling_and_default_zoom`, `test_python_launch_preferences_gestures_persist_and_reopen`, `test_settings_in_hidden_opengl_and_recreated_context`; smoke.cpp | Actual Search/auto-DPI Apply and host scaling sources exist. API setup of scale/zoom is not real Preferences input for those fields. Hidden GL/headless checks do not certify OS DPI/accessibility. |
| E-D | test_settings.py `test_native_consumption_of_colors_text_profiles_and_results`; [test_rounded_editor.py](../../tests/python/test_rounded_editor.py) | API-supplied colors/text plus actual rounded input/restart tests exist. Complete per-field Preferences→Drawing consumer matrix is missing. |
| E-S | test_settings.py `test_wand_matches_native_settings`; [test_region_enlargement_command.py](../../tests/python/test_region_enlargement_command.py); [test_selection_enlargement_command.py](../../tests/python/test_selection_enlargement_command.py) | Wand updates settings/invokes actions through API and derives its expected mask with `ij.wand_mask`; it is not independent actual-input acceptance. Enlargement has separate existing command/dialog/workflow checks. |
| E-V | [test_animation_ui.py](../../tests/python/test_animation_ui.py), [test_animation_timing_ui.py](../../tests/python/test_animation_timing_ui.py), animation_ui_probe | Axis/timing interactions exist; not every persisted FPS consumer or export mode. |
| E-C | [test_conversion_options.py](../../tests/python/test_conversion_options.py), especially validation, default reopen, range policies, frozen draft and actual preference checkbox tests | Native option/reference behavior and selected UI paths exist. Full RGB-weight/range/bit preference cross-product needs separate bound evidence. |
| E-H | [test_histogram_preferences.py](../../tests/python/test_histogram_preferences.py), [test_histogram_ui.py](../../tests/python/test_histogram_ui.py), histogram_defaults_ui_probe | Valid accepted defaults, invalid/cancel/failure, protected files and actual histogram routes exist. Captured ranges and ROI geometry are not global mutable defaults. |
| E-T | [test_threshold_preferences.py](../../tests/python/test_threshold_preferences.py), manual_threshold_ui_probe; historical p10d receipt | Auto/cancel/restart and write protection source tests exist. Retired historical build locators are not current proof. Preserve Auto's commit point. |
| E-B | [test_binary_iterations_consumers.py](../../tests/python/test_binary_iterations_consumers.py), [test_binary_pad_edges_consumers.py](../../tests/python/test_binary_pad_edges_consumers.py), native consumers | Pinned API and actual host consumer sources for iterations/pad exist; their historical fingerprints are not current four-setting certification. |
| E-R | [test_history_settings_ui.py](../../tests/python/test_history_settings_ui.py) HU01–HU04; [test_history_settings.py](../../tests/python/test_history_settings.py); [test_virtual_view_cache.py](../../tests/python/test_virtual_view_cache.py); [test_render_memory.py](../../tests/python/test_render_memory.py); P13i budget routes | Logical policies/pools and selected actual refusal/recovery sources exist. Provider/driver/other scratch exclusions remain; aggregate process RSS is not established. P13i qualification belongs to its final receipt. |
| E-M | test_settings.py colors/profiles/results consumer test | API changes exercise plumbing. Actual precision/columns/profile edits and corresponding CSV/plot behavior need a measurement packet. |
| E-F | test_settings.py `test_pdf_consumes_preferences_and_accepts_explicit_overrides` | API defaults/overrides exist. No current paired Preferences→PDF export interaction is established here. |
| E-U | [test_startup_ui.py](../../tests/python/test_startup_ui.py), [test_startup_settings_export.py](../../tests/python/test_startup_settings_export.py), startup_ui_probe/native startup | Text limits/workspace/policy/paired control sources exist. Bind original fresh-process and saved-file evidence before reuse. |
| E-K | [test_shortcuts_ui.py](../../tests/python/test_shortcuts_ui.py), [test_shortcuts_ui_supplement.py](../../tests/python/test_shortcuts_ui_supplement.py), native ShortcutEditor/compiler | Capture/conflict/revert/focus/inactive-row sources exist. CFG1 must not introduce another shortcut parser or alter portable conflict semantics. |

`tests/fixtures/settings-consumer-baseline.cpp` is explicitly an immutable **historical 37-definition synthetic validator fixture**. Its descriptor disclaims current settings coverage. Do not update its definitions/fingerprint to 61, or count a synthetic graph pass as product evidence.

## Implemented, missing and unverified are different states

- **Implemented:** all 61 current keys, common Preferences draft/file lifecycle and consumer paths listed above. Their presence does not complete every ImageJ/Fiji configuration option.
- **Missing configuration surfaces in this schema:** there is no persisted text font family/style/alignment default (only text_size), point counter/auto-measure/next-slice defaults (only point_size), extra Wand mode default beyond tolerance/connectivity, or a general codec/sequence default group (Files currently contains PDF DPI/orientation). Existing per-tool/per-format dialogs may offer some options; absence from shared Settings is not proof those dialogs are missing. Assign each additional default and its existing/new consumer after the pinned option inventory resolves its exact contract; do not add speculative keys in CFG1.
- **Unverified complete chains:** most generic Preferences field→consumer→fresh-launch combinations, original installed evidence availability, actual OS DPI/input, assistive technology, macOS and resource/RSS acceptance. Existing kernel/API or source-only tests do not substitute for those checks.
- **Confirmed observation gap selected by CFG1:** the two existing wand fields lack the complete per-field ID/clip/focus readback needed by the established actual-input producer convention. No wand behavioral defect is claimed from this audit.

## CFG1 implementation contract

### Required behavior and literal fixture

Use an owned eager UInt8 single-plane 3×3 dataset, rows `[10,20,20]`, `[20,10,20]`, `[20,20,11]`, seed `(0,0)`. No external image/model/reference dependency. This transparent native consumer fixture is not upstream ImageJ wand-parity certification.

| Tolerance / connectivity | Literal selected coordinates; row-major 0/255 mask |
| --- | --- |
| 0 /4 (defaults) | `(0,0)`; `ff0000000000000000` |
| 0 /8 | `(0,0),(1,1)`; `ff000000ff00000000` |
| 1 /8 | `(0,0),(1,1),(2,2)`; `ff000000ff000000ff` |
| 1 /4 | `(0,0)`; `ff0000000000000000` |

Physically choose the existing Wand tool and click the real owning canvas seed. Do not call `Window.action("wand")`, `wand_mask`, set selection or directly update settings as tested actions. Dataset/camera setup and explicitly seeded opaque preference compartments are labeled fixtures. Expected masks are the literals above, never product flood-fill/rasterization output. Capture native mask bytes, Selection/Previous, exact source words, calibration, saved ROI/overlays, history, dirty and revisions. No pixel mutation, pixel-history or workflow entry is invented by transient selection. Independently check the existing Selection/Previous sequence; selection changes may change data identity/selection revision and must not be mistaken for pixel mutation.

Open Preferences through existing real controls; Search `wand` exposes both fields in Selection. Type through actual scoped control IDs, physical keyboard/mouse and verified focus. Draft edits have no consumer effect. Apply publishes/persists the whole valid pair atomically. Connectivity6 and tolerance −1 refuse without changing either live value, persisted bytes or existing selection. Native API additionally refuses nonfinite tolerance. Preserve native `{4,8}` and finite `[0,DBL_MAX]`; no clamping or broadened grammar. Failed drafts remain correctable. Close/Revert discard drafts. Defaults stages the Selection group and still requires Apply; Search does not redefine group scope. Hold unrelated Selection settings at known defaults in this small packet and check preservation, without claiming every group's complete reset behavior.

### Exact ownership after C activates CFG1

| Agent | Initial write allowlist | Deliverable and restrictions |
| --- | --- | --- |
| A | New `tests/native/test_settings_wand_consumer.cpp`; optional `tests/python/test_settings_wand_contract.py` only after C assigns it | Literal four-mask contract, atomic map validation and native Preferences roundtrip. Existing settings/preferences/segmentation production files remain read-only unless an executed red grants a narrow repair. No new API or kernel. |
| B | Narrow observations in `src/desktop/settings_panel.cpp` for two wand fields; new `src/desktop/settings_wand_ui_probe.hpp/.cpp`; route hooks only in `src/desktop/main.cpp` and `python.cpp` | Shared actual-input producer, exact item/clip/hover/focus observations and persistence handoff. Reuse free-function probe pattern. Request any window.hpp declaration before edit; no processing/settings/history change. |
| C/root | CMake; new `tests/python/test_settings_wand_ui.py`; independent SDK consumer if required; capture/profile registration in `scripts/ci_observations.py`; this card and receipts | Literal expected masks, source/product identities, installed four-context runs, explicit persisted-phase orchestration and cleanup. Only C builds/installs/commits. |

Any request for window.cpp must name the exact function and interface. It is not in CFG1's initial allowlist; P13i owns its held bytes until release. Public headers/central bindings remain A/C owned. Every assignment reminds workers they share the checkout and must preserve others' edits.

### First concrete product-source change

Add **observations only** for `wand_tolerance` InputDouble and `wand_connectivity` InputInt in SettingsPanel. The existing Number branch has no per-key diagnostics; the Integer branch lacks full identity for connectivity. Reuse the scoped editable-label ID convention and actual ImGui group/item rectangle: record effective clip, enabled/active/focused/visible, draft and applied values. For InputInt, distinguish editable text ID/point from step-button/group bounds exactly as the existing history/memory controls do. Do not fabricate a hovered ID or mark clipped items as hittable.

This instrumentation enables missing acceptance. The wand algorithm, setting validation and widgets are already implemented. If the new checks pass, **no behavioral repair is required**. Only an executed counterexample authorizes further production changes.

## Actual-input routes, states and acceptance IDs

Four bounded route IDs in both installed launches on Windows/Linux: **16 planned process recipes**, not passes. Persistence phases share exactly one C-owned preferences file; every unrelated recipe has its own parent directory. Label and hash-bind that intended reuse. P13i's 64 routes remain unchanged; CFG1 uses a separate capture group.

| Route | Real event sequence and mandatory full states | Independent assertions |
| --- | --- | --- |
| `wand-consume` | `before` → default Wand → `default_selected`; edit 0/8 Apply/Close → `eight_applied`; Wand → `eight_selected`; edit 1/8 Apply/Close → `tolerant_applied`; Wand → `tolerant_selected`; edit 1/4 Apply/Close + Wand → `four_selected` | CFG1-UI01: four literal masks, exact pixel/calibration/carrier preservation, applied values, Selection/Previous sequence and source identity. |
| `wand-drafts` | Open/Search → `opened`; stage 1/6 → `invalid_draft`; Apply → `refused`; correct 8 → `corrected`; Revert → `reverted`; stage valid 1/8, Close/reopen → `closed_reopened`; real edit 1/8 + Apply → `accepted`; Defaults → `defaults_draft`; Apply/Close → `defaults_applied` | CFG1-UI02: whole-pair/file/selection atomicity, key-specific diagnostic, correction, Revert/Close, and nontrivial Defaults 1/8→0/4. Add negative-tolerance refusal/correction as bounded scalar milestones around accepted state, with exact settings/file/owner observations. |
| `wand-persist` | Labeled opaque JSON fixture; actual edit 1/8 → `draft`; Apply → `saved`; Close → `closed` | CFG1-UI03: independently parsed exact pair, preserved unknown values/root/extras, no temporary siblings; exact file path/hash/bytes for next process. |
| `wand-reopen` | Fresh executable/Editor loads actual previous file → `loaded`; actual Wand → `selected`; actual Preferences open → `shown` | CFG1-UI04: loaded 1/8 and literal three-pixel mask, actual displayed controls, unchanged saved-file hash, distinct process/host identity; no API restoration. |

Proposed envelope `cfg1.settings-wand.ui.001`: `acceptance_claim:false`, route/context, fixture identity, source/host/session identities, full states, scalar delivered frames, actual controls, raw masks/sample words, preference envelope/byte hashes, errors, final usage and bounds. Freeze exact field names with C before code. No success flag replaces an assertion. Missing/duplicate/unfinished states fail closed.

Keep each capture strictly below 16 MiB, 750 frames and 60 seconds. The fixture is tiny; preserve raw bytes/events rather than raising bounds. Fail immediately on missing controls, wrong hover/clip/focus ownership or terminal error. Scalar milestones must list all required fields and cannot replace the full source checkpoints.

| Acceptance ID | Required predicates |
| --- | --- |
| CFG1-N01 | Defaults tolerance 0/connectivity 4; strict types/ranges; invalid combined patch leaves the full Settings map unchanged. Include 6 and negative/nonfinite tolerance. |
| CFG1-N02 | Native Settings→consumer agrees with four literal masks; explicit action/native arguments do not mutate detached defaults; source pixels unchanged. |
| CFG1-P01 | Native/Python version1/unknown-compartment preservation, invalid-save-before-publication and recovery backup policy. Reuse existing tests only after consumed-input/evidence identity verification. |
| CFG1-P02 | Installed C++ and Python read the actual UI-written file and expose identical values. Settings mutation alone remains detached. |
| CFG1-UI01..04 | Four routes in all four contexts, exact masks and actual control/state chronology, no hidden skips or missing recipes. |
| CFG1-T01 | Capture/profile rejects missing/duplicate cases, stale product/file hashes, malformed/oversized raw payload, timeout and failed child exit. Synthetic transport checks are separate from product evidence. |
| CFG1-X01 | Final source/product manifest, installed native origins, standalone with Python paths removed, bounded verified retention/cleanup; no new dependency. |

## Dependency order and efficient handoff

1. C closes or explicitly pauses P13i and releases named shared seams. CFG1's audit/card may proceed during that hold; no frozen product input is changed by this document.
2. A freezes existing settings/selection-history semantics and prepares literal native tests. B freezes actual widget routes/schema and adds only the two observations/shared probe. C writes independent readers and inventories reusable persistence evidence concurrently. No alternate backend or new filter is needed.
3. A/B hand off source hashes; C takes the sole build/install lock, registers files, reuses active caches/runtimes, runs native/API checks before UI. Repair only diagnosed failures under named ownership.
4. C runs all contexts and the explicit persistence phase pairs, verifies exact file/hash continuity and affected regression, writes a scoped receipt and cleans owned scratch after hash-verified capture retention. Source readiness or producer completion is not acceptance.
5. Keep the remaining 59-key consumer matrix, broader wand options, scientific/XYZ tools, macOS/OS input/GPU/accessibility/performance and M0/M13 closure open. Select the next bounded consumer from evidence, not a rewrite of existing foundations.

## Reviewed input identities

These SHA-256 values bind a source audit only, not installed products. Later changes require new input identities and revalidation of affected statements.

| Source | SHA-256 |
| --- | --- |
| `src/native/settings.cpp` | `1584383b7f43bb3c2cc8429fa3ed3a96be2249a9ac47d556c27fa40ce8708957` |
| `src/native/preferences.cpp` | `b826da224f4ea3636db7b1aab24dd988c93f0a0424bbf16562c03626603df21a` |
| `src/desktop/settings_panel.cpp` | `e4b10052a3043e7e706cb70da3911713d3f68e1807f960a7e0e0c0f364b83217` |
| `src/desktop/settings_io.cpp` | `565e21a5f5d6ca4cf00794873b6cfdf0e98c69cf9136aeb6d9a79ca873683b90` |
| `src/desktop/window.cpp` | `15c43e9f50415d793cc363da2d22424e233c40cbe51a6357d867c6d4c8dc067b` |
| `src/native/segmentation.cpp` | `f345561a4470086bdca3c621080b4f8c6f3510a8adf5e020db68459e44a327eb` |
| `tests/python/test_settings.py` | `81c0e4180f651878c1eb7752254129c6886258fcbe114c0876f6de4a9879b786` |
| `tests/fixtures/settings-consumer-baseline-source.json` | `a12b5612fbc1a12af1e9ade77dbed5fac38eca16b7fa208bd057134dbc4997fa` |
