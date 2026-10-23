# Keeping RGB adjustments

In Brightness/Contrast, **Keep current adjustment** freezes the current mapping,
components and selection, then starts an identity draft with the same components.
This lets another adjustment operate on the accumulated result without changing
components or modifying source pixels. Reset changes only the current draft.
Apply commits every saved stage and plane visit in one paired pixel/display edit;
Undo and Redo restore that complete edit.

The action `promote_rgb_contrast_stage` takes no parameters and is shared by the
C++ and Python ImGui frontends. Native session admission occurs before replacing
saved intent. Busy or incompatible sources, refused sessions, scalar images and
explicit uniform targets reject it. Restart RGB session discards saved editing
intent and begins again from the live source.

Open **Saved RGB adjustments** to select a saved stage from any visit. Change its
minimum, maximum or components, then press **Update saved adjustment**. **Move
earlier** and **Move later** reorder saved stages within that visit. **Remove saved
adjustment** deletes the selected stage. These operations retain each stage's
frozen selection and whole-plane policy. They recompute the private preview;
source pixels and history change only when Apply is pressed.

The final visit's last stage is the live draft and is excluded from this editor.
A historical visit must retain at least one stage. Reordering across visits,
changing historical geometry and authoring arbitrary plane visits remain open.
Real selection/navigation/returning Auto/Reset input coverage and the
historical Auto evidence-retention gate also remain open. This feature does not
establish complete ImageJ/Fiji parity or change the original 99 requirements.

The C++ `RgbContrastSessionEditor` and its Python binding expose
`set_saved_stage(visit, stage, options)`, `remove_saved_stage(visit, stage)` and
`move_saved_stage(visit, stage, destination)`. Indices are zero-based. Every
candidate passes record and resource admission before replacing immutable state;
invalid indices, live-draft access, invalid bounds/components and empty-visit
removal preserve the entire prior command without sample reads. Detached command
values and copied editors retain their old plans. The shared panel actions are
`select_rgb_contrast_stage`, `update_rgb_contrast_stage`,
`remove_rgb_contrast_stage` and `move_rgb_contrast_stage`; their exact fields are
`visit, stage`, plus `minimum, maximum, channels` for update or `destination` for
move. Booleans and fractional indices/component masks are refused.

The new installed tests use independent byte expectations: an all-30 RGB image
maps 30 to 170 with bounds 10..40, then 170 to 255 with bounds 0..170. Changing
selection after Keep leaves the first mask frozen; disjoint new pixels map 30 to
45. Final stack Apply uses only the final draft on other planes. All seven
component combinations, selection and stack choices run through both the action
and actual ImGui button, checking source immutability, Reset, complete recording
and paired history. A C++ HeadlessInputSession also clicks the actual button and
checks both stages and exact final bytes.

See the [stage-creation receipt](reviews/p12a-rgb-stage001.json) and
[saved-stage editing receipt](reviews/p12b-rgb-stage-edit001.json) for platform
results and retained evidence. New order-sensitive literal tests distinguish
30 → 85 → 128 from reordered 30 → 45 → 149. Both hosts run the actual stage list,
range field, update, move and remove controls in a native ImGui input session,
plus an installed public C++ SDK consumer and installed Python consumers.
