#include "blackbird/raw_backstop.hpp"
namespace blackbird {
std::string_view raw_backstop_description() noexcept {
  return R"(Backstop v1 E16/R24/native fields/no padding.4700 parentR,journal16,
sequence:u64,end:u64;4701 caller end.4702 source ownerR,role:u8,size:size_t,whole
unchanged bytes. Roles1 sourcePath2 destination3 missionBBM4 selectedContextBBM
5 formattedAssessment6 choiceBBM7 action8 oraclePath9 expected10 prior11 actual
12 outcomeBBM13 blockedBBM14 phase15 role16 admittedInput17 contextHead18 model
19 effort20 contextEntryID21 identityHex22 pivotKey23 requiredString24 protectedPath
25 operationName26 backstopPacketBBM.4703 reached predicate ownerR,kind:u8,bool;
kinds1 paused2 missionObject3 missionSize4 missionPresent5 selectedEmpty6 selectedSize
7 inputTruncated8 unknown9 sourceQuiescent10 pathAbsolute11 expectedEmpty12
expectedSize13 duplicate14 oracleExists15 stale16 matches17 expired18 interrupted
19 finalQuiescent20 rootLive;21 turnRunning22 operationActive23 failureAbsent24
claimed25 failedInterrupted26 cancelled27 uncontained28 nonquiescent29 sourceNotLive
30 issuerZero31 requiredEmpty32 requiredOversize33 provider34 notAssessing35 callLimit
36 execOrRestart37 fileMutation38 relativeEmpty39 relativeInside40 existsResolved
41 inspectionLimit42 regular43 equivalent44 lastUser.4704 ownerR,attempt16,kind:u8,
actualOrdinal:size_t,inputExtent:size_t.4705 ownerR,ownedCauseCode:u8,detail:i64.
4706 mutation ownerR,kind:u8,[size_t count only kinds5/6/7]; fixed kinds1 ticket2 root
3 seed4 instructions8 cancellationRestore.4710 parentR;4711 caller end,[actual
returned cause code/detail on success].4712 ownerR,mode_t,destinationOriginalR;
4713 requestR,int rc,[int errno only negative].4714 ownerR,size_t requested;
4715 requestR,int rc,[int errno if negative|exact initialized acquired bytes on
success].4720 wholeR,unsigned pivot;4721 caller end;4722 pivotR,bool assessing,
unsigned calls;4723 caller end;4724 choice original ownerR,role:u8,size:size_t,bytes.
4725 pivotR,actionOriginalR,choiceOriginalR;4726 caller end.4727 pivotR,fileHelperR,
actualMatches:bool.4730 pivotR,size_t operationName;4731 caller end;4732 pivotR,
requestedPathOriginalR;4733 caller end;4734 policyR,bool assessing,unsigned actual calls
only after the existing increment.4740 ownerR,u8 filesystemMethod,operandR,
[secondOperandR only relative/equivalent];4741 caller end; returned methods1/2 actual int error_code/categoryPointer,
method3 no extra,methods4-8 actual bool. Actual filesystem_error branch includes
its owned int/categoryPointer and no returned bool/entry. Methods1 canonical2 weak
3 relative4 exists5 regular6 equivalent7 recursiveCreate8 recursiveIncrement.
Caller end requestR,branch:u8,stage:u8,ownedErrorPresent:bool,[code:u8,detail:i64].
File methods6/7/8 preserve actual absolute/header-prefix/weak-path native acquisitions.
No second encoder/formatter, metric traversal or replay. Failed capture writes
existing8001 and escapes before recovery fallback; actual business errors retain
the original two pivots and existing deadline policy. Null context remains opt-out.)";
}
} // namespace blackbird
