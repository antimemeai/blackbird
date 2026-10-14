# Release Beads fixture allocation cut

Original Release run39/40, failed selected argv allocation reached assertion.
Injector selected_allocation_stack(size) demands size>=8192 plus a dladdr frame
whose symbol contains RawBeadsArgv and push. Optimized executable has ZERO
RawBeadsArgv::push symbols; Debug has 23. The template is inline in the header.
Release can inline away the call frame the fault injector requires. Original
failure is retained; no change to product, assertion, or compiler configuration.
Thus selected fault is untested in Release, not a passing product outcome.

Original main aborts before cache_sharing and grant_boundaries. A context-owned
copy selects only these existing unchanged two functions, retaining external
fixture child dispatch. Build with exact Release flags/link libraries; run those
two groups directly so failed fault selection does not mask later cases. This
continuation does not pass/replace the original failed allocation oracle.
ASan/Debug and Linux results will determine how widely fault selection fails.
