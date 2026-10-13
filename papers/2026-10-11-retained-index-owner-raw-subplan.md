# Whole semantic retained/index raw-capture subplan

2026-10-11. The ONE whole plan attack resolved M1–M6. Reserved sites3100–3299.
This is the authoritative plan; ignored context/retained-index-study/subplan.md
retains acquisition/source notes, not current credit/layout decisions. Main-only
capacities and no duplicate semantic-loan telemetry are approved design choices.
Whole-main activation remains held until all actual owners/loans are connected
and qualified. Source implementation is not claimed by this paper.

## Invariant and boundaries

Retained-state hooks emit native values unchanged into the audit log; failed
ingestion writes the existing8001 ingest-failure event. Requests and actual returns are
separate events. Preserve actual originals/pending/prepared/poisoned state when
work fails. Authoritative journal ack, semantic accepted facts, selected derived
root/catalog/slot, permanent environment authority and dispatch rights are distinct.
No writer serialization, representation conversion, metric-only traversal, packet
re-encoding, extra variant visitor, checksum recomputation or fake missing fields.
Powerloss is excluded; no evals, timers, dependencies, brokers, subscriber registry,
retry policy or extra panel is introduced.

This owns RetainedState/FramedJournal semantic custody/recovery and original
selection, RecoveryIndex, ArchiveCatalog, SavedState/scan hints, EnvironmentHead
and RetainedEnvironment. Reader owns physical product acquisition3000–3099;
Provider owns operation17; rawquery2300 is already settled. Existing RawRegion
1090/1091/1092/1094 remains append's single timing/outcome/error capture. New3100+
fields link its actual BEGIN and add native arguments/custody rather than repeat it.
Metadata-only cursor/state/header/tail/count accessors are copied at owner sites,
not wrapped on every getter invocation. Inline forwarding uses the actual callee's
receipt instead of adding an identical API layer.

## Literature/reference grounding

Use already acquired Saltzer/Gintell raw interference; UNIX raw streams/filters;
Lampson narrow interfaces/background ownership; Reno kern_ktrace/kdump actual
syscall/request/return/input boundaries and its omitted-error/synchronous-vnode
counterexamples. Existing storage-performance-2026-10-07/STUDY and
COLD-HISTORY-R2-DESIGN: pinned LMDB700e10f91a65 commit4383/4570,
pickmeta5012/write_meta4868; flush and metadata selection differ, unbounded EINTR
is not a canceled finite allowance. LMDB lmdb.h98–100/668–675/894–897/1125–1126
associates lifetimes with explicit serialized owners. SQLite5af1b822f5da
walIndexRecover1422 is derived acceleration; LevelDB7ee830d02b62
VersionSetRecover861 refuses missing selected manifests rather than invent empty
history. Existing Bitcask locator/Aeron snapshot borrow references apply to cold
history. QUARANTINE.md and existing references.json retain acquisition hashes and
restoration commands; original archives intact. No dependency or imported ref test.

## Implementation arrangement and provenance

Add raw_retained.hpp/cpp fixed native field helpers/owner trace and typed finite
RawRetainedSettlementLoan; no generic dispatch-to-encoder table. Request helper
accepts literal site and native ByteViews; stages caller result before success
terminal. Native Error fields are emitted only when an actual Error is owned;
optional fields are shorter variants, not zeros. E16 raw envelope/R24 actual
issuing epoch+ordinal. Receipts always link their own actual request; original
resource/setup R may refer across producers and remains explicitly separate.

RawRetainedOrigin has actual parent R plus optional borrowed root-specific semantic
loan. RawRetainedScope validates explicit root target/current authorizing producer,
ordinary/no_new mode and Job eligibility, then borrows/restores selected root,
FramedJournal and ArchiveCatalog origins. No ambient TLS right. Preserve the
existing narrow RawRegionPrepaidScope as an independently paid bank.

Root authority is Reader's explicitly shared product-root authority: immutable
setup R/handle, lexical sequential current issuer across whole Terminal perform,
actual join before main idle or next worker, selected current native journal/
catalog. Root factory receives that explicit owner before construction; it is
also supplied to native directory/files. No raw_enabled-derived product-storage
opt-in. Existing unselected custom JournalDirectory/JournalFile callbacks forward
normally and do not invent native events. Product-root whole-main opt-in remains
held until all reviewed caller propagation below is implemented and qualified.

Job grants are issued on Job.raw_context during its existing pre-thread binding;
pending_join becomes usable only after actual worker.join. Settlement binds that
same issuer sequentially and lends the loan to its business closure. Parent current
issuer is restored after scope. Never-started/abandoned job loan END occurs only
under proven never-started or joined binding, before claims/handle destruction.
Later explicit live retry pays new credit before effect, on that same joined issuer.
Surviving FactHistory/ImmutableBytes readers retain actual file/catalog/generation,
shared root authority and actual creation R, never a dead RetainedState pointer or
migrated creator claims. Lazy invocation emits on actual authorized issuer/loan.
Reader's ONE CleanupState serializes every cleanup bank mutation/close/unlock/DIR/
END without waiting. Semantic unique-owner teardown125 is prepaid there before
successful Root construction and copies final native state before member drops;
partial failed factories have no completed semantic owner END and release unused
credit through that same cleanup accounting. No duplicate semantic lazy-reader close.

## Exact selected records and native widths

Native identity16, RecordRef/SourceRef24, cursor/predecessor32, pageRef20 (8/8/4),
PhysicalJournalRecord66 (16/16/2/8/8/8/4/4), native key40, Header81 maximum,
HeadSelector56, JournalCapacity16, Limits8, EnvironmentBounds48, kind2,
evidence/state1, slot4, size_t8, bool1, Error9. Copy fields, never padded structs.
All table credits include E/R; sum maximum native variants for fixed credit,
release unused capacity normally. Missing fields are not emitted.

|Sites/source operation|Actual native fields after E/R|CONTROL maximum|
|---|---|---:|
|3100 append args|expectedcursor32/S8/E8/diagnostic1/settlement1/protectedpresence1/ownedcredit16|107|
|3101 append custody|actualcursor32/prepared1/pending1/protectedpresence1/ownedremainingcredit16|91|
|3102 pending original|actual ProvisionalCapture origin16/first8/length8/purpose2|74|
|3103 prepared/published selection|actual stage1/counter8/queryroot20/prepared1/pending1|71, max2 perappend|
|3104/3105 Retained create/open|request create Header81/cap16/method1; open also use_scan1/allow_full_replay1/max_full_replay_bytes8; return cursor32/state1/usedhint1/compact1/reconciled1/branch1/ownedError9|maximum148+86=234|
|RetainedState indexed attachment3106/3107|maxbytes8; result actualroot20/attached1/branch1/error9|48+71=119 plus actual populate children|
|RetainedState actual owner teardown3108/3109|currentcursor32; reachedwriterstate1/pending1/prepared1/branch1/error9|72+53=125, separately held before construction; no ordinary queries on cleanup issuer|
|3110/3111 save_current_state|request cursor32/savedpresence1/ownedslot4/compact1; return selectedboundary32/slot4/archived_facts8/archived_records8/proposed_selection_accepted1/branch1/ownedError9|78+103=181|
|3112 actual encoder header selection|already computed native kind2/schema8/dependencies.size8|58|
|3113 actual decoder header selection|existing decoder.number locals schema8/kind8/count8|64|
|3114 source staged|actual SourceRef24/payload_offset8/payload_size4|76|
|3115/3116 apply|request actualrecord24/evidence1/dependencies.size8; return branch1/ownedError9/root20/counter8|73+78=151|
|3117 actual dependency|actual SourceRef24 at existing dependency loop|64|
|3118 actual existing/uncertain match|actual RecordRef24/evidence1 on successful existing()/existing_uncertain() result only; never generic lookup or public inspection|65|
|3119/3120 submit|request actual settlement1; return existing1/ownedRef24/evidence1/branch1/ownedError9|41+76=117|
|3121/3122 retain rejection|request expectedcursor32/S8/E8/actualreason9; return prefixretained1/pending1/state1/branch1/ownedError9|97+53=150|
|3123 actual source group|existing offset8/count8|56|
|3124/3125 reserve identity|request namespace8/cursor8/limit8/highwater8/state1; return issuedID16/namespace8/cursor8/limit8/branch1/ownedError9|73+90=163|
|protect/refresh settlement3126/3127|actualcredit16; return actualpresent1/credit16/branch1/error9|56+67=123; protect successful scope also holds release3128/3129=40+50=90|
|RetainedState replay3130/3131|currentprefix32/captureonly.size8/maintenanceend8; result root20/counter8/recordingfailed1/prepared1/reconciled1/branch1/error9|88+81=169 plus actual batches/reads/apply|
|replay batch3132/3133|batchfirst8/actualfirstindex8/endindex8/rollbackcounter8/root20; return actualaccepted1/counter8/root20/facts.size8/sources.size8/branch1/error9|92+95=187 plus children|
|confirm/reconcile3134/3135|currentcursor32/currentreconciled1; result cursor32/state1/reconciled1/recordingfailed1/branch1/error9|73+85=158 plus staged replay/verifier|
|actual verifier3136/3137|actual attempts.size8; native Result branch/error|48+50=98; actual each selected AttemptState metadata separately before callback|
|3138/3139 decision getter|request actualID16; return sixIDs96/planned.size8/continuation.size8/branch1/ownedError9|56+162=218|
|3140/3141 lookup|actual family1/kind2/identity16/second16; return actualRef24/evidence1/branch1/ownedError9|75+75=150|
|3142/3143 attempt getter|request actualattemptID16; return Admission IDs48/input.size8/input_from_invocation1, opened1, observationpresence1 and actualphase1/disposition1/size8 when owned, receiptpresence1 and failurepresence1/actualError9 when owned, evidence1/reconcile1, branch1/ownedouterError9|56+132=188|
|fact/factpage3144/3145|firstordinal8/actual limit8; native branch/error and actual selected result count8|56+58=114; per actual returned factRef24/evidence1 =65|
|history snapshot3146/3147|nativecount8/savedpresence1/currentboundary32; native branch/error and actual selected boundary32/tail.size8|81+90=171; closure owns file/catalog/root authority, not dead RetainedState|
|history reader invocation3148/3149|actual creationR24/ordinal8; native branch/error with factRef24/evidence1|72+75=147, then actual catalog/packet children; bounded page<=16|
|source resolver3150/3151|actualSourceRef24; native branch/error and actualreturnedbytecount8|64+58=122 plus selected descriptor/packet children|
|retained dispatch3152/3153|actualattempt16; returneddispatched1/effectbranch1/effectError9/recordingbranch1/recordingError9/outerbranch1/outerError9|56+71=127; deactivate selected cleanup loans at real body boundary, restore before receipt|
|3154 actual public fact/page selection|actualRef24/evidence1 on successful fact() or FactHistory::page() result selection only, associated with that public request; never existing()/existing_uncertain()/lookup()/history closure construction|65 per actual public result; ZERO in selected stop/Job/receipt/exec topology|
|3156/3157 unresolved attempts|request actualcursor32; return result.size8/branch1/ownedError9|72+58=130|
|3158/3159 invocation getter|request ID16; return threeIDs48/input.size8/branch1/ownedError9|56+106=162|
|3160/3161 framed append|drafts.size8/currentcursor32/state1; return actualcursor32/state1/branch1/ownedError9|81+83=164|
|3162 actual prepared/parsed/hint frame|already-owned PhysicalJournalRecord66|106|
|3163 actual commit|existing first8/drafts.size8/encoded.size8/actualbusinesscrc4|68|
|3164/3165 actual checked packet|record66/boundary32/method1; direct return acquiredvector.size8 only on success, ownedstate1 on both Result branches; archive/lazy omit unowned state; branch1/ownedError9|139+59=198|
|3166/3167 Framed create/open|request create Header81/cap16/strength1/method1; open also use_scan1/resumepresence1/ownedresume(cursor32/indexed_records8/crc4)/selectorpresence1; return cursor32/state1/usedhint1/archived_records8/branch1/ownedError9|maximum186+92=278|
|FramedJournal scan/restage3168/3169|actualcursor32/propagatenativereaderrors1/inrestage1; return actualcursor32/state1/diagnostic_offset8/available_end8/problem presence1/problem9|74+109=183 plus actual frame reads/parse selections|
|selected recovered prefix3170/3171|actualpredecessor32; result actualcursor32/state1/branch1/error9|72+83=155|
|physical reject batch3172/3173|actualbatchfirst8/actualreason9; result actualcursor32/state1/diagnostic_offset8/available_end8/branch1/error9|57+99=156|
|3174/3175 reader creation, two native variants|archive: request boundary32, return created1/state1/branch1/ownedError9; payload: request actualrecord66/boundary32/method1, return created1/branch1/ownedError9|archive72+52=124 /payload139+51=190|
|original range3176/3177|actualoffset8/requestedlength8; return nativeobservedextent8/actualreturnedbytes.size8/branch1/error9|56+66=122; acquired original bytes already physical capture|
|3178/3179 Framed confirm/reconcile|request cursor32/state1/actual-called-method1; return cursor32/state1/diagnostic_offset8/available_end8/problem_presence1/ownedproblem9/branch1/ownedAPIError9|74+109=183|
|RecoveryIndex open3180/3181|maxbytes8; result actualend8/branch1/error9|48+58=106|
|RecoveryIndex find3182/3183|actualroot20/key40; result found1/actualvalue8/branch1/error9|100+59=159 plus reached page/selection children|
|RecoveryIndex put3184/3185|actualroot20/key40/value8; result actualnewroot20/branch1/error9|108+70=178 plus reached page writes/splits|
|RecoveryIndex read3186/3187|actualPageRef20; nativeleaf1/decodedcount8/branch1/error9|60+59=119; raw native page bytes belong physical capture|
|RecoveryIndex write3188/3189|actualend8/nodeleaf1/entries.size8/children.size8; actualRef20/newend8/branch1/error9|65+78=143 plus actual address burn68 paid before write|
|RecoveryIndex actualselected entry3190 / actualchild3191 / actualsplit3192|nativeentry key40/value8; childPageRef20; splitseparator key40/value8/left20/right20|88 /60 /128; copy reached existing product values|
|RecoveryIndex range3193/3194|actualroot20/lowerkey40/limit8; actual remaininglimit8/branch1/error9|108+58=166 plus actual page/callback children|
|RecoveryIndex callback3195/3196|actualentry key40/value8; actual bool1/branch1/error9|88+51=139; prepay before callback, stop does no extra read|
|RecoveryIndex sync3197/3198|nativeactualend8; nativeResult branch/error|48+50=98 plus native sync|
|3199 actual index address burn|already-created actualPageRef20/reachednewend8 BEFORE native write, prepaid before end_ mutation|68|
|3200/3201 catalog publish/merge|request Header81/boundary32/input.size8/previouspresence1/ownedpreviouscount8; result written_count8/branch1/ownedError9|170+58=228|
|ArchiveCatalog open3202/3203|Header81/boundary32/maximum_entries8; result actualcount8/branch1/error9|161+58=219; file native header/extent/read physical capture|
|3204/3205 catalog find|actual key40/boundary32; actualowned PhysicalRecord66/branch1/ownedError9|112+116=228|
|3206/3207 catalog entry|actual ordinal8; actualkey40/PhysicalRecord66/branch1/ownedError9|48+156=204|
|ArchiveCatalog selected output3208|actual outputordinal8/key40/PhysicalRecord66|154 before existing encoder/write, no metrics traversal|
|saved load3220/3221|actual journalcursor32/capacity16/beforeScan1; return nativeboundary32/slot4/factcount8/issuer8/unresolved.size8/branch1/error9|89+110=199, plus at most two slot calls|
|saved slot3222/3223|actualslot4/beforeScan1; same selected return fields|45+110=155; actual swallowed native errors recorded on that branch before ignored return|
|saved publish3224/3225|actualsavedboundary32/factcount8/issuer8/slot4/unresolved.size8; return actualboundary32/slot4/branch1/error9|100+86=186 plus real packet/header/anchor/native effects|
|saved root selection3226|actualboundary32/slot4/factcount8/issuer8/unresolved.size8|100, at actual selection before native in-memory swap/retirement|
|saved unresolved record3227|actual factRef24/evidence1|65, in existing selected unresolved loop only|
|scan checkpoint load3228/3229|actualslot4; result actualcursor32/records.size8/slot4/branch1/error9|44+94=138; actual record children106 at existing parse loop|
|scan checkpoint publish3230/3231|actualcursor32/records.size8/checkpoint_attempt8/strength1; selected slot is not yet owned at entry, omitted; return actualcursor32/used1/checkpoint_attempt8/[actual selectedslot4]/branch1/error9|93+95=188 plus bounded existing records/native publication|
|3232/3233 actual resident retirement|BEFORE actual clear: boundary32/facts.size8/sources.size8/physicalrecords.size8; AFTER actual clear: archived_facts8/archived_records8/journal_archived_records8/tail_facts.size8/tail_sources.size8/physicalrecords.size8|96 before /88 after|
|head prepare/open3240/3241|actualHeader81/strength1; result actualselector56/healthy1/initialprepared1/attempted1/branch1/error9|122+109=231|
|head publish-initial3242/3243|actualselector56/strength1; same reached return fields|97+109=206|
|head replace3244/3245|actualexpectedselector56/nextID16/strength1; same actualselected return fields|113+109=222|
|3246/3247 Head read_selector|request expectedroot environment16/journal16; return actualselector56/branch1/ownedError9|72+106=178|
|head reached transaction3248|actualhealthy1/initialprepared1/attempted1/published1/intransaction1/selection56|101, before actual attempted effect and final state uses held bank|
|3250/3251 Head write_selector|request actualselector56/strength1; return actual existing attempted outflag1/branch1/ownedError9|97+51=148|
|3252/3253 current_programs|request savedpresence1/ownedboundary32; return actualpackets.size8/branch1/ownedError9|73+58=131|
|RetainedEnvironment create/open3260/3261|actualHeader81/rootcapacity16/bounds48; return actualcursor32/state1/headselector56/busy1/continuationrequired1/branch1/error9|185+141=326 plus actual chain/header/journal children|
|environment submit/proposal3262/3263|actualexpectedcursor32/sources.size8/events.size8; result actualcursor32/existing1/capturepresence1/actualcapture34/events.size8/branch1/error9|88+126=214; each real result Submission66 if needed|
|environment selected segment/header3264/3265|actualjournalID16; result actualHeader81/branch1/error9|56+131=187 plus native header inspection|
|environment selected continuation3266|actualoriginHeader81/originpolicy16/selectedpredecessor32|169, copy values existing selected load logic already owns|
|environment maintenance replay3267/3268|actualHeader81/originpolicy16; return actualend8/captureonly.size8/queryroot20/counter8/branch1/error9|137+94=231 plus selected children|
|environment stage original3269/3270|actualmarkerRef24/ProvisionalCapture34/originHeader81/policy16; return actualnativecapturefirst8/length8/branch1/error9|195+66=261 plus actual selected capture chunks|
|environment provisional capture3271|actualcaptureRef24/actualnativeoffset8/nativepayload_size4/actualpurpose2|82, at existing chunk selection, bytes are physical acquired originals|
|environment confirm/reconcile3272/3273|actualcursor32/state1; result actualcursor32/headhealthy1/continuationrequired1/branch1/error9|73+84=157 plus historical journals/verifier/semanticchildren|
|3286 actual selected AttemptState/verifier input|native82-byte maximum state, copied in actual existing loop, no extra iteration|122|
Codec selection is an internal actual native header observation, not another codec
request/return layer. Add default-inactive explicit RawRetainedOrigin argument to
selected existing codec calls. Encoder stores its existing schema/kind expression
in a product local used by the existing writer; decoder exposes its ALREADY computed
uint64 locals. No extra visitor, narrowing or re-encoding. Codec Error closes the
actual owner request. Delaying these fields until the product reaches them preserves
truth on early refusal. apply/submit entry never invent an unavailable kind;
lookup never calculates decodedkind for emission. Actual byte originals already
enter through physical/selected original owners; this unit does not clone payloads
or traverse immutable cold bytes for metrics. Commit uses the actual business CRC
local already passed to put_little, not a second checksum/decode.


All site layouts are specified once in the preceding authoritative table. Ordinary
APIs acquire their fixed pair and reached final metadata before work, then admit
each selected page/frame/merge entry/replay batch/callback before its next effect.
No aggregate history traversal for metrics. Native creation/open variants are
explicit: create never emits fake open policy fields. Reader-creation variants are
distinguished by their documented native argument length, not a serializer registry.
Codec errors close actual owner requests; no additional codec API receipt layer.


Saved optional load/slot/result variants include the native absent/present branch;
fields only when present. Actual swallowed Error is emitted before existing optional
fallback; structural ignored/refused branch is not invented I/O failure. Factory
partial returns omit unowned cursor/state. Index key/opcode/CRC come from actual
product locals, never new metric calculations. Full Header predecessor fields only
when actually present. Wrapper data copied at caller native operations shares the
explicit resource R, so paths/names already captured by linked physical children
are not serialized again by semantic writers.

The found-record sites are disjoint source boundaries. 3118 records only a
successful return of existing()/existing_uncertain() (retained_state.cpp662–724,
called by apply, append refusal handling and submit). Generic lookup3140/3141
already returns its actual Ref/evidence and never emits 3118 or 3154. 3154 belongs
only to the public inspection request: one successful fact() result or each fact
actually selected by FactHistory::page() after its read succeeded. The direct
history closure's request3148/3149 separately describes that actual read; it does
not emit 3154 on its own. A fact_page wrapper delegates its public page request
and does not duplicate the page's result-selection event. A nested page->fact
call has two actual public requests with distinct parent R; each selection is
associated with its own request, never emitted twice for one request.

Selected Context stop never calls fact(), fact_page(), FactHistory::page() or its
read closure: refresh_history225–230 only constructs history_snapshot, and
view707–715 returns existing entries. The two possible snapshot constructions and
archive_reader factories are the separately paid590; construction does not read
or select a historical fact. The initial Context archive loader at296 and initial
snapshot492 are ordinary setup, outside the canceled stop contract. Joined Job,
receipt and fresh exec select through lookup()/existing()/existing_uncertain(),
never these public fact/page APIs. Therefore their actual 3154 allowance is ZERO,
and 65X pays only 3118. An extra public inspection in no_new is outside these
contracts and must refuse before acquisition/effect or receive a separately
prepaid actual inspection envelope; it cannot spend the found-match allowance.

Every temporary native field has named owned storage through synchronous emission.
Encoder kind/schema/dependency-count are lexical native locals, used by both the
existing business encoder and the raw fields; dependency .size() is stored before
making a ByteView. Decoder schema/kind/count are its existing u64 locals, never
narrowed for emission. Any other .size(), boolean comparison or variant selector
used as a native field similarly has a local owner lasting through raw_emit.
Field views never refer to a full-expression temporary or a getter's temporary
return. Emission copies synchronously and retains no caller view. This includes
nested codec emissions and all optional field arrays, not merely the top-level
trace request. Direct ASan variants must exercise encoder and decoder fields.

Ordinary indexed put max_depth32: at most32 page reads/65writes, possible32splits/
32selectedentries/31child selections; source-derived semantic upper26473 is an
optional whole indexed budget, separate from its native16KiB page I/O. find full
upper8643. Normal range pays each actual nextpage/callback139, stopping does no extra
read. Failed burned/orphan writes do not alter old published query root. Selected
no_new contracts exclude newly attached indexed roots before effect unless explicit
independent native/index topology is funded; current main has no query_index_.

## Canceled selected topology and exact semantic banks

Bsemantic =646K+182S+257E+64D+117U+224Q+56G+163I+150L+65X
           +228A+204P+198H+58*(E+2Q)+64H.

Kappendcalls, Ssourcedrafts, Esemanticdrafts, Ddependencies, Usubmitcalls,
Qrejections, Gsourcegroups, Iidentitycalls, Llogical lookups including recursive
Admission->Invocation, Xactualexisting/uncertain possible selections,
Acatalogfinds, Pcatalogentrycalls, Hpositive checked packet reads.
X<=2E+2K+2U. These are admission API bounds, NOT emitted derived metric counts.
Codec counts E+2Q are actual appendix encodes plus rejection's two marker encodes;
H is actual positive selected packet decodes. No standalone3280–3285 semantic-loan
telemetry: instrumentation scaffolding must not duplicate real owner/physical
grant lifecycle. Reader3033–3038 owns actual native grants/authority scopes.


The coefficients are the following literal site sums, not a body encoder or
runtime count calculation:

|Coefficient|Prepaid native site sum|
|---|---|
|646K|3100args107 +3101custody91 +3102ordinarypending74 +2*3103selection71 +3160/3161Framed pair164 +3163commit68 =646|
|182S|3114actualstagedsource76 +3162actualphysicalsourceframe106 =182|
|257E|3115/3116actualapplypair151 +3162actualphysicalsemanticframe106 =257|
|224Q|3121/3122actualrejectionpair150 +3102distinctrejectedpending74 =224|
|64D|3117actualdependency64|
|117U|3119/3120actualsubmitpair117|
|56G|3123actualselectedsourcegroup56|
|163I|3124/3125actualidentityreservepair163|
|150L|3140/3141actualnative lookuppair150|
|65X|3118actualownedexisting/uncertainselection65|
|228A|3204/3205actualcatalogfindpair228|
|204P|3206/3207actualcatalogentrypair204|
|198H|3164/3165actualcheckedpacketpair198|
|58*(E+2Q)|3112existingproductencoder header selection58: each actual append event encode plus rejection's empty_marker and marker encodes|
|64H|3113existingproductdecoder header selection64 at actual positive selected packet decode|

646K conservatively prepays ordinarypending even for diagnostic append children
that never construct one, and two selection records even if native ack fails.
Only actual reached fields are emitted; unused allowance is released. 224Q pays
ONE DIFFERENT outer rejected pending proposal: its native metadata is created in
retain_rejection before the diagnostic source-only/marker children. It is not
charged again as an invented child's ordinary pending packet.

K includes actual normal attempt plus possible source-group and marker child
append calls. PhysicalFrame106 is paid per maximum draft even when validation
refuses before those frames exist; no frame event is fabricated. The native Framed
pair/commit allowance likewise remains unused if no Framed append is reached.
Existing RawRegion BEGIN/observations/END/error/outcome are EXCLUDED from every
coefficient above. They remain caller's independent 1090-family bank: Context6912,
exec3*1728, Job1728, or Provider actualR(S,E). Semantic loan never uses those bytes.

Proposed_batch_acknowledged starts false. ONLY ack of that exact outer proposed
batch sets it true and fills caller physical metadata. Diagnostic source/marker
children have their own native acknowledgments, never receive the caller's proposed
outflag/span, and NEVER set the original proposal's flag. A rejected operation can
therefore retain real diagnostic children while its proposed_ack stays false.

|Selected owner|K/S/E/D/U/Q/G/I/L/X/A/P/H|New semantic CONTROL|
|---|---|---:|
|Context stop|16/8/8/8/4/4/8/2/8/56/8/264/0|78536 (incl2*(history171+archivecreation124)=590)|
|Joined Job terminal|4/2/2/2/1/1/2/0/7/14/7/231/3|55741|
|Adapter receipt alone|4/2/2/2/1/1/2/0/4/14/4/132/1|33887|
|Generic terminal primary/capacity-fallback|4/2/3/2/2/1/2/0/14/28/14/462/6|107639|
|Fresh exec prebody+receipt+no-child union|12/6/6/6/3/3/6/0/24/42/24/792/9|188868 (inclattempt188+dispatch127=315)|

Context existing2286+RawRegion6912=9198 remains separate; parent turn existing
fixed1346+9198=10544. Job worker4658 and receipt/prebody exec3*1728 regions are
separate. Provider operation17 region R(S,E)=276+52(S+E) remains separately owned;
none of its A bank pays new semantic or physical events.

Fresh attempt() has fivefinds, Decision one, submitOpen seven on logical failure;
receiptfour and initialterminalseven =>24. Native lookup EIO/corrupt throws into
append catch without logical-error extra existing probe. Current Coding writes
only first terminal observations; no prior running/settling positive observation
assumed. Recovered/alreadyopened/accepted duplicate requiring cold equality does
not inherit this fresh contract or dispatch rights. Additional required acquisition
refuses before effect and preserves originals/debt; explicit later live retry pays
its actual already-owned metadata before another attempt. Finite paid native short/
EINTR quotas can exhaust honestly; no success guarantee for arbitrary schedules.

No_new optional checkpoint refuses before the callback/read/write if unpaid, records
actual maintenance failure and lets admitted append proceed. Ordinary maintenance
remains enabled/restored after scope; threshold native oracle must prove it runs
again. No permanent blanket maintenance disable on normal calls.

## Acknowledged extent/caller API seam and physical loans

append optional caller span<PhysicalJournalRecord> and proposed_batch_acknowledged
outflag: validate exact sources+events cardinality BEFORE effect. After actual
journal ack copy already-owned records and set true noexcept before fallible
followup. Diagnostic child acknowledgment does not mark original proposal accepted.
Selected exec fixed3 caller descriptors preserve actual Decision/Invocation/Admission
metadata on command owner. No per-fact mirror. Recovered metadata comes from actual
required replay/lookup before effect, not metrics-only read; never dispatch rights.

Coding prepares fresh complete exec bundle after actual serialized input and
continuation buffers exist, BEFORE ordered3-record admission. Scope beforedispatch/
Open, suspended before actual adapter body, resumed forreceipt; separate nochild
terminal possible. THREE native physical scopes are paid. Current Job gets its own
separate prelaunch joined terminal bundle on child lane. Before launch caller has
actual3 acknowledged records; physical loan validates explicit active root authority
and target, not file creator equality. Same resource creation R stays immutable.


The nine positive FRAME upper bound is a fresh-exec union, with source coordinates
from current src/retained_state.cpp. Direct catalog positive selections reach
read_catalog_payload at472–475 and decode at476–482. A selected schema2 Admission
recursively looks up Invocation at480–485. Direct Framed read716–737 allocates/
reads32+payload bytes; no112root header here.

|Positive frame|Reached source path|Native frame bytes|
|---|---|---:|
|1 Admission|dispatch1780 ->attempt1590–1591|88|
|2 Invocation|that Admission resolves atlookup480–485|32+I|
|3 Decision|dispatch1792–1793 DIRECT lookup, not publicdecision()|32+D|
|4 Admission|dispatch1804 ->submitOpen ->apply879–880|88|
|5 Invocation|that Admission resolves atlookup480–485|32+I|
|6 Open|postbody dispatch1813–1817 ->receipt apply872–873|56|
|7 Admission|actual optional nochildterminal Coding2135–2139 ->apply879–880|88|
|8 Invocation|that Admission resolves atlookup480–485|32+I|
|9 Open|same postbodyterminal apply881–882|56|

The prebody attempt Open lookup1594–1595 and Open validation lookup881–882 are
ABSENT for a freshly admitted unopened attempt; there is no third positive Open
frame. submit's duplicate probes1556–1568 and applyexisting760–764 are likewise
fresh negative probes for Open/receipt/terminal. Their logical-failure additional
lookup is negative or throws before another positive packet, preserving the stated
selected contract. Recovered/previouslyopened/prior-terminal shapes are excluded
from that contract before effect; they require their own paid live retry envelope.

Thus positivebyte B=3*88+3*(32+I)+2*56+(32+D)=504+3I+D.
The fixed nativefull calls are THREE Admission88 and TWO Open56 =5; plus
three invocation frame slicesceil((32+I)/16MiB) and one Decision sliceceil((32+D)/16MiB).
Therefore N=5+3ceilInv+ceilDecision. Short/EINTR uses finite held call quota then
refuses BEFORE another unpaid syscall; no acquisition success is promised for an
arbitrary schedule. Codec H9 matches those nine positive frame decodes.

Both prebody lookups can actually remain resident in current Coding; this is an
upper envelope that also permits their real previously acknowledged packets to
have entered the current Root's selected catalog before scope entry. It does not
force a checkpoint/read solely to make the metric or spend unused capacity.
Public decision3138/3139 is NOT called by current dispatch1793; its standalone218
pair belongs to ordinary callers/recovery only. Fresh exec API sum is actual
attempt188 +dispatch127 =315. Decision's logical lookup/packet costs are already
L/A/P/H above, so charging its public API again would be a fabricated call.

For selected positive native reads actual API args Bbytes/Ncalls are paid before
acquisition, bytes charged after actual positive return, per-call prefix49 preserved.
Catalog probe quota separate. B/N from actual ack extents and already-produced native
business buffers, no packet re-encode. Positive direct frames =32+payload; lazy
closure additionally112rootheader only when really invoked.

Physical formula Linux CONTROL=L+1047K+810O+303J+271(GP+N);
Mac=L+1015K+774O+303J+271(GP+N); DATA=F+86J+145GP+B+49N.
J=ceil(F/16MiB)+K-1; P=0 absentarchive, else min(bit_width(actualA)+1,33).
L411(stop/Job1scope) or789(exec3scopes): grant3033/3034=172,
each actual activation3035/3036/3037=189, ownerEND3038=50.

Context K16/O4/G8/B=N0, Factualprotected, J=ceilF/16M+15.
Job K4/O1/G7, F=M+384, B=I+176, N=2+ceil((32+I)/16M), J=ceilF/16M+3.
Exec K12/O3/G24, F1324, B=504+3I+D,
N=5+3ceil((32+I)/16M)+ceil((32+D)/16M), J12 for full native calls.
IactualInvocationpayload=60+ownedinputBBMsize; DactualDecisionpayload=
128+ownedcontinuationBBMsize. Native missing/corrupt/error branches remain distinct.

A whole new CONTROL+DATA bundle is admitted outside product/spawn/cleanup locks
before authoritative admission. Sequential claims on sole producer are sufficient:
existing claim_capacity tests maximum_grant=capacity-reserved-held BEFORE waiting.
Any partial new bundle failure releases partial new holds; prior stop protection
remains until COMPLETE replacement accepted. Published byte pressure uses existing
cancellable native readiness. No group broker/new capacity API/self-deadlock wait.

## Terminal owner handoff and launch failure

TerminalUI::start presently joins1477–1478, prepares child/context/dispatch1494–1497,
then constructs jthread1517–1519 whose child binding begins1521 and whole perform
runs1525. Completion joins1692 before idle_work1711. These are the actual hooks:

1. After any old worker is joined, before prompt transcript/start effects, prepare
   a COMPLETE candidate semantic+physical stop bundle on the never-started child
   handle. Reader's owner.prepare_handoff(child, actualdispatch) and this stop
   preparation use the SAME explicit child binding/whole-root authority target.
   Compute actual protectedF on the current exclusive root owner before binding
   the child; preparation copies owned root-selection metadata, performs no product
   mutation, and cannot acquire raw capacity while a product/spawn/cleanup lock is
   held. Failure releases only candidate grants under never-started child binding,
   retains the prompt and OLD protection, and does not create a worker.
2. Parent owns a launch guard containing OLD owner/complete stop bundle, child
   handle/candidate bundle and Reader's RawStorageHandoff. The callable captures
   only a borrowed guard/candidate reference plus its normal owned prompt/context;
   it does not own raw stop tokens whose callable destructor could run on main after
   failed jthread creation. Guard/candidate storage is allocated BEFORE launch.
3. BEFORE invoking std::jthread, install candidate root/stop ownership with prepared
   noexcept swaps, retaining OLDmain in the guard. The worker may start immediately
   inside the constructor and sees only candidate child custody. Its actual binding
   and raw_worker_started precede authority entry; perform uses the actual command
   region1030 BEGIN R, not the dispatch request as a fictional BEGIN.
4. Constructor success commits by a noexcept parent step: release OLDmain only
   while main remains its sole raw producer owner. UI rendering never binds the
   worker. Constructor throw means no worker was created: restore OLD root/stop
   ownership FIRST with noexcept swaps, then explicitly bind the never-started
   child to close/release its candidate/physical handoff. No OLDmain fallback is
   kept across arbitrary successful child protection growth.
5. The candidate must outlive worker completion. Authority scope ends before the
   completion message. At both start's previous-worker join and Kind::complete /
   orderly exit join, actual join establishes exclusive child ownership. Preserve
   the child handle and complete residual stop bundle until transfer has accepted.
6. BEFORE any new main-idle root effect, acquire a COMPLETE new main bundle under
   actual main binding; only successful admission permits noexcept selected-owner
   swap and joined-child release under explicit exclusive child binding. Sticky
   cancellation/full capacity is not cleared or bypassed. Refusal preserves joined
   child debt/bank, blocks fresh root/next-worker effects and retains queued prompt;
   ordinary UI notices/input/rendering continue on main after binding restoration.
   A new prepared child follows exactly steps1–4 and cannot skip this refusal.

A selected canceled join cleanup can temporarily bind the proven joined child
and enter its explicit root/loan scope, restoring main before any UI emission.
It can use ONLY that loan's actual remaining scope/call/byte allowance. Stop-kind
physical loan permits ONE activation; if the worker already used it, a second join
activation is refused. An exhausted semantic/physical bank cannot fund a retry.
Originals, actual terminal/debt and the joined handle remain owned on refusal;
there is no assertion that canceled cleanup always completes or idle can safely
perform all ordinary operations. Existing main idle_work engine.poll_commands and
poll_participants must check this owner admission before semantic root effects;
CommandJobs native control/display alone does not authorize Context append. On
final engine shutdown the same selected residual-custody rule applies; it cannot
silently destroy an unresolved issuing handle or rebind a live worker.

Direct native handoff cuts: actual child grant failure before launch; std::thread
launch allocation/native refusal with OLDmain exact protected bank still usable;
worker runs before constructor returns and uses only child grants; child growsF,
join with sticky cancellation/new-main refusal leaves child debt and main UI alive;
exhausted stop scope cannot reactivate; later real live admission restores main or
next child with no claim migration. These augment this SAME reviewed owner unit.

## Main capacities and same-issuer composition

Proposed explicit PRODUCT RawRuntime options before first producer:
DATA256MiB/CONTROL1MiB/failure128KiB. General defaults remain64MiB/256KiB/128KiB.
Use actual protectedF/ownedI,D/configuredM for ordinary admission; do not hold a
maximal future history/debt loan unconditionally. Sparse mapping payload is not
pre-zeroed resident memory. Every additional ACTUAL live hold is checked natively
and may refuse BEFORE admission; arbitrary recursive/custom workloads not promised.
Whole-main native workload qualification remains required after caller integration.

ValueLimits16MiB; Application envelope30/frame32/commit56 plusissuer16/frame32/
commit56 => each actual Context obligation<=16MiB+222. Two obligations+8192floor.
Debt_locked counts one unresolved Job; max_records256, operation_depth<=16, largest
extra expression(depth+debt+2)*8192 => maximum actual protectedF35,807,676.
AtFmax: stopJ18/P33 LinuxCONTROL97401/DATA35847504.
Ordinary exec admits the actual checked checkpoint catalog capacity
min(max_records*6,UINT32_MAX) before effects. Its three submission phases may
retarget at most once each after optional maintenance; diagnostic children never
run that callback. Each held148 request/return pair names the actual current scope,
new journal/catalog resources and reached catalog count before attachment. Refusal
leaves old scope pointers detached and preserves the original callback outcome.
Stop and joined Job no_new scopes do not run optional maintenance or retarget.

Full legalI16MiB+60/D16MiB+128: execB67109676/N13, physicalCONTROL238018/
DATA67227509. Core same-main/TUI peak including OLD+NEW stop loans:

CONTROL=2*78536+188868+2*97401+238018+10544+5184=794488.
DATA=2*35847504+67227509=138922517 (~132.49MiB).

The semantic Context stop allowance and its physical stop loan form ONE persistent
bundle owned with actual settlement protection. They survive ordinary protected
operations and a completed turn while outstanding command debt still requires
stop custody. Every refresh prepares a COMPLETE fresh semantic78536+physical
bundle before changing protection; OLD and NEW both remain held until the native
refresh is accepted. On refusal, NEW grants are released and OLD remains available.
On acceptance, ownership swaps are noexcept and OLD grants are released on their
actual issuing producer. This replaces the former one-semantic/two-physical sum:
there is no asserted semantic reuse across refreshed guarantees. The worst case
above is TWO full semantic stop banks as well as TWO physical stop banks on the
same issuer. No more than one replacement bundle can be pending: whole perform /
root authority serializes refresh and does not invoke callbacks while assembling
or committing the replacement. If ownership changes to another prepared TUI
issuer, grants never move: previous issuing-handle release occurs only after its
actual joined/exclusive ownership boundary, and new grants belong to the new
issuer. The SAME-issuer peak conservatively covers two consecutive refreshes
performed by main or one actual TUI worker before ownership changes.

The existing9198 is NOT a persistent raw stop bundle. It is two subclaims2286+
6912 from one stack RawTurnTrace10544 constructed at the actual turn entry
(raw_turn.cpp226–266). Refresh_settlement/protect_workflow does not construct a
second RawTurnTrace or reissue those claims. They remain held once for that turn,
are consumed by actual context/region cleanup, then release at that trace's
terminal/destruction. Concurrent/reentrant turns are refused by the existing
turn_running admission before another active turn; an ordinary protected operation
outside a turn must pay its own Context/region attempt before effects, not borrow
a destroyed trace. Exec's separate THREE1728 region banks cover its three actual
semantic phases independently. The physical/semantic stop refresh does not renew
that exec region bank either. No assertion of two9198 banks is made for one active
turn. Ordinary protection request/return and held release pairs are additional
actual native held credits checked before effect; they are not silently deducted
from this core subtotal.


Current default256KiB CONTROL and64MiB DATA do not qualify.1MiB/256MiB support this
core peak with actual residual holds still checked; no declaration of whole-main
success from each loan individually fitting. Job lane distinct: worker4658+
semantic55741+physical70912 atM32MiB/I16MiB+60/P33/J6/N4; DATA50366475 (~48.03MiB).
Cleanup/SIGINT/Participant/provider-native lanes are distinct; provider originals/
operation17A/authored/turn/task/participant owner terminals/ordinary pairs sharing
main/TUI require ACTUAL held accounting, not assumed separate credit. Pre-effect
capacity refusal preserves prior protected banks and original already-admitted debt.

## Genuine red / direct oracles / qualification

Published8f23fc3 matching headers were archived into ignored context, and the
qualified Debug coding/colleague/foundation libraries were preserved unchanged
before peer rebuild. A standalone strict -g arm64 fixture links those archives.
Actual forwarding NativeJournalDirectory returned successful state-slot rename
and following directory sync. Measured postack allocations:32-byte shared catalog
control block and128-byte make_unique<SavedState>. The failure cut matches the
measured allocation leaf AND actual make_unique<SavedState> stack after that real
ack. Accepted disk slot sequence8, live saved sequence6, authoritative sequence8;
independent reopened native Root selects8 and, after actual confirm_recovery,
returns the same four original facts/RecordRefs. Final assertion fails on live
versus selected saved boundary. Separate actual accepted Root append preserved ten
raw records and zero3100, failing its missing-owner assertion. Baseline source,
logs and restored headers remain context/retained-index-study/baseline-8f23fc3.
These are genuine native reds, not a substituted callback or hypothetical flag.

Before implementation native RawRuntime+actual NativeJournalDirectory forwarding
fixture must prove missing current3100+ records and actual save_current_state
postack allocation defect. Fault named real make_unique<SavedState> after actual
slot replace+following directorysync, independent acceptedslot/liveboundary/catalog/
exactcoldhistory. Stage sharedcatalog AND SavedState before publish; only noexcept
accepted in-memory transfers afterward. No compensating slot write/fabricatedack.

Direct checks use real closed raw records, own actual producer ordinals and earlier
request refs; independent authoritative journal commit/facts/native bytes rather
than expected copies of emitter implementation. Each native reached request has
one terminal or actual fatal process-custody record when that policy applies.

* Native opcode schema/kind/count and alloptional variants, native error widths,
  no decoder narrowing, no fake RecordRef on getters; rawnativepath/checksum bytes.
* Actual source+event batch ACKspan vs authoritativeframes, accepted diagnostic
  children NOT proposedack; live/uncertain duplicate, stale/invalid multi-group
  rejection nativeoriginals, dependency conflict, partial writes/sync/EIO custody.
* Issuer firstrefill/cached/reopen/overflow/fencedafterack: no ID escapes nativefailure,
  unused reserved range burned. Actual global namespace and selectedroot/cursor.
* Replay validbatch theninvalid rollback/pendingissuer; compact/full/hint/savedslot
  corruption ormissingcatalog fallback and boundedrefusal, never false empty/certain.
* Index immutability/split/orphanwrite/actual pagecorruption/range stop noextraread;
  mergeequal/replace/final nativecatalog count andactualpositive/negative locatorreads.
* FactHistory/lazySource outlives Root and generation replacement; actual112header
  onlyduringlazycall, no extra read fromcopy/metadata/destruction; correct authorized
  MAIN/TUI/joinedJob issuer andONE cleanupbank release across arbitraryfinaldrops.
* Saved publication alloc-beforeACK and unchangedrealEIO; resident retirement only
  after actualselectedslot and provenemptyunresolved; oldgeneration readers remain.
* Head permanentlease/exactselector/attemptedfailure unhealthy; selectedchain cycle,
  namespace/predecessor refusal, continuation/provisionalcapture bytes, reconciliation
  neveropensoldattempt orrepeatsactualeffect.
* Real fullCONTROL/sticky cancel Contextstop and actual command postbody receipt/
  terminal acrossprelaunch/join/loans; acquiredshortprefix retains beforeunpaidnextcall
  refusal; no recorderfixture release mistakenforproductprogress.
* Exactdefault too-small before3record admission; configuredactualcombined grants,
  replacementold+new peak, additionalactualheld lease refusal nohang/original loss;
  realsmall interactiveexec, maximal legal input/continuation andmultipleoutstandingJobs.
* Ordinary compactthreshold maintenance genuinelyruns after no_new refusal/restoration,
  whole-perform nonturn checkpoint/mainidle afterjoin, no hidden disabled maintenance.

One strict compiler worker/focused meaningful native checks, narrow static and
format/diff after release; parent owns ONE whole code/oracle attack and affected
supported Linux/Darwin debug/release/ASan/fullgate. No duplicate panel/test campaign.

## Explicit review choices / release dependencies

1. Accept main-only256MiBDATA/1MiBCONTROL plus actualexisting max-grant refusal,
   rather than current defaults that cannot fit mandatory selectedfullbounds.
2. Accept one real physical grant lifecycle (1/1/3scopes), no duplicate semantic
   scaffolding telemetry; remaining3280–3285 stays unused.
3. Reader shared authority/CleanupState and physical loan411/1233/B,N seam must land
   before wholemain product opt-in. Currentrootcaller Context/turn, exec/Job,
   Provider operation17 scope restore and Terminal wholeperform must be integrated
   as SAME units; no cancel fallback fresh claims or file-creator issuer equality.
4. The already computed native codec locals are captured in fixed explicit origins;
   no per-fact field, observer, encoder registry or metrics-only read. Sharedpairs'
   shorter nativevariants are described exactly; no fabricated defaults.

The ONE whole plan attack is resolved in the companion attack paper. Genuine
published-header/old-library reds are preserved below. Implementation proceeds
under coordinated source ownership; actual main capacity/workload qualification
and whole-main activation are still required.

## Current implementation checks

The literal owner hooks and native helper compile. Narrow static analysis passes
for the ten changed semantic translation units and their native fixture; formatter
and diff checks pass. The two focused native cases pass in the debug profile
(0.55 seconds). Every emitted prepared physical frame is compared with independent
CRC-valid journal bytes; checks also compare exact source bytes/offsets, actual
append success/refusal arguments, prepared/published custody, issuer request/return
references, and independently reopened retained facts. The optional caller metadata
span is implemented: actual outer acknowledgment fills it without allocating;
accepted diagnostic children leave the rejected outer proposal's flag false.

Actual canceled Context finish(false), view and stop.linkage append run under their
held semantic/physical banks while fresh admission refuses sticky socket EOF.
Environment create/reopen and head transaction fields agree with independently
decoded CRC-valid head bytes. The SavedState checks preserve the measured old
postpublication-allocation red, verify zero allocation after acknowledgment, cut
the real prepublication allocation, and compare exact disk/live selected boundaries.

The selected exec carrier, joined CommandJobs terminal and production turn stop
borrowing are implemented. The carrier prepares from the already-owned codec
buffers before the ordered admission and couples its actual acknowledged
Admission.attempt to dispatch. Launched exec reaches prebody and receipt scopes;
its joined Job owns terminal settlement. No-child exec uses the third main scope.
Generic receipt and terminal have the separate finite banks described below.
Whole caller Debug native checks pass, including actual nested Lua-to-exec,
large result locators, source-capacity refusal and canceled/fullCONTROL receipt.
The exact16MiB Invocation native case compares its CRC-valid disk frame with
3033 B/N and three actual16MiB+32 positive acquisitions through archived
dispatch/receipt/terminal. Narrow static passes for the changed production TUs and final fixture;
ONE whole code/oracle review and supported-profile
qualification remain the current completion boundary. Main activation is a
candidate qualified by its real CLI oracle, pending the integrated source gate.

### Generic adapter receipt caller

Before the ordered Decision/Invocation/Admission append, the caller prepares the
separate 33887-byte semantic receipt bank. Its physical loan selects
adapter_receipt, F420/K4/O1/G4/S1, B56/N1: the actual Receipt codec permits36
bytes and its diagnostic rejection envelope384; the sole positive dependency is
the actual Open frame32+payload24. Catalog capacity is the checked business
maximum, not the present count. At P33/J4 this holds42664 CONTROL/20009 DATA
on Linux,76551 CONTROL including semantic capture. Root.dispatch activates its
one no_new receipt scope after the actual body returns and destroys it before
its enclosing native dispatch origin. The body and reentrant work borrow none
of this credit. Failure to activate preserves the actual effect in DispatchReport
and records receipt refusal without fresh postbody admission. The later terminal
observation is a distinct operation and does not reuse this exhausted receipt
loan. Nested Lua-to-exec composition includes the still-held outer receipt bank;
a deeper nesting that cannot fit configured capacity refuses before its ordered
admission rather than promising that arbitrary nesting fits.

### Generic terminal observation caller

Coding's terminal observation carries the retained result locator. Its sole
result_id-empty path first replaces the result with the actual capacity Error;
other retention errors throw before terminal construction, preserving the paid raw
result and actual operation failure without claiming a business locator. The
existing operation-slot8192 bound plus384 diagnostic envelope therefore admits
F8576. The actual two calls are primary terminal submit and, only on capacity,
the existing bounded fallback settlement. Capacity can occur after candidate
apply, so the first may acquire Admission/Invocation/Open before refusing, but
capacity branches retain no rejection children and write no frames. One lexical
scope spans both calls. K4 covers the two original attempts and fallback's two
possible diagnostic children; O2/G14 and B=2*(I+176),
N=2*(2+ceil((I+32)/16MiB)) pay both reached positive dependency traversals.
The literal semantic row is K4/S2/E3/D2/U2/Q1/sourceGroups2/I0/L14/X28/
A14/P462/H6,107639 CONTROL. E3 covers both terminal attempts and a diagnostic
marker; first capacity never creates diagnostic children. Joined Job remains
its one-attempt55741/max_payload+384 contract. Physical kind adapter_terminal
selects one ordinary-issuer scope with no joined handoff or retarget permission.
At formalP33/Imax its physical bank is134801 CONTROL/33631206 DATA. With
generic receipt this adds318991 CONTROL, which does not fit alongside the
advertised worst core794488 in1MiB. Main's actual200000-record catalog capacity
is1200000/P22; each additional actual nested holder must admit before effects or
refuse. Real nested Lua-to-exec qualification is required; arbitrary recursive
maximum workloads are not promised. Primary result retention keeps its actual
interrupted/native failure path and already-paid raw original; this grant does
not authorize another full output-retention operation.
