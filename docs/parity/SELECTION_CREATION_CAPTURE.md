# Selection creation input capture

Creation composition, resolution, record validation, replay, explicit rebind and
transaction preparation retain their admitted inputs before invoking callbacks.
A C++ caller may reassign its snapshot, request, record, resolved command, limits
or execution controls from a checkpoint or progress callback. That reassignment
cannot change the operation's owner, equation, result or cancellation token.
Cancellation through the captured shared token remains effective. Exceptions and
cancellation, including at final progress, discard private work without publishing.

Borrowed admission precedes owned captures. Working estimates include the captured
transport during validation and rebinding, and the resolved command retained
during preparation. Rebinding separately bounds original-record validation while
the replacement request is retained, even when its new equation is much smaller.
Captured request/transport ownership transfers into the result
where possible. Estimates are conservative logical storage bounds; callable
internals, allocator overhead, pixel history, publication and externally retained
transaction previews remain outside them. Existing V1/V2 wire equations and
candidate restrictions are unchanged.

Native tests reassign the arguments at the first checkpoint, initial progress
and final progress across all six operations. They also replace the execution
control and cancel its original token. Existing literal Boolean cell expectations,
source identity, Previous selection, unrelated annotations and owner/history
checks remain in force.
An additional public-API regression finds the minimum original-validation budget
and confirms that rebinding refuses before callbacks when its extra capture no
longer fits. The prior implementation fails this regression.

The next regional creation work is ordered as follows:

1. Add borrowed two-operand full-region estimation. Establish intrinsic frame,
   vertex and packed-storage bounds before making operand copies. Keep candidate
   vertex limits distinct from the total Boolean operand allowance.
2. Add an explicit full-geometry request mode and a V3 creation record with closed,
   nested geometry attributes and signed regional origins. Preserve V1/V2 canvas
   equations. Validate whole-envelope depth, intrinsic packed storage and the
   original equation before owner/revision checks or rebinding.
3. Route creation previews, completion and modifier gestures through the full
   algebra when their base is regional. Publish scientific selections through the
   metadata controller. Keep previews transient and capture Previous once.
4. Extend portable workflow decoding/replay and the optional Python interface.
   Verify forged results, stale sources, cap boundaries, cancellation, sessions,
   recording and actual modifier input in both ImGui launches on both hosts.
5. Continue regional rotation, continuous composite paths, axis/planar remapping
   and ImageJ ROI interchange. Separately qualify large-ROI runtime admission.

This prerequisite does not implement regional creation or complete any original
ImageJ/Fiji requirement. See the [scoped receipt](reviews/p12p-creation-capture001.json).
