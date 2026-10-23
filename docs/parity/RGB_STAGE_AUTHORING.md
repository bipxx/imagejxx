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

Stage creation is distinct from editing an earlier stage. Selecting, deleting,
reordering or modifying saved stages and authoring arbitrary plane visits remain
open. Real selection/navigation/returning Auto/Reset input coverage and the
historical Auto evidence-retention gate also remain open. This feature does not
establish complete ImageJ/Fiji parity or change the original 99 requirements.

The new installed tests use independent byte expectations: an all-30 RGB image
maps 30 to 170 with bounds 10..40, then 170 to 255 with bounds 0..170. Changing
selection after Keep leaves the first mask frozen; disjoint new pixels map 30 to
45. Final stack Apply uses only the final draft on other planes. All seven
component combinations, selection and stack choices run through both the action
and actual ImGui button, checking source immutability, Reset, complete recording
and paired history. A C++ HeadlessInputSession also clicks the actual button and
checks both stages and exact final bytes.

See the [scoped qualification receipt](reviews/p12a-rgb-stage001.json) for final
platform results and retained evidence.
