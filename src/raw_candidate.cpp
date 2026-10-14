#include "blackbird/raw_candidate.hpp"
namespace blackbird {
std::string_view raw_candidate_description() noexcept {
  return R"(candidate3900-3959;E16=u32 bytes,u32 site,u64 ordinal;R=lane16,u64 ordinal;native fields,no padding.
Common end:R,u8 RawOperationBranch,u8 stage,bool ownedError,[ErrorCode,i64 detail iff ownedError].
3900:int actual argc.3902:common end,bool reachedReturn,[int actual0 iff reachedReturn]. Failed branches do not assert an outer CLI return.
3901:argv-acquisitionR,size_t actual index,size_t N,unchanged actual CString operand buffer bytes including its existing NUL. Parent3953 is itself parented by3900. No joined argv/environment.
3903:callerR,u8 inputRole(1packet,2JSON),size_t pathNativeLength.3904:common end; on returned bool FileOwnerPresent,FileOwnerR,size_t acquiredBytes. FileOwnerR identifies progressive File acquisition, not invented joined input original.
3905:callerR,size_t commandLength,command bytes.3906:common end,[bool completedValue=true on returned].
3907:ownerR,u8 role,size_t N,original existing bytes. Role is native owner-specific: filesystem1firstPath2secondPath; constructedPath7result; lock1CString; write3templateCString4createdCString5targetCString6parentCString; save1templateCString2locator3prior textual locator; field6quarantineParentCString10name11base12message13reconcile14observedBranch15observedHead; integer20Number.text. POSIX CString sources include existing NUL; C++ path.native/Value strings do not.
3908:ownerR,u8 predicate,bool result,bool indexPresent=false,bool sourcePresent=false. Current predicate1leased2capRefused3poolIsPrimary4duplicateName5poolFull6queueEmpty7dirty8preexistingStaged. No invented zero index/source.
3910:callerR,size_t actionLength,action bytes.3911:common end,[bool completed=true,recordEncodedOriginalR,stateEncodedOriginalR only after both writes returned].
3912:ownerR,u8 role,size_t N,unchanged acquired original bytes. Role1packetEncode2writeInput3GitUntrimmedStdout/runObservedOutput4runPartial5CLIpacket6CLIjson. SAME encoded body is reused by write sourceR/off/count, not reencoded/recopied.
3913:saveR,priorTextLocatorR(or zero if absent/nontext),bool completed=true,recordEncodedOriginalR,stateEncodedOriginalR; reached only after both actual durable writes.
3914:callerR,size_t actual argc after existing Git prefix insertion.3915:common end,[bool child=true,actualChildR,bool callerExit=false,bool output=true,size_t untrimmedBytes on returned]. Git collect retains no exit pointer; native Child owns its exit/nonzero refusal.
3916:Git/runR,size_t actual index,size_t N,original actual CString operand buffer bytes including its existing NUL.3917:Git/runR,actualChildR; reached actual spawn linkage, not inferred exit.
3918:callerR,size_t actual seconds,size_t existing limit,size_t actual argc.3919:common end,[bool exitObserved,[int actualExit iff observed],bool output=true,outputOriginalR,size_t N on returned]. Run parent spans intent/unlock/Child/output/reacquire/observation and paid native Child cleanup.
3920:runR,u8 phase(1intentSaved2observationSaved),actualEncodedRecordR,size_t existing slot.3921:runR,bool partial,outputOriginalR,size_t actual N.
3922:fieldOwnerR,u8 role,size_t N,existing supplied string. Role1stopped2evidence3sourceCommit4qualification5effectiveProgram6request7mode; capture follows original validation short circuits, no live-activation/containment inference.
3930:callerR,u8 method,following native fields by actual method. Method1lockOpen:int flags,mode_t mode,pathCStringR;2mkstemp:templateCStringR;3fsync:int fd;4rename:createdCStringR,targetCStringR;5directoryOpen:int flags,parentCStringR.3931:requestR,int return,bool negative,[int errno iff negative].
3932:callerR,int fd,inputOriginalR,size_t existing offset,size_t remainingRequested.3933:requestR,ssize_t native return,bool negative,[int errno iff negative]. Every reached attempt including original EINTR retry is distinct.
3934:actualOpenR,int acquired fd,u8 resourceKind(1lock2writeTemporary3writeDirectory4eventPlaceholder5quarantineDirectory).3935:actualOpenR,int fd,int closeReturn,bool negative,[int errno iff negative]. Credit98=acquired45+fd-bearing-close53 held before acquisition; ownership spent before close, no repeated close/publication replay.
3936:callerR,int fd,int actual flockOperation.3937:requestR,int return,bool negative,[int errno iff negative].
3938:callerR,u8 filesystemMethod,[actual file_type,perms only method5]. Methods1throwingCanonical3throwingExists4symlinkStatus5isSymlink(already acquired status)6isRegularFile7createDirectories8rename. Absolute is unrecorded bootstrap, not claimed method2. Actual path inputs are separate3907 originals.
3939:common end; returned path:bool nativeCode=false,bool completed=true,actualFileAcquisitionOwnerR,size_t N; returned predicate:bool nativeCode=false,bool completed=true,bool actualResult; returned status:bool nativeCode=false,bool completed=true,actual file_type,perms; returned void:bool nativeCode=false,bool completed=true. Actual filesystem_error:standard common end,bool nativeCode=true,int actual error_code,bool completed=false. Other failures have common end only. No second status/probe.
3940:callerR,size_t existing pathNativeLength.3941:common end.3942:ownerR,size_t N,original existing path.string bytes before use.
3943:callerR.3944:common end; SAME encode_packet_string once, completed body3912 role1, funding precedes encoder.
3945:callerR.3946:common end; actual lock construction. Native open/flock parent that actual constructor; later unlock/relock take current invocation parent.
3947:callerR.3948:common end; actual write/publication; existing inputR reused when supplied. Native requested offsets/returns preserved before next effect.
3949:callerR.3950:common end; actual existing path construction, prepaid3907 role7 result before use.
3951:callerR.3952:common end; actual reached field acquisition, borrowed original before existing string copy.3953:CLI R.3954:common end; actual argv acquisition owner.3955:CLI R.3956:common end; existing native packet/JSON output acquired once before stdout.
3957:callerR.3958:common end; existing integer-field parse.3959:integerOwnerR,std::errc actual from_chars.ec,const char* actual returned ptr,size_t existing n. Number.text original3907 role20 captured once; no extra parser/formatter.
Default unchanged valid argc4 activation follows original pool absolute/createDirectories plus private-owned0700 .raw-candidate and observation EnvironmentId bootstrap. Invalid arity/bootstrap remain outside3900. Binding outlives lexical locks/children. Actual raw failures escape before unknown save/new work. NativeChild's existing ETIMEDOUT business translation consumes only its own identical active individual-admission marker; actual ECANCELED and previous markers remain. No PID adoption/effect replay/global codec coverage.)";
}
} // namespace blackbird
